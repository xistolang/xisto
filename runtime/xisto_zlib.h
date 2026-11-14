/* Xisto runtime: node:zlib over zlib 1.3.1 (third_party/zlib, compiled once into build/libxszlib.a with Z_PREFIX,
   so its symbols are z_deflate... and cannot meet a system libz). Appended after xisto_rt.h when a program imports
   node:zlib; the library is linked only then. One call compresses or decompresses a whole buffer, the way Node's
   *Sync functions do (runtime/node/zlib.ts builds the API on it). */
#define Z_PREFIX 1
#include "zlib-inc/zlib.h"

/* the modes, as Node numbers them (zlib.constants.DEFLATE...) */
enum { XS_Z_DEFLATE = 1, XS_Z_INFLATE, XS_Z_GZIP, XS_Z_GUNZIP, XS_Z_DEFLATERAW, XS_Z_INFLATERAW, XS_Z_UNZIP };

/* the last failure of xs_zlib_run: zlib's code (Z_DATA_ERROR...) and message, read by zlib.ts to build the Error */
static XS_TLS int xs_zlib_errno;
static XS_TLS char xs_zlib_msg[128];

XS_NOINLINE int64_t xs_zlib_last_errno(void) { return xs_zlib_errno; }
XS_NOINLINE xs_str *xs_zlib_last_message(void) { return xs_str_new(xs_zlib_msg, strlen(xs_zlib_msg)); }

static void xs_zlib_fail(int code, const char *msg) {
    xs_zlib_errno = code;
    snprintf(xs_zlib_msg, sizeof xs_zlib_msg, "%s", msg ? msg : "zlib error");
}

/* the output buffer, grown by doubling */
typedef struct { uint8_t *p; size_t len, cap; } xs_zbuf;
static bool xs_zbuf_room(xs_zbuf *b, size_t want) {
    if (b->cap - b->len >= want) return true;
    size_t cap = b->cap ? b->cap : 16384;
    while (cap - b->len < want) cap *= 2;
    uint8_t *p = realloc(b->p, cap);
    if (!p) return false;
    b->p = p;
    b->cap = cap;
    return true;
}

/* Compresses or decompresses data in one call (mode: XS_Z_*). The options are Node's: level, windowBits,
   memLevel, strategy, dictionary (or null), finishFlush (Z_FINISH, or Z_SYNC_FLUSH to accept a truncated
   stream), maxOutputLength and chunkSize: the output is asked of zlib chunkSize bytes at a time, as Node does,
   which matters for the bytes: level 0 cuts its stored blocks where the output space ends. Returns a Buffer, or null with xs_zlib_errno and xs_zlib_msg set: zlib's code
   and message, or 1 for an output past maxOutputLength. */
