/* redis (node-redis v4 contract): a Redis client, RESP2 over TCP. Builds on the coroutine runtime (sockets).
   - One connection per worker thread, pipelined as node-redis does: a command is written at once (writers take
     turns) and its coroutine joins a FIFO of pending replies; Redis answers in order, so one coroutine at a time
     reads replies and hands each to the head of the FIFO. Inside a request handler every wait yields the
     coroutine; a parked coroutine is woken once, only by the one that has its reply (or the reading turn).
   - url: redis://[[user]:password@]host[:port][/db]; AUTH and SELECT run once per connection.
   - A command is one write of a RESP array of bulk strings; the reply is read whole before it is returned.
   - Server errors (-ERR ...) become thrown Errors with the server's text; a broken connection is closed and the
     next command opens a new one. rediss:// (TLS) is not supported yet: it is refused, never downgraded. */

typedef struct xs_rd_conn {
    int fd;
    char *in;
    size_t in_len, in_cap, in_off;
    bool broken;
    struct xs_rd_conn *next;
} xs_rd_conn;
#define XS_RD_MAX_CLIENTS 8
struct xs_rd_reply;
/* a command waiting for its reply, or a coroutine waiting for its turn to write */
typedef struct xs_rd_wait {
    xs_coro *co;
    bool parked;                /* in xs_coro_yield on our account: may be woken (once) */
    bool done, failed;
    struct xs_rd_reply *reply;
    struct xs_rd_wait *next;
} xs_rd_wait;
typedef struct {
    xs_rd_conn *c;              /* NULL until opened, and after it broke */
    xs_rd_wait *head, *tail;    /* pending replies, in the order the commands were written */
    xs_rd_wait *wq_head, *wq_tail; /* coroutines waiting to write */
    bool reading, writing;
} xs_rd_local;
typedef struct xn_redis_NativeRedis {
    xs_hdr hdr;
    char *host, *user, *password;
    int port, db, id;
} xn_redis_NativeRedis;
static XS_TLS xs_rd_local xs_rd_locals[XS_RD_MAX_CLIENTS];
static int xs_rd_nclients;

static void xn_redis_NativeRedis_free(void *p) {
    xn_redis_NativeRedis *r = p;
    free(r->host);
    free(r->user);
    if (r->password) { memset(r->password, 0, strlen(r->password)); free(r->password); }
    xs_obj_free(p);
}
static void xn_redis_NativeRedis_print(void *p) { (void)p; fputs("RedisClient {}", stdout); }

static char *xs_rd_cfg_str(xs_jv *cfg, const char *key) {
    xs_jv *v = cfg ? xs_json_field_opt(cfg, key) : NULL;
    return v && v->kind == XJ_STR ? xs_strndup_(v->str, v->slen) : NULL;
}
static int64_t xs_rd_cfg_int(xs_jv *cfg, const char *key, int64_t dflt) {
    xs_jv *v = cfg ? xs_json_field_opt(cfg, key) : NULL;
    return v && v->kind == XJ_NUM ? (int64_t)v->num : dflt;
}
static int xs_rd_hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static char *xs_rd_unescape(const char *s, size_t n) { /* %XX in the user and password of a URL */
    char *o = malloc(n + 1);
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n && xs_rd_hex(s[i + 1]) >= 0 && xs_rd_hex(s[i + 2]) >= 0) { o[k++] = (char)(xs_rd_hex(s[i + 1]) * 16 + xs_rd_hex(s[i + 2])); i += 2; }
        else o[k++] = s[i];
    }
    o[k] = 0;
    return o;
}
/* redis://[[user]:password@]host[:port][/db] */
static void xs_rd_parse_url(xn_redis_NativeRedis *r, const char *url) {
    const char *p;
    if (strncmp(url, "redis://", 8) == 0) p = url + 8;
    else if (strncmp(url, "rediss://", 9) == 0) xs_panic("redis: rediss:// (TLS) is not supported yet");
    else xs_panic("redis: the url must start with redis://");
    const char *at = strchr(p, '@');
    const char *slash0 = strchr(p, '/');
    if (at && (!slash0 || at < slash0)) {
        const char *colon = memchr(p, ':', (size_t)(at - p));
        if (colon) {
            if (colon > p) { free(r->user); r->user = xs_rd_unescape(p, (size_t)(colon - p)); }
            free(r->password);
            r->password = xs_rd_unescape(colon + 1, (size_t)(at - colon - 1));
        } else {
            free(r->password);
            r->password = xs_rd_unescape(p, (size_t)(at - p));
        }
        p = at + 1;
    }
    const char *end = p + strcspn(p, ":/");
    if (end > p) { free(r->host); r->host = xs_strndup_(p, (size_t)(end - p)); }
    if (*end == ':') { r->port = atoi(end + 1); end += 1 + strspn(end + 1, "0123456789"); }
    if (*end == '/' && end[1]) r->db = atoi(end + 1);
}

