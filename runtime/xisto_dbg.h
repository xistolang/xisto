/* ---- debugger agent: only in binaries built with --debug (docs/debugger.md) ----
   The compiler puts XS_DBG(point) before every statement and XS_DBG_ENTER(name) at the start of every function.
   A statement hook costs a store and a load of xs_dbg_armed while nothing is asked of the debugger. When a
   breakpoint, a step or a pause is pending, xs_dbg_hit decides whether to stop; a stopped thread waits there
   while the agent thread answers the client (Debug Adapter Protocol over XISTO_DEBUG_FD or 127.0.0.1). */
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>

typedef struct { const char *file; int line, col; } xs_dbg_point;
extern const xs_dbg_point xs_dbg_points[];
extern const int xs_dbg_npoints;

/* how the debugger shows a value of one static type: render prints it as console.log would (to stdout,
   redirected while rendering); count/child list the fields of an object or the elements of an array */
typedef struct xs_dbg_child xs_dbg_child;
typedef struct xs_dbg_type {
    const char *name;
    void (*render)(void *p);
    int (*count)(void *p);
    void (*child)(void *p, int i, xs_dbg_child *c);
    const char *(*set)(void *p, const char *text, int mode); /* NULL: not editable; returns an error message or NULL */
} xs_dbg_type;
struct xs_dbg_child { const char *name; int index; const xs_dbg_type *type; void *addr; };
/* a local: visible while the frame's point is in [from, to] and its slot holds its address */
typedef struct { const char *name; const xs_dbg_type *type; int from, to, mode; } xs_dbg_var; /* mode: 0 borrowed (a parameter), 1 owned, 2 read-only (const, captured, this), 3 a QBE shadow copy (read-only) */

/* ---- parsing what the user types for setVariable (F4) ---- */
static const char *xs_dbg_trim(const char *t, size_t *n) {
    while (*t == ' ' || *t == '\t') t++;
    size_t l = strlen(t);
    while (l && (t[l - 1] == ' ' || t[l - 1] == '\t' || t[l - 1] == '\n')) l--;
    *n = l;
    return t;
}
static bool xs_dbg_is_null(const char *t) { size_t n; t = xs_dbg_trim(t, &n); return n == 4 && strncmp(t, "null", 4) == 0; }
static const char *xs_dbg_parse_int(const char *t0, int64_t *out) {
    size_t n;
    const char *t = xs_dbg_trim(t0, &n);
    char buf[32];
    if (!n || n >= sizeof buf) return "expected an integer";
    memcpy(buf, t, n);
    buf[n] = 0;
    char *end;
    errno = 0;
    long long v = strtoll(buf, &end, 10);
    if (*end) return "expected an integer";
    if (errno == ERANGE) return "out of range for int";
    *out = v;
    return NULL;
}
static const char *xs_dbg_parse_num(const char *t0, double *out) {
    size_t n;
    const char *t = xs_dbg_trim(t0, &n);
    char buf[64];
    if (!n || n >= sizeof buf) return "expected a number";
    memcpy(buf, t, n);
    buf[n] = 0;
    if (strcmp(buf, "NaN") == 0) { *out = NAN; return NULL; }
    if (strcmp(buf, "Infinity") == 0) { *out = INFINITY; return NULL; }
    if (strcmp(buf, "-Infinity") == 0) { *out = -INFINITY; return NULL; }
    char *end;
    double v = strtod(buf, &end);
    if (*end || isnan(v) || isinf(v)) return "expected a number";
    *out = v;
    return NULL;
}
static const char *xs_dbg_parse_bool(const char *t0, bool *out) {
    size_t n;
    const char *t = xs_dbg_trim(t0, &n);
    if (n == 4 && strncmp(t, "true", 4) == 0) { *out = true; return NULL; }
    if (n == 5 && strncmp(t, "false", 5) == 0) { *out = false; return NULL; }
    return "expected true or false";
}
/* 'text' or "text" with \n \t \\ \' \" escapes; a bare word is accepted too (enum members) */
static const char *xs_dbg_parse_str(const char *t0, char **out, size_t *len) {
    size_t n;
    const char *t = xs_dbg_trim(t0, &n);
    char *o = malloc(n + 1);
    size_t k = 0;
    if (n >= 2 && (t[0] == '\'' || t[0] == '"') && t[n - 1] == t[0]) {
        for (size_t i = 1; i + 1 < n; i++) {
            char c = t[i];
            if (c == '\\' && i + 2 < n) {
                char e = t[++i];
                c = e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == '0' ? '\0' : e;
            } else if (c == t[0]) { free(o); return "unescaped quote inside the string"; }
            o[k++] = c;
        }
    } else {
        for (size_t i = 0; i < n; i++) {
            if (!(isalnum((unsigned char)t[i]) || t[i] == '_' || t[i] == '-')) { free(o); return "expected a string: 'text' or \"text\""; }
            o[k++] = t[i];
        }
        if (!k) { free(o); return "expected a string: 'text' or \"text\""; }
    }
    o[k] = 0;
    *out = o;
    *len = k;
    return NULL;
}
/* a string slot takes a new owned string; the old one is released only if the slot owned it (a parameter
   borrows its caller's: that one is left alone, and the new string stays alive until exit) */
static void xs_dbg_str_store(void *slot, void *nv, int mode) {
    void *old = *(void **)slot;
    *(void **)slot = nv;
    if (mode == 1 && old) xs_release(old);
}

typedef struct xs_dbg_frame {
    struct xs_dbg_frame *parent;
    const char *name;
    int point;          /* the statement about to run */
    int depth;
    int last;           /* the previous statement hook of this frame (-1 before the first) */
    unsigned long serial;
    const xs_dbg_var *vars;
    void **slots;
    int nvars;
} xs_dbg_frame;

static XS_TLS xs_dbg_frame *xs_dbg_top;
static XS_TLS unsigned long xs_dbg_serial;
/* threads that ran Xisto code: the DAP threads (1 = main; workers after). A thread's frames are read through
   its xs_dbg_top, which stays valid while it is parked at a hook (all-stop). */
static XS_TLS int xs_dbg_tid;
typedef struct { xs_dbg_frame **topp; bool alive; } xs_dbg_thread;
static xs_dbg_thread xs_dbg_threads[256];
static int xs_dbg_nthreads;
static pthread_mutex_t xs_dbg_tmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t xs_dbg_tkey;
static void xs_dbg_thread_gone(void *p) {
    int id = (int)(intptr_t)p;
    pthread_mutex_lock(&xs_dbg_tmu);
    if (id >= 1 && id <= xs_dbg_nthreads) xs_dbg_threads[id - 1].alive = false;
    pthread_mutex_unlock(&xs_dbg_tmu);
}
static void xs_dbg_register(void) {
    pthread_mutex_lock(&xs_dbg_tmu);
    if (xs_dbg_nthreads < 256) {
        xs_dbg_threads[xs_dbg_nthreads] = (xs_dbg_thread){&xs_dbg_top, true};
        xs_dbg_tid = ++xs_dbg_nthreads;
        pthread_setspecific(xs_dbg_tkey, (void *)(intptr_t)xs_dbg_tid);
    } else xs_dbg_tid = -1;
    pthread_mutex_unlock(&xs_dbg_tmu);
}
static volatile int xs_dbg_armed;
static int *volatile xs_dbg_bpv; /* breakpoint id per point (xs_dbg.bp), read by the inline hook */
static void xs_dbg_hit(xs_dbg_frame *f);
static void xs_dbg_stop(xs_dbg_frame *f, const char *reason, int bp_id);
static void xs_dbg_refs_reset(void);
/* a frame id is thread * 100000 + depth from the top (thread 0: the stopped thread) */
#define XS_DBG_FRAMES 100000
static xs_dbg_frame *xs_dbg_thread_top(int tid);
static bool xs_dbg_bp_fires(xs_dbg_frame *f, int id);
static inline void xs_dbg_leave(xs_dbg_frame *f) { xs_dbg_top = f->parent; }