XS_NOINLINE xs_Uint8Array *xs_zlib_run(int64_t mode, xs_Uint8Array *data, int64_t level, int64_t window_bits, int64_t mem_level,
                                       int64_t strategy, xs_Uint8Array *dict, int64_t finish_flush, int64_t max_out,
                                       int64_t chunk_size) {
    z_stream s;
    memset(&s, 0, sizeof s);
    bool comp = mode == XS_Z_DEFLATE || mode == XS_Z_GZIP || mode == XS_Z_DEFLATERAW;
    int wb = (int)window_bits;
    const uint8_t *in = data->data;
    size_t inlen = (size_t)data->f_length;
    /* unzip: a gzip stream is gunzipped (and then, as there, members after the first are read too) */
    if (mode == XS_Z_UNZIP && inlen >= 2 && in[0] == 0x1f && in[1] == 0x8b) mode = XS_Z_GUNZIP;
    int rc;
    if (comp) {
        if (mode == XS_Z_GZIP) wb += 16;
        else if (mode == XS_Z_DEFLATERAW) wb = -(wb == 8 ? 9 : wb); /* (zlib 1.2.9+ refuses 8 for raw deflate; Node asks for 9) */
        rc = deflateInit2(&s, (int)level, Z_DEFLATED, wb, (int)mem_level, (int)strategy);
        if (rc == Z_OK && dict && mode != XS_Z_GZIP) rc = deflateSetDictionary(&s, dict->data, (uInt)dict->f_length);
    } else {
        if (mode == XS_Z_GUNZIP) wb += 16;
        else if (mode == XS_Z_UNZIP) wb += 32;
        else if (mode == XS_Z_INFLATERAW) wb = -wb;
        rc = inflateInit2(&s, wb);
        if (rc == Z_OK && dict && mode == XS_Z_INFLATERAW) rc = inflateSetDictionary(&s, dict->data, (uInt)dict->f_length);
    }
    if (rc != Z_OK) {
        xs_zlib_fail(rc, rc == Z_MEM_ERROR ? "Out of memory" : "Init error");
        return NULL;
    }
    xs_zbuf out = {0};
    s.next_in = (Bytef *)in;
    s.avail_in = (uInt)inlen; /* (inputs past 4 GB: fed in pieces below) */
    size_t left = inlen - s.avail_in;
    int flush = (int)finish_flush;
    for (;;) {
        if (!xs_zbuf_room(&out, (size_t)chunk_size)) { rc = Z_MEM_ERROR; s.msg = "Out of memory"; break; }
        s.next_out = out.p + out.len;
        s.avail_out = (uInt)chunk_size;
        uInt before = s.avail_out;
        if (s.avail_in == 0 && left) { size_t k = left > (1u << 30) ? (1u << 30) : left; s.avail_in = (uInt)k; left -= k; }
        int f = left ? Z_NO_FLUSH : flush;
        rc = comp ? deflate(&s, f) : inflate(&s, f);
        out.len += before - s.avail_out;
        if (max_out >= 0 && out.len > (size_t)max_out) { rc = 1; break; }
        if (!comp && rc == Z_NEED_DICT) {
            if (!dict) { s.msg = "Missing dictionary"; break; }
            rc = inflateSetDictionary(&s, dict->data, (uInt)dict->f_length);
            if (rc != Z_OK) { s.msg = "Bad dictionary"; break; }
            continue;
        }
        if (rc == Z_STREAM_END) {
            /* Node's gunzip: another member may follow (concatenated .gz files); trailing zero bytes end the input */
            if (!comp && mode == XS_Z_GUNZIP && s.avail_in > 0 && s.next_in[0] != 0) {
                inflateReset(&s);
                continue;
            }
            rc = Z_OK;
            break;
        }
        if (rc == Z_BUF_ERROR && s.avail_out != 0) { /* no progress possible: all the input is in, the stream did not end */
            if (flush == Z_FINISH || comp) { s.msg = "unexpected end of file"; break; }
            rc = Z_OK;
            break;
        }
        if (rc == Z_BUF_ERROR) rc = Z_OK; /* (the output was full: grown, then on) */
        if (rc != Z_OK) break;
        if (s.avail_out != 0 && s.avail_in == 0 && !left && flush != Z_FINISH) break; /* (a sync flush: all that can be given) */
    }
    if (comp) deflateEnd(&s); else inflateEnd(&s);
    if (rc != Z_OK) {
        if (rc == 1) xs_zlib_fail(1, "");
        else xs_zlib_fail(rc, s.msg ? s.msg : rc == Z_DATA_ERROR ? "invalid data" : "zlib error");
        free(out.p);
        return NULL;
    }
    xs_Uint8Array *r = xs_u8_raw((int64_t)out.len, true, false);
    if (out.len) memcpy(r->data, out.p, out.len);
    free(out.p);
    return r;
}

/* zlib.crc32(data, value): CRC-32 (the gzip one), continued from value */
XS_NOINLINE double xs_zlib_crc32(xs_Uint8Array *data, double value) {
    uLong c = (uLong)(uint32_t)value;
    const uint8_t *p = data->data;
    int64_t n = data->f_length;
    while (n > 0) {
        uInt k = n > (1 << 30) ? (1u << 30) : (uInt)n;
        c = crc32(c, p, k);
        p += k;
        n -= k;
    }
    return (double)(uint32_t)c;
}