/* createClient({ url, password, username, database, socket: { host, port } }): the options arrive as JSON */
XS_NOINLINE xn_redis_NativeRedis *xn_redis_NativeRedis_new(xs_str *config) {
    xn_redis_NativeRedis *r = xs_obj_alloc(sizeof *r, xn_redis_NativeRedis_free, xn_redis_NativeRedis_print);
    xs_jarena_reset();
    xs_jv *cfg = xs_json_parse(config);
    r->host = xs_strndup_("localhost", 9);
    r->port = 6379;
    char *url = xs_rd_cfg_str(cfg, "url");
    if (url) { xs_rd_parse_url(r, url); free(url); }
    xs_jv *sock = cfg ? xs_json_field_opt(cfg, "socket") : NULL;
    if (sock && sock->kind == XJ_OBJ) {
        char *h = xs_rd_cfg_str(sock, "host");
        if (h) { free(r->host); r->host = h; }
        r->port = (int)xs_rd_cfg_int(sock, "port", r->port);
    }
    char *pw = xs_rd_cfg_str(cfg, "password");
    if (pw) { free(r->password); r->password = pw; }
    char *user = xs_rd_cfg_str(cfg, "username");
    if (user) { free(r->user); r->user = user; }
    r->db = (int)xs_rd_cfg_int(cfg, "database", r->db);
    xs_jv_free(cfg);
    r->id = __atomic_fetch_add(&xs_rd_nclients, 1, __ATOMIC_RELAXED);
    if (r->id >= XS_RD_MAX_CLIENTS) xs_panic("redis: too many clients (8)");
    return r;
}