#define XS_DBG_ENTER(nm) \
    if (__builtin_expect(!xs_dbg_tid, 0)) xs_dbg_register(); \
    xs_dbg_frame dbgf_ __attribute__((cleanup(xs_dbg_leave))) = { xs_dbg_top, (nm), 0, xs_dbg_top ? xs_dbg_top->depth + 1 : 0, -1, ++xs_dbg_serial, NULL, NULL, 0 }; \
    xs_dbg_top = &dbgf_
/* the frame is re-installed at every statement: after a throw (longjmp) the first statement of the catch
   repairs the shadow stack without the try having to save it */
/* xs_dbg_armed: 0 nothing asked; 1 only breakpoints (only their points call xs_dbg_hit); 2 a step, a pause or a
   stop is pending (every statement does) */
#define XS_DBG(id) do { xs_dbg_top = &dbgf_; dbgf_.point = (id); \
    if (__builtin_expect(xs_dbg_armed, 0) && (xs_dbg_armed > 1 || xs_dbg_bpv[(id)])) xs_dbg_hit(&dbgf_); } while (0)

enum { XS_STEP_NONE, XS_STEP_NEXT, XS_STEP_IN, XS_STEP_OUT };
static struct {
    int fd;                       /* the client; -1: no debugger attached */
    pthread_mutex_t mu, wmu;      /* state; writes to fd */
    pthread_cond_t cv;            /* stopped -> resumed, not configured -> configured */
    bool configured;
    int *bp;                      /* breakpoint id per point, 0 = none */
    int nbp;                      /* points with a breakpoint */
    int next_bp_id;
    int step, step_depth;
    unsigned long step_serial;
    pthread_t step_thread;
    bool pause_req;
    bool exc_all, exc_uncaught;   /* setExceptionBreakpoints */
    bool log_stdout;              /* launch {logToStdout}: logpoints print in the program's own output (the CLI) */
    char exc_name[64], exc_message[512];
    bool stopped;
    xs_dbg_frame *stopped_frame;
    pthread_t stopped_thread;
    int stopped_tid;
    void (*job)(void *);          /* work the stopped thread runs for the agent (render, set) */
    void *job_arg;
    long seq;
} xs_dbg = { -1, PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER };

/* per breakpoint id: condition, hit condition, log message, hits so far */
typedef struct { char *cond, *hit, *log; long hits; int next; } xs_dbg_bpinfo; /* next: another breakpoint on the same line */
static xs_dbg_bpinfo *xs_dbg_bpi;
static int xs_dbg_nbpi;
static xs_dbg_bpinfo *xs_dbg_bpinfo_of(int id) {
    if (id >= xs_dbg_nbpi) {
        xs_dbg_bpi = realloc(xs_dbg_bpi, sizeof(xs_dbg_bpinfo) * (size_t)(id + 16));
        memset(xs_dbg_bpi + xs_dbg_nbpi, 0, sizeof(xs_dbg_bpinfo) * (size_t)(id + 16 - xs_dbg_nbpi));
        xs_dbg_nbpi = id + 16;
    }
    return &xs_dbg_bpi[id];
}
static void xs_dbg_rearm(void) {
    xs_dbg_bpv = xs_dbg.bp;
    xs_dbg_armed = xs_dbg.step != XS_STEP_NONE || xs_dbg.pause_req || xs_dbg.stopped ? 2 : xs_dbg.nbp > 0 && xs_dbg.bp ? 1 : 0;
}

static void xs_dbg_send(dj_buf *b) {
    pthread_mutex_lock(&xs_dbg.wmu);
    if (xs_dbg.fd >= 0 && !dj_write_message(xs_dbg.fd, b->data)) xs_dbg.fd = -1;
    pthread_mutex_unlock(&xs_dbg.wmu);
    free(b->data);
}
static long xs_dbg_next_seq(void) { return __atomic_add_fetch(&xs_dbg.seq, 1, __ATOMIC_SEQ_CST); }
static void xs_dbg_event(const char *event, const char *body) {
    dj_buf b = {0};
    dj_printf(&b, "{\"seq\":%ld,\"type\":\"event\",\"event\":\"%s\"", xs_dbg_next_seq(), event);
    if (body) dj_printf(&b, ",\"body\":%s", body);
    dj_puts(&b, "}");
    xs_dbg_send(&b);
}
static void xs_dbg_respond(const dj *req, bool ok, const char *body, const char *message) {
    dj_buf b = {0};
    dj_printf(&b, "{\"seq\":%ld,\"type\":\"response\",\"request_seq\":%lld,\"success\":%s,\"command\":", xs_dbg_next_seq(), dj_int(req, "seq", 0), ok ? "true" : "false");
    dj_quote(&b, dj_str(req, "command", ""));
    if (message) { dj_puts(&b, ",\"message\":"); dj_quote(&b, message); }
    if (body) dj_printf(&b, ",\"body\":%s", body);
    dj_puts(&b, "}");
    xs_dbg_send(&b);
}

/* a statement that continues a line already started in this frame (a; b; on one line) never stops */
static void xs_dbg_hit(xs_dbg_frame *f) {
    int p = f->point;
    const xs_dbg_point *pt = &xs_dbg_points[p];
    bool cont = f->last >= 0 && f->last < p && xs_dbg_points[f->last].line == pt->line && xs_dbg_points[f->last].file == pt->file;
    f->last = p;
    /* fast path, no lock: armed only because some other line has a breakpoint (racy reads are benign: the
       state is re-read under the lock before stopping) */
    if (!__atomic_load_n(&xs_dbg.stopped, __ATOMIC_RELAXED) && !__atomic_load_n(&xs_dbg.pause_req, __ATOMIC_RELAXED) &&
        __atomic_load_n(&xs_dbg.step, __ATOMIC_RELAXED) == XS_STEP_NONE && !(xs_dbg.bp && __atomic_load_n(&xs_dbg.bp[p], __ATOMIC_RELAXED)))
        return;
    pthread_mutex_lock(&xs_dbg.mu);
    while (xs_dbg.stopped) pthread_cond_wait(&xs_dbg.cv, &xs_dbg.mu); /* all-stop: another thread is stopped */
    const char *reason = NULL;
    int bp_id = 0;
    if (xs_dbg.pause_req) {
        reason = "pause";
        xs_dbg.pause_req = false;
    } else if (!cont) {
        if (xs_dbg.step != XS_STEP_NONE && pthread_equal(xs_dbg.step_thread, pthread_self())) {
            if (xs_dbg.step == XS_STEP_IN) reason = "step";
            else if (xs_dbg.step == XS_STEP_NEXT && (f->serial == xs_dbg.step_serial || f->depth < xs_dbg.step_depth)) reason = "step";
            else if (xs_dbg.step == XS_STEP_OUT && f->depth < xs_dbg.step_depth) reason = "step";
        }
        if (xs_dbg.bp && xs_dbg.bp[p] && !reason) {
            for (int id = xs_dbg.bp[p]; id; id = xs_dbg_bpinfo_of(id)->next) /* every one counts its hit and logs */
                if (xs_dbg_bp_fires(f, id) && !bp_id) {
                    bp_id = id;
                    reason = "breakpoint";
                }
        } else if (xs_dbg.bp && xs_dbg.bp[p] && !xs_dbg_bpinfo_of(xs_dbg.bp[p])->log) {
            bp_id = xs_dbg.bp[p]; /* a step landed on a breakpoint: report it as the breakpoint */
            reason = "breakpoint";
        }
    }
    if (!reason) { pthread_mutex_unlock(&xs_dbg.mu); return; }
    xs_dbg_stop(f, reason, bp_id);
    pthread_mutex_unlock(&xs_dbg.mu);
}

