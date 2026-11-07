/* xisto debug: the command-line debugger. It builds the program with --debug, starts it with one end of a
   socketpair in XISTO_DEBUG_FD and speaks the Debug Adapter Protocol with the agent inside it
   (runtime/xisto_dbg.h): the same requests an IDE sends. Commands in the style of gdb; --batch reads them from
   a file and echoes each one, which is how tests/debug drives it. */
#include "xisto.h"
#if defined(_WIN32) /* the debugger needs fork, socketpair and poll: macOS and Linux only for now */
int cmd_debug(int argc, char **argv) { (void)argc; (void)argv; fprintf(stderr, "xisto: the debugger is not available on Windows yet\n"); return 2; }
#else
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "../runtime/xisto_dbg_json.h"

bool xs_build_debug(const char *file, const char *exe);

typedef struct { int id, agent_id; char *path; int line, actual; bool verified; char *cond, *hits, *log; } Bp;
typedef struct { char *name, *path; int line; } Frame;

static int fd = -1;
static pid_t child = -1;
static long seq;
static bool batch;
static FILE *cmds;
static volatile sig_atomic_t interrupted;
static char cwd[PATH_MAX];
static char *main_file;

static Bp *bps;
static int nbps, next_bp;
static Frame *frames;
static int nframes, cur;

static bool started, stopped, finished;
static char stop_reason[32];
static char stop_text[600];
static int stop_bp;

static void on_sigint(int sig) { (void)sig; interrupted = 1; }

static const char *rel(const char *path) {
    size_t n = strlen(cwd);
    if (path && strncmp(path, cwd, n) == 0 && path[n] == '/') return path + n + 1;
    return path;
}
static char *real(const char *p) {
    char buf[PATH_MAX];
    return realpath(p, buf) ? xs_strndup(buf, strlen(buf)) : xs_strndup(p, strlen(p));
}

static long request(const char *command, const char *args) {
    dj_buf b = {0};
    long s = ++seq;
    dj_printf(&b, "{\"seq\":%ld,\"type\":\"request\",\"command\":\"%s\"", s, command);
    if (args) dj_printf(&b, ",\"arguments\":%s", args);
    dj_puts(&b, "}");
    if (fd >= 0 && !dj_write_message(fd, b.data)) finished = true;
    free(b.data);
    return s;
}

static void handle_event(dj *m) {
    const char *ev = dj_str(m, "event", "");
    dj *body = dj_get(m, "body");
    if (strcmp(ev, "stopped") == 0) {
        stopped = true;
        snprintf(stop_reason, sizeof stop_reason, "%s", dj_str(body, "reason", "pause"));
        snprintf(stop_text, sizeof stop_text, "%s", dj_str(body, "text", ""));
        dj *ids = dj_get(body, "hitBreakpointIds");
        stop_bp = ids && ids->kind == DJ_ARR && ids->n > 0 ? (int)ids->items[0]->num : 0;
    } else if (strcmp(ev, "terminated") == 0 || strcmp(ev, "exited") == 0) {
        finished = true;
    } else if (strcmp(ev, "output") == 0) {
        const char *cat = dj_str(body, "category", "console");
        printf("%s%s", strcmp(cat, "stderr") == 0 ? "warning: " : "log: ", dj_str(body, "output", ""));
    }
}

/* reads messages until the response to request s; events met on the way are handled. NULL: the program ended */
static dj *wait_response(long s) {
    for (;;) {
        char *text = dj_read_message(fd);
        if (!text) { finished = true; return NULL; }
        dj *m = dj_parse(text);
        free(text);
        if (!m) continue;
        if (strcmp(dj_str(m, "type", ""), "response") == 0 && dj_int(m, "request_seq", -1) == s) return m;
        if (strcmp(dj_str(m, "type", ""), "event") == 0) handle_event(m);
        dj_free(m);
    }
}
static dj *call(const char *command, const char *args) { return wait_response(request(command, args)); }

/* runs until the program stops or ends; Ctrl-C asks the agent to pause */
static void wait_stop(void) {
    while (!stopped && !finished) {
        if (interrupted) {
            interrupted = 0;
            request("pause", "{\"threadId\":1}");
        }
        struct pollfd p = {fd, POLLIN, 0};
        int r = poll(&p, 1, 200);
        if (r <= 0) continue;
        char *text = dj_read_message(fd);
        if (!text) { finished = true; break; }
        dj *m = dj_parse(text);
        free(text);
        if (m && strcmp(dj_str(m, "type", ""), "event") == 0) handle_event(m);
        dj_free(m);
    }
}