/* ---- RESP ---- */
typedef struct xs_rd_reply {
    char type;                  /* + - : $ * ; '_' for nil */
    long long n;                /* integer, or the length of an array */
    char *s;                    /* simple and bulk strings, errors */
    size_t len;
    struct xs_rd_reply **el;
} xs_rd_reply;
static void xs_rd_reply_free(xs_rd_reply *r) {
    if (!r) return;
    if (r->el) for (long long i = 0; i < r->n; i++) xs_rd_reply_free(r->el[i]);
    free(r->el);
    free(r->s);
    free(r);
}
static bool xs_rd_fill(xs_rd_conn *c) {
    if (c->in_off > 0 && c->in_off == c->in_len) c->in_off = c->in_len = 0;
    if (c->in_len == c->in_cap) {
        if (c->in_off > 0) { memmove(c->in, c->in + c->in_off, c->in_len - c->in_off); c->in_len -= c->in_off; c->in_off = 0; }
        else { c->in_cap = c->in_cap ? c->in_cap * 2 : 16384; c->in = realloc(c->in, c->in_cap); if (!c->in) xs_panic("out of memory"); }
    }
    for (;;) {
        ssize_t r = read(c->fd, c->in + c->in_len, c->in_cap - c->in_len);
        if (r > 0) { c->in_len += (size_t)r; return true; }
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && errno == EAGAIN) { xs_wait_fd(c->fd, EVFILT_READ); continue; }
        c->broken = true;
        return false;
    }
}
/* the next line (without \r\n), malloc'd */
static char *xs_rd_line(xs_rd_conn *c, size_t *len) {
    for (;;) {
        char *start = c->in + c->in_off;
        size_t avail = c->in_len - c->in_off;
        char *nl = avail >= 2 ? xs_find(start, avail, "\r\n", 2) : NULL;
        if (nl) {
            *len = (size_t)(nl - start);
            char *s = xs_strndup_(start, *len);
            c->in_off += *len + 2;
            return s;
        }
        if (avail > (64u << 20)) { c->broken = true; return NULL; }
        if (!xs_rd_fill(c)) return NULL;
    }
}
static bool xs_rd_bytes(xs_rd_conn *c, char *out, size_t n) {
    size_t got = 0;
    while (got < n) {
        size_t avail = c->in_len - c->in_off;
        if (avail) {
            size_t k = avail < n - got ? avail : n - got;
            memcpy(out + got, c->in + c->in_off, k);
            c->in_off += k;
            got += k;
        } else if (!xs_rd_fill(c)) return false;
    }
    return true;
}
static xs_rd_reply *xs_rd_read(xs_rd_conn *c, int depth) {
    size_t len;
    char *line = xs_rd_line(c, &len);
    if (!line) return NULL;
    xs_rd_reply *r = calloc(1, sizeof *r);
    r->type = line[0];
    switch (line[0]) {
    case '+': case '-':
        r->s = xs_strndup_(line + 1, len - 1);
        r->len = len - 1;
        break;
    case ':':
        r->n = strtoll(line + 1, NULL, 10);
        break;
    case '$': {
        long long n = strtoll(line + 1, NULL, 10);
        if (n < 0) { r->type = '_'; break; }
        if (n > (512LL << 20)) { c->broken = true; free(line); free(r); return NULL; }
        r->s = malloc((size_t)n + 1);
        r->len = (size_t)n;
        char crlf[2];
        if (!xs_rd_bytes(c, r->s, (size_t)n) || !xs_rd_bytes(c, crlf, 2)) { free(line); xs_rd_reply_free(r); return NULL; }
        r->s[n] = 0;
        break;
    }
    case '*': {
        long long n = strtoll(line + 1, NULL, 10);
        if (n < 0) { r->type = '_'; break; }
        if (depth > 16 || n > 10000000) { c->broken = true; free(line); free(r); return NULL; }
        r->el = calloc((size_t)(n ? n : 1), sizeof(xs_rd_reply *));
        for (long long i = 0; i < n; i++) {
            if (!(r->el[i] = xs_rd_read(c, depth + 1))) { r->n = i; free(line); xs_rd_reply_free(r); return NULL; }
            r->n = i + 1;
        }
        r->n = n;
        break;
    }
    default:
        c->broken = true;
        free(line);
        free(r);
        return NULL;
    }
    free(line);
    return r;
}
static bool xs_rd_send(xs_rd_conn *c, int argc, const char **argv, const size_t *argl) {
    xs_sb sb = {0};
    char head[32];
    snprintf(head, sizeof head, "*%d\r\n", argc);
    xs_sb_puts(&sb, head);
    for (int i = 0; i < argc; i++) {
        snprintf(head, sizeof head, "$%zu\r\n", argl[i]);
        xs_sb_puts(&sb, head);
        xs_sb_put(&sb, argv[i], argl[i]);
        xs_sb_put(&sb, "\r\n", 2);
    }
    bool ok = xs_write_all(c->fd, sb.data, sb.len);
    free(sb.data);
    if (!ok) c->broken = true;
    return ok;
}
/* one command on c; NULL when the connection broke */
static xs_rd_reply *xs_rd_call(xs_rd_conn *c, int argc, const char **argv, const size_t *argl) {
    if (!xs_rd_send(c, argc, argv, argl)) return NULL;
    return xs_rd_read(c, 0);
}

