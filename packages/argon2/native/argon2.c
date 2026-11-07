/* argon2 (npm contract: hash, verify), argon2id per RFC 9106, version 0x13, the PHC string format of
   node-argon2 and its defaults (m=64 MiB, t=3, p=4, 16-byte salt, 32-byte hash). Lanes run on threads, as in
   the reference implementation; inside a request handler the whole computation runs on a helper thread and
   the coroutine waits on a pipe, so the event loop keeps serving other requests (node-argon2 uses the libuv
   thread pool for the same reason). Builds on node:crypto (base64, random bytes) and the coroutine runtime. */
#include <pthread.h>

/* ---- BLAKE2b (RFC 7693), unkeyed, any output length up to 64 ---- */
typedef struct { uint64_t h[8], t; uint8_t buf[128]; size_t n, outlen; } xs_blake2b;
static const uint64_t xs_b2_iv[8] = {0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
                                     0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};
static const uint8_t xs_b2_sigma[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4}, {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13}, {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11}, {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5}, {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}};
#define XS_ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
static uint64_t xs_load64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; } /* little-endian targets only */
static void xs_store64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }
static void xs_store32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void xs_b2_compress(xs_blake2b *s, const uint8_t *blk, bool last) {
    uint64_t m[16], v[16];
    for (int i = 0; i < 16; i++) m[i] = xs_load64(blk + 8 * i);
    for (int i = 0; i < 8; i++) { v[i] = s->h[i]; v[i + 8] = xs_b2_iv[i]; }
    v[12] ^= s->t;
    if (last) v[14] = ~v[14];
#define XS_B2G(a, b, c, d, x, y) do { v[a] += v[b] + (x); v[d] = XS_ROR64(v[d] ^ v[a], 32); v[c] += v[d]; v[b] = XS_ROR64(v[b] ^ v[c], 24); \
        v[a] += v[b] + (y); v[d] = XS_ROR64(v[d] ^ v[a], 16); v[c] += v[d]; v[b] = XS_ROR64(v[b] ^ v[c], 63); } while (0)
    for (int r = 0; r < 12; r++) {
        const uint8_t *sg = xs_b2_sigma[r];
        XS_B2G(0, 4, 8, 12, m[sg[0]], m[sg[1]]); XS_B2G(1, 5, 9, 13, m[sg[2]], m[sg[3]]);
        XS_B2G(2, 6, 10, 14, m[sg[4]], m[sg[5]]); XS_B2G(3, 7, 11, 15, m[sg[6]], m[sg[7]]);
        XS_B2G(0, 5, 10, 15, m[sg[8]], m[sg[9]]); XS_B2G(1, 6, 11, 12, m[sg[10]], m[sg[11]]);
        XS_B2G(2, 7, 8, 13, m[sg[12]], m[sg[13]]); XS_B2G(3, 4, 9, 14, m[sg[14]], m[sg[15]]);
    }
#undef XS_B2G
    for (int i = 0; i < 8; i++) s->h[i] ^= v[i] ^ v[i + 8];
}
static void xs_b2_init(xs_blake2b *s, size_t outlen) {
    memcpy(s->h, xs_b2_iv, sizeof s->h);
    s->h[0] ^= 0x01010000ull ^ outlen;
    s->t = 0;
    s->n = 0;
    s->outlen = outlen;
}
static void xs_b2_update(xs_blake2b *s, const void *data, size_t len) {
    const uint8_t *p = data;
    while (len) {
        if (s->n == 128) { s->t += 128; xs_b2_compress(s, s->buf, false); s->n = 0; }
        size_t take = 128 - s->n < len ? 128 - s->n : len;
        memcpy(s->buf + s->n, p, take);
        s->n += take; p += take; len -= take;
    }
}
static void xs_b2_final(xs_blake2b *s, uint8_t *out) {
    s->t += s->n;
    memset(s->buf + s->n, 0, 128 - s->n);
    xs_b2_compress(s, s->buf, true);
    uint8_t full[64];
    for (int i = 0; i < 8; i++) xs_store64(full + 8 * i, s->h[i]);
    memcpy(out, full, s->outlen);
}
/* H' of Argon2 (RFC 9106 3.3): variable-length hash */
static void xs_b2_long(uint8_t *out, uint32_t outlen, const uint8_t *in, size_t inlen) {
    uint8_t len4[4];
    xs_store32(len4, outlen);
    xs_blake2b s;
    if (outlen <= 64) {
        xs_b2_init(&s, outlen);
        xs_b2_update(&s, len4, 4);
        xs_b2_update(&s, in, inlen);
        xs_b2_final(&s, out);
        return;
    }
    uint8_t v[64];
    xs_b2_init(&s, 64);
    xs_b2_update(&s, len4, 4);
    xs_b2_update(&s, in, inlen);
    xs_b2_final(&s, v);
    memcpy(out, v, 32);
    out += 32;
    uint32_t left = outlen - 32;
    while (left > 64) {
        xs_b2_init(&s, 64);
        xs_b2_update(&s, v, 64);
        xs_b2_final(&s, v);
        memcpy(out, v, 32);
        out += 32;
        left -= 32;
    }
    xs_b2_init(&s, left);
    xs_b2_update(&s, v, 64);
    xs_b2_final(&s, out);
}