static char *source_line(const char *path, int line) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char buf[4096];
    int n = 0;
    char *res = NULL;
    while (fgets(buf, sizeof buf, f)) {
        if (++n == line) {
            size_t len = strlen(buf);
            while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = 0;
            res = xs_strndup(buf, len);
            break;
        }
    }
    fclose(f);
    return res;
}
static void show_line(const char *path, int line, bool mark) {
    char *text = source_line(path, line);
    printf("%s%4d | %s\n", mark ? "=>" : "  ", line, text ? text : "");
    free(text);
}

static void load_frames(void) {
    for (int i = 0; i < nframes; i++) { free(frames[i].name); free(frames[i].path); }
    nframes = 0;
    cur = 0;
    dj *r = call("stackTrace", "{\"threadId\":1}");
    if (!r) return;
    dj *list = dj_get(dj_get(r, "body"), "stackFrames");
    for (int i = 0; list && list->kind == DJ_ARR && i < list->n; i++) {
        dj *f = list->items[i];
        frames = realloc(frames, sizeof(Frame) * (size_t)(nframes + 1));
        const char *name = dj_str(f, "name", "?"), *path = dj_str(dj_get(f, "source"), "path", "");
        frames[nframes++] = (Frame){xs_strndup(name, strlen(name)), xs_strndup(path, strlen(path)), (int)dj_int(f, "line", 0)};
    }
    dj_free(r);
}
static void show_frame(int i) {
    if (i < 0 || i >= nframes) return;
    printf("#%d  %s at %s:%d\n", i, frames[i].name, rel(frames[i].path), frames[i].line);
    show_line(frames[i].path, frames[i].line, true);
}
static void report_stop(void) {
    load_frames();
    if (!nframes) return;
    int local = 0;
    for (int i = 0; i < nbps; i++) if (bps[i].agent_id == stop_bp) local = bps[i].id;
    if (strcmp(stop_reason, "breakpoint") == 0 && local) printf("Breakpoint %d, ", local);
    else if (strcmp(stop_reason, "pause") == 0) printf("Paused, ");
    else if (strcmp(stop_reason, "exception") == 0) printf("Exception %s\n  thrown in ", stop_text);
    printf("%s at %s:%d\n", frames[0].name, rel(frames[0].path), frames[0].line);
    show_line(frames[0].path, frames[0].line, true);
}

/* the breakpoints of one file, sent together (DAP replaces a file's list on each request) */
static void sync_file(const char *path) {
    dj_buf a = {0};
    dj_puts(&a, "{\"source\":{\"path\":");
    dj_quote(&a, path);
    dj_puts(&a, "},\"breakpoints\":[");
    int k = 0;
    for (int i = 0; i < nbps; i++)
        if (strcmp(bps[i].path, path) == 0) {
            dj_printf(&a, "%s{\"line\":%d", k++ ? "," : "", bps[i].line);
            if (bps[i].cond) { dj_puts(&a, ",\"condition\":"); dj_quote(&a, bps[i].cond); }
            if (bps[i].hits) { dj_puts(&a, ",\"hitCondition\":"); dj_quote(&a, bps[i].hits); }
            if (bps[i].log) { dj_puts(&a, ",\"logMessage\":"); dj_quote(&a, bps[i].log); }
            dj_puts(&a, "}");
        }
    dj_puts(&a, "]}");
    dj *r = call("setBreakpoints", a.data);
    free(a.data);
    if (!r) return;
    dj *list = dj_get(dj_get(r, "body"), "breakpoints");
    k = 0;
    for (int i = 0; i < nbps; i++) {
        if (strcmp(bps[i].path, path) != 0) continue;
        dj *b = list && list->kind == DJ_ARR && k < list->n ? list->items[k] : NULL;
        k++;
        bps[i].verified = dj_bool(b, "verified", false);
        bps[i].actual = (int)dj_int(b, "line", bps[i].line);
        bps[i].agent_id = (int)dj_int(b, "id", 0);
    }
    dj_free(r);
}