/* stop this thread at frame f until the client resumes it (called with mu held) */
static void xs_dbg_stop_ex(xs_dbg_frame *f, const char *reason, int bp_id, const char *description);
static void xs_dbg_stop(xs_dbg_frame *f, const char *reason, int bp_id) { xs_dbg_stop_ex(f, reason, bp_id, NULL); }
static void xs_dbg_stop_ex(xs_dbg_frame *f, const char *reason, int bp_id, const char *description) {
    xs_dbg.step = XS_STEP_NONE;
    xs_dbg.stopped = true;
    xs_dbg.stopped_frame = f;
    xs_dbg.stopped_thread = pthread_self();
    xs_dbg.stopped_tid = xs_dbg_tid > 0 ? xs_dbg_tid : 1;
    xs_dbg_rearm();
    fflush(stdout);
    dj_buf body = {0};
    dj_printf(&body, "{\"reason\":\"%s\",\"threadId\":%d,\"allThreadsStopped\":true", reason, xs_dbg.stopped_tid);
    if (bp_id) dj_printf(&body, ",\"hitBreakpointIds\":[%d]", bp_id);
    if (description) { dj_puts(&body, ",\"description\":"); dj_quote(&body, description); dj_puts(&body, ",\"text\":"); dj_quote(&body, description); }
    dj_puts(&body, "}");
    xs_dbg_event("stopped", body.data);
    free(body.data);
    while (xs_dbg.stopped) {
        if (xs_dbg.job) {
            xs_dbg.job(xs_dbg.job_arg);
            xs_dbg.job = NULL;
            pthread_cond_broadcast(&xs_dbg.cv);
        } else pthread_cond_wait(&xs_dbg.cv, &xs_dbg.mu);
    }
    xs_dbg_refs_reset();
    xs_dbg_rearm();
}

/* the event loop woke with nothing to run: a pending pause stops at the frame that is waiting (listen) */
static void (*xs_dbg_waker)(void);
static void xs_dbg_idle(void) {
    if (!xs_dbg_armed) return;
    pthread_mutex_lock(&xs_dbg.mu);
    if (xs_dbg.pause_req && xs_dbg_top) {
        xs_dbg.pause_req = false;
        xs_dbg_stop(xs_dbg_top, "pause", 0);
    }
    pthread_mutex_unlock(&xs_dbg.mu);
}

/* resume the stopped thread; step = what it should do next (called with mu held) */
static void xs_dbg_resume(int step) {
    if (!xs_dbg.stopped) return;
    xs_dbg.step = step;
    if (step != XS_STEP_NONE) {
        xs_dbg.step_depth = xs_dbg.stopped_frame->depth;
        xs_dbg.step_serial = xs_dbg.stopped_frame->serial;
        xs_dbg.step_thread = xs_dbg.stopped_thread; /* steps follow the thread that stopped */
    }
    xs_dbg.stopped = false;
    xs_dbg.stopped_frame = NULL;
    xs_dbg_rearm();
    pthread_cond_broadcast(&xs_dbg.cv);
}

static char *xs_dbg_realpath(const char *p) {
    char buf[PATH_MAX];
    return realpath(p, buf) ? strdup(buf) : strdup(p);
}

static void xs_dbg_set_breakpoints(const dj *req) {
    const dj *args = dj_get(req, "arguments");
    const char *path0 = dj_str(dj_get(args, "source"), "path", "");
    char *path = xs_dbg_realpath(path0);
    const dj *list = dj_get(args, "breakpoints");
    dj_buf body = {0};
    dj_puts(&body, "{\"breakpoints\":[");
    pthread_mutex_lock(&xs_dbg.mu);
    if (!xs_dbg.bp) xs_dbg.bp = calloc((size_t)(xs_dbg_npoints ? xs_dbg_npoints : 1), sizeof(int));
    for (int i = 0; i < xs_dbg_npoints; i++)
        if (xs_dbg.bp[i] && strcmp(xs_dbg_points[i].file, path) == 0) { xs_dbg.bp[i] = 0; xs_dbg.nbp--; }
    for (int k = 0; list && list->kind == DJ_ARR && k < list->n; k++) {
        int want = (int)dj_int(list->items[k], "line", 0);
        int line = INT_MAX; /* the first line at or after the requested one that has a statement */
        for (int i = 0; i < xs_dbg_npoints; i++)
            if (xs_dbg_points[i].line >= want && xs_dbg_points[i].line < line && strcmp(xs_dbg_points[i].file, path) == 0) line = xs_dbg_points[i].line;
        if (k) dj_puts(&body, ",");
        if (line == INT_MAX) {
            dj_printf(&body, "{\"verified\":false,\"line\":%d,\"message\":\"no statement at or after this line\"}", want);
            continue;
        }
        int id = ++xs_dbg.next_bp_id;
        xs_dbg_bpinfo *bi = xs_dbg_bpinfo_of(id);
        const char *cond = dj_str(list->items[k], "condition", NULL), *hit = dj_str(list->items[k], "hitCondition", NULL), *log = dj_str(list->items[k], "logMessage", NULL);
        bi->cond = cond && *cond ? strdup(cond) : NULL;
        bi->hit = hit && *hit ? strdup(hit) : NULL;
        bi->log = log && *log ? strdup(log) : NULL;
        bi->hits = 0;
        bi->next = 0;
        for (int i = 0; i < xs_dbg_npoints; i++)
            if (xs_dbg_points[i].line == line && strcmp(xs_dbg_points[i].file, path) == 0) {
                if (!xs_dbg.bp[i]) xs_dbg.nbp++;
                else if (!bi->next) bi->next = xs_dbg.bp[i]; /* the same line again: chained */
                xs_dbg.bp[i] = id;
            }
        dj_printf(&body, "{\"id\":%d,\"verified\":true,\"line\":%d}", id, line);
    }
    xs_dbg_rearm();
    pthread_mutex_unlock(&xs_dbg.mu);
    dj_puts(&body, "]}");
    xs_dbg_respond(req, true, body.data, NULL);
    free(body.data);
    free(path);
}