/* ---- Argon2id (RFC 9106), version 0x13. Lanes run on threads, as the reference implementation does. ---- */
typedef struct { uint64_t v[128]; } xs_a2block;
static inline uint64_t xs_a2_fBlaMka(uint64_t x, uint64_t y) { return x + y + 2 * ((x & 0xffffffffull) * (y & 0xffffffffull)); }
#define XS_A2G(a, b, c, d) do { a = xs_a2_fBlaMka(a, b); d = XS_ROR64(d ^ a, 32); c = xs_a2_fBlaMka(c, d); b = XS_ROR64(b ^ c, 24); \
        a = xs_a2_fBlaMka(a, b); d = XS_ROR64(d ^ a, 16); c = xs_a2_fBlaMka(c, d); b = XS_ROR64(b ^ c, 63); } while (0)
#define XS_A2P(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15) do { \
        XS_A2G(v0, v4, v8, v12); XS_A2G(v1, v5, v9, v13); XS_A2G(v2, v6, v10, v14); XS_A2G(v3, v7, v11, v15); \
        XS_A2G(v0, v5, v10, v15); XS_A2G(v1, v6, v11, v12); XS_A2G(v2, v7, v8, v13); XS_A2G(v3, v4, v9, v14); } while (0)
/* next = G(prev, ref) (xor next when with_xor: passes after the first) */
static void xs_a2_fill(const xs_a2block *prev, const xs_a2block *ref, xs_a2block *next, bool with_xor) {
    xs_a2block r, t;
    for (int i = 0; i < 128; i++) r.v[i] = prev->v[i] ^ ref->v[i];
    t = r;
    if (with_xor) for (int i = 0; i < 128; i++) t.v[i] ^= next->v[i];
    for (int i = 0; i < 8; i++) {
        uint64_t *q = &r.v[16 * i];
        XS_A2P(q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9], q[10], q[11], q[12], q[13], q[14], q[15]);
    }
    for (int i = 0; i < 8; i++) {
        uint64_t *q = r.v;
        XS_A2P(q[2 * i], q[2 * i + 1], q[2 * i + 16], q[2 * i + 17], q[2 * i + 32], q[2 * i + 33], q[2 * i + 48], q[2 * i + 49],
               q[2 * i + 64], q[2 * i + 65], q[2 * i + 80], q[2 * i + 81], q[2 * i + 96], q[2 * i + 97], q[2 * i + 112], q[2 * i + 113]);
    }
    for (int i = 0; i < 128; i++) next->v[i] = t.v[i] ^ r.v[i];
}
typedef struct {
    xs_a2block *mem;
    uint32_t passes, lanes, lane_len, seg_len;
} xs_a2;
typedef struct { xs_a2 *a; uint32_t pass, lane, slice; } xs_a2pos;
static void xs_a2_segment(xs_a2pos *ps) {
    xs_a2 *a = ps->a;
    uint32_t pass = ps->pass, lane = ps->lane, slice = ps->slice;
    bool data_indep = pass == 0 && slice < 2; /* Argon2id: Argon2i addressing for the first half of the first pass */
    xs_a2block zero = {{0}}, input = {{0}}, addr = {{0}};
    if (data_indep) {
        input.v[0] = pass; input.v[1] = lane; input.v[2] = slice; input.v[3] = (uint64_t)a->lanes * a->lane_len;
        input.v[4] = a->passes; input.v[5] = 2; /* type: Argon2id */
    }
    uint32_t start = pass == 0 && slice == 0 ? 2 : 0;
    if (data_indep && start) { input.v[6]++; xs_a2_fill(&zero, &input, &addr, false); xs_a2_fill(&zero, &addr, &addr, false); }
    uint32_t cur = lane * a->lane_len + slice * a->seg_len + start;
    uint32_t prev = cur % a->lane_len == 0 ? cur + a->lane_len - 1 : cur - 1;
    for (uint32_t i = start; i < a->seg_len; i++, cur++, prev++) {
        if (cur % a->lane_len == 1) prev = cur - 1;
        uint64_t rnd;
        if (data_indep) {
            if (i % 128 == 0) { input.v[6]++; xs_a2_fill(&zero, &input, &addr, false); xs_a2_fill(&zero, &addr, &addr, false); }
            rnd = addr.v[i % 128];
        } else {
            rnd = a->mem[prev].v[0];
        }
        uint32_t ref_lane = (uint32_t)((rnd >> 32) % a->lanes);
        if (pass == 0 && slice == 0) ref_lane = lane;
        /* the reference area: blocks already computed that may be referenced (RFC 9106 3.4.1.2) */
        uint32_t area;
        bool same = ref_lane == lane;
        if (pass == 0) {
            if (slice == 0) area = i - 1;
            else area = same ? slice * a->seg_len + i - 1 : slice * a->seg_len - (i == 0 ? 1 : 0);
        } else {
            area = same ? a->lane_len - a->seg_len + i - 1 : a->lane_len - a->seg_len - (i == 0 ? 1 : 0);
        }
        uint64_t x = rnd & 0xffffffffull;
        x = (x * x) >> 32;
        uint64_t rel = area - 1 - ((area * x) >> 32);
        uint32_t start_pos = pass != 0 && slice != 3 ? (slice + 1) * a->seg_len : 0;
        uint32_t ref_idx = (uint32_t)((start_pos + rel) % a->lane_len);
        xs_a2_fill(&a->mem[prev], &a->mem[ref_lane * a->lane_len + ref_idx], &a->mem[cur], pass != 0);
    }
}
static void *xs_a2_thread(void *p) { xs_a2_segment(p); return NULL; }
/* out: taglen bytes. m_kib: memory in KiB, t: passes, p: lanes. false when the parameters are out of range. */
static bool xs_argon2id_ex(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t slen, const uint8_t *key, size_t klen,
                           const uint8_t *ad, size_t adlen, uint32_t t, uint32_t m_kib, uint32_t p, uint8_t *out, uint32_t taglen) {
    if (p < 1 || p > 64 || t < 1 || m_kib < 8 * p || m_kib > (1u << 22) || taglen < 4 || taglen > 1024 || slen < 8) return false;
    uint8_t h0[72], w[4];
    xs_blake2b s;
    xs_b2_init(&s, 64);
    xs_store32(w, p); xs_b2_update(&s, w, 4);
    xs_store32(w, taglen); xs_b2_update(&s, w, 4);
    xs_store32(w, m_kib); xs_b2_update(&s, w, 4);
    xs_store32(w, t); xs_b2_update(&s, w, 4);
    xs_store32(w, 0x13); xs_b2_update(&s, w, 4);
    xs_store32(w, 2); xs_b2_update(&s, w, 4);
    xs_store32(w, (uint32_t)pwlen); xs_b2_update(&s, w, 4); xs_b2_update(&s, pw, pwlen);
    xs_store32(w, (uint32_t)slen); xs_b2_update(&s, w, 4); xs_b2_update(&s, salt, slen);
    xs_store32(w, (uint32_t)klen); xs_b2_update(&s, w, 4); if (klen) xs_b2_update(&s, key, klen);
    xs_store32(w, (uint32_t)adlen); xs_b2_update(&s, w, 4); if (adlen) xs_b2_update(&s, ad, adlen);
    xs_b2_final(&s, h0);
    xs_a2 a;
    a.passes = t;
    a.lanes = p;
    uint32_t blocks = m_kib / (4 * p) * 4 * p;
    a.lane_len = blocks / p;
    a.seg_len = a.lane_len / 4;
    a.mem = malloc((size_t)blocks * sizeof(xs_a2block));
    if (!a.mem) return false;
    uint8_t tmp[1024];
    for (uint32_t l = 0; l < p; l++) {
        for (uint32_t k = 0; k < 2; k++) {
            xs_store32(h0 + 64, k);
            xs_store32(h0 + 68, l);
            xs_b2_long(tmp, 1024, h0, 72);
            for (int i = 0; i < 128; i++) a.mem[l * a.lane_len + k].v[i] = xs_load64(tmp + 8 * i);
        }
    }
    xs_a2pos pos[64];
    pthread_t th[64];
    for (uint32_t pass = 0; pass < t; pass++)
        for (uint32_t slice = 0; slice < 4; slice++) {
            for (uint32_t l = 0; l < p; l++) {
                pos[l] = (xs_a2pos){&a, pass, l, slice};
                if (p == 1 || pthread_create(&th[l], NULL, xs_a2_thread, &pos[l]) != 0) { xs_a2_segment(&pos[l]); th[l] = 0; }
            }
            if (p > 1) for (uint32_t l = 0; l < p; l++) if (th[l]) pthread_join(th[l], NULL);
        }
    xs_a2block final = a.mem[a.lane_len - 1];
    for (uint32_t l = 1; l < p; l++) for (int i = 0; i < 128; i++) final.v[i] ^= a.mem[l * a.lane_len + a.lane_len - 1].v[i];
    for (int i = 0; i < 128; i++) xs_store64(tmp + 8 * i, final.v[i]);
    xs_b2_long(out, taglen, tmp, 1024);
    memset(a.mem, 0, (size_t)blocks * sizeof(xs_a2block)); /* the memory held derived password material */
    free(a.mem);
    memset(tmp, 0, sizeof tmp);
    return true;
}