static char *dup_trim(const char *s, size_t n) {
    while (n && *s == ' ') { s++; n--; }
    while (n && s[n - 1] == ' ') n--;
    return n ? xs_strndup(s, n) : NULL;
}
/* b LOC [if COND] [hits H]; log LOC MESSAGE (log = 1) */
static void cmd_break(const char *arg0, bool log) {
    while (*arg0 == ' ') arg0++;
    const char *sp = strchr(arg0, ' ');
    char *loc = xs_strndup(arg0, sp ? (size_t)(sp - arg0) : strlen(arg0));
    const char *rest = sp ? sp : "";
    char *cond = NULL, *hits = NULL, *msg = NULL;
    if (log) msg = dup_trim(rest, strlen(rest));
    else {
        const char *h = strstr(rest, " hits ");
        const char *c = strstr(rest, " if ");
        if (c && (!h || c < h)) cond = dup_trim(c + 4, h ? (size_t)(h - c - 4) : strlen(c + 4));
        if (h) hits = dup_trim(h + 6, strlen(h + 6));
    }
    if (log && !msg) { printf("usage: log FILE:LINE MESSAGE with {path} values\n"); return; }
    const char *arg = loc;
    const char *colon = strrchr(arg, ':');
    char *path;
    int line;
    if (colon) {
        char *file = xs_strndup(arg, (size_t)(colon - arg));
        path = real(file);
        line = atoi(colon + 1);
    } else {
        const char *file = nframes ? frames[cur].path : main_file;
        path = xs_strndup(file, strlen(file));
        line = atoi(arg);
    }
    if (line <= 0) { printf("usage: b FILE:LINE or b LINE\n"); return; }
    bps = realloc(bps, sizeof(Bp) * (size_t)(nbps + 1));
    Bp *b = &bps[nbps++];
    *b = (Bp){++next_bp, 0, path, line, line, false, cond, hits, msg};
    int id = b->id;
    sync_file(path);
    for (int i = 0; i < nbps; i++) {
        if (bps[i].id != id) continue;
        if (bps[i].verified) printf("%s %d at %s:%d%s%s%s%s\n", log ? "Logpoint" : "Breakpoint", id, rel(path), bps[i].actual,
                                   cond ? " if " : "", cond ? cond : "", hits ? " hits " : "", hits ? hits : "");
        else printf("Breakpoint %d not set: no statement at or after %s:%d\n", id, rel(path), line);
    }
}
static void cmd_delete(int id) {
    for (int i = 0; i < nbps; i++) {
        if (bps[i].id != id) continue;
        char *path = bps[i].path;
        memmove(&bps[i], &bps[i + 1], sizeof(Bp) * (size_t)(nbps - i - 1));
        nbps--;
        sync_file(path);
        printf("Deleted breakpoint %d\n", id);
        return;
    }
    printf("No breakpoint %d\n", id);
}

static char *dup_trim(const char *s, size_t n);
/* p EXPR: a path in the selected frame (a.b[2].c) */
static void cmd_print(const char *expr) {
    while (*expr == ' ') expr++;
    dj_buf a = {0};
    dj_printf(&a, "{\"frameId\":%d,\"context\":\"repl\",\"expression\":", cur);
    dj_quote(&a, expr);
    dj_puts(&a, "}");
    dj *r = call("evaluate", a.data);
    free(a.data);
    if (!r) return;
    if (dj_bool(r, "success", false)) printf("%s = %s\n", expr, dj_str(dj_get(r, "body"), "result", ""));
    else printf("%s: %s\n", expr, dj_str(r, "message", "cannot evaluate"));
    dj_free(r);
}
/* set PATH = VALUE */
static void cmd_set(const char *arg) {
    const char *eq = strchr(arg, '=');
    if (!eq) { printf("usage: set PATH = VALUE\n"); return; }
    char *path = dup_trim(arg, (size_t)(eq - arg)), *value = dup_trim(eq + 1, strlen(eq + 1));
    if (!path || !value) { printf("usage: set PATH = VALUE\n"); return; }
    dj_buf a = {0};
    dj_printf(&a, "{\"frameId\":%d,\"expression\":", cur);
    dj_quote(&a, path);
    dj_puts(&a, ",\"value\":");
    dj_quote(&a, value);
    dj_puts(&a, "}");
    dj *r = call("setExpression", a.data);
    free(a.data);
    if (!r) return;
    if (dj_bool(r, "success", false)) printf("%s = %s\n", path, dj_str(dj_get(r, "body"), "value", ""));
    else printf("cannot set %s: %s\n", path, dj_str(r, "message", "?"));
    dj_free(r);
}
/* locals / globals of the selected frame: one line each */
static void cmd_scope(int which) {
    char args[64];
    snprintf(args, sizeof args, "{\"frameId\":%d}", cur);
    dj *r = call("scopes", args);
    if (!r) return;
    dj *list = dj_get(dj_get(r, "body"), "scopes");
    long ref = 0;
    for (int i = 0; list && list->kind == DJ_ARR && i < list->n; i++)
        if (strcmp(dj_str(list->items[i], "name", ""), which ? "Globals" : "Locals") == 0) ref = dj_int(list->items[i], "variablesReference", 0);
    dj_free(r);
    if (!ref) { printf(which ? "No globals (this is the top level: see locals).\n" : "No locals.\n"); return; }
    snprintf(args, sizeof args, "{\"variablesReference\":%ld}", ref);
    r = call("variables", args);
    if (!r) return;
    list = dj_get(dj_get(r, "body"), "variables");
    if (!list || list->n == 0) printf(which ? "No globals yet.\n" : "No locals.\n");
    for (int i = 0; list && list->kind == DJ_ARR && i < list->n; i++)
        printf("%s = %s\n", dj_str(list->items[i], "name", "?"), dj_str(list->items[i], "value", ""));
    dj_free(r);
}