static void xs_dbg_stack_trace(const dj *req) {
    dj_buf body = {0};
    pthread_mutex_lock(&xs_dbg.mu);
    if (!xs_dbg.stopped) {
        pthread_mutex_unlock(&xs_dbg.mu);
        xs_dbg_respond(req, false, NULL, "not stopped");
        return;
    }
    int tid = (int)dj_int(dj_get(req, "arguments"), "threadId", xs_dbg.stopped_tid);
    dj_puts(&body, "{\"stackFrames\":[");
    int n = 0;
    for (xs_dbg_frame *f = xs_dbg_thread_top(tid); f; f = f->parent, n++) {
        const xs_dbg_point *pt = &xs_dbg_points[f->point];
        const char *base = strrchr(pt->file, '/');
        if (n) dj_puts(&body, ",");
        dj_printf(&body, "{\"id\":%d,\"name\":", tid * XS_DBG_FRAMES + n);
        dj_quote(&body, f->name);
        dj_printf(&body, ",\"line\":%d,\"column\":%d,\"source\":{\"name\":", pt->line, pt->col);
        dj_quote(&body, base ? base + 1 : pt->file);
        dj_puts(&body, ",\"path\":");
        dj_quote(&body, pt->file);
        dj_puts(&body, "}}");
    }
    pthread_mutex_unlock(&xs_dbg.mu);
    dj_printf(&body, "],\"totalFrames\":%d}", n);
    xs_dbg_respond(req, true, body.data, NULL);
    free(body.data);
}

/* ---- variables (valid while stopped) ----
   A variablesReference names either the locals or the globals of a frame, or a structured value (its type and
   address). The table is emptied when the program resumes. */
typedef struct { int frame, which; const xs_dbg_type *type; void *addr; } xs_dbg_ref; /* which: 0 locals, 1 globals, 2 value */
static xs_dbg_ref *xs_dbg_refs;
static int xs_dbg_nrefs;
static void xs_dbg_refs_reset(void) { xs_dbg_nrefs = 0; }
static int xs_dbg_ref_new(int frame, int which, const xs_dbg_type *t, void *addr) {
    for (int i = 0; i < xs_dbg_nrefs; i++)
        if (xs_dbg_refs[i].which == which && xs_dbg_refs[i].frame == frame && xs_dbg_refs[i].type == t && xs_dbg_refs[i].addr == addr) return i + 1;
    xs_dbg_refs = realloc(xs_dbg_refs, sizeof(xs_dbg_ref) * (size_t)(xs_dbg_nrefs + 1));
    xs_dbg_refs[xs_dbg_nrefs++] = (xs_dbg_ref){frame, which, t, addr};
    return xs_dbg_nrefs;
}
static xs_dbg_frame *xs_dbg_thread_top(int tid) {
    if (tid == xs_dbg.stopped_tid || tid == 0) return xs_dbg.stopped_frame;
    if (tid < 1 || tid > xs_dbg_nthreads || !xs_dbg_threads[tid - 1].alive) return NULL;
    return *xs_dbg_threads[tid - 1].topp;
}
static xs_dbg_frame *xs_dbg_frame_at(int id) {
    xs_dbg_frame *f = xs_dbg_thread_top(id / XS_DBG_FRAMES);
    for (int i = 0; f && i < id % XS_DBG_FRAMES; i++) f = f->parent;
    return f;
}
static xs_dbg_frame *xs_dbg_root(xs_dbg_frame *f) { while (f && f->parent) f = f->parent; return f; }

/* the value as console.log prints it, at most 4 KB (runs on the stopped thread) */
static void xs_dbg_render(dj_buf *out, const xs_dbg_type *t, void *addr) {
    char *text = NULL;
    size_t len = 0;
    FILE *mem = open_memstream(&text, &len);
    if (!mem) { dj_puts(out, "\"?\""); return; }
    FILE *saved = stdout;
    fflush(saved);
    stdout = mem;
    xs_dbg_print_budget = 7; /* six levels of objects, then [Object]: cycles end */
    t->render(addr);
    xs_dbg_print_budget = 0;
    fflush(mem);
    stdout = saved;
    fclose(mem);
    if (len > 4096) { text[4093] = '.'; text[4094] = '.'; text[4095] = '.'; text[4096] = 0; }
    dj_quote(out, text ? text : "");
    free(text);
}
static void xs_dbg_variable(dj_buf *out, int frame, const char *name, int index, const xs_dbg_type *t, void *addr) {
    dj_puts(out, "{\"name\":");
    if (name) dj_quote(out, name);
    else dj_printf(out, "\"[%d]\"", index);
    dj_puts(out, ",\"value\":");
    xs_dbg_render(out, t, addr);
    dj_puts(out, ",\"type\":");
    dj_quote(out, t->name);
    int ref = t->count && t->count(addr) > 0 ? xs_dbg_ref_new(frame, 2, t, addr) : 0;
    dj_printf(out, ",\"variablesReference\":%d", ref);
    if (t->set) dj_puts(out, ",\"presentationHint\":{\"attributes\":[]}");
    else dj_puts(out, ",\"presentationHint\":{\"attributes\":[\"readOnly\"]}");
    dj_puts(out, "}");
}
/* the locals of f visible at its current point; with all, every one, newest declaration first for lookups */
static int xs_dbg_visible(xs_dbg_frame *f, int k) {
    if (!f || k < 0 || k >= f->nvars || !f->slots[k]) return 0;
    return f->point >= f->vars[k].from && f->point <= f->vars[k].to;
}

typedef struct { const dj *req; dj_buf body; const char *error; } xs_dbg_job;

static void xs_dbg_job_scopes(void *p) {
    xs_dbg_job *j = p;
    int id = (int)dj_int(dj_get(j->req, "arguments"), "frameId", 0);
    xs_dbg_frame *f = xs_dbg_frame_at(id);
    if (!f) { j->error = "no such frame"; return; }
    dj_printf(&j->body, "{\"scopes\":[{\"name\":\"Locals\",\"presentationHint\":\"locals\",\"variablesReference\":%d,\"expensive\":false}", xs_dbg_ref_new(id, 0, NULL, NULL));
    if (f->parent) dj_printf(&j->body, ",{\"name\":\"Globals\",\"variablesReference\":%d,\"expensive\":false}", xs_dbg_ref_new(id, 1, NULL, NULL));
    dj_puts(&j->body, "]}");
}
static void xs_dbg_job_variables(void *p) {
    xs_dbg_job *j = p;
    int ref = (int)dj_int(dj_get(j->req, "arguments"), "variablesReference", 0);
    if (ref < 1 || ref > xs_dbg_nrefs) { j->error = "no such variables reference"; return; }
    xs_dbg_ref r = xs_dbg_refs[ref - 1];
    dj_puts(&j->body, "{\"variables\":[");
    int n = 0;
    if (r.which == 2) {
        int count = r.type->count(r.addr);
        for (int i = 0; i < count; i++) {
            xs_dbg_child c = {0};
            r.type->child(r.addr, i, &c);
            if (n++) dj_puts(&j->body, ",");
            xs_dbg_variable(&j->body, r.frame, c.name, c.index, c.type, c.addr);
        }
    } else {
        xs_dbg_frame *f = xs_dbg_frame_at(r.frame);
        if (r.which == 1) f = xs_dbg_root(f);
        for (int k = 0; f && k < f->nvars; k++) {
            if (!xs_dbg_visible(f, k)) continue;
            bool shadowed = false; /* a later declaration of the same name hides this one */
            for (int m = k + 1; m < f->nvars; m++) if (xs_dbg_visible(f, m) && strcmp(f->vars[m].name, f->vars[k].name) == 0) shadowed = true;
            if (shadowed) continue;
            if (n++) dj_puts(&j->body, ",");
            xs_dbg_variable(&j->body, r.frame, f->vars[k].name, 0, f->vars[k].type, f->slots[k]);
        }
    }
    dj_puts(&j->body, "]}");
}