static bool xs_argon2id(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t slen, uint32_t t, uint32_t m_kib, uint32_t p,
                        uint8_t *out, uint32_t taglen) {
    return xs_argon2id_ex(pw, pwlen, salt, slen, NULL, 0, NULL, 0, t, m_kib, p, out, taglen);
}

/* "$argon2id$v=19$m=65536,t=3,p=4$<salt b64>$<hash b64>" (PHC string, as node-argon2 writes it) */
static bool xs_argon2_encode(const uint8_t *pw, size_t pwlen, uint32_t t, uint32_t m, uint32_t p, char *out, size_t cap) {
    uint8_t salt[16], hash[32];
    xs_random_bytes(salt, sizeof salt);
    if (!xs_argon2id(pw, pwlen, salt, sizeof salt, t, m, p, hash, sizeof hash)) return false;
    char s64[32], h64[64];
    xs_b64_encode(salt, sizeof salt, s64, true);
    xs_b64_encode(hash, sizeof hash, h64, true);
    /* the PHC format is base64 without padding, standard alphabet */
    for (char *c = s64; *c; c++) { if (*c == '-') *c = '+'; else if (*c == '_') *c = '/'; }
    for (char *c = h64; *c; c++) { if (*c == '-') *c = '+'; else if (*c == '_') *c = '/'; }
    int n = snprintf(out, cap, "$argon2id$v=19$m=%u,t=%u,p=%u$%s$%s", m, t, p, s64, h64);
    return n > 0 && (size_t)n < cap;
}
static bool xs_argon2_verify(const char *enc, const uint8_t *pw, size_t pwlen) {
    unsigned m, t, p;
    int consumed = 0;
    if (sscanf(enc, "$argon2id$v=19$m=%u,t=%u,p=%u$%n", &m, &t, &p, &consumed) != 3 || consumed == 0) return false;
    const char *salt64 = enc + consumed, *dollar = strchr(salt64, '$');
    if (!dollar) return false;
    uint8_t salt[64], want[128], got[128];
    long slen = (long)(dollar - salt64) <= 86 ? xs_b64_decode(salt64, (size_t)(dollar - salt64), salt) : -1;
    size_t hlen64 = strlen(dollar + 1);
    long hlen = hlen64 <= 170 ? xs_b64_decode(dollar + 1, hlen64, want) : -1;
    if (slen < 8 || hlen < 4) return false;
    if (!xs_argon2id(pw, pwlen, salt, (size_t)slen, t, m, p, got, (uint32_t)hlen)) return false;
    return xs_ct_equal(want, got, (size_t)hlen);
}