static void resume(const char *command) {
    fflush(stdout);
    stopped = false;
    if (!started) {
        started = true;
        dj_free(call("configurationDone", NULL));
    } else {
        dj_free(call(command, "{\"threadId\":1}"));
    }
    wait_stop();
    if (stopped) report_stop();
}

static void help(void) {
    printf("b FILE:LINE | b LINE   set a breakpoint          d N        delete breakpoint N\n"
           "info b                 list breakpoints          r | c      run / continue\n"
           "n                      next (step over)          s          step into\n"
           "fin                    step out (finish)         bt         call stack\n"
           "f N | up | down        select a frame            l          list source around the frame\n"
           "p PATH                 print a value (a.b[2].c)  locals     locals of the frame\n"
           "globals                top-level variables       catch all|uncaught|off  stop on exceptions\n"
           "b LOC if PATH OP VALUE conditional breakpoint    b LOC hits N|>N|%N  by hit count\n"
           "log LOC TEXT {path}    print without stopping    set PATH = VALUE  change a value (numbers, booleans, strings, enums, null)\n"
           "q                      quit (ends the program)\n");
}

/* one command; false to leave */
static bool command(char *line) {
    char *s = line;
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\r')) s[--n] = 0;
    if (batch) printf("(xdb) %s\n", s);
    if (!*s) return true;
    char word[32] = {0};
    sscanf(s, "%31s", word);
    const char *arg = s + strlen(word);
    bool live = stopped && !finished;
    if (strcmp(word, "b") == 0 || strcmp(word, "break") == 0) cmd_break(arg, false);
    else if (strcmp(word, "log") == 0) cmd_break(arg, true);
    else if (strcmp(word, "catch") == 0) {
        while (*arg == ' ') arg++;
        const char *f = strcmp(arg, "all") == 0 ? "[\"all\"]" : strcmp(arg, "uncaught") == 0 ? "[\"uncaught\"]" : strcmp(arg, "off") == 0 ? "[]" : NULL;
        if (!f) printf("usage: catch all | uncaught | off\n");
        else {
            char a[64];
            snprintf(a, sizeof a, "{\"filters\":%s}", f);
            dj_free(call("setExceptionBreakpoints", a));
            printf("Stop on %s exceptions.\n", strcmp(arg, "off") == 0 ? "no" : arg);
        }
    }
    else if (strcmp(word, "d") == 0 || strcmp(word, "delete") == 0) cmd_delete(atoi(arg));
    else if (strcmp(word, "info") == 0 || strcmp(word, "i") == 0) {
        if (!nbps) printf("No breakpoints.\n");
        for (int i = 0; i < nbps; i++)
            printf("%d  %s:%d%s%s%s%s%s%s%s\n", bps[i].id, rel(bps[i].path), bps[i].actual, bps[i].cond ? " if " : "", bps[i].cond ? bps[i].cond : "",
                   bps[i].hits ? " hits " : "", bps[i].hits ? bps[i].hits : "", bps[i].log ? " log: " : "", bps[i].log ? bps[i].log : "",
                   bps[i].verified ? "" : "  (not set)");
    } else if (strcmp(word, "r") == 0 || strcmp(word, "run") == 0 || strcmp(word, "c") == 0 || strcmp(word, "continue") == 0) {
        if (started && !live) printf("The program is running.\n");
        else resume("continue");
    } else if (strcmp(word, "n") == 0 || strcmp(word, "next") == 0 || strcmp(word, "s") == 0 || strcmp(word, "step") == 0 ||
               strcmp(word, "fin") == 0 || strcmp(word, "finish") == 0) {
        if (!live) printf("The program is not stopped.\n");
        else resume(word[0] == 'n' ? "next" : word[0] == 's' ? "stepIn" : "stepOut");
    } else if (strcmp(word, "bt") == 0 || strcmp(word, "where") == 0) {
        if (!live) printf("The program is not stopped.\n");
        for (int i = 0; live && i < nframes; i++) printf("%s#%d  %s at %s:%d\n", i == cur ? "> " : "  ", i, frames[i].name, rel(frames[i].path), frames[i].line);
    } else if (strcmp(word, "f") == 0 || strcmp(word, "frame") == 0 || strcmp(word, "up") == 0 || strcmp(word, "down") == 0) {
        int to = word[0] == 'u' ? cur + 1 : word[0] == 'd' ? cur - 1 : atoi(arg);
        if (!live) printf("The program is not stopped.\n");
        else if (to < 0 || to >= nframes) printf("No frame %d.\n", to);
        else { cur = to; show_frame(cur); }
    } else if (strcmp(word, "l") == 0 || strcmp(word, "list") == 0) {
        const char *path = live && nframes ? frames[cur].path : main_file;
        int at = live && nframes ? frames[cur].line : 1;
        for (int k = at - 3 < 1 ? 1 : at - 3; k <= at + 3; k++) {
            char *t = source_line(path, k);
            if (!t) break;
            free(t);
            show_line(path, k, live && k == at);
        }
    } else if (strcmp(word, "p") == 0 || strcmp(word, "print") == 0) {
        if (!live) printf("The program is not stopped.\n");
        else cmd_print(arg);
    } else if (strcmp(word, "set") == 0) {
        if (!live) printf("The program is not stopped.\n");
        else cmd_set(arg);
    } else if (strcmp(word, "locals") == 0 || strcmp(word, "globals") == 0) {
        if (!live) printf("The program is not stopped.\n");
        else cmd_scope(word[0] == 'g');
    } else if (strcmp(word, "q") == 0 || strcmp(word, "quit") == 0) {
        return false;
    } else if (strcmp(word, "help") == 0 || strcmp(word, "h") == 0) {
        help();
    } else {
        printf("Unknown command '%s' (help lists them).\n", word);
    }
    fflush(stdout);
    return true;
}