/* a path: name, then .field or [index] steps. Resolves to the type and address of the value. */
static const char *xs_dbg_resolve_m(xs_dbg_frame *f, const char *expr, const xs_dbg_type **t, void **addr, int *mode);
static const char *xs_dbg_resolve(xs_dbg_frame *f, const char *expr, const xs_dbg_type **t, void **addr) { int m; return xs_dbg_resolve_m(f, expr, t, addr, &m); }
static const char *xs_dbg_resolve_m(xs_dbg_frame *f, const char *expr, const xs_dbg_type **t, void **addr, int *mode) {
    const char *s = expr;
    while (*s == ' ') s++;
    char name[128];
    size_t n = 0;
    while ((isalnum((unsigned char)*s) || *s == '_' || *s == '$') && n + 1 < sizeof name) name[n++] = *s++;
    name[n] = 0;
    if (!n) return "expected a variable name (only paths are evaluated: a.b[2].c)";
    *t = NULL;
    xs_dbg_frame *where[2] = {f, xs_dbg_root(f)}; /* the frame, then the globals */
    for (int w = 0; w < 2 && !*t; w++)
        for (int k = where[w]->nvars - 1; k >= 0; k--)
            if (xs_dbg_visible(where[w], k) && strcmp(where[w]->vars[k].name, name) == 0) { *t = where[w]->vars[k].type; *addr = where[w]->slots[k]; *mode = where[w]->vars[k].mode; break; }
    if (!*t) return "not a variable in scope here";
    for (;;) {
        while (*s == ' ') s++;
        if (!*s) return NULL;
        int count = (*t)->count ? (*t)->count(*addr) : 0;
        if (*mode != 3) *mode = 1; /* a field or an element: owned by its object or array (QBE: still read-only) */
        if (*s == '.') {
            s++;
            n = 0;
            while ((isalnum((unsigned char)*s) || *s == '_' || *s == '$') && n + 1 < sizeof name) name[n++] = *s++;
            name[n] = 0;
            bool found = false;
            for (int i = 0; i < count && !found; i++) {
                xs_dbg_child c = {0};
                (*t)->child(*addr, i, &c);
                if (c.name && strcmp(c.name, name) == 0) { *t = c.type; *addr = c.addr; found = true; }
            }
            if (!found) return count ? "no such field" : "not an object (or null)";
        } else if (*s == '[') {
            char *end;
            long i = strtol(s + 1, &end, 10);
            if (end == s + 1 || *end != ']') return "expected [index]";
            s = end + 1;
            xs_dbg_child c = {0};
            if (i < 0 || i >= count) return "index out of range (or not an array)";
            (*t)->child(*addr, (int)i, &c);
            if (c.name) return "not an array";
            *t = c.type;
            *addr = c.addr;
        } else return "only paths are evaluated: a.b[2].c";
    }
}
static void xs_dbg_job_evaluate(void *p) {
    xs_dbg_job *j = p;
    const dj *args = dj_get(j->req, "arguments");
    int id = (int)dj_int(args, "frameId", 0);
    xs_dbg_frame *f = xs_dbg_frame_at(id);
    const xs_dbg_type *t;
    void *addr;
    if (!f) { j->error = "no such frame"; return; }
    if ((j->error = xs_dbg_resolve(f, dj_str(args, "expression", ""), &t, &addr))) return;
    dj_puts(&j->body, "{\"result\":");
    xs_dbg_render(&j->body, t, addr);
    dj_puts(&j->body, ",\"type\":");
    dj_quote(&j->body, t->name);
    dj_printf(&j->body, ",\"variablesReference\":%d}", t->count && t->count(addr) > 0 ? xs_dbg_ref_new(id, 2, t, addr) : 0);
}

/* ---- conditions, hit counts, logpoints (evaluated on the program's thread, inside xs_dbg_hit) ---- */
/* the rendered value of a path, malloc'd; NULL with *err set when it does not resolve */
static char *xs_dbg_render_text(xs_dbg_frame *f, const char *path, const char **err) {
    const xs_dbg_type *t;
    void *addr;
    if ((*err = xs_dbg_resolve(f, path, &t, &addr))) return NULL;
    dj_buf q = {0};
    xs_dbg_render(&q, t, addr); /* quoted JSON string of the console.log text */
    dj *v = dj_parse(q.data);
    free(q.data);
    char *text = v && v->str ? strdup(v->str) : strdup("");
    dj_free(v);
    return text;
}
/* COND: PATH, or PATH OP LITERAL (== != === !== < <= > >=; a number, 'text', "text", true, false, null) */
static bool xs_dbg_condition(xs_dbg_frame *f, const char *cond, const char **err) {
    static const char *ops[] = {"===", "!==", "==", "!=", "<=", ">=", "<", ">"};
    const char *at = NULL, *op = NULL;
    for (const char *s = cond; *s && !at; s++)
        for (int k = 0; k < 8; k++)
            if (strncmp(s, ops[k], strlen(ops[k])) == 0) { at = s; op = ops[k]; break; }
    char path[256];
    size_t pn = at ? (size_t)(at - cond) : strlen(cond);
    if (pn >= sizeof path) pn = sizeof path - 1;
    memcpy(path, cond, pn);
    path[pn] = 0;
    char *v = xs_dbg_render_text(f, path, err);
    if (!v) return true; /* an error stops, so the user sees it */
    bool r;
    if (!op) r = strcmp(v, "0") != 0 && strcmp(v, "false") != 0 && strcmp(v, "null") != 0 && strcmp(v, "''") != 0 && strcmp(v, "NaN") != 0;
    else {
        const char *lit = at + strlen(op);
        while (*lit == ' ') lit++;
        char want[512];
        snprintf(want, sizeof want, "%s", lit);
        size_t wl = strlen(want);
        while (wl && want[wl - 1] == ' ') want[--wl] = 0;
        if (wl >= 2 && (want[0] == '"' || want[0] == '\'') && want[wl - 1] == want[0]) { /* strings render quoted with ' */
            want[0] = want[wl - 1] = '\'';
        }
        char *e1, *e2;
        double a = strtod(v, &e1), b = strtod(want, &e2);
        bool numeric = *v && *e1 == 0 && *want && *e2 == 0;
        int c = numeric ? (a < b ? -1 : a > b ? 1 : 0) : strcmp(v, want);
        if (op[0] == '=') r = c == 0;
        else if (op[0] == '!') r = c != 0;
        else if (!numeric) { *err = "< and > compare numbers only"; r = true; }
        else if (strcmp(op, "<") == 0) r = c < 0;
        else if (strcmp(op, "<=") == 0) r = c <= 0;
        else if (strcmp(op, ">") == 0) r = c > 0;
        else r = c >= 0;
    }
    free(v);
    return r;
}
/* hitCondition: N or ==N (the Nth hit), >N, >=N, <N, <=N, %N (every Nth) */
static bool xs_dbg_hits(long hits, const char *h) {
    while (*h == ' ') h++;
    long n;
    if (h[0] == '%') { n = atol(h + 1); return n > 0 && hits % n == 0; }
    if (h[0] == '>' && h[1] == '=') return hits >= atol(h + 2);
    if (h[0] == '<' && h[1] == '=') return hits <= atol(h + 2);
    if (h[0] == '>') return hits > atol(h + 1);
    if (h[0] == '<') return hits < atol(h + 1);
    if (h[0] == '=' && h[1] == '=') return hits == atol(h + 2);
    return hits == atol(h);
}
static void xs_dbg_output(const char *category, const char *text) {
    dj_buf b = {0};
    dj_puts(&b, "{\"category\":");
    dj_quote(&b, category);
    dj_puts(&b, ",\"output\":");
    dj_quote(&b, text);
    dj_puts(&b, "}");
    xs_dbg_event("output", b.data);
    free(b.data);
}
/* a logpoint's message with each {path} replaced by its value (strings without quotes, as console.log) */
static void xs_dbg_log(xs_dbg_frame *f, const char *msg) {
    dj_buf out = {0};
    for (const char *s = msg; *s;) {
        if (*s == '{') {
            const char *end = strchr(s, '}');
            if (end) {
                char path[256];
                size_t n = (size_t)(end - s - 1) < sizeof path - 1 ? (size_t)(end - s - 1) : sizeof path - 1;
                memcpy(path, s + 1, n);
                path[n] = 0;
                const char *err;
                char *v = xs_dbg_render_text(f, path, &err);
                if (!v) { dj_puts(&out, "<"); dj_puts(&out, err); dj_puts(&out, ">"); }
                else {
                    size_t vl = strlen(v);
                    if (vl >= 2 && v[0] == '\'' && v[vl - 1] == '\'') dj_put(&out, v + 1, vl - 2);
                    else dj_puts(&out, v);
                    free(v);
                }
                s = end + 1;
                continue;
            }
        }
        dj_put(&out, s, 1);
        s++;
    }
    dj_puts(&out, "\n");
    if (xs_dbg.log_stdout) { fputs("log: ", stdout); fputs(out.data, stdout); fflush(stdout); }
    else { fflush(stdout); xs_dbg_output("console", out.data); }
    free(out.data);
}
/* whether breakpoint id stops here (called with mu held, on the program's thread) */
static bool xs_dbg_bp_fires(xs_dbg_frame *f, int id) {
    xs_dbg_bpinfo *b = xs_dbg_bpinfo_of(id);
    const char *err = NULL;
    if (b->cond && !xs_dbg_condition(f, b->cond, &err)) return false;
    if (err) {
        char m[300];
        snprintf(m, sizeof m, "breakpoint condition '%s': %s\n", b->cond, err);
        xs_dbg_output("stderr", m);
        return true;
    }
    b->hits++;
    if (b->hit && !xs_dbg_hits(b->hits, b->hit)) return false;
    if (b->log) { xs_dbg_log(f, b->log); return false; }
    return true;
}