static void xs_rd_close(xs_rd_conn *c) {
    if (c->fd >= 0) close(c->fd);
    free(c->in);
    free(c);
}
static xs_rd_conn *xs_rd_open(xn_redis_NativeRedis *r, char *err, size_t errcap) {
    xs_rd_conn *c = calloc(1, sizeof *c);
    if (!c) xs_panic("out of memory");
    c->fd = xs_connect(r->host, r->port);
    if (c->fd < 0) { snprintf(err, errcap, "connect ECONNREFUSED %s:%d", r->host, r->port); free(c); return NULL; }
    if (r->password) {
        const char *av[3] = {"AUTH", r->user ? r->user : r->password, r->password};
        size_t al[3] = {4, strlen(av[1]), strlen(r->password)};
        xs_rd_reply *rep = xs_rd_call(c, r->user ? 3 : 2, av, al);
        if (!rep || rep->type == '-') {
            snprintf(err, errcap, "%s", rep && rep->s ? rep->s : "connection lost during AUTH");
            xs_rd_reply_free(rep);
            xs_rd_close(c);
            return NULL;
        }
        xs_rd_reply_free(rep);
    }
    if (r->db) {
        char db[16];
        snprintf(db, sizeof db, "%d", r->db);
        const char *av[2] = {"SELECT", db};
        size_t al[2] = {6, strlen(db)};
        xs_rd_reply *rep = xs_rd_call(c, 2, av, al);
        if (!rep || rep->type == '-') {
            snprintf(err, errcap, "%s", rep && rep->s ? rep->s : "connection lost during SELECT");
            xs_rd_reply_free(rep);
            xs_rd_close(c);
            return NULL;
        }
        xs_rd_reply_free(rep);
    }
    return c;
}
static void xs_rd_throw(const char *msg) {
    static XS_TLS char buf[600];
    snprintf(buf, sizeof buf, "%s", msg);
    xs_throw(xs_Error_from(buf));
}
static void xs_rd_wake(xs_rd_wait *w) {
    if (w->parked) { w->parked = false; xs_ready_push(w->co); }
}
static void xs_rd_park(xs_rd_wait *w) {
    w->parked = true;
    xs_coro_yield();
    w->parked = false;
}
/* the writing turn: one coroutine writes (and opens the connection) at a time, so commands never interleave */
static void xs_rd_write_begin(xs_rd_local *l) {
    if (!l->writing) { l->writing = true; return; }
    xs_rd_wait w = {xs_cur_coro, false, false, false, NULL, NULL};
    if (l->wq_tail) l->wq_tail->next = &w; else l->wq_head = &w;
    l->wq_tail = &w;
    while (!w.done) xs_rd_park(&w); /* handed the turn by xs_rd_write_end */
}
static void xs_rd_write_end(xs_rd_local *l) {
    xs_rd_wait *w = l->wq_head;
    if (!w) { l->writing = false; return; }
    l->wq_head = w->next;
    if (!l->wq_head) l->wq_tail = NULL;
    w->done = true; /* the turn passes on: writing stays true */
    xs_rd_wake(w);
}
/* the connection broke: every pending command fails, the next one opens a new connection */
static void xs_rd_fail_all(xs_rd_local *l) {
    if (l->c) { xs_rd_close(l->c); l->c = NULL; }
    xs_rd_wait *w = l->head;
    l->head = l->tail = NULL;
    while (w) {
        xs_rd_wait *n = w->next;
        w->failed = w->done = true;
        xs_rd_wake(w);
        w = n;
    }
}
static void xs_rd_throw(const char *msg);
/* one command: written in turn, its reply read by whoever holds the reading turn */
static xs_rd_reply *xs_rd_pipelined(xn_redis_NativeRedis *r, int argc, const char **argv, const size_t *argl) {
    xs_rd_local *l = &xs_rd_locals[r->id];
    xs_rd_write_begin(l);
    if (!l->c && l->head) { xs_rd_write_end(l); xs_rd_throw("redis: the connection was lost"); }
    if (!l->c) {
        char err[512] = {0};
        l->c = xs_rd_open(r, err, sizeof err);
        if (!l->c) { xs_rd_write_end(l); xs_rd_throw(err); }
    }
    xs_rd_wait p = {xs_cur_coro, false, false, false, NULL, NULL};
    if (l->tail) l->tail->next = &p; else l->head = &p;
    l->tail = &p;
    bool sent = xs_rd_send(l->c, argc, argv, argl);
    xs_rd_write_end(l);
    if (!sent) xs_rd_fail_all(l);
    while (!p.done) {
        if (l->reading) { xs_rd_park(&p); continue; }
        l->reading = true;
        while (!p.done) {
            xs_rd_reply *rep = l->c ? xs_rd_read(l->c, 0) : NULL;
            if (!rep) { xs_rd_fail_all(l); break; }
            xs_rd_wait *h = l->head;
            l->head = h->next;
            if (!l->head) l->tail = NULL;
            h->reply = rep;
            h->done = true;
            if (h != &p) xs_rd_wake(h);
        }
        l->reading = false;
        if (l->head) xs_rd_wake(l->head); /* the next in line reads now */
    }
    if (p.failed) { xs_rd_reply_free(p.reply); xs_rd_throw("redis: the connection was lost"); }
    return p.reply;
}
/* a command from xisto: the arguments are strings; the reply, or a thrown Error */
static xs_rd_reply *xs_rd_command(xn_redis_NativeRedis *r, xs_arr_ref *args) {
    int n = args ? (int)args->len : 0;
    if (n == 0) xs_panic("redis: empty command");
    const char **av = malloc(sizeof(char *) * (size_t)n);
    size_t *al = malloc(sizeof(size_t) * (size_t)n);
    for (int i = 0; i < n; i++) {
        xs_str *s = args->data[i];
        av[i] = s ? s->data : "";
        al[i] = s ? s->len : 0;
    }
    xs_rd_reply *rep = xs_rd_pipelined(r, n, av, al);
    free(av);
    free(al);
    if (rep->type == '-') {
        char msg[600];
        snprintf(msg, sizeof msg, "%s", rep->s ? rep->s : "ERR");
        xs_rd_reply_free(rep);
        xs_rd_throw(msg);
    }
    return rep;
}