/* ---- xisto dap: a debug adapter over stdin/stdout, for IDEs (docs/debugger.md, F5) ----
   The IDE speaks DAP to this process. initialize is answered here; launch {program, args, cwd} builds the
   program with --debug, starts it with the agent on a socketpair and its output on pipes; from then on requests
   go to the agent and its answers and events come back, and the program's output becomes output events. */
static int ide_out = 1;
static long ide_seq = 1000000; /* events made here: far from the agent's numbers */
static void ide_send(const char *body) { dj_write_message(ide_out, body); }
static void ide_event(const char *event, const char *body) {
    dj_buf b = {0};
    dj_printf(&b, "{\"seq\":%ld,\"type\":\"event\",\"event\":\"%s\"", ++ide_seq, event);
    if (body) dj_printf(&b, ",\"body\":%s", body);
    dj_puts(&b, "}");
    ide_send(b.data);
    free(b.data);
}
static void ide_respond(const dj *req, bool ok, const char *body, const char *message) {
    dj_buf b = {0};
    dj_printf(&b, "{\"seq\":%ld,\"type\":\"response\",\"request_seq\":%lld,\"success\":%s,\"command\":", ++ide_seq, dj_int(req, "seq", 0), ok ? "true" : "false");
    dj_quote(&b, dj_str(req, "command", ""));
    if (message) { dj_puts(&b, ",\"message\":"); dj_quote(&b, message); }
    if (body) dj_printf(&b, ",\"body\":%s", body);
    dj_puts(&b, "}");
    ide_send(b.data);
    free(b.data);
}
static void ide_output(const char *category, const char *text, size_t n) {
    dj_buf b = {0};
    char *t = xs_strndup(text, n);
    dj_puts(&b, "{\"category\":");
    dj_quote(&b, category);
    dj_puts(&b, ",\"output\":");
    dj_quote(&b, t);
    dj_puts(&b, "}");
    ide_event("output", b.data);
    free(b.data);
    free(t);
}