/* ---- exceptions: the throw (or the runtime error) stops before unwinding, with the variables alive ---- */
static void xs_dbg_exception(const char *name, const char *message, bool caught) {
    if (xs_dbg.fd < 0 || !xs_dbg_top) return;
    if (!(xs_dbg.exc_all || (xs_dbg.exc_uncaught && !caught))) return;
    pthread_mutex_lock(&xs_dbg.mu);
    while (xs_dbg.stopped) pthread_cond_wait(&xs_dbg.cv, &xs_dbg.mu);
    snprintf(xs_dbg.exc_name, sizeof xs_dbg.exc_name, "%s", name ? name : "Error");
    snprintf(xs_dbg.exc_message, sizeof xs_dbg.exc_message, "%s", message ? message : "");
    char desc[600];
    snprintf(desc, sizeof desc, "%s: %s", xs_dbg.exc_name, xs_dbg.exc_message);
    xs_dbg_stop_ex(xs_dbg_top, "exception", 0, desc);
    pthread_mutex_unlock(&xs_dbg.mu);
}

/* ---- setVariable / setExpression (F4): only in a --debug binary, on the stopped thread ---- */
static void xs_dbg_assign(xs_dbg_job *j, int frame, const xs_dbg_type *t, void *addr, int mode, const char *value) {
    if (mode == 2) { j->error = "read-only: a const, a captured variable or this"; return; }
    if (mode == 3) { j->error = "read-only in a QBE build: its locals live in registers (debug with the C backend to edit)"; return; }
    if (!t->set) { j->error = "not editable: only numbers, booleans, strings, enums and null can be set (objects, arrays and maps keep their identity)"; return; }
    if ((j->error = t->set(addr, value, mode))) return;
    dj_puts(&j->body, "{\"value\":");
    xs_dbg_render(&j->body, t, addr);
    dj_puts(&j->body, ",\"result\":");
    xs_dbg_render(&j->body, t, addr);
    dj_puts(&j->body, ",\"type\":");
    dj_quote(&j->body, t->name);
    dj_printf(&j->body, ",\"variablesReference\":%d}", t->count && t->count(addr) > 0 ? xs_dbg_ref_new(frame, 2, t, addr) : 0);
}
static void xs_dbg_job_set_variable(void *p) {
    xs_dbg_job *j = p;
    const dj *args = dj_get(j->req, "arguments");
    int ref = (int)dj_int(args, "variablesReference", 0);
    const char *name = dj_str(args, "name", ""), *value = dj_str(args, "value", "");
    if (ref < 1 || ref > xs_dbg_nrefs) { j->error = "no such variables reference"; return; }
    xs_dbg_ref r = xs_dbg_refs[ref - 1];
    if (r.which == 2) {
        int count = r.type->count(r.addr);
        for (int i = 0; i < count; i++) {
            xs_dbg_child c = {0};
            r.type->child(r.addr, i, &c);
            char idx[24];
            snprintf(idx, sizeof idx, "[%d]", c.index);
            if (c.name ? strcmp(c.name, name) == 0 : strcmp(idx, name) == 0) { xs_dbg_assign(j, r.frame, c.type, c.addr, 1, value); return; }
        }
        j->error = "no such field";
        return;
    }
    xs_dbg_frame *f = xs_dbg_frame_at(r.frame);
    if (r.which == 1) f = xs_dbg_root(f);
    for (int k = f ? f->nvars - 1 : -1; k >= 0; k--)
        if (xs_dbg_visible(f, k) && strcmp(f->vars[k].name, name) == 0) { xs_dbg_assign(j, r.frame, f->vars[k].type, f->slots[k], f->vars[k].mode, value); return; }
    j->error = "not a variable in scope here";
}
static void xs_dbg_job_set_expression(void *p) {
    xs_dbg_job *j = p;
    const dj *args = dj_get(j->req, "arguments");
    int id = (int)dj_int(args, "frameId", 0);
    xs_dbg_frame *f = xs_dbg_frame_at(id);
    const xs_dbg_type *t;
    void *addr;
    int mode = 1;
    if (!f) { j->error = "no such frame"; return; }
    if ((j->error = xs_dbg_resolve_m(f, dj_str(args, "expression", ""), &t, &addr, &mode))) return;
    xs_dbg_assign(j, id, t, addr, mode, dj_str(args, "value", ""));
}