/* connect(): opens a connection now, so a wrong address or password fails at start, not at the first command */
XS_NOINLINE void xn_redis_NativeRedis_connect(xn_redis_NativeRedis *r) {
    const char *av[1] = {"PING"};
    size_t al[1] = {4};
    xs_rd_reply *rep = xs_rd_pipelined(r, 1, av, al);
    bool ok = rep->type == '+';
    char msg[512];
    snprintf(msg, sizeof msg, "%s", rep->type == '-' && rep->s ? rep->s : "redis: unexpected reply to PING");
    xs_rd_reply_free(rep);
    if (!ok) xs_rd_throw(msg);
}
/* string replies (+OK, bulk, nil -> null) */
XS_NOINLINE xs_str *xn_redis_NativeRedis_str(xn_redis_NativeRedis *r, xs_arr_ref *args) {
    xs_rd_reply *rep = xs_rd_command(r, args);
    xs_str *s = NULL;
    if (rep->type == '+' || rep->type == '$') s = xs_str_new(rep->s, rep->len);
    else if (rep->type == ':') { char b[24]; int n = snprintf(b, sizeof b, "%lld", rep->n); s = xs_str_new(b, (size_t)n); }
    else if (rep->type != '_') { xs_rd_reply_free(rep); xs_panic("redis: expected a string reply"); }
    xs_rd_reply_free(rep);
    return s;
}
/* integer replies */
XS_NOINLINE int64_t xn_redis_NativeRedis_int(xn_redis_NativeRedis *r, xs_arr_ref *args) {
    xs_rd_reply *rep = xs_rd_command(r, args);
    int64_t v = rep->type == ':' ? rep->n : rep->type == '$' || rep->type == '+' ? strtoll(rep->s, NULL, 10) : 0;
    xs_rd_reply_free(rep);
    return v;
}
/* arrays of strings (KEYS, SMEMBERS, LRANGE...); a nil element becomes "" */
XS_NOINLINE xs_arr_ref *xn_redis_NativeRedis_strs(xn_redis_NativeRedis *r, xs_arr_ref *args) {
    xs_rd_reply *rep = xs_rd_command(r, args);
    xs_arr_ref *a = xs_arr_ref_new(rep->type == '*' ? (size_t)rep->n : 0, xs_print_str_quoted_);
    for (long long i = 0; rep->type == '*' && i < rep->n; i++) {
        xs_rd_reply *e = rep->el[i];
        xs_arr_ref_push(a, e->s ? xs_str_new(e->s, e->len) : xs_str_new("", 0));
    }
    xs_rd_reply_free(rep);
    return a;
}
/* arrays of nullable strings (MGET): a nil element is null */
XS_NOINLINE xs_arr_ref *xn_redis_NativeRedis_strsn(xn_redis_NativeRedis *r, xs_arr_ref *args) {
    xs_rd_reply *rep = xs_rd_command(r, args);
    xs_arr_ref *a = xs_arr_ref_new(rep->type == '*' ? (size_t)rep->n : 0, xs_print_str_quoted_or_null_);
    for (long long i = 0; rep->type == '*' && i < rep->n; i++) {
        xs_rd_reply *e = rep->el[i];
        xs_arr_ref_push(a, e->type == '_' ? NULL : xs_str_new(e->s ? e->s : "", e->len));
    }
    xs_rd_reply_free(rep);
    return a;
}
/* quit(): closes this thread's connection once nothing is pending on it */
XS_NOINLINE void xn_redis_NativeRedis_close(xn_redis_NativeRedis *r) {
    xs_rd_local *l = &xs_rd_locals[r->id];
    if (l->c && !l->head && !l->writing && !l->reading) { xs_rd_close(l->c); l->c = NULL; }
}