/* builds and starts the program for launch; NULL on success, else the message for the IDE */
static const char *dap_launch(const dj *args, int *agent, int *out_fd, int *err_fd, char **exe_out, char **dir_out) {
    const char *program = dj_str(args, "program", NULL);
    if (!program) return "launch needs \"program\": the .ts file to debug";
    const char *wd = dj_str(args, "cwd", NULL);
    if (wd && chdir(wd) != 0) return "cannot change to cwd";
    const char *backend = dj_str(args, "backend", "c");
    if (strcmp(backend, "qbe") == 0) setenv("XISTO_DEBUG_BACKEND", "qbe", 1); /* variables read-only there */
    else if (strcmp(backend, "c") == 0) unsetenv("XISTO_DEBUG_BACKEND");
    else return "backend must be \"c\" or \"qbe\"";
    if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
    const char *tmpdir = getenv("TMPDIR");
    char *dir = xs_sprintf("%s/xisto-dbg-XXXXXX", tmpdir ? tmpdir : "/tmp");
    if (!mkdtemp(dir)) return "cannot create a temp dir";
    char *exe = xs_sprintf("%s/program", dir);
    /* compile errors go to the IDE's console: the compiler writes them to stderr, which is ours */
    int errp[2];
    if (pipe(errp) != 0) return "pipe failed";
    fflush(stderr);
    int saved = dup(2);
    dup2(errp[1], 2);
    close(errp[1]);
    pid_t builder = fork();
    if (builder == 0) { close(errp[0]); _exit(xs_build_debug(program, exe) ? 0 : 1); }
    dup2(saved, 2);
    close(saved);
    char buf[4096];
    ssize_t r;
    while ((r = read(errp[0], buf, sizeof buf)) > 0) ide_output("stderr", buf, (size_t)r);
    close(errp[0]);
    int st = 0;
    waitpid(builder, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { rmdir(dir); return "the program does not compile (the errors are in the debug console)"; }

    int sv[2], po[2], pe[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0 || pipe(po) != 0 || pipe(pe) != 0) return "cannot create the program's channels";
    const dj *list = dj_get(args, "args");
    child = fork();
    if (child == 0) {
        close(sv[0]); close(po[0]); close(pe[0]);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, 0); close(devnull); }
        dup2(po[1], 1);
        dup2(pe[1], 2);
        close(po[1]); close(pe[1]);
        char num[16];
        snprintf(num, sizeof num, "%d", sv[1]);
        setenv("XISTO_DEBUG_FD", num, 1);
        int n = list && list->kind == DJ_ARR ? list->n : 0;
        char **av = calloc((size_t)n + 2, sizeof(char *));
        av[0] = exe;
        for (int k = 0; k < n; k++) av[k + 1] = list->items[k]->str ? list->items[k]->str : "";
        execv(exe, av);
        _exit(127);
    }
    close(sv[1]); close(po[1]); close(pe[1]);
    *agent = sv[0];
    *out_fd = po[0];
    *err_fd = pe[0];
    *exe_out = exe;
    *dir_out = dir;
    return NULL;
}