/* run fn on the stopped thread and answer the request with what it wrote */
static void xs_dbg_on_stopped(const dj *req, void (*fn)(void *)) {
    xs_dbg_job j = {req, {0}, NULL};
    pthread_mutex_lock(&xs_dbg.mu);
    if (!xs_dbg.stopped) {
        pthread_mutex_unlock(&xs_dbg.mu);
        xs_dbg_respond(req, false, NULL, "not stopped");
        return;
    }
    xs_dbg.job = fn;
    xs_dbg.job_arg = &j;
    pthread_cond_broadcast(&xs_dbg.cv);
    while (xs_dbg.job) pthread_cond_wait(&xs_dbg.cv, &xs_dbg.mu);
    pthread_mutex_unlock(&xs_dbg.mu);
    if (j.error) xs_dbg_respond(req, false, NULL, j.error);
    else xs_dbg_respond(req, true, j.body.data, NULL);
    free(j.body.data);
}

/* attach over TCP (XISTO_DEBUG_PORT): the token every session must present in attach (or launch) */
static char xs_dbg_token[65];
static bool xs_dbg_tcp;
static bool xs_dbg_token_ok(const char *t) {
    size_t n = strlen(xs_dbg_token);
    if (!t || strlen(t) != n) return false;
    unsigned char diff = 0; /* constant time */
    for (size_t i = 0; i < n; i++) diff |= (unsigned char)(t[i] ^ xs_dbg_token[i]);
    return diff == 0;
}

/* one client, until it disconnects or goes away */
static void xs_dbg_session(void) {
    bool authed = !xs_dbg_tcp;
    for (;;) {
        char *text = dj_read_message(xs_dbg.fd);
        if (!text) break; /* the client went away: the program runs on, as if never debugged */
        dj *req = dj_parse(text);
        free(text);
        if (!req) continue;
        const char *cmd = dj_str(req, "command", "");
        if (!authed && strcmp(cmd, "initialize") != 0) {
            bool ok = (strcmp(cmd, "attach") == 0 || strcmp(cmd, "launch") == 0) && xs_dbg_token_ok(dj_str(dj_get(req, "arguments"), "token", NULL));
            if (!ok) {
                xs_dbg_respond(req, false, NULL, "this program is debugged with a token: attach with {\"token\": ...} (it was printed at start)");
                dj_free(req);
                break;
            }
            authed = true;
        }
        if (strcmp(cmd, "initialize") == 0) {
            xs_dbg_respond(req, true, "{\"supportsConfigurationDoneRequest\":true,\"supportsTerminateRequest\":true,"
                "\"supportsConditionalBreakpoints\":true,\"supportsHitConditionalBreakpoints\":true,\"supportsLogPoints\":true,"
                "\"supportsEvaluateForHovers\":true,\"supportsExceptionInfoRequest\":true,\"supportsSetVariable\":true,\"supportsSetExpression\":true,"
                "\"exceptionBreakpointFilters\":[{\"filter\":\"all\",\"label\":\"All exceptions\",\"default\":false},"
                "{\"filter\":\"uncaught\",\"label\":\"Uncaught exceptions\",\"default\":true}]}", NULL);
            xs_dbg_event("initialized", NULL);
        } else if (strcmp(cmd, "launch") == 0 || strcmp(cmd, "attach") == 0) {
            xs_dbg.log_stdout = dj_bool(dj_get(req, "arguments"), "logToStdout", false);
            xs_dbg_respond(req, true, NULL, NULL);
        } else if (strcmp(cmd, "setBreakpoints") == 0) {
            xs_dbg_set_breakpoints(req);
        } else if (strcmp(cmd, "setExceptionBreakpoints") == 0) {
            const dj *filters = dj_get(dj_get(req, "arguments"), "filters");
            bool all = false, uncaught = false;
            for (int i = 0; filters && filters->kind == DJ_ARR && i < filters->n; i++) {
                const char *fl = filters->items[i]->str ? filters->items[i]->str : "";
                if (strcmp(fl, "all") == 0) all = true;
                if (strcmp(fl, "uncaught") == 0) uncaught = true;
            }
            xs_dbg.exc_all = all;
            xs_dbg.exc_uncaught = uncaught;
            xs_dbg_respond(req, true, NULL, NULL);
        } else if (strcmp(cmd, "exceptionInfo") == 0) {
            dj_buf b = {0};
            dj_puts(&b, "{\"exceptionId\":");
            dj_quote(&b, xs_dbg.exc_name);
            dj_puts(&b, ",\"description\":");
            dj_quote(&b, xs_dbg.exc_message);
            dj_printf(&b, ",\"breakMode\":\"%s\"}", xs_dbg.exc_all ? "always" : "unhandled");
            xs_dbg_respond(req, true, b.data, NULL);
            free(b.data);
        } else if (strcmp(cmd, "configurationDone") == 0) {
            xs_dbg_respond(req, true, NULL, NULL);
            pthread_mutex_lock(&xs_dbg.mu);
            xs_dbg.configured = true;
            pthread_cond_broadcast(&xs_dbg.cv);
            pthread_mutex_unlock(&xs_dbg.mu);
        } else if (strcmp(cmd, "threads") == 0) {
            dj_buf b = {0};
            dj_puts(&b, "{\"threads\":[");
            pthread_mutex_lock(&xs_dbg_tmu);
            int k = 0;
            for (int i = 0; i < xs_dbg_nthreads; i++) {
                if (!xs_dbg_threads[i].alive) continue;
                if (i == 0) dj_printf(&b, "%s{\"id\":1,\"name\":\"main\"}", k++ ? "," : "");
                else dj_printf(&b, "%s{\"id\":%d,\"name\":\"worker %d\"}", k++ ? "," : "", i + 1, i);
            }
            if (!k) dj_puts(&b, "{\"id\":1,\"name\":\"main\"}");
            pthread_mutex_unlock(&xs_dbg_tmu);
            dj_puts(&b, "]}");
            xs_dbg_respond(req, true, b.data, NULL);
            free(b.data);
        } else if (strcmp(cmd, "stackTrace") == 0) {
            xs_dbg_stack_trace(req);
        } else if (strcmp(cmd, "scopes") == 0) {
            xs_dbg_on_stopped(req, xs_dbg_job_scopes);
        } else if (strcmp(cmd, "variables") == 0) {
            xs_dbg_on_stopped(req, xs_dbg_job_variables);
        } else if (strcmp(cmd, "evaluate") == 0) {
            xs_dbg_on_stopped(req, xs_dbg_job_evaluate);
        } else if (strcmp(cmd, "setVariable") == 0) {
            xs_dbg_on_stopped(req, xs_dbg_job_set_variable);
        } else if (strcmp(cmd, "setExpression") == 0) {
            xs_dbg_on_stopped(req, xs_dbg_job_set_expression);
        } else if (strcmp(cmd, "continue") == 0 || strcmp(cmd, "next") == 0 || strcmp(cmd, "stepIn") == 0 || strcmp(cmd, "stepOut") == 0) {
            int step = cmd[0] == 'c' ? XS_STEP_NONE : cmd[0] == 'n' ? XS_STEP_NEXT : cmd[4] == 'I' ? XS_STEP_IN : XS_STEP_OUT;
            /* the answer goes out before the program is let go: once running, it may reach its end and exit()
               before this thread writes, and the IDE would wait for the answer forever (XB-033, seen on a
               1-CPU machine). A stopped program stays stopped until resumed, so the order is safe. */
            pthread_mutex_lock(&xs_dbg.mu);
            bool was = xs_dbg.stopped;
            pthread_mutex_unlock(&xs_dbg.mu);
            if (!was) xs_dbg_respond(req, false, NULL, "not stopped");
            else {
                xs_dbg_respond(req, true, step == XS_STEP_NONE ? "{\"allThreadsContinued\":true}" : NULL, NULL);
                pthread_mutex_lock(&xs_dbg.mu);
                if (xs_dbg.stopped) xs_dbg_resume(step);
                pthread_mutex_unlock(&xs_dbg.mu);
            }
        } else if (strcmp(cmd, "pause") == 0) {
            pthread_mutex_lock(&xs_dbg.mu);
            xs_dbg.pause_req = !xs_dbg.stopped;
            xs_dbg_rearm();
            pthread_mutex_unlock(&xs_dbg.mu);
            if (xs_dbg_waker) xs_dbg_waker(); /* a server waiting for connections runs no statement: wake it */
            xs_dbg_respond(req, true, NULL, NULL);
        } else if (strcmp(cmd, "disconnect") == 0 || strcmp(cmd, "terminate") == 0) {
            /* a launched program ends with its session; an attached one runs on unless asked */
            bool kill_it = strcmp(cmd, "terminate") == 0 || dj_bool(dj_get(req, "arguments"), "terminateDebuggee", !xs_dbg_tcp);
            xs_dbg_respond(req, true, NULL, NULL);
            if (kill_it) { fflush(stdout); _exit(137); }
            pthread_mutex_lock(&xs_dbg.mu); /* detach: forget everything and let the program run */
            if (xs_dbg.bp) memset(xs_dbg.bp, 0, sizeof(int) * (size_t)xs_dbg_npoints);
            xs_dbg.nbp = 0;
            xs_dbg.pause_req = false;
            xs_dbg_resume(XS_STEP_NONE);
            pthread_mutex_unlock(&xs_dbg.mu);
            dj_free(req);
            break;
        } else {
            xs_dbg_respond(req, false, NULL, "unsupported request");
        }
        dj_free(req);
    }
    pthread_mutex_lock(&xs_dbg.mu);
    xs_dbg.configured = true; /* a client that left before configurationDone does not hold the program */
    if (xs_dbg.bp) memset(xs_dbg.bp, 0, sizeof(int) * (size_t)xs_dbg_npoints);
    xs_dbg.nbp = 0;
    xs_dbg.step = XS_STEP_NONE;
    xs_dbg.pause_req = false;
    xs_dbg.exc_all = xs_dbg.exc_uncaught = false;
    xs_dbg_resume(XS_STEP_NONE);
    pthread_cond_broadcast(&xs_dbg.cv);
    pthread_mutex_unlock(&xs_dbg.mu);
}
static void *xs_dbg_agent(void *unused) {
    (void)unused;
    xs_dbg_session();
    return NULL;
}
/* XISTO_DEBUG_PORT: sessions one after another on 127.0.0.1 only */
static int xs_dbg_listen_fd = -1;
static void *xs_dbg_acceptor(void *unused) {
    (void)unused;
    for (;;) {
        int c = accept(xs_dbg_listen_fd, NULL, NULL);
        if (c < 0) { if (errno == EINTR) continue; break; }
        fcntl(c, F_SETFD, FD_CLOEXEC);
        pthread_mutex_lock(&xs_dbg.wmu);
        xs_dbg.fd = c;
        pthread_mutex_unlock(&xs_dbg.wmu);
        xs_dbg_session();
        pthread_mutex_lock(&xs_dbg.wmu);
        xs_dbg.fd = -1;
        pthread_mutex_unlock(&xs_dbg.wmu);
        close(c);
    }
    return NULL;
}
static void xs_dbg_random_token(char *out, size_t n) {
    unsigned char raw[16];
    FILE *r = fopen("/dev/urandom", "rb");
    size_t got = r ? fread(raw, 1, sizeof raw, r) : 0;
    if (r) fclose(r);
    if (got != sizeof raw) { fprintf(stderr, "xisto: no randomness for the debugger token\n"); exit(70); }
    for (size_t i = 0; i < sizeof raw && 2 * i + 2 < n; i++) snprintf(out + 2 * i, 3, "%02x", raw[i]);
}