/* ---- the package's bindings ---- */
typedef struct { const char *pw; size_t pwlen; const char *digest; bool verify; char out[160]; bool ok; int fd; } xn_argon2_job;
static void xn_argon2_run(xn_argon2_job *j) {
    if (j->verify) j->ok = xs_argon2_verify(j->digest, (const uint8_t *)j->pw, j->pwlen);
    else j->ok = xs_argon2_encode((const uint8_t *)j->pw, j->pwlen, 3, 65536, 4, j->out, sizeof j->out);
}
/* Admission: at most xn_argon2_limit() hashes run at once (each holds 64 MiB and p lane threads). More would
   not finish sooner, the CPU is already saturated, and memory would grow with the number of concurrent logins.
   node-argon2 gets the same bound from the libuv thread pool. Default: enough jobs to fill the cores
   (ceil(cores / lanes)); XISTO_ARGON2_JOBS overrides. Waiting happens on the helper thread, before allocating. */
static pthread_mutex_t xn_argon2_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t xn_argon2_cv = PTHREAD_COND_INITIALIZER;
static int xn_argon2_running;
static int xn_argon2_limit(void) {
    static int limit;
    if (!limit) {
        const char *e = getenv("XISTO_ARGON2_JOBS");
        int n = e ? atoi(e) : 0;
        if (n < 1) {
            long cores = sysconf(_SC_NPROCESSORS_ONLN);
            n = cores > 0 ? (int)((cores + 3) / 4) : 1; /* 4 = the lanes of the default parameters */
        }
        limit = n;
    }
    return limit;
}
static void xn_argon2_enter(void) {
    pthread_mutex_lock(&xn_argon2_mu);
    while (xn_argon2_running >= xn_argon2_limit()) pthread_cond_wait(&xn_argon2_cv, &xn_argon2_mu);
    xn_argon2_running++;
    pthread_mutex_unlock(&xn_argon2_mu);
}
static void xn_argon2_leave(void) {
    pthread_mutex_lock(&xn_argon2_mu);
    xn_argon2_running--;
    pthread_cond_signal(&xn_argon2_cv);
    pthread_mutex_unlock(&xn_argon2_mu);
}
static void *xn_argon2_thread(void *p) {
    xn_argon2_job *j = p;
    xn_argon2_enter();
    xn_argon2_run(j);
    xn_argon2_leave();
    char one = 1;
    ssize_t w = write(j->fd, &one, 1);
    (void)w;
    return NULL;
}
/* on a helper thread when called from a request handler, inline otherwise */
static void xn_argon2_do(xn_argon2_job *j) {
    int fds[2];
    pthread_t th;
    if (!xs_cur_coro || pipe(fds) != 0) { xn_argon2_run(j); return; }
    j->fd = fds[1];
    if (pthread_create(&th, NULL, xn_argon2_thread, j) != 0) { close(fds[0]); close(fds[1]); xn_argon2_run(j); return; }
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    char c;
    while (read(fds[0], &c, 1) != 1) xs_wait_fd(fds[0], EVFILT_READ);
    pthread_join(th, NULL);
    close(fds[0]);
    close(fds[1]);
}
XS_NOINLINE xs_str *xn_argon2_hash(xs_str *plain) {
    xn_argon2_job j = {.pw = plain->data, .pwlen = (size_t)plain->len};
    xn_argon2_do(&j);
    if (!j.ok) xs_panic("argon2: hashing failed");
    return xs_str_new(j.out, strlen(j.out));
}
XS_NOINLINE bool xn_argon2_verify(xs_str *digest, xs_str *plain) {
    xn_argon2_job j = {.pw = plain->data, .pwlen = (size_t)plain->len, .digest = digest->data, .verify = true};
    xn_argon2_do(&j);
    return j.ok;
}