static int cmd_dap(void) {
    signal(SIGPIPE, SIG_IGN);
    int in = 0;
    ide_out = dup(1);
    dup2(2, 1); /* nothing but DAP may reach the IDE's channel */
    int agent = -1, pout = -1, perr = -1;
    char *exe = NULL, *dir = NULL;
    long agent_init = -1; /* our own initialize to the agent: its answer is not the IDE's */
    bool launched = false, ended = false, attached = false, quitting = false;
    for (;;) {
        struct pollfd p[4] = {{in, POLLIN, 0}, {agent, POLLIN, 0}, {pout, POLLIN, 0}, {perr, POLLIN, 0}};
        if (poll(p, 4, 500) < 0 && errno != EINTR) break;
        if (p[2].revents & (POLLIN | POLLHUP)) {
            char buf[4096];
            ssize_t r = read(pout, buf, sizeof buf);
            if (r > 0) ide_output("stdout", buf, (size_t)r);
            else { close(pout); pout = -1; }
        }
        if (p[3].revents & (POLLIN | POLLHUP)) {
            char buf[4096];
            ssize_t r = read(perr, buf, sizeof buf);
            if (r > 0) ide_output("stderr", buf, (size_t)r);
            else { close(perr); perr = -1; }
        }
        if (agent >= 0 && (p[1].revents & (POLLIN | POLLHUP))) {
            char *text = dj_read_message(agent);
            if (!text) { close(agent); agent = -1; }
            else {
                dj *m = dj_parse(text);
                bool ours = m && strcmp(dj_str(m, "type", ""), "response") == 0 && dj_int(m, "request_seq", -1) == agent_init;
                bool term = m && strcmp(dj_str(m, "event", ""), "terminated") == 0; /* sent once the output is drained */
                if (!ours && !term) ide_send(text);
                dj_free(m);
                free(text);
            }
        }
        if (attached && !ended && agent < 0) { /* the attached program ended, or let us go */
            ide_event("terminated", NULL);
            ended = true;
        }
        if (launched && !attached && !ended && agent < 0 && pout < 0 && perr < 0) { /* the program is gone and its output is out */
            int status = 0, code = 0;
            if (child > 0 && waitpid(child, &status, 0) == child) code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            char body[48];
            snprintf(body, sizeof body, "{\"exitCode\":%d}", code);
            ide_event("exited", body);
            ide_event("terminated", NULL);
            ended = true;
            if (exe) unlink(exe);
            if (dir) rmdir(dir);
        }
        if (quitting && ended) break; /* the session is over: the agent answered the disconnect and closed */
        if (p[0].revents & (POLLIN | POLLHUP)) {
            char *text = dj_read_message(in);
            if (!text) break; /* the IDE closed: end the program with it */
            dj *req = dj_parse(text);
            const char *cmd = dj_str(req, "command", "");
            if (strcmp(cmd, "initialize") == 0) {
                ide_respond(req, true, "{\"supportsConfigurationDoneRequest\":true,\"supportsTerminateRequest\":true,"
                    "\"supportsConditionalBreakpoints\":true,\"supportsHitConditionalBreakpoints\":true,\"supportsLogPoints\":true,"
                    "\"supportsEvaluateForHovers\":true,\"supportsExceptionInfoRequest\":true,\"supportsSetVariable\":true,\"supportsSetExpression\":true,"
                    "\"exceptionBreakpointFilters\":[{\"filter\":\"all\",\"label\":\"All exceptions\",\"default\":false},"
                    "{\"filter\":\"uncaught\",\"label\":\"Uncaught exceptions\",\"default\":true}]}", NULL);
            } else if (strcmp(cmd, "launch") == 0 && !launched) {
                const char *err = dap_launch(dj_get(req, "arguments"), &agent, &pout, &perr, &exe, &dir);
                if (err) ide_respond(req, false, NULL, err);
                else {
                    launched = true;
                    fd = agent;
                    agent_init = 1;
                    dj_write_message(agent, "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\",\"arguments\":{\"adapterID\":\"xisto\"}}");
                    ide_respond(req, true, NULL, NULL); /* the agent's initialized event follows: breakpoints, then configurationDone */
                }
            } else if (strcmp(cmd, "attach") == 0 && !launched) {
                /* a program already running with XISTO_DEBUG_PORT: connect, and pass the IDE's attach (its token) on */
                const dj *args = dj_get(req, "arguments");
                int port = (int)dj_int(args, "port", 0);
                int sk = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in a;
                memset(&a, 0, sizeof a);
                a.sin_family = AF_INET;
                a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                a.sin_port = htons((uint16_t)port);
                if (port <= 0 || sk < 0 || connect(sk, (struct sockaddr *)&a, sizeof a) != 0) {
                    if (sk >= 0) close(sk);
                    ide_respond(req, false, NULL, "cannot connect to 127.0.0.1:port (start the program with XISTO_DEBUG_PORT)");
                } else {
                    agent = sk;
                    fd = agent;
                    launched = attached = true;
                    agent_init = 1;
                    dj_write_message(agent, "{\"seq\":1,\"type\":\"request\",\"command\":\"initialize\",\"arguments\":{\"adapterID\":\"xisto\"}}");
                    dj_write_message(agent, text); /* the agent answers the IDE: success, or a bad token */
                }
            } else if (strcmp(cmd, "disconnect") == 0 && (agent < 0 || ended)) {
                ide_respond(req, true, NULL, NULL);
                dj_free(req);
                free(text);
                break;
            } else if (agent >= 0) {
                if (strcmp(cmd, "disconnect") == 0 || strcmp(cmd, "terminate") == 0) quitting = true;
                if (!dj_write_message(agent, text)) ide_respond(req, false, NULL, "the program has ended");
            } else {
                ide_respond(req, false, NULL, launched ? "the program has ended" : "launch first");
            }
            dj_free(req);
            free(text);
        }
    }
    if (child > 0 && !ended) { kill(child, SIGKILL); waitpid(child, NULL, 0); }
    if (exe) unlink(exe);
    if (dir) rmdir(dir);
    return 0;
}