static void xs_dbg_atexit(void) {
    if (xs_dbg.fd < 0) return;
    fflush(stdout);
    xs_dbg_event("terminated", NULL);
}

/* XISTO_DEBUG_FD: a socket from `xisto debug` / `xisto dap` (the program waits for configurationDone).
   XISTO_DEBUG_PORT: listen on 127.0.0.1 for IDEs to attach later, with XISTO_DEBUG_TOKEN or a random token
   printed on stderr; the program does not wait. Without either the --debug binary runs like any other. */
static void xs_dbg_init(void) {
    pthread_key_create(&xs_dbg_tkey, xs_dbg_thread_gone);
    if (!xs_dbg_tid) xs_dbg_register(); /* the main thread is thread 1 */
    const char *port = getenv("XISTO_DEBUG_PORT");
    if (port && *port && !getenv("XISTO_DEBUG_FD")) {
        const char *tok = getenv("XISTO_DEBUG_TOKEN");
        if (tok && strlen(tok) >= 16 && strlen(tok) < sizeof xs_dbg_token) snprintf(xs_dbg_token, sizeof xs_dbg_token, "%s", tok);
        else {
            if (tok && *tok) { fprintf(stderr, "xisto: XISTO_DEBUG_TOKEN must have 16 to 64 characters\n"); exit(2); }
            xs_dbg_random_token(xs_dbg_token, sizeof xs_dbg_token);
        }
        unsetenv("XISTO_DEBUG_PORT");
        unsetenv("XISTO_DEBUG_TOKEN");
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s >= 0) fcntl(s, F_SETFD, FD_CLOEXEC);
        int one = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* never another interface */
        a.sin_port = htons((uint16_t)atoi(port));
        socklen_t al = sizeof a;
        if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) != 0 || listen(s, 1) != 0 || getsockname(s, (struct sockaddr *)&a, &al) != 0) {
            fprintf(stderr, "xisto: the debugger cannot listen on 127.0.0.1:%s\n", port);
            exit(2);
        }
        xs_dbg_listen_fd = s;
        xs_dbg_tcp = true;
        xs_dbg.configured = true;
        signal(SIGPIPE, SIG_IGN);
        fprintf(stderr, "xisto: debugger listening on 127.0.0.1:%d (token %s)\n", ntohs(a.sin_port), xs_dbg_token);
        fflush(stderr);
        pthread_t t;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        pthread_create(&t, &at, xs_dbg_acceptor, NULL);
        return;
    }
    const char *fd = getenv("XISTO_DEBUG_FD");
    if (!fd || !*fd) return;
    xs_dbg.fd = atoi(fd);
    unsetenv("XISTO_DEBUG_FD"); /* not inherited by child processes: neither the variable nor the socket */
    fcntl(xs_dbg.fd, F_SETFD, FD_CLOEXEC);
    signal(SIGINT, SIG_IGN); /* Ctrl-C belongs to the debugger: it becomes a pause */
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    atexit(xs_dbg_atexit);
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &at, xs_dbg_agent, NULL);
    pthread_mutex_lock(&xs_dbg.mu);
    while (!xs_dbg.configured) pthread_cond_wait(&xs_dbg.cv, &xs_dbg.mu);
    pthread_mutex_unlock(&xs_dbg.mu);
}