int cmd_debug(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "dap") == 0) return cmd_dap();
    int i = 2;
    const char *batch_file = NULL;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "--batch") == 0 && i + 1 < argc) batch_file = argv[++i];
        else if (strcmp(argv[i], "--backend=c") == 0) unsetenv("XISTO_DEBUG_BACKEND");
        else if (strcmp(argv[i], "--backend=qbe") == 0) setenv("XISTO_DEBUG_BACKEND", "qbe", 1); /* variables read-only */
        else { fprintf(stderr, "xisto debug: unknown option %s\n", argv[i]); return 2; }
    }
    if (i >= argc) { fprintf(stderr, "usage: xisto debug [--batch CMDS] FILE.ts [ARGS...]\n"); return 2; }
    const char *file = argv[i++];
    if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
    main_file = real(file);
    cmds = stdin;
    if (batch_file) {
        batch = true;
        if (!(cmds = fopen(batch_file, "r"))) { fprintf(stderr, "xisto debug: cannot read %s\n", batch_file); return 2; }
    }

    const char *tmpdir = getenv("TMPDIR");
    char *dir = xs_sprintf("%s/xisto-dbg-XXXXXX", tmpdir ? tmpdir : "/tmp");
    if (!mkdtemp(dir)) { fprintf(stderr, "xisto: cannot create temp dir\n"); return 1; }
    const char *base = strrchr(file, '/');
    base = base ? base + 1 : file;
    char *exe = xs_sprintf("%s/%.*s", dir, (int)(strlen(base) > 3 && strcmp(base + strlen(base) - 3, ".ts") == 0 ? strlen(base) - 3 : strlen(base)), base);
    if (!xs_build_debug(file, exe)) { rmdir(dir); return 1; }

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { perror("socketpair"); return 1; }
    fflush(stdout);
    child = fork();
    if (child == 0) {
        close(sv[0]);
        char num[16];
        snprintf(num, sizeof num, "%d", sv[1]);
        setenv("XISTO_DEBUG_FD", num, 1);
        char **av = calloc((size_t)(argc - i + 2), sizeof(char *));
        av[0] = exe;
        for (int k = i; k < argc; k++) av[k - i + 1] = argv[k];
        execv(exe, av);
        perror("exec");
        _exit(127);
    }
    close(sv[1]);
    fd = sv[0];
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint; /* no SA_RESTART: a blocked read returns, the pause goes out */
    sigaction(SIGINT, &sa, NULL);

    dj_free(call("initialize", "{\"clientID\":\"xdb\",\"adapterID\":\"xisto\",\"linesStartAt1\":true,\"columnsStartAt1\":true,\"pathFormat\":\"path\"}"));
    dj_free(call("launch", "{\"logToStdout\":true}")); /* logpoints in order with the program's output */
    dj_free(call("setExceptionBreakpoints", "{\"filters\":[\"uncaught\"]}")); /* the IDEs' default */
    if (!batch) printf("Debugging %s. Set breakpoints, then r to run (help lists the commands).\n", rel(main_file));

    char line[1024];
    for (;;) {
        if (finished) break;
        if (!batch) { printf("(xdb) "); fflush(stdout); }
        if (!fgets(line, sizeof line, cmds)) {
            if (!batch && ferror(cmds) && errno == EINTR) { clearerr(cmds); interrupted = 0; printf("\n"); continue; }
            /* end of the commands: let the program finish */
            if (!started || stopped) {
                if (batch) printf("(xdb) (end of commands: continue)\n");
                while (!finished) resume("continue"); /* each further stop is reported, then continued */
            }
            break;
        }
        if (!command(line)) {
            if (!finished) dj_free(call("disconnect", "{\"terminateDebuggee\":true}"));
            break;
        }
    }
    if (!finished) wait_stop();
    int status = 0, code = 0;
    if (child > 0 && waitpid(child, &status, 0) == child) code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    fflush(stdout);
    printf("Program exited with code %d\n", code);
    close(fd);
    unlink(exe);
    rmdir(dir);
    return code;
}
#endif /* !_WIN32 */
