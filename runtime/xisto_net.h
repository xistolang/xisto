/* Xisto network runtime: HTTP/1.1 client (fetch) and server (Server). Embedded only when the program uses them.
   Single thread, kqueue, non-blocking sockets, keep-alive, no TLS. */
#if defined(_WIN32)
/* ======== Windows: the POSIX layer the rest of this file needs ======== */
/* Windows (xisto build --target=windows-x64): the POSIX calls the network runtime makes, over Winsock and
   Win32, so runtime/xisto_net.h stays one program for every system.
   - Sockets are SOCKET handles kept in ints (the runtime indexes its tables by them); read/write/close on
     them are recv/send/closesocket, and Winsock's error codes become errno values.
   - pipe() (the shutdown signal's wake-up) is a pair of connected loopback sockets, so the event loop can
     wait on it like on any socket.
   - kqueue/kevent: the same small look-alike as on Linux (one read and one write interest per descriptor),
     over WSAPoll.
   - mmap/mprotect/munmap for coroutine stacks: VirtualAlloc/VirtualProtect/VirtualFree.
   - Workers (XISTO_WORKERS): servers are built with XS_THREADS (mingw's winpthreads), and the kqueue
     look-alike keeps its queues per thread; tests/windows/run.sh runs the HTTP pair with four of them. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#ifndef EINPROGRESS
#define EINPROGRESS 36
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

static void xs_wsa_init(void) {
    static bool done;
    if (!done) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); done = true; }
}
static int xs_wsa_errno(void) {
    switch (WSAGetLastError()) {
    case WSAEWOULDBLOCK: return EAGAIN;
    case WSAEINPROGRESS: return EINPROGRESS;
    case WSAEINTR: return EINTR;
    case WSAECONNRESET: return ECONNRESET;
    case WSAECONNREFUSED: return ECONNREFUSED;
    case WSAECONNABORTED: return ECONNABORTED;
    case WSAENOTCONN: return ENOTCONN;
    case WSAETIMEDOUT: return ETIMEDOUT;
    case WSAEADDRINUSE: return EADDRINUSE;
    case WSAEHOSTUNREACH: return EHOSTUNREACH;
    case WSAENETUNREACH: return ENETUNREACH;
    default: return EIO;
    }
}
#define XS_SOCK(fd) ((SOCKET)(intptr_t)(fd))
static int xs_w_socket(int d, int t, int p) {
    xs_wsa_init();
    SOCKET s = socket(d, t, p);
    if (s == INVALID_SOCKET) { errno = xs_wsa_errno(); return -1; }
    return (int)(intptr_t)s;
}
static int xs_w_ret(int r) { if (r == SOCKET_ERROR) { errno = xs_wsa_errno(); return -1; } return r; }
static int xs_w_bind(int fd, const struct sockaddr *a, socklen_t l) { return xs_w_ret(bind(XS_SOCK(fd), a, l)); }
static int xs_w_listen(int fd, int n) { return xs_w_ret(listen(XS_SOCK(fd), n)); }
static int xs_w_accept(int fd, struct sockaddr *a, socklen_t *l) {
    SOCKET s = accept(XS_SOCK(fd), a, l);
    if (s == INVALID_SOCKET) { errno = xs_wsa_errno(); return -1; }
    return (int)(intptr_t)s;
}
static int xs_w_connect(int fd, const struct sockaddr *a, socklen_t l) {
    if (connect(XS_SOCK(fd), a, l) == SOCKET_ERROR) {
        int e = WSAGetLastError();
        errno = e == WSAEWOULDBLOCK ? EINPROGRESS : xs_wsa_errno(); /* a non-blocking connect under way */
        return -1;
    }
    return 0;
}
static int xs_w_setsockopt(int fd, int lvl, int opt, const void *v, socklen_t l) { return xs_w_ret(setsockopt(XS_SOCK(fd), lvl, opt, (const char *)v, l)); }
static int xs_w_getsockopt(int fd, int lvl, int opt, void *v, socklen_t *l) { return xs_w_ret(getsockopt(XS_SOCK(fd), lvl, opt, (char *)v, l)); }
static ssize_t xs_w_read(int fd, void *b, size_t n) { int r = recv(XS_SOCK(fd), (char *)b, (int)(n > INT_MAX ? INT_MAX : n), 0); return r == SOCKET_ERROR ? (errno = xs_wsa_errno(), -1) : r; }
static ssize_t xs_w_write(int fd, const void *b, size_t n) { int r = send(XS_SOCK(fd), (const char *)b, (int)(n > INT_MAX ? INT_MAX : n), 0); return r == SOCKET_ERROR ? (errno = xs_wsa_errno(), -1) : r; }
static ssize_t xs_w_send(int fd, const void *b, size_t n, int fl) { int r = send(XS_SOCK(fd), (const char *)b, (int)(n > INT_MAX ? INT_MAX : n), fl); return r == SOCKET_ERROR ? (errno = xs_wsa_errno(), -1) : r; }
static int xs_w_close(int fd) { return closesocket(XS_SOCK(fd)) == SOCKET_ERROR ? -1 : 0; }
struct iovec { void *iov_base; size_t iov_len; };
static ssize_t xs_w_writev(int fd, const struct iovec *v, int n) {
    WSABUF b[8];
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) { b[i].buf = (char *)v[i].iov_base; b[i].len = (ULONG)v[i].iov_len; }
    DWORD sent = 0;
    if (WSASend(XS_SOCK(fd), b, (DWORD)n, &sent, 0, NULL, NULL) == SOCKET_ERROR) { errno = xs_wsa_errno(); return -1; }
    return (ssize_t)sent;
}
#ifndef F_GETFL
#define F_GETFL 3
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x4000
#endif
static int xs_w_fcntl(int fd, int cmd, ...) {
    if (cmd == F_SETFL) { u_long on = 1; return ioctlsocket(XS_SOCK(fd), FIONBIO, &on) == SOCKET_ERROR ? -1 : 0; }
    return 0;
}
/* a connected pair of loopback sockets in place of a pipe */
static int xs_w_pipe(int fds[2]) {
    xs_wsa_init();
    SOCKET l = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int al = sizeof a;
    if (l == INVALID_SOCKET || bind(l, (struct sockaddr *)&a, al) || listen(l, 1) || getsockname(l, (struct sockaddr *)&a, &al)) { closesocket(l); return -1; }
    SOCKET c = socket(AF_INET, SOCK_STREAM, 0);
    if (c == INVALID_SOCKET || connect(c, (struct sockaddr *)&a, al)) { closesocket(l); closesocket(c); return -1; }
    SOCKET s = accept(l, NULL, NULL);
    closesocket(l);
    if (s == INVALID_SOCKET) { closesocket(c); return -1; }
    fds[0] = (int)(intptr_t)s;
    fds[1] = (int)(intptr_t)c;
    return 0;
}
static int xs_w_getaddrinfo(const char *h, const char *p, const struct addrinfo *hi, struct addrinfo **r) { xs_wsa_init(); return getaddrinfo(h, p, hi, r); }
#define getaddrinfo xs_w_getaddrinfo /* name lookup comes before the first socket: Winsock starts there too */
#define socket xs_w_socket
#define bind xs_w_bind
#define listen xs_w_listen
#define accept xs_w_accept
#define connect xs_w_connect
#define setsockopt xs_w_setsockopt
#define getsockopt xs_w_getsockopt
#define read xs_w_read
#define write xs_w_write
#define send xs_w_send
#define close xs_w_close
#define writev xs_w_writev
#define fcntl xs_w_fcntl
#define pipe xs_w_pipe
#define poll WSAPoll

/* signals: SIGINT and SIGTERM through the C library's signal() */
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
struct sigaction { void (*sa_handler)(int); int sa_flags; int sa_mask; };
static int xs_w_sigaction(int sig, const struct sigaction *sa, struct sigaction *old) {
    (void)old;
    if (sig == SIGPIPE) return 0;
    return signal(sig, sa->sa_handler) == SIG_ERR ? -1 : 0;
}
#define sigaction(s, a, o) xs_w_sigaction((s), (a), (o))
#define xs_w_signal_ignore(s) ((void)0)

/* coroutine stacks */
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 0
#define MAP_ANON 0
#define MAP_FAILED NULL
static void *xs_w_mmap(size_t n) { return VirtualAlloc(NULL, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); }
static int xs_w_mprotect(void *p, size_t n, int prot) {
    DWORD old;
    return VirtualProtect(p, n, prot == PROT_NONE ? PAGE_NOACCESS : PAGE_READWRITE, &old) ? 0 : -1;
}
#define mmap(a, n, p, f, fd, o) xs_w_mmap(n)
#define mprotect(a, n, p) xs_w_mprotect((a), (n), (p))
#define munmap(a, n) (VirtualFree((a), 0, MEM_RELEASE) ? 0 : -1)

/* kqueue look-alike over WSAPoll: one read and one write interest per descriptor */
#define EVFILT_READ (-1)
#define EVFILT_WRITE (-2)
#define EV_ADD 0x1
#define EV_DELETE 0x2
#define EV_ONESHOT 0x10
struct kevent { uintptr_t ident; int16_t filter; uint16_t flags; uint32_t fflags; intptr_t data; void *udata; };
#define EV_SET(kevp, a, b, c, d, e, f) do { (kevp)->ident = (uintptr_t)(a); (kevp)->filter = (int16_t)(b); (kevp)->flags = (uint16_t)(c); \
    (kevp)->fflags = (uint32_t)(d); (kevp)->data = (intptr_t)(e); (kevp)->udata = (f); } while (0)
typedef struct { int fd; void *rd, *wr; bool rd_on, wr_on, rd_once, wr_once; } xs_wfd;
typedef struct { xs_wfd *v; int n, cap; } xs_wq;
static XS_TLS xs_wq xs_wqs[16];
static XS_TLS int xs_nwq;
static int kqueue(void) { xs_wsa_init(); return xs_nwq < 16 ? xs_nwq++ : -1; }
static xs_wfd *xs_wq_get(xs_wq *q, int fd, bool add) {
    for (int i = 0; i < q->n; i++) if (q->v[i].fd == fd) return &q->v[i];
    if (!add) return NULL;
    if (q->n == q->cap) { q->cap = q->cap ? q->cap * 2 : 64; q->v = realloc(q->v, sizeof(xs_wfd) * (size_t)q->cap); }
    q->v[q->n] = (xs_wfd){.fd = fd};
    return &q->v[q->n++];
}
static void xs_wq_drop(xs_wq *q, xs_wfd *w) { if (!w->rd_on && !w->wr_on) *w = q->v[--q->n]; }
static int kevent(int kq, const struct kevent *ch, int nch, struct kevent *out, int nout, const struct timespec *timeout) {
    xs_wq *q = &xs_wqs[kq];
    for (int i = 0; i < nch; i++) {
        int fd = (int)ch[i].ident;
        bool rd = ch[i].filter == EVFILT_READ;
        xs_wfd *w = xs_wq_get(q, fd, !(ch[i].flags & EV_DELETE));
        if (!w) continue;
        if (ch[i].flags & EV_DELETE) {
            if (rd) { w->rd_on = false; w->rd = NULL; } else { w->wr_on = false; w->wr = NULL; }
            xs_wq_drop(q, w);
        } else {
            if (rd) { w->rd_on = true; w->rd = ch[i].udata; w->rd_once = (ch[i].flags & EV_ONESHOT) != 0; }
            else { w->wr_on = true; w->wr = ch[i].udata; w->wr_once = (ch[i].flags & EV_ONESHOT) != 0; }
        }
    }
    if (nout == 0) return 0;
    int ms = timeout ? (int)(timeout->tv_sec * 1000 + (timeout->tv_nsec + 999999) / 1000000) : -1;
    if (q->n == 0) { /* nothing to watch: only the timeout */
        if (ms > 0) Sleep((DWORD)ms);
        return 0;
    }
    WSAPOLLFD *pf = malloc(sizeof(WSAPOLLFD) * (size_t)q->n);
    for (int i = 0; i < q->n; i++) {
        pf[i].fd = XS_SOCK(q->v[i].fd);
        pf[i].events = (short)((q->v[i].rd_on ? POLLRDNORM : 0) | (q->v[i].wr_on ? POLLWRNORM : 0));
        pf[i].revents = 0;
    }
    int nq = q->n;
    int r = WSAPoll(pf, (ULONG)nq, ms);
    if (r < 0) { free(pf); errno = xs_wsa_errno(); return -1; }
    int k = 0;
    for (int i = 0; i < nq && k + 2 <= nout; i++) {
        short e = pf[i].revents;
        if (!e) continue;
        xs_wfd *w = xs_wq_get(q, (int)(intptr_t)pf[i].fd, false);
        if (!w) continue;
        int fd = w->fd;
        if ((e & (POLLRDNORM | POLLHUP | POLLERR)) && w->rd_on) {
            EV_SET(&out[k], fd, EVFILT_READ, 0, 0, 0, w->rd); k++;
            if (w->rd_once) w->rd_on = false;
        }
        if ((e & (POLLWRNORM | POLLHUP | POLLERR)) && w->wr_on) {
            EV_SET(&out[k], fd, EVFILT_WRITE, 0, 0, 0, w->wr); k++;
            if (w->wr_once) w->wr_on = false;
        }
        xs_wq_drop(q, w);
    }
    free(pf);
    return k;
}
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <dlfcn.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#endif
#if defined(_WIN32)
/* (the kqueue look-alike is above) */
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#else
/* Linux: a small kqueue look-alike on top of epoll, so the server code stays the same.
   Each fd keeps a read interest and a write interest (each with its own udata and oneshot flag);
   the epoll mask is the union, and epoll_wait results are split back into read/write events. */
#include <sys/epoll.h>
#define EVFILT_READ (-1)
#define EVFILT_WRITE (-2)
#define EV_ADD 0x1
#define EV_DELETE 0x2
#define EV_ONESHOT 0x10
struct kevent { uintptr_t ident; int16_t filter; uint16_t flags; uint32_t fflags; intptr_t data; void *udata; };
#define EV_SET(kevp, a, b, c, d, e, f) do { (kevp)->ident = (uintptr_t)(a); (kevp)->filter = (int16_t)(b); (kevp)->flags = (uint16_t)(c); \
    (kevp)->fflags = (uint32_t)(d); (kevp)->data = (intptr_t)(e); (kevp)->udata = (f); } while (0)
typedef struct { void *rd, *wr; bool rd_on, wr_on, rd_once, wr_once, registered; } xs_fdw;
static XS_TLS xs_fdw *xs_fdt;
static XS_TLS size_t xs_fdt_n;
static int kqueue(void) { return epoll_create1(0); }
static xs_fdw *xs_fdw_get(int fd) {
    if ((size_t)fd >= xs_fdt_n) {
        size_t n = xs_fdt_n ? xs_fdt_n : 1024;
        while (n <= (size_t)fd) n *= 2;
        xs_fdt = realloc(xs_fdt, n * sizeof(xs_fdw));
        memset(xs_fdt + xs_fdt_n, 0, (n - xs_fdt_n) * sizeof(xs_fdw));
        xs_fdt_n = n;
    }
    return &xs_fdt[fd];
}
static void xs_ep_sync(int ep, int fd) {
    xs_fdw *w = xs_fdw_get(fd);
    uint32_t mask = (w->rd_on ? EPOLLIN : 0) | (w->wr_on ? EPOLLOUT : 0);
    struct epoll_event ev = {0};
    ev.events = mask | EPOLLRDHUP;
    ev.data.fd = fd;
    if (!mask) {
        if (w->registered) epoll_ctl(ep, EPOLL_CTL_DEL, fd, &ev);
        w->registered = false;
        return;
    }
    int op = w->registered ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (epoll_ctl(ep, op, fd, &ev) < 0) { /* the fd was closed and reused, or registered elsewhere */
        if (errno == ENOENT) epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);
        else if (errno == EEXIST) epoll_ctl(ep, EPOLL_CTL_MOD, fd, &ev);
    }
    w->registered = true;
}
static int kevent(int ep, const struct kevent *ch, int nch, struct kevent *out, int nout, const struct timespec *timeout) {
    for (int i = 0; i < nch; i++) {
        int fd = (int)ch[i].ident;
        xs_fdw *w = xs_fdw_get(fd);
        bool rd = ch[i].filter == EVFILT_READ;
        if (ch[i].flags & EV_DELETE) {
            if (rd) { w->rd_on = false; w->rd = NULL; } else { w->wr_on = false; w->wr = NULL; }
        } else {
            if (rd) { w->rd_on = true; w->rd = ch[i].udata; w->rd_once = (ch[i].flags & EV_ONESHOT) != 0; }
            else { w->wr_on = true; w->wr = ch[i].udata; w->wr_once = (ch[i].flags & EV_ONESHOT) != 0; }
        }
        xs_ep_sync(ep, fd);
    }
    if (nout == 0) return 0;
    struct epoll_event evs[128];
    int n = epoll_wait(ep, evs, nout / 2 < 128 ? nout / 2 : 128, timeout ? (int)(timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000) : -1);
    if (n < 0) return -1;
    int k = 0;
    for (int i = 0; i < n; i++) {
        int fd = evs[i].data.fd;
        xs_fdw *w = xs_fdw_get(fd);
        bool changed = false;
        uint32_t e = evs[i].events;
        if ((e & (EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLRDHUP)) && w->rd_on) {
            EV_SET(&out[k], fd, EVFILT_READ, 0, 0, 0, w->rd); k++;
            if (w->rd_once) { w->rd_on = false; changed = true; }
        }
        if ((e & (EPOLLOUT | EPOLLHUP | EPOLLERR)) && w->wr_on) {
            EV_SET(&out[k], fd, EVFILT_WRITE, 0, 0, 0, w->wr); k++;
            if (w->wr_once) { w->wr_on = false; changed = true; }
        }
        if (changed) xs_ep_sync(ep, fd);
    }
    return k;
}
#endif
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#if !defined(_WIN32)
#include <poll.h>
#include <sys/mman.h>
#endif

/* ---- coroutines: one small stack per in-flight request; `await fetch` yields to the event loop ----
   xs_ctx_swap(old, new): saves the callee-saved registers on the current stack, stores the stack pointer
   in *old, switches to the new stack and restores its registers. A new coroutine's first "return" lands
   in xs_coro_tramp, which calls the entry function and then xs_coro_finish. */
#if defined(__APPLE__)
#define XS_SYM(x) "_" #x
#else
#define XS_SYM(x) #x
#endif
/* the context switch and the coroutine exit live in the runtime library only, not in a split build's program */
#ifdef XS_RT_DECL
#define XS_RT_ASM(code)
#else
#define XS_RT_ASM(code) __asm__(code)
#endif
#if defined(__aarch64__)
XS_RT_ASM(
    ".text\n"
    ".globl " XS_SYM(xs_ctx_swap) "\n"
    ".p2align 2\n"
    XS_SYM(xs_ctx_swap) ":\n"     /* x0 = void **old_sp, x1 = void *new_sp */
    "  stp x19, x20, [sp, #-16]!\n"
    "  stp x21, x22, [sp, #-16]!\n"
    "  stp x23, x24, [sp, #-16]!\n"
    "  stp x25, x26, [sp, #-16]!\n"
    "  stp x27, x28, [sp, #-16]!\n"
    "  stp x29, x30, [sp, #-16]!\n"
    "  stp d8,  d9,  [sp, #-16]!\n"
    "  stp d10, d11, [sp, #-16]!\n"
    "  stp d12, d13, [sp, #-16]!\n"
    "  stp d14, d15, [sp, #-16]!\n"
    "  mov x2, sp\n"
    "  str x2, [x0]\n"
    "  mov sp, x1\n"
    "  ldp d14, d15, [sp], #16\n"
    "  ldp d12, d13, [sp], #16\n"
    "  ldp d10, d11, [sp], #16\n"
    "  ldp d8,  d9,  [sp], #16\n"
    "  ldp x29, x30, [sp], #16\n"
    "  ldp x27, x28, [sp], #16\n"
    "  ldp x25, x26, [sp], #16\n"
    "  ldp x23, x24, [sp], #16\n"
    "  ldp x21, x22, [sp], #16\n"
    "  ldp x19, x20, [sp], #16\n"
    "  ret\n"
    ".globl " XS_SYM(xs_coro_tramp) "\n"
    ".p2align 2\n"
    XS_SYM(xs_coro_tramp) ":\n"   /* x19 = entry function, x20 = argument */
    "  mov x0, x20\n"
    "  blr x19\n"
    "  bl " XS_SYM(xs_coro_finish) "\n"
    "  brk #0\n");
#define XS_FRAME_WORDS 20
#define XS_FRAME_INIT(f, entry, arg) do { (f)[8] = 0; (f)[9] = (uint64_t)xs_coro_tramp; (f)[18] = (uint64_t)(entry); (f)[19] = (uint64_t)(arg); } while (0)
#define XS_FRAME_TOP_PAD 0
#elif defined(__x86_64__) && defined(_WIN32)
/* Windows x64: rcx = old_sp, rdx = new_sp; rbx rbp rdi rsi r12-r15 and xmm6-xmm15 are callee-saved */
XS_RT_ASM(
    ".text\n"
    ".globl " XS_SYM(xs_ctx_swap) "\n"
    ".p2align 4\n"
    XS_SYM(xs_ctx_swap) ":\n"
    "  pushq %rbp\n  pushq %rbx\n  pushq %rdi\n  pushq %rsi\n"
    "  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n"
    "  subq $160, %rsp\n"
    "  movdqu %xmm6, 0(%rsp)\n  movdqu %xmm7, 16(%rsp)\n  movdqu %xmm8, 32(%rsp)\n  movdqu %xmm9, 48(%rsp)\n"
    "  movdqu %xmm10, 64(%rsp)\n  movdqu %xmm11, 80(%rsp)\n  movdqu %xmm12, 96(%rsp)\n  movdqu %xmm13, 112(%rsp)\n"
    "  movdqu %xmm14, 128(%rsp)\n  movdqu %xmm15, 144(%rsp)\n"
    "  movq %rsp, (%rcx)\n"
    "  movq %rdx, %rsp\n"
    "  movdqu 0(%rsp), %xmm6\n  movdqu 16(%rsp), %xmm7\n  movdqu 32(%rsp), %xmm8\n  movdqu 48(%rsp), %xmm9\n"
    "  movdqu 64(%rsp), %xmm10\n  movdqu 80(%rsp), %xmm11\n  movdqu 96(%rsp), %xmm12\n  movdqu 112(%rsp), %xmm13\n"
    "  movdqu 128(%rsp), %xmm14\n  movdqu 144(%rsp), %xmm15\n"
    "  addq $160, %rsp\n"
    "  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n"
    "  popq %rsi\n  popq %rdi\n  popq %rbx\n  popq %rbp\n"
    "  ret\n"
    ".globl " XS_SYM(xs_coro_tramp) "\n"
    ".p2align 4\n"
    XS_SYM(xs_coro_tramp) ":\n"   /* r12 = entry, r13 = argument; rsp 16-aligned; 32 bytes of shadow space */
    "  movq %r13, %rcx\n"
    "  subq $32, %rsp\n"
    "  callq *%r12\n"
    "  callq " XS_SYM(xs_coro_finish) "\n"
    "  ud2\n");
/* frame, low to high: xmm6..xmm15 (20 words) r15 r14 r13 r12 rsi rdi rbx rbp ret, then 2 words so that rsp is
   16-aligned after the ret */
#define XS_FRAME_WORDS 31
#define XS_FRAME_INIT(f, entry, arg) do { (f)[22] = (uint64_t)(arg); (f)[23] = (uint64_t)(entry); (f)[27] = 0; (f)[28] = (uint64_t)xs_coro_tramp; } while (0)
#define XS_FRAME_TOP_PAD 0
#elif defined(__x86_64__)
XS_RT_ASM(
    ".text\n"
    ".globl " XS_SYM(xs_ctx_swap) "\n"
    ".p2align 4\n"
    XS_SYM(xs_ctx_swap) ":\n"     /* rdi = void **old_sp, rsi = void *new_sp */
    "  pushq %rbp\n"
    "  pushq %rbx\n"
    "  pushq %r12\n"
    "  pushq %r13\n"
    "  pushq %r14\n"
    "  pushq %r15\n"
    "  movq %rsp, (%rdi)\n"
    "  movq %rsi, %rsp\n"
    "  popq %r15\n"
    "  popq %r14\n"
    "  popq %r13\n"
    "  popq %r12\n"
    "  popq %rbx\n"
    "  popq %rbp\n"
    "  ret\n"
    ".globl " XS_SYM(xs_coro_tramp) "\n"
    ".p2align 4\n"
    XS_SYM(xs_coro_tramp) ":\n"   /* r12 = entry function, r13 = argument; rsp is 16-byte aligned here */
    "  movq %r13, %rdi\n"
    "  callq *%r12\n"
    "  callq " XS_SYM(xs_coro_finish) "\n"
    "  ud2\n");
/* frame, low to high: r15 r14 r13 r12 rbx rbp ret; after `ret` rsp = top - 16, aligned as at a call site */
#define XS_FRAME_WORDS 9
#define XS_FRAME_INIT(f, entry, arg) do { (f)[2] = (uint64_t)(arg); (f)[3] = (uint64_t)(entry); (f)[5] = 0; (f)[6] = (uint64_t)xs_coro_tramp; } while (0) /* rbp = 0 ends stack traces */
#define XS_FRAME_TOP_PAD 0
#else
#error "coroutines are implemented for arm64 and x86-64"
#endif
void xs_ctx_swap(void **old_sp, void *new_sp);
void xs_coro_tramp(void);

#define XS_CORO_STACK (64 * 1024)
#define XS_PAGE 16384 /* also a multiple of the 4 KB pages of x86-64 and most Linux arm64 kernels */

typedef struct xs_client xs_client;
typedef struct xs_coro xs_coro;
/* What a parked coroutine waits on: a socket and/or a timer. Events carry a waiter, not the coroutine,
   and a waiter that is no longer armed ignores its event (the other one won the race, or it is stale). */
typedef struct xs_waiter { int kind; xs_coro *co; bool armed; bool fired; int tfd; uint64_t armed_batch; uint32_t tgen; } xs_waiter; /* kind 2 */
static XS_TLS uint64_t xs_batch; /* event batches seen by this loop: a waiter armed in this batch ignores its events */
struct xs_coro {
    int kind;                 /* 1 (clients are 0, waiters 2) */
    xs_waiter fdw, tmw;
    void *sp;
    char *stack;              /* mmap'd, with a guard page at the bottom */
    bool done;
    /* per-coroutine runtime state, swapped in and out with the stack */
    void **ar_pool; size_t ar_len, ar_cap;
    jmp_buf **try_stack; int try_depth; void *thrown;
    jmp_buf *recover; const char *recover_msg;
    void ***cln; size_t cln_n, cln_cap, recover_cln; /* its cleanup stack (xs_cln_push) */
    uint64_t req_t0;
    void (*entry)(void *); void *arg;
    bool holds_lock;
    void (*on_done)(struct xs_coro *); /* not a request: what to do when it finishes (Promise.all tasks) */
    /* a shared-stack coroutine (Promise.all tasks) runs on the thread's one task stack; parked, the part it
       uses lives here and is copied back to the same addresses before it runs again */
    bool shared;
    char *copy, *copy_base;
    size_t copy_len, copy_cap;
    void *als;                /* its node:async_hooks context (an owned reference; xs_als_cur while it runs) */
    struct xs_coro *next;     /* free list */
};
static XS_TLS xs_coro *xs_coro_dead; /* finished during this event batch: recycled after it */

static XS_TLS uint64_t xs_req_t0;
static int64_t xs_inflight_total; /* requests whose handler is running or parked, across workers */
static XS_TLS xs_coro *xs_cur_coro;   /* NULL on the scheduler's own stack */
static XS_TLS void *xs_sched_sp;
static XS_TLS xs_coro *xs_coro_free;
static XS_TLS int xs_kq = -1;         /* this worker's kqueue, for async waits */
/* the scheduler's own copies of the swapped state */
static XS_TLS void **xs_main_ar_pool; static XS_TLS size_t xs_main_ar_len, xs_main_ar_cap;
static XS_TLS jmp_buf **xs_main_try; static XS_TLS int xs_main_try_depth; static XS_TLS void *xs_main_thrown;
static XS_TLS jmp_buf *xs_main_recover; static XS_TLS const char *xs_main_recover_msg;
static XS_TLS void ***xs_main_cln; static XS_TLS size_t xs_main_cln_n, xs_main_cln_cap, xs_main_recover_cln;
static XS_TLS void *xs_main_als;

__attribute__((used)) void xs_coro_finish(void);
#ifndef XS_RT_DECL
void xs_coro_finish(void) {
    xs_coro *co = xs_cur_coro;
    co->done = true;
    xs_ctx_swap(&co->sp, xs_sched_sp); /* never comes back */
}
#endif

XS_NOINLINE xs_coro *xs_coro_new(void (*entry)(void *), void *arg) {
    xs_coro *co = xs_coro_free;
    if (co) {
        xs_coro_free = co->next;
    } else {
        co = calloc(1, sizeof *co);
        co->kind = 1;
        co->fdw.kind = co->tmw.kind = 2;
        co->fdw.co = co->tmw.co = co;
        co->fdw.tfd = co->tmw.tfd = -1;
        co->stack = mmap(NULL, XS_CORO_STACK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (co->stack == MAP_FAILED) xs_panic("cannot allocate a coroutine stack");
        mprotect(co->stack, XS_PAGE, PROT_NONE); /* guard page */
        co->ar_cap = 64;
        co->ar_pool = malloc(co->ar_cap * sizeof(void *));
        co->try_stack = NULL; /* grows on first use (xs_try_push) */
    }
    if (co->als) xs_release_(co->als);
    co->als = xs_als_cur ? xs_retain_(xs_als_cur) : NULL; /* the context it was created in */
    co->done = false;
    co->on_done = NULL;
    co->ar_len = 0;
    co->try_depth = 0;
    co->thrown = NULL;
    co->recover = NULL;
    co->entry = entry;
    co->arg = arg;
    /* initial frame, matching what xs_ctx_swap pops: d14..d8, x29/x30, x27..x19 */
    uint64_t *top = (uint64_t *)(co->stack + XS_CORO_STACK);
    uint64_t *f = top - XS_FRAME_WORDS;
    memset(f, 0, XS_FRAME_WORDS * sizeof(uint64_t));
    XS_FRAME_INIT(f, entry, arg);
    co->sp = f;
    return co;
}

/* ---- shared-stack coroutines ----
   10k tasks parked at once had 10k stacks: one page each at least (16 KB on macOS) and a page fault to start
   every one. Tasks run instead on one stack per thread; when one parks, only the bytes it uses (usually
   well under 1 KB) are saved, and put back at the same addresses (so pointers into it stay valid) when it
   resumes. Nothing outside a parked task may point into its stack: channel waits are on the heap. */
#define XS_SHSTACK (1024 * 1024)
static XS_TLS char *xs_shstack;
static XS_TLS xs_coro *xs_coro_free_sh;
XS_NOINLINE char *xs_shstack_top(void) {
    if (!xs_shstack) {
        xs_shstack = mmap(NULL, XS_SHSTACK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (xs_shstack == MAP_FAILED) xs_panic("cannot allocate the task stack");
        mprotect(xs_shstack, XS_PAGE, PROT_NONE); /* guard page */
    }
    return xs_shstack + XS_SHSTACK;
}
XS_NOINLINE xs_coro *xs_coro_new_shared(void (*entry)(void *), void *arg) {
    xs_coro *co = xs_coro_free_sh;
    if (co) {
        xs_coro_free_sh = co->next;
    } else {
        co = calloc(1, sizeof *co);
        co->kind = 1;
        co->fdw.kind = co->tmw.kind = 2;
        co->fdw.co = co->tmw.co = co;
        co->fdw.tfd = co->tmw.tfd = -1;
        co->shared = true;
        co->ar_cap = 8;
        co->ar_pool = malloc(co->ar_cap * sizeof(void *));
    }
    if (co->als) xs_release_(co->als);
    co->als = xs_als_cur ? xs_retain_(xs_als_cur) : NULL; /* the context it was created in */
    co->done = false;
    co->on_done = NULL;
    co->ar_len = 0;
    co->try_depth = 0;
    co->thrown = NULL;
    co->recover = NULL;
    co->entry = entry;
    co->arg = arg;
    /* the initial frame goes in the saved copy: the stack may be in use by whoever creates us */
    size_t fb = XS_FRAME_WORDS * sizeof(uint64_t);
    if (co->copy_cap < fb) { co->copy_cap = fb < 256 ? 256 : fb; co->copy = realloc(co->copy, co->copy_cap); }
    uint64_t *f = (uint64_t *)co->copy;
    memset(f, 0, fb);
    XS_FRAME_INIT(f, entry, arg);
    co->copy_base = xs_shstack_top() - fb;
    co->copy_len = fb;
    co->sp = co->copy_base;
    return co;
}
/* the lowest address of the task stack in use by a coroutine that just parked (generators it runs keep their
   own stacks: the task's part ends where the outermost of them was entered) */
static char *xs_gen_shlow(char *lo, char *base, char *top); /* after the generators */
static char *xs_shstack_low(xs_coro *co) {
    char *lo = co->sp, *base = xs_shstack, *top = xs_shstack + XS_SHSTACK;
    if (lo < base || lo >= top) lo = top;
    return xs_gen_shlow(lo, base, top);
}

static void xs_coro_recycle(xs_coro *co) {
    /* not reused before the current batch of events is done: a stale event may still name its waiters */
    co->next = xs_coro_dead;
    xs_coro_dead = co;
}
static void xs_coro_flush_dead(void) {
    while (xs_coro_dead) {
        xs_coro *co = xs_coro_dead;
        xs_coro_dead = co->next;
        if (co->shared) { co->next = xs_coro_free_sh; xs_coro_free_sh = co; }
        else { co->next = xs_coro_free; xs_coro_free = co; }
    }
}

/* a parked task's stack bytes, copied out and back: under AddressSanitizer the copy carries the stack's poisoned
   redzones, which an instrumented memcpy reports; this copy is not instrumented (a normal build uses memcpy) */
#ifdef XS_ASAN
#include <sanitizer/asan_interface.h>
__attribute__((no_sanitize("address"))) static void xs_stack_copy(void *dst, const void *src, size_t n) {
    volatile char *d = dst;
    const volatile char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    /* the shadow of a task stack put back is the last task's: clear it (the heap is what this build checks) */
    if ((char *)dst >= xs_shstack && (char *)dst < xs_shstack + XS_SHSTACK) __asan_unpoison_memory_region(xs_shstack, XS_SHSTACK);
}
/* after a catch on a task's stack: the frames the longjmp skipped, below this one, lose their poison */
__attribute__((no_sanitize("address"))) static void xs_asan_clear_below(void) {
    char *sp = (char *)__builtin_frame_address(0);
    xs_coro *co = xs_cur_coro;
    if (!co) return; /* (the thread's own stack: the sanitizer's longjmp handling clears it) */
    char *lo = co->shared ? xs_shstack : co->stack ? co->stack + XS_PAGE : NULL;
    if (lo && sp > lo) __asan_unpoison_memory_region(lo, (size_t)(sp - lo));
}
__attribute__((constructor)) static void xs_asan_hook(void) { xs_asan_after_jump = xs_asan_clear_below; }
#else
#define xs_stack_copy memcpy
#endif
/* Runs or continues a coroutine until it yields or finishes. */
static void xs_coro_resume(xs_coro *co) {
    /* park the scheduler's runtime state, install the coroutine's */
    xs_main_ar_pool = xs_ar_pool; xs_main_ar_len = xs_ar_len; xs_main_ar_cap = xs_ar_cap;
    xs_main_try = xs_try_stack; xs_main_try_depth = xs_try_depth; xs_main_thrown = xs_thrown;
    xs_main_recover = xs_recover; xs_main_recover_msg = xs_recover_msg;
    xs_main_cln = xs_cln; xs_main_cln_n = xs_cln_n; xs_main_cln_cap = xs_cln_cap; xs_main_recover_cln = xs_recover_cln;
    xs_cln = co->cln; xs_cln_n = co->cln_n; xs_cln_cap = co->cln_cap; xs_recover_cln = co->recover_cln;
    xs_ar_pool = co->ar_pool; xs_ar_len = co->ar_len; xs_ar_cap = co->ar_cap;
    xs_try_stack = co->try_stack; xs_try_depth = co->try_depth; xs_thrown = co->thrown;
    xs_recover = co->recover; xs_recover_msg = co->recover_msg;
    xs_req_t0 = co->req_t0;
    xs_main_als = xs_als_cur; xs_als_cur = co->als; co->als = NULL;
    xs_cur_coro = co;
    if (co->shared) xs_stack_copy(co->copy_base, co->copy, co->copy_len); /* its stack back where it was */
#ifdef XS_ASAN
    else if (co->stack) __asan_unpoison_memory_region(co->stack + XS_PAGE, XS_CORO_STACK - XS_PAGE); /* (a reused stack keeps another run's shadow) */
#endif
    xs_ctx_swap(&xs_sched_sp, co->sp);
    if (co->shared && !co->done) { /* parked: keep the part of the task stack it uses */
        char *lo = xs_shstack_low(co);
        size_t n = (size_t)(xs_shstack + XS_SHSTACK - lo);
        if (n > co->copy_cap) { co->copy_cap = n + 256; co->copy = realloc(co->copy, co->copy_cap); }
        xs_stack_copy(co->copy, lo, n);
        co->copy_base = lo;
        co->copy_len = n;
    }
    /* back on the scheduler: save the coroutine's state, restore ours */
    co->ar_pool = xs_ar_pool; co->ar_len = xs_ar_len; co->ar_cap = xs_ar_cap;
    co->try_stack = xs_try_stack; co->try_depth = xs_try_depth; co->thrown = xs_thrown;
    co->recover = xs_recover; co->recover_msg = xs_recover_msg;
    co->cln = xs_cln; co->cln_n = xs_cln_n; co->cln_cap = xs_cln_cap; co->recover_cln = xs_recover_cln;
    xs_cln = xs_main_cln; xs_cln_n = xs_main_cln_n; xs_cln_cap = xs_main_cln_cap; xs_recover_cln = xs_main_recover_cln;
    co->req_t0 = xs_req_t0;
    if (co->done) { if (xs_als_cur) xs_release_(xs_als_cur); co->als = NULL; } else co->als = xs_als_cur;
    xs_als_cur = xs_main_als; xs_main_als = NULL;
    xs_cur_coro = NULL;
    xs_ar_pool = xs_main_ar_pool; xs_ar_len = xs_main_ar_len; xs_ar_cap = xs_main_ar_cap;
    xs_try_stack = xs_main_try; xs_try_depth = xs_main_try_depth; xs_thrown = xs_main_thrown;
    xs_recover = xs_main_recover; xs_recover_msg = xs_main_recover_msg;
}

static bool xs_handler_lock;
static pthread_mutex_t xs_handler_mutex = PTHREAD_MUTEX_INITIALIZER;
static void xs_coro_yield(void) {
    xs_coro *co = xs_cur_coro;
    /* a parked handler must not keep the handler lock (XISTO_LOCK): others would deadlock behind it */
    bool held = xs_handler_lock && co->holds_lock;
    if (held) pthread_mutex_unlock(&xs_handler_mutex);
    xs_ctx_swap(&co->sp, xs_sched_sp);
    if (held) pthread_mutex_lock(&xs_handler_mutex);
}

#if !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(_WIN32)
#include <sys/timerfd.h>
#endif
/* Timers live in a heap per event loop (thread), not in the kernel: arming one is a push, the loop waits in
   kevent only until the nearest deadline, and disarming just forgets it (an entry whose waiter was re-armed
   or disarmed is skipped when it comes up). With 10k sleeping tasks this took 2 syscalls per sleep away, and
   macOS stopped coalescing (delaying) thousands of kernel timers. */
typedef struct { uint64_t at; xs_waiter *w; uint32_t gen; } xs_tmr;
static XS_TLS xs_tmr *xs_tmrs;
static XS_TLS size_t xs_ntmr, xs_tmrcap;
static void xs_tmr_push(xs_tmr t) {
    if (xs_ntmr == xs_tmrcap) { xs_tmrcap = xs_tmrcap ? xs_tmrcap * 2 : 64; xs_tmrs = realloc(xs_tmrs, sizeof(xs_tmr) * xs_tmrcap); }
    size_t i = xs_ntmr++;
    while (i > 0 && xs_tmrs[(i - 1) / 2].at > t.at) { xs_tmrs[i] = xs_tmrs[(i - 1) / 2]; i = (i - 1) / 2; }
    xs_tmrs[i] = t;
}
static xs_tmr xs_tmr_pop(void) {
    xs_tmr top = xs_tmrs[0], last = xs_tmrs[--xs_ntmr];
    size_t i = 0;
    for (;;) {
        size_t c = 2 * i + 1;
        if (c >= xs_ntmr) break;
        if (c + 1 < xs_ntmr && xs_tmrs[c + 1].at < xs_tmrs[c].at) c++;
        if (xs_tmrs[c].at >= last.at) break;
        xs_tmrs[i] = xs_tmrs[c];
        i = c;
    }
    if (xs_ntmr) xs_tmrs[i] = last;
    return top;
}
static void xs_timer_arm(xs_waiter *w, int64_t ms) {
    w->armed = true;
    w->fired = false;
    w->armed_batch = xs_batch;
    w->tgen++;
    xs_tmr_push((xs_tmr){xs_now() + (uint64_t)(ms < 0 ? 0 : ms) * 1000000ull, w, w->tgen});
}
static void xs_timer_disarm(xs_waiter *w) { w->armed = false; }
/* how long the loop may sleep in kevent: until the nearest live timer (false: no timer) */
static bool xs_tmr_timeout(struct timespec *ts) {
    while (xs_ntmr && (!xs_tmrs[0].w->armed || xs_tmrs[0].w->tgen != xs_tmrs[0].gen)) xs_tmr_pop(); /* forgotten ones */
    if (!xs_ntmr) return false;
    uint64_t now = xs_now(), at = xs_tmrs[0].at;
    uint64_t d = at > now ? at - now : 0;
    ts->tv_sec = (time_t)(d / 1000000000ull);
    ts->tv_nsec = (long)(d % 1000000000ull);
    return true;
}
/* resumes the coroutines whose timers are due; after(co) runs once each is back on the loop */
static void xs_tmr_fire(void (*after)(xs_coro *)) {
    uint64_t now = xs_now();
    while (xs_ntmr && xs_tmrs[0].at <= now) {
        xs_tmr t = xs_tmr_pop();
        if (!t.w->armed || t.w->tgen != t.gen) continue;
        t.w->armed = false;
        t.w->fired = true;
        xs_coro *co = t.w->co;
        xs_coro_resume(co);
        if (after) after(co);
        now = xs_now();
    }
}

/* Waits until fd is readable/writable or the timeout (ms, -1 = none) passes. Yields inside a coroutine,
   polls from plain code. Returns false on timeout. */
static bool xs_wait_fd_ms(int fd, int16_t filter, int64_t timeout_ms) {
    if (xs_cur_coro && xs_kq >= 0) {
        xs_coro *co = xs_cur_coro;
        struct kevent ev;
        co->fdw.armed = true;
        co->fdw.fired = false;
        co->fdw.armed_batch = xs_batch;
        EV_SET(&ev, fd, filter, EV_ADD | EV_ONESHOT, 0, 0, &co->fdw);
        kevent(xs_kq, &ev, 1, NULL, 0, NULL);
        if (timeout_ms >= 0) xs_timer_arm(&co->tmw, timeout_ms);
        xs_coro_yield();
        bool ready = co->fdw.fired;
        if (co->fdw.armed) { /* the timer won: stop watching the socket */
            co->fdw.armed = false;
            EV_SET(&ev, fd, filter, EV_DELETE, 0, 0, NULL);
            kevent(xs_kq, &ev, 1, NULL, 0, NULL);
        }
        if (timeout_ms >= 0) xs_timer_disarm(&co->tmw);
        return ready;
    }
    struct pollfd p = {fd, filter == EVFILT_READ ? POLLIN : POLLOUT, 0};
    int r;
    while ((r = poll(&p, 1, timeout_ms < 0 ? -1 : (int)timeout_ms)) < 0 && errno == EINTR) {}
    return r > 0;
}
static void xs_wait_fd(int fd, int16_t filter) { xs_wait_fd_ms(fd, filter, -1); }

static XS_TLS int64_t xs_tasks_live; /* Promise.all/race tasks not finished yet */
static void xs_loop_run(bool (*stop)(void *), void *arg, int64_t deadline_ms);
static void xs_unh_check(void); /* (Promise values, below) */
/* setTimeout from node:timers/promises: inside a handler the coroutine sleeps and the server keeps going */
XS_NOINLINE void xs_sleep_ms(int64_t ms) {
    if (ms < 0) ms = 0;
    if (xs_cur_coro && xs_kq >= 0) {
        xs_coro *co = xs_cur_coro;
        xs_timer_arm(&co->tmw, ms);
        xs_coro_yield();
        xs_timer_disarm(&co->tmw);
        return;
    }
    if (xs_tasks_live > 0) { xs_loop_run(NULL, NULL, (int64_t)xs_date_now() + ms); return; } /* background tasks go on meanwhile */
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000};
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {}
}

/* ---- channels: values between coroutines (request handlers) of the same event loop ----
   receive() parks the handler until a value arrives; send() parks it only when the buffer is full.
   A woken coroutine goes on the ready list; the event loop resumes it after the current one yields. */
static XS_TLS xs_coro *xs_ready_head, *xs_ready_tail;
static void xs_ready_push(xs_coro *co) {
    co->next = NULL;
    if (xs_ready_tail) xs_ready_tail->next = co; else xs_ready_head = co;
    xs_ready_tail = co;
}

/* ---- Promise.all / Promise.race: every element runs as a coroutine on the event loop ----
   The compiler turns each element into a thunk (a closure of no arguments, or the .map callback with its
   element). Inside a coroutine (a request handler, another task) the children go on the ready list and the
   caller parks until they are done; from plain code a loop runs here until they are. I/O inside the tasks
   (fetch, pg, redis, sleep) parks only its own task, so the waits overlap. A thrown error is kept and thrown
   again in the caller once the group settles. race: the first to finish wins; the others go on in the
   background (as in Node, the program waits for them before exiting). */
typedef struct xs_tgroup xs_tgroup;
typedef struct xs_task {
    xs_closure *cl;
    int kind, argkind;      /* val kinds of the result and of the argument (0 number, 1 int, 2 bool, 3 reference) */
    bool has_arg;
    xs_val arg;             /* .map callbacks: the element (borrowed: the source array outlives the group) */
    xs_val out;
    bool done, taken;
    void *err;
    xs_tgroup *g;
} xs_task;
struct xs_tgroup { int n, pending, refs, mode, winner; xs_coro *parent; bool parent_waiting; xs_task t[]; };

XS_NOINLINE xs_tgroup *xs_par_new(int n, int mode) {
    xs_tgroup *g = calloc(1, sizeof *g + sizeof(xs_task) * (size_t)(n ? n : 1));
    g->n = n;
    g->pending = n;
    g->refs = n + 1;
    g->mode = mode;
    g->winner = -1;
    for (int i = 0; i < n; i++) g->t[i].g = g;
    return g;
}
static void xs_par_unref(xs_tgroup *g) {
    if (--g->refs) return;
    for (int i = 0; i < g->n; i++) {
        xs_task *t = &g->t[i];
        xs_release(t->cl);
        if (t->done && !t->taken && t->kind == 3 && !t->err) xs_release(t->out.p);
        if (t->err && !t->taken) xs_release(t->err);
    }
    free(g);
}
static xs_val xs_task_call(xs_task *t) {
    xs_val v;
    void *fn = t->cl->fn, *cl = t->cl;
#define XS_TCALL(R, field) \
    if (!t->has_arg) v.field = ((R (*)(void *))fn)(cl); \
    else switch (t->argkind) { \
        case 0: v.field = ((R (*)(void *, double))fn)(cl, t->arg.d); break; \
        case 1: v.field = ((R (*)(void *, int64_t))fn)(cl, t->arg.i); break; \
        case 2: v.field = ((R (*)(void *, bool))fn)(cl, t->arg.i != 0); break; \
        default: v.field = ((R (*)(void *, void *))fn)(cl, t->arg.p); break; }
    switch (t->kind) {
    case 0: XS_TCALL(double, d) break;
    case 1: XS_TCALL(int64_t, i) break;
    case 2: { bool b; XS_TCALL(bool, i) (void)b; break; }
    default: XS_TCALL(void *, p) break;
    }
#undef XS_TCALL
    return v;
}
static void xs_task_entry(void *p) {
    xs_task *t = p;
    size_t mark = xs_ar_mark();
    jmp_buf jb;
    xs_try_push(&jb);
    if (_setjmp(jb) == 0) {
        t->out = xs_task_call(t);
        xs_try_pop();
    } else {
        t->err = xs_caught();
    }
    xs_ar_drain(mark);
    t->done = true;
    xs_tgroup *g = t->g;
    g->pending--;
    if (g->winner < 0) g->winner = (int)(t - g->t);
    bool settled = g->mode == 1 ? true : g->pending == 0;
    if (settled && g->parent && g->parent_waiting) { g->parent_waiting = false; xs_ready_push(g->parent); }
}
static void xs_task_done(xs_coro *co) {
    xs_task *t = co->arg;
    xs_tasks_live--;
    xs_coro_recycle(co);
    xs_par_unref(t->g);
}
static void xs_task_after(xs_coro *co) { if (co->done && co->on_done) co->on_done(co); }
static bool xs_par_settled(xs_tgroup *g) { return g->mode == 1 ? g->winner >= 0 : g->pending == 0; }
/* runs the loop from plain code until stop() (or nothing more can happen) */
XS_NOINLINE void xs_loop_run(bool (*stop)(void *), void *arg, int64_t deadline_ms) {
    if (xs_kq < 0) xs_kq = kqueue();
    struct kevent evs[64];
    for (;;) {
        while (xs_ready_head) {
            xs_coro *co = xs_ready_head;
            xs_ready_head = co->next;
            if (!xs_ready_head) xs_ready_tail = NULL;
            xs_coro_resume(co);
            if (co->done && co->on_done) co->on_done(co);
        }
        xs_coro_flush_dead();
        xs_unh_check();
        if (stop && stop(arg)) return;
        int64_t wait = -1;
        if (deadline_ms >= 0) { wait = deadline_ms - (int64_t)xs_date_now(); if (wait <= 0) return; }
        if (!stop && xs_tasks_live == 0 && deadline_ms < 0) return;
        struct timespec ts = {wait / 1000, (wait % 1000) * 1000000}, tt;
        bool has_w = wait >= 0;
        if (xs_tmr_timeout(&tt) && (!has_w || tt.tv_sec < ts.tv_sec || (tt.tv_sec == ts.tv_sec && tt.tv_nsec < ts.tv_nsec))) { ts = tt; has_w = true; }
        int n = kevent(xs_kq, NULL, 0, evs, 64, xs_ready_head ? &(struct timespec){0, 0} : has_w ? &ts : NULL);
        xs_batch++;
        if (n < 0) { if (errno == EINTR) continue; xs_panic("kevent failed"); }
        xs_tmr_fire(xs_task_after);
        for (int i = 0; i < n; i++) {
            if (!evs[i].udata || *(int *)evs[i].udata != 2) continue;
            xs_waiter *w = evs[i].udata;
            if (!w->armed || w->armed_batch == xs_batch) continue;
            w->armed = false;
            w->fired = true;
            xs_coro *co = w->co;
            xs_coro_resume(co);
            if (co->done && co->on_done) co->on_done(co);
        }
    }
}
static bool xs_par_stop(void *g) { return xs_par_settled(g); }
/* runs the group's tasks; returns when it settles (all done, or the first for race) */
static void xs_tasks_drain(void);
XS_NOINLINE void xs_par_run(xs_tgroup *g) {
    xs_exit_hook = xs_tasks_drain;
    if (xs_cur_coro && xs_kq >= 0) { /* in a coroutine: the loop that runs us runs them */
        for (int i = 0; i < g->n; i++) {
            xs_coro *co = xs_coro_new_shared(xs_task_entry, &g->t[i]);
            co->on_done = xs_task_done;
            xs_tasks_live++;
            xs_ready_push(co);
        }
        g->parent = xs_cur_coro;
        while (!xs_par_settled(g)) { g->parent_waiting = true; xs_coro_yield(); }
        g->parent = NULL;
    } else {
        /* from plain code each task starts now; one that finishes without waiting hands its stack to the next */
        if (xs_kq < 0) xs_kq = kqueue();
        for (int i = 0; i < g->n; i++) {
            xs_coro *co = xs_coro_new_shared(xs_task_entry, &g->t[i]);
            co->on_done = xs_task_done;
            xs_tasks_live++;
            xs_coro_resume(co);
            if (co->done) { co->on_done(co); xs_coro_flush_dead(); }
        }
        xs_loop_run(xs_par_stop, g, -1);
    }
}
/* after reading the results: the first error (all), or the winner's (race), is thrown */
XS_NOINLINE void xs_par_check(xs_tgroup *g) {
    void *err = NULL;
    if (g->mode == 1) { xs_task *w = &g->t[g->winner]; if (w->err) { err = w->err; w->taken = true; } }
    else for (int i = 0; i < g->n && !err; i++) if (g->t[i].err) { err = g->t[i].err; g->t[i].taken = true; }
    if (err) {
        for (int i = 0; i < g->n; i++) if (g->mode == 0 && g->t[i].kind == 3 && !g->t[i].err) g->t[i].taken = false; /* results unread: released with the group */
        xs_par_unref(g);
        xs_throw(err);
    }
}
static inline xs_val xs_par_take(xs_tgroup *g, int i) { g->t[i].taken = true; return g->t[i].out; }
/* the program ends when the background tasks (race losers) do, as in Node */
XS_NOINLINE void xs_tasks_drain(void) { if (xs_tasks_live > 0 && !xs_cur_coro) xs_loop_run(NULL, NULL, -1); xs_unh_check(); }

/* ---- Promise values (XB-106) ----
   await f() and a statement f() call the async function directly (its I/O parks the caller): no promise is made.
   A promise is made only where the program keeps one: const p = f(), a field or parameter of type Promise<T>,
   p.then/catch/finally, new Promise(...), Promise.resolve/reject as a value. The call then runs on a coroutine of
   its own, started at once (as in Node, the body runs until its first wait before the caller goes on); the promise
   settles when it returns or throws. then/catch/finally callbacks are microtasks: coroutines on the ready list, in
   the order their promises settle. await of a pending promise parks the coroutine (from plain code, runs the loop);
   a rejection nobody handles ends the program as Node does (exit code 1). */
typedef struct xs_promise xs_promise;
typedef struct xs_pwait { xs_coro *co; struct xs_pwait *next; } xs_pwait; /* on the heap: a parked task's stack moves */
typedef struct xs_preact {
    int how;                /* 0 then (ok and/or err), 1 finally (ok, no argument) */
    xs_closure *ok, *err;
    int okk, errk;          /* what each returns: a val kind, 4 nothing, 5 a promise (awaited) */
    xs_promise *src, *dst;
    struct xs_preact *next;
} xs_preact;
struct xs_promise {
    xs_hdr hdr;
    int8_t state;           /* 0 pending, 1 fulfilled, 2 rejected */
    int8_t kind;            /* of the value: 0 number, 1 int, 2 boolean, 3 reference, 4 none (void) */
    bool handled, in_unh;
    xs_val val;             /* fulfilled: owned when a reference */
    void *err;              /* rejected: the Error, owned */
    xs_pwait *waiters;
    xs_preact *reacts, *reacts_tail;
    xs_promise *unh_next;
};
static XS_TLS xs_promise *xs_unh; /* rejected, no handler yet */
static void xs_promise_free(void *p) {
    xs_promise *q = p;
    if (q->state == 1 && q->kind == 3) xs_release_(q->val.p);
    if (q->state == 2 && q->err) xs_release_(q->err);
    xs_obj_free(p);
}
static void xs_print_any_quoted(void *p);
static void xs_promise_print(void *p) {
    xs_promise *q = p;
    fputs("Promise { ", XS_OUT);
    if (q->state == 0) fputs("<pending>", XS_OUT);
    else if (q->state == 2) { fputs("<rejected> ", XS_OUT); xs_print_obj(q->err); }
    else switch (q->kind) {
        case 0: xs_print_num(q->val.d); break;
        case 1: xs_print_int(q->val.i); break;
        case 2: xs_print_bool(q->val.i != 0); break;
        case 3: xs_print_any_quoted(q->val.p); break;
        default: fputs("undefined", XS_OUT); break;
    }
    fputs(" }", XS_OUT);
}
XS_NOINLINE xs_promise *xs_promise_make(int kind) {
    xs_promise *p = xs_obj_alloc(sizeof *p, xs_promise_free, xs_promise_print);
    p->kind = (int8_t)kind;
    return p;
}
static void xs_unh_drop(xs_promise *p) {
    if (!p->in_unh) return;
    for (xs_promise **pp = &xs_unh; *pp; pp = &(*pp)->unh_next)
        if (*pp == p) { *pp = p->unh_next; break; }
    p->in_unh = false;
    xs_release_(p);
}
XS_NOINLINE void xs_preact_sched(xs_preact *r);
XS_NOINLINE void xs_promise_settled(xs_promise *p) {
    for (xs_pwait *w = p->waiters, *n; w; w = n) { n = w->next; xs_ready_push(w->co); free(w); }
    p->waiters = NULL;
    xs_preact *r = p->reacts;
    p->reacts = p->reacts_tail = NULL;
    if (r) p->handled = true;
    for (xs_preact *n; r; r = n) { n = r->next; xs_preact_sched(r); }
    if (p->state == 2 && !p->handled) { p->in_unh = true; p->unh_next = xs_unh; xs_unh = xs_retain_(p); }
}
XS_NOINLINE void xs_promise_fulfill(xs_promise *p, xs_val v) {
    if (p->state != 0) { if (p->kind == 3) xs_release_(v.p); return; }
    p->state = 1;
    p->val = v;
    xs_promise_settled(p);
}
XS_NOINLINE void xs_promise_reject(xs_promise *p, void *err) {
    if (p->state != 0) { xs_release_(err); return; }
    p->state = 2;
    p->err = err;
    xs_promise_settled(p);
}
/* a rejection that no handler took by the time the loop has nothing ready: the program ends, as in Node */
static void xs_unh_check(void) {
    if (!xs_unh) return;
    xs_promise *p = xs_unh;
    while (p->unh_next) p = p->unh_next; /* the first rejected */
    xs_Error *e = p->err;
    fflush(stdout);
    xs_out_drain();
    fprintf(stderr, "uncaught Error (in promise): %s\n", e->f_message->data);
    if (e->trace) xs_trace_print(e->trace, stderr);
    if (!xs_never) exit(1);
}

/* resume a coroutine from inside another (a call that makes a promise starts its coroutine at once): the outer
   coroutine's runtime state is the "scheduler's" for this resume, and the real scheduler's is kept aside */
static void xs_coro_resume_any(xs_coro *co) {
    if (!xs_cur_coro) { xs_coro_resume(co); return; }
    xs_coro *outer = xs_cur_coro;
    void **m_ar = xs_main_ar_pool; size_t m_al = xs_main_ar_len, m_ac = xs_main_ar_cap;
    jmp_buf **m_try = xs_main_try; int m_td = xs_main_try_depth; void *m_th = xs_main_thrown;
    jmp_buf *m_rec = xs_main_recover; const char *m_rm = xs_main_recover_msg;
    void ***m_cln = xs_main_cln; size_t m_cn = xs_main_cln_n, m_cc = xs_main_cln_cap, m_rc = xs_main_recover_cln;
    void *m_als = xs_main_als;
    void *sched = xs_sched_sp;
    xs_coro_resume(co);
    xs_sched_sp = sched;
    xs_main_ar_pool = m_ar; xs_main_ar_len = m_al; xs_main_ar_cap = m_ac;
    xs_main_try = m_try; xs_main_try_depth = m_td; xs_main_thrown = m_th;
    xs_main_recover = m_rec; xs_main_recover_msg = m_rm;
    xs_main_cln = m_cln; xs_main_cln_n = m_cn; xs_main_cln_cap = m_cc; xs_main_recover_cln = m_rc;
    xs_main_als = m_als;
    xs_cur_coro = outer;
}

/* a call kept as a promise: the compiler packs the callee's arguments (run reads them, afree releases them) */
typedef struct xs_spawn { xs_promise *p; xs_val (*run)(void *); void (*afree)(void *); void *args; } xs_spawn;
XS_NOINLINE void xs_spawn_entry(void *x) {
    xs_spawn *s = x;
    size_t mark = xs_ar_mark();
    jmp_buf jb;
    xs_try_push(&jb);
    if (_setjmp(jb) == 0) {
        xs_val v = s->run(s->args);
        xs_try_pop();
        xs_promise_fulfill(s->p, v);
    } else {
        xs_promise_reject(s->p, xs_caught());
    }
    xs_ar_drain(mark);
    s->afree(s->args);
    s->args = NULL;
}
static void xs_spawn_done(xs_coro *co) {
    xs_spawn *s = co->arg;
    xs_tasks_live--;
    xs_coro_recycle(co);
    xs_release_(s->p);
    free(s);
}
static void xs_tasks_drain(void);
XS_NOINLINE xs_promise *xs_promise_spawn(int kind, xs_val (*run)(void *), void (*afree)(void *), void *args) {
    if (xs_kq < 0) xs_kq = kqueue();
    xs_exit_hook = xs_tasks_drain;
    xs_promise *p = xs_promise_make(kind);
    xs_spawn *s = malloc(sizeof *s);
    s->p = xs_retain_(p);
    s->run = run;
    s->afree = afree;
    s->args = args;
    xs_coro *co = xs_coro_new(xs_spawn_entry, s);
    co->on_done = xs_spawn_done;
    xs_tasks_live++;
    xs_coro_resume_any(co); /* runs until its first wait, as an async function does */
    if (co->done) { co->on_done(co); if (!xs_cur_coro) xs_coro_flush_dead(); }
    return p;
}
XS_NOINLINE xs_promise *xs_promise_resolved(int kind, xs_val v) {
    xs_promise *p = xs_promise_make(kind);
    if (kind == 3) xs_retain_(v.p);
    p->state = 1;
    p->val = v;
    return p;
}
static inline xs_promise *xs_promise_resolved_d(double v) { return xs_promise_resolved(0, (xs_val){.d = v}); }
static inline xs_promise *xs_promise_resolved_i(int64_t v) { return xs_promise_resolved(1, (xs_val){.i = v}); }
static inline xs_promise *xs_promise_resolved_b(bool v) { return xs_promise_resolved(2, (xs_val){.i = v}); }
static inline xs_promise *xs_promise_resolved_p(void *v) { return xs_promise_resolved(3, (xs_val){.p = v}); }
static inline xs_promise *xs_promise_resolved_v(void) { return xs_promise_resolved(4, (xs_val){0}); }
#define xs_promise_resolved_vx(x) ((void)(x), xs_promise_resolved_v()) /* Promise.resolve(null), Promise.resolve(f()) with f void */
XS_NOINLINE xs_promise *xs_promise_rejected(int kind, void *err) {
    xs_promise *p = xs_promise_make(kind);
    xs_exit_hook = xs_tasks_drain;
    xs_promise_reject(p, xs_retain_(err));
    return p;
}

/* await p */
static bool xs_promise_stop(void *p) {
    xs_promise *q = p;
    if (q->state != 0) return true;
    if (xs_tasks_live == 0 && !xs_ready_head) { /* nothing left that could settle it: Node's loop would be empty */
        xs_unh_check();
        fflush(stdout);
        xs_out_drain();
        exit(0);
    }
    return false;
}
XS_NOINLINE xs_val xs_promise_await(xs_promise *p) {
    /* the awaiting task holds the promise while it waits, as in Node: the one who handed it over (a field, the cell of
       a variable a reaction captures) may let it go meanwhile (a .catch that clears the cache it lived in freed it,
       and the task woke up reading freed memory) */
    xs_retain_(p);
    p->handled = true;
    xs_unh_drop(p);
    if (xs_cur_coro && xs_kq >= 0) {
        if (p->state != 0 && xs_ready_head) { xs_ready_push(xs_cur_coro); xs_coro_yield(); } /* one tick: the reactions queued before go first */
        while (p->state == 0) {
            xs_pwait *w = malloc(sizeof *w);
            w->co = xs_cur_coro;
            w->next = NULL;
            xs_pwait **pp = &p->waiters;
            while (*pp) pp = &(*pp)->next;
            *pp = w;
            xs_coro_yield();
        }
    } else if (p->state == 0 || xs_ready_head) {
        if (xs_kq < 0) xs_kq = kqueue();
        xs_loop_run(xs_promise_stop, p, -1);
    }
    if (p->state == 2) {
        void *e = xs_retain_(p->err);
        xs_release_(p);
        xs_throw(e);
    }
    xs_val v = p->val;
    if (p->kind == 3) xs_retain_(v.p);
    xs_release_(p);
    return v;
}
XS_NOINLINE double xs_promise_await_d(xs_promise *p) { return xs_promise_await(p).d; }
XS_NOINLINE int64_t xs_promise_await_i(xs_promise *p) { return xs_promise_await(p).i; }
XS_NOINLINE bool xs_promise_await_b(xs_promise *p) { return xs_promise_await(p).i != 0; }
XS_NOINLINE void *xs_promise_await_p(xs_promise *p) { return p ? xs_promise_await(p).p : NULL; } /* (await of null: null) */
XS_NOINLINE void xs_promise_await_v(xs_promise *p) { if (p) (void)xs_promise_await(p); }

/* calls a callback: argument of kind ak (5: none), result of kind rk (4: none, 5: a promise, awaited) */
XS_NOINLINE xs_val xs_pcall(xs_closure *cl, int ak, xs_val a, int rk) {
    xs_val v = {0};
    void *fn = cl->fn;
#define XS_PCALL(R, put) \
    switch (ak) { \
        case 0: put ((R (*)(void *, double))fn)(cl, a.d); break; \
        case 1: put ((R (*)(void *, int64_t))fn)(cl, a.i); break; \
        case 2: put ((R (*)(void *, bool))fn)(cl, a.i != 0); break; \
        case 3: put ((R (*)(void *, void *))fn)(cl, a.p); break; \
        default: put ((R (*)(void *))fn)(cl); break; }
    switch (rk) {
    case 0: XS_PCALL(double, v.d =) break;
    case 1: XS_PCALL(int64_t, v.i =) break;
    case 2: XS_PCALL(bool, v.i =) break;
    case 3: XS_PCALL(void *, v.p =) break;
    case 4: XS_PCALL(void, ) break;
    default: {
        XS_PCALL(void *, v.p =)
        xs_promise *q = v.p;
        jmp_buf jb;
        xs_try_push(&jb);
        if (_setjmp(jb) == 0) { v = xs_promise_await(q); xs_try_pop(); }
        else { xs_release_(q); xs_throw(xs_caught()); }
        xs_release_(q);
        break;
    }
    }
#undef XS_PCALL
    return v;
}
XS_NOINLINE void xs_preact_run(void *x) {
    xs_preact *r = x;
    xs_promise *s = r->src, *d = r->dst;
    size_t mark = xs_ar_mark();
    jmp_buf jb;
    xs_try_push(&jb);
    if (_setjmp(jb) == 0) {
        if (r->how == 1) {
            xs_val ig = xs_pcall(r->ok, 5, (xs_val){0}, r->okk);
            if (r->okk == 3) xs_release_(ig.p);
            xs_try_pop();
            if (s->state == 1) { xs_val v = s->val; if (s->kind == 3) xs_retain_(v.p); xs_promise_fulfill(d, v); }
            else xs_promise_reject(d, xs_retain_(s->err));
        } else {
            bool ok = s->state == 1;
            xs_closure *cb = ok ? r->ok : r->err;
            if (!cb) {
                xs_try_pop();
                if (ok) { xs_val v = s->val; if (s->kind == 3) xs_retain_(v.p); xs_promise_fulfill(d, v); }
                else xs_promise_reject(d, xs_retain_(s->err));
            } else {
                int ck = ok ? r->okk : r->errk;
                xs_val v = xs_pcall(cb, ok ? (s->kind == 4 ? 5 : s->kind) : 3, ok ? s->val : (xs_val){.p = s->err}, ck);
                xs_try_pop();
                if (d->kind == 4 && ck == 3 && v.p) { xs_release_(v.p); v.p = NULL; } /* a dropped promise's value */
                xs_promise_fulfill(d, v);
            }
        }
    } else {
        xs_promise_reject(d, xs_caught());
    }
    xs_ar_drain(mark);
}
static void xs_preact_done(xs_coro *co) {
    xs_preact *r = co->arg;
    xs_tasks_live--;
    xs_coro_recycle(co);
    xs_release_(r->src);
    xs_release_(r->dst);
    if (r->ok) xs_release_(r->ok);
    if (r->err) xs_release_(r->err);
    free(r);
}
XS_NOINLINE void xs_preact_sched(xs_preact *r) {
    xs_coro *co = xs_coro_new_shared(xs_preact_run, r);
    co->on_done = xs_preact_done;
    xs_ready_push(co);
}
/* p.then(ok, err) / p.catch(err) / p.finally(ok): the new promise (kind dk) */
XS_NOINLINE xs_promise *xs_promise_then(xs_promise *p, int how, xs_closure *ok, int okk, xs_closure *err, int errk, int dk) {
    if (xs_kq < 0) xs_kq = kqueue();
    xs_exit_hook = xs_tasks_drain;
    xs_promise *d = xs_promise_make(dk);
    xs_preact *r = calloc(1, sizeof *r);
    r->how = how;
    r->ok = ok ? xs_retain_(ok) : NULL;
    r->err = err ? xs_retain_(err) : NULL;
    r->okk = okk;
    r->errk = errk;
    r->src = xs_retain_(p);
    r->dst = xs_retain_(d);
    xs_tasks_live++;
    p->handled = true;
    xs_unh_drop(p);
    if (p->state != 0) xs_preact_sched(r);
    else {
        if (p->reacts_tail) p->reacts_tail->next = r; else p->reacts = r;
        p->reacts_tail = r;
    }
    return d;
}

/* Promise.race(ps): a promise that settles as the first of them does */
static void xs_presolver_print(void *x);
XS_NOINLINE void xs_prace_fwd(xs_promise *from, xs_promise *to) {
    if (from->state == 1) { xs_val v = from->val; if (from->kind == 3) xs_retain_(v.p); xs_promise_fulfill(to, v); }
    else xs_promise_reject(to, xs_retain_(from->err));
}
typedef struct { xs_closure c; xs_promise *to, *from; } xs_prace_cl;
static void xs_prace_cl_free(void *x) { xs_prace_cl *r = x; xs_release_(r->to); xs_release_(r->from); xs_obj_free(x); }
XS_NOINLINE void xs_prace_cb(void *c) { xs_prace_cl *r = c; xs_prace_fwd(r->from, r->to); }
XS_NOINLINE xs_promise *xs_promise_race_arr(xs_arr_ref *ps) {
    if (ps->len == 0) xs_panic("Promise.race of an empty list never settles");
    xs_promise *to = xs_promise_make(((xs_promise *)ps->data[0])->kind);
    for (size_t i = 0; i < ps->len && to->state == 0; i++) {
        xs_promise *q = ps->data[i];
        if (q->state != 0) { q->handled = true; xs_unh_drop(q); xs_prace_fwd(q, to); break; }
        xs_prace_cl *cl = xs_obj_alloc(sizeof *cl, xs_prace_cl_free, xs_presolver_print);
        cl->c.fn = (void *)xs_prace_cb;
        cl->to = xs_retain_(to);
        cl->from = xs_retain_(q);
        xs_promise *d = xs_promise_then(q, 1, &cl->c, 4, NULL, 4, q->kind);
        d->handled = true;
        xs_release_(d);
        xs_release_(cl);
    }
    return to;
}
/* new Promise((resolve, reject) => ...): the two functions are closures that hold the promise */
typedef struct { xs_closure c; xs_promise *p; } xs_presolver;
static void xs_presolver_free(void *x) { xs_release_(((xs_presolver *)x)->p); xs_obj_free(x); }
static void xs_presolver_print(void *x) { (void)x; fputs("[Function (anonymous)]", XS_OUT); }
XS_NOINLINE void xs_pres_d(void *c, double v) { xs_promise_fulfill(((xs_presolver *)c)->p, (xs_val){.d = v}); }
XS_NOINLINE void xs_pres_i(void *c, int64_t v) { xs_promise_fulfill(((xs_presolver *)c)->p, (xs_val){.i = v}); }
XS_NOINLINE void xs_pres_b(void *c, bool v) { xs_promise_fulfill(((xs_presolver *)c)->p, (xs_val){.i = v}); }
XS_NOINLINE void xs_pres_p(void *c, void *v) { xs_promise_fulfill(((xs_presolver *)c)->p, (xs_val){.p = v ? xs_retain_(v) : NULL}); }
XS_NOINLINE void xs_pres_v(void *c) { xs_promise_fulfill(((xs_presolver *)c)->p, (xs_val){0}); }
XS_NOINLINE void xs_prej(void *c, void *e) { xs_exit_hook = xs_tasks_drain; xs_promise_reject(((xs_presolver *)c)->p, xs_retain_(e)); }
static xs_closure *xs_presolver_new(xs_promise *p, void *fn) {
    xs_presolver *r = xs_obj_alloc(sizeof *r, xs_presolver_free, xs_presolver_print);
    r->c.fn = fn;
    r->p = xs_retain_(p);
    return &r->c;
}
XS_NOINLINE xs_promise *xs_promise_new(int kind, xs_closure *exec) {
    if (xs_kq < 0) xs_kq = kqueue();
    xs_promise *p = xs_promise_make(kind);
    void *fn = kind == 0 ? (void *)xs_pres_d : kind == 1 ? (void *)xs_pres_i : kind == 2 ? (void *)xs_pres_b : kind == 3 ? (void *)xs_pres_p : (void *)xs_pres_v;
    xs_closure *res = xs_presolver_new(p, fn), *rej = xs_presolver_new(p, (void *)xs_prej);
    jmp_buf jb;
    xs_try_push(&jb);
    if (_setjmp(jb) == 0) {
        ((void (*)(void *, void *, void *))exec->fn)(exec, res, rej);
        xs_try_pop();
    } else {
        xs_exit_hook = xs_tasks_drain;
        xs_promise_reject(p, xs_caught());
    }
    xs_release_(res);
    xs_release_(rej);
    return p;
}

typedef struct xs_chan_wait { xs_coro *co; xs_val val; bool has_val; struct xs_chan_wait *next; } xs_chan_wait;
typedef struct xs_chan {
    xs_hdr hdr;
    int kind;                 /* 0 num, 1 int, 2 bool, 3 heap pointer */
    xs_val *buf; size_t cap, len, head;
    xs_chan_wait *receivers, *senders; /* FIFO of parked coroutines */
    bool closed;
} xs_chan;
static void xs_chan_free(void *p) {
    xs_chan *c = p;
    for (size_t i = 0; i < c->len; i++) if (c->kind == 3) xs_release_(c->buf[(c->head + i) % c->cap].p);
    free(c->buf);
    xs_obj_free(p);
}
static void xs_chan_print(void *p) { fprintf(XS_OUT, "Channel { size: %zu }", ((xs_chan *)p)->len); }
static bool xs_channels_used;
XS_NOINLINE xs_chan *xs_chan_new(int kind, int64_t cap) {
    if (cap < 0) xs_panic("channel capacity must be non-negative");
    xs_channels_used = true;
    xs_chan *c = xs_obj_alloc(sizeof *c, xs_chan_free, xs_chan_print);
    c->kind = kind;
    c->cap = (size_t)cap;
    c->buf = cap ? malloc(sizeof(xs_val) * (size_t)cap) : NULL;
    return c;
}
static void xs_chan_enqueue_wait(xs_chan_wait **list, xs_chan_wait *w) {
    w->next = NULL;
    while (*list) list = &(*list)->next;
    *list = w;
}
XS_NOINLINE void xs_chan_need_coro(const char *what) {
    if (!xs_cur_coro) {
        static XS_TLS char buf[160];
        snprintf(buf, sizeof buf, "Channel.%s would block outside a request handler", what);
        xs_panic(buf);
    }
}
/* takes ownership of v */
XS_NOINLINE void xs_chan_send(xs_chan *c, xs_val v) {
    if (c->closed) { if (c->kind == 3) xs_release_(v.p); xs_panic("send on a closed channel"); }
    if (c->receivers) { /* hand it straight to the first waiting receiver */
        xs_chan_wait *w = c->receivers;
        c->receivers = w->next;
        w->val = v;
        w->has_val = true;
        xs_ready_push(w->co);
        return;
    }
    if (c->len < c->cap) { c->buf[(c->head + c->len++) % c->cap] = v; return; }
    xs_chan_need_coro("send");
    /* the wait record is on the heap: a parked task's stack may be saved away (shared-stack tasks) */
    xs_chan_wait *wp = malloc(sizeof *wp);
    *wp = (xs_chan_wait){xs_cur_coro, v, true, NULL};
    xs_chan_enqueue_wait(&c->senders, wp);
    xs_coro_yield(); /* resumed once a receiver took the value (or the channel closed) */
    xs_chan_wait w = *wp;
    free(wp);
    if (w.has_val) { if (c->kind == 3) xs_release_(w.val.p); xs_panic("send on a closed channel"); }
}
/* the result is owned by the caller */
XS_NOINLINE xs_val xs_chan_receive(xs_chan *c) {
    if (c->len > 0) {
        xs_val v = c->buf[c->head];
        c->head = (c->head + 1) % c->cap;
        c->len--;
        if (c->senders) { /* a parked sender can now put its value in the buffer */
            xs_chan_wait *w = c->senders;
            c->senders = w->next;
            c->buf[(c->head + c->len++) % c->cap] = w->val;
            w->has_val = false;
            xs_ready_push(w->co);
        }
        return v;
    }
    if (c->senders) { /* unbuffered handoff */
        xs_chan_wait *w = c->senders;
        c->senders = w->next;
        w->has_val = false;
        xs_ready_push(w->co);
        return w->val;
    }
    if (c->closed) xs_panic("receive on a closed channel");
    xs_chan_need_coro("receive");
    xs_chan_wait *wp = malloc(sizeof *wp);
    *wp = (xs_chan_wait){xs_cur_coro, {0}, false, NULL};
    xs_chan_enqueue_wait(&c->receivers, wp);
    xs_coro_yield();
    xs_chan_wait w = *wp;
    free(wp);
    if (!w.has_val) xs_panic("receive on a closed channel");
    return w.val;
}
static void xs_chan_close(xs_chan *c) {
    if (c->closed) return;
    c->closed = true;
    for (xs_chan_wait *w = c->receivers; w; w = w->next) xs_ready_push(w->co);
    for (xs_chan_wait *w = c->senders; w; w = w->next) xs_ready_push(w->co);
    c->receivers = c->senders = NULL;
}

/* ---- TLS hooks (runtime/xisto_tls.h is appended after this file when XS_WITH_TLS is defined) ---- */
typedef struct xs_tls xs_tls;
typedef struct xs_tls_cred xs_tls_cred_t;
#ifdef XS_WITH_TLS
static xs_tls *xs_tls_client_new(const char *host);
static const char *xs_tls_err(xs_tls *t);
static bool xs_tls_write_all(xs_tls *t, int fd, const char *p, size_t n, int64_t timeout_ms, uint64_t deadline);
static ssize_t xs_tls_read_some(xs_tls *t, int fd, char *out, size_t cap, int64_t timeout_ms, uint64_t deadline);
static ssize_t xs_tls_srv_read(xs_tls *t, int fd, char *out, size_t cap);
static ssize_t xs_tls_srv_write(xs_tls *t, int fd, const char *p, size_t n);
static bool xs_tls_srv_pending(xs_tls *t);
static void xs_tls_srv_close_notify(xs_tls *t, int fd);
static void xs_tls_free(xs_tls *t);
#define XS_TLS_FREE(t) do { if (t) xs_tls_free(t); } while (0)
static int xs_tls_last_error(xs_tls *t);
static xs_tls *xs_tls_server_new(xs_tls_cred_t *cr);
#define XS_TLS_PENDING(c) ((c)->tls && xs_tls_srv_pending((c)->tls))
#else
#define XS_TLS_PENDING(c) false
#define XS_TLS_FREE(t) free(t)
#endif

/* ---- objects visible to the program ---- */

typedef struct xs_Response { xs_hdr hdr; int64_t f_status; bool f_ok; xs_map *f_headers; xs_str *body; xs_str *f_url; } xs_Response;
typedef struct xs_HttpRequest {
    xs_hdr hdr;
    xs_str *f_method, *f_path, *f_body;
    const char *query;     /* points into the request buffer, "" if none */
    size_t query_len;
    const char *headers;   /* raw header block */
    size_t headers_len;
    const char *pnames[8]; size_t pname_len[8];  /* route parameters (/todos/:id) */
    const char *pvals[8]; size_t pval_len[8];
    int nparams;
    char *raw;             /* private copy of the request head: query, headers and parameters point into it */
} xs_HttpRequest;
typedef struct xs_HttpResponse { xs_hdr hdr; int64_t f_status; xs_str *f_body; const char *content_type; xs_stream *stream;
                                 xs_str *ctype_own; xs_str *extra; /* HttpResponse.of's content type; header() lines */ } xs_HttpResponse;

typedef xs_closure *xs_handler;
#define XS_LAT_SAMPLES 2048
typedef struct {
    uint64_t count, errors, total_ns, max_ns;
    uint32_t samples[XS_LAT_SAMPLES]; /* ring of recent latencies in microseconds, for percentiles */
    uint32_t nsamples, next;
} xs_route_metrics;
typedef struct { char method[8]; xs_str *path; xs_handler fn; xs_route_metrics m; } xs_route;
typedef struct xs_Server { xs_hdr hdr; xs_route *routes; int nroutes, cap; void *cred; } xs_Server;

static void xs_Response_free(void *p) { xs_release(((xs_Response *)p)->body); xs_release(((xs_Response *)p)->f_url); if (((xs_Response *)p)->f_headers) xs_release(((xs_Response *)p)->f_headers); xs_obj_free(p); }
static void xs_Response_print(void *p) { fprintf(XS_OUT, "Response { status: %lld, ok: %s }", (long long)((xs_Response *)p)->f_status, ((xs_Response *)p)->f_ok ? "true" : "false"); }
static xs_str *xs_Response_text(xs_Response *r) { return xs_retain(r->body); }
/* res.arrayBuffer(): the body's bytes (a Buffer.from(...) of it is what code does with it) */
static xs_Uint8Array *xs_Response_arrayBuffer(xs_Response *r) {
    xs_Uint8Array *a = xs_u8_new(r->body->len);
    if (r->body->len) memcpy(a->data, r->body->data, (size_t)r->body->len);
    return a;
}

static void xs_HttpRequest_free(void *p) {
    xs_HttpRequest *r = p;
    xs_release(r->f_method); xs_release(r->f_path); xs_release(r->f_body);
    free(r->raw);
    xs_obj_free(p);
}
static void xs_HttpRequest_print(void *p) { fprintf(XS_OUT, "HttpRequest { method: "); xs_print_str_quoted(((xs_HttpRequest *)p)->f_method); fprintf(XS_OUT, ", path: "); xs_print_str_quoted(((xs_HttpRequest *)p)->f_path); fprintf(XS_OUT, " }"); }

static void xs_HttpResponse_free(void *p) {
    xs_HttpResponse *r = p;
    xs_release(r->f_body); xs_release(r->stream); xs_release(r->ctype_own); xs_release(r->extra);
    xs_obj_free(p);
}
static void xs_HttpResponse_print(void *p) { fprintf(XS_OUT, "HttpResponse { status: %lld }", (long long)((xs_HttpResponse *)p)->f_status); }

static xs_HttpResponse *xs_HttpResponse_make(int64_t status, xs_str *body, const char *ctype) {
    xs_HttpResponse *r = xs_obj_alloc(sizeof *r, xs_HttpResponse_free, xs_HttpResponse_print);
    r->f_status = status;
    r->f_body = xs_retain(body);
    r->content_type = ctype;
    return r;
}
static xs_HttpResponse *xs_HttpResponse_text(xs_str *body) { return xs_HttpResponse_make(200, body, "text/plain; charset=utf-8"); }
static xs_HttpResponse *xs_HttpResponse_json(xs_str *body) { return xs_HttpResponse_make(200, body, "application/json"); }
static xs_HttpResponse *xs_HttpResponse_status(int64_t code, xs_str *body) { return xs_HttpResponse_make(code, body, "text/plain; charset=utf-8"); }
static bool xs_has_ctl(const char *p, size_t n);
/* HttpResponse.of(status, contentType, body) */
XS_NOINLINE xs_HttpResponse *xs_HttpResponse_of(int64_t code, xs_str *ctype, xs_str *body) {
    if (xs_has_ctl(ctype->data, (size_t)ctype->len)) xs_panic("HttpResponse.of: invalid content type");
    xs_HttpResponse *r = xs_HttpResponse_make(code, body, ctype->data);
    r->ctype_own = xs_retain(ctype);
    return r;
}
/* res.header(name, value): one more response header; returns the response */
XS_NOINLINE xs_HttpResponse *xs_HttpResponse_header(xs_HttpResponse *r, xs_str *name, xs_str *value) {
    if (xs_has_ctl(name->data, (size_t)name->len) || xs_has_ctl(value->data, (size_t)value->len) || memchr(name->data, ':', (size_t)name->len))
        xs_panic("HttpResponse.header: invalid header");
    size_t old = r->extra ? (size_t)r->extra->len : 0, n = old + (size_t)name->len + (size_t)value->len + 4;
    if (n > 2048) xs_panic("HttpResponse.header: headers too large");
    xs_str *e = xs_str_alloc(n);
    if (old) memcpy(e->data, r->extra->data, old);
    char *q = e->data + old;
    memcpy(q, name->data, (size_t)name->len); q += name->len;
    memcpy(q, ": ", 2); q += 2;
    memcpy(q, value->data, (size_t)value->len); q += value->len;
    memcpy(q, "\r\n", 2);
    xs_release(r->extra);
    r->extra = e;
    return xs_retain(r);
}
/* a response whose body is pulled from a Stream<string>, one chunk at a time, as the client can take it */
static xs_HttpResponse *xs_HttpResponse_stream(xs_stream *st) {
    static xs_str *empty;
    if (!empty) empty = xs_str_immortal("", 0);
    xs_HttpResponse *r = xs_HttpResponse_make(200, empty, "text/plain; charset=utf-8");
    r->stream = xs_retain(st);
    return r;
}

static void xs_Server_free(void *p) {
    xs_Server *s = p;
    for (int i = 0; i < s->nroutes; i++) { xs_release(s->routes[i].path); xs_release(s->routes[i].fn); }
    free(s->routes);
    xs_obj_free(p);
}
static void xs_Server_print(void *p) { fprintf(XS_OUT, "Server { routes: %d }", ((xs_Server *)p)->nroutes); }
static xs_Server *xs_Server_new(void) { return xs_obj_alloc(sizeof(xs_Server), xs_Server_free, xs_Server_print); }

static void xs_Server_route(xs_Server *s, const char *method, xs_str *path, xs_handler fn) {
    if (s->nroutes == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->routes = realloc(s->routes, sizeof(xs_route) * (size_t)s->cap);
    }
    xs_route r;
    strncpy(r.method, method, sizeof r.method - 1);
    r.method[sizeof r.method - 1] = 0;
    r.path = xs_retain(path);
    r.fn = xs_retain(fn);
    s->routes[s->nroutes++] = r;
}
static void xs_Server_get(xs_Server *s, xs_str *path, xs_handler fn) { xs_Server_route(s, "GET", path, fn); }
static void xs_Server_post(xs_Server *s, xs_str *path, xs_handler fn) { xs_Server_route(s, "POST", path, fn); }
static xs_str *xs_url_decode(const char *s, size_t len);
static void xs_Server_put(xs_Server *s, xs_str *path, xs_handler fn) { xs_Server_route(s, "PUT", path, fn); }
static void xs_Server_delete(xs_Server *s, xs_str *path, xs_handler fn) { xs_Server_route(s, "DELETE", path, fn); }
/* server.fallback(fn): what answers a request no route takes (an unknown path, or a known one with another method),
   in place of the plain 404/405: a framework's own not-found answer (Nest: 404 "Cannot GET /x") */
static void xs_Server_fallback(xs_Server *s, xs_handler fn) {
    xs_str *any = xs_str_new("*", 1);
    xs_Server_route(s, "*", any, fn);
    xs_release(any);
}

/* Matches "/todos/:id" against "/todos/42", filling the request's parameters. */
static bool xs_route_match(xs_str *pattern, const char *path, size_t plen, xs_HttpRequest *req) {
    const char *p = pattern->data, *pe = pattern->data + pattern->len;
    const char *q = path, *qe = path + plen;
    int n = 0;
    while (p < pe && q < qe) {
        if (*p == ':') {
            const char *name = ++p;
            while (p < pe && *p != '/') p++;
            const char *val = q;
            while (q < qe && *q != '/') q++;
            if (q == val) return false;
            if (req && n < 8) { req->pnames[n] = name; req->pname_len[n] = (size_t)(p - name); req->pvals[n] = val; req->pval_len[n] = (size_t)(q - val); n++; }
            continue;
        }
        if (*p != *q) return false;
        p++; q++;
    }
    if (req) req->nparams = n;
    return p == pe && q == qe;
}

static xs_str *xs_HttpRequest_param(xs_HttpRequest *r, xs_str *name) {
    for (int i = 0; i < r->nparams; i++)
        if ((int64_t)r->pname_len[i] == name->len && memcmp(r->pnames[i], name->data, (size_t)name->len) == 0)
            return xs_url_decode(r->pvals[i], r->pval_len[i]);
    return xs_str_new("", 0);
}

/* ---- workers ----
   XISTO_WORKERS=N runs N event loops, each on its own thread with its own listening socket (SO_REUSEPORT):
   the kernel spreads connections; threads share nothing but the routes and the program's globals.
   XISTO_LOCK=1 (the default when N > 1) runs handlers one at a time under a mutex, so handlers that
   touch globals stay correct; XISTO_LOCK=0 removes the lock for stateless handlers. */
static int xs_workers = 1;

/* ---- metrics ---- */

static uint64_t xs_total_requests;

static void xs_metrics_begin(void) { xs_req_t0 = xs_now(); }

/* counters are atomic so workers can share them; the latency ring is approximate under contention */
static void xs_metrics_end(xs_route *r, int64_t status) {
    uint64_t dt = xs_now() - xs_req_t0;
    xs_route_metrics *m = &r->m;
    __atomic_fetch_add(&m->count, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&xs_total_requests, 1, __ATOMIC_RELAXED);
    if (status >= 400) __atomic_fetch_add(&m->errors, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&m->total_ns, dt, __ATOMIC_RELAXED);
    if (dt > m->max_ns) m->max_ns = dt;
    uint32_t slot = __atomic_fetch_add(&m->next, 1, __ATOMIC_RELAXED) % XS_LAT_SAMPLES;
    m->samples[slot] = (uint32_t)(dt / 1000);
    if (m->nsamples < XS_LAT_SAMPLES) __atomic_fetch_add(&m->nsamples, 1, __ATOMIC_RELAXED);
}

static int xs_cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static uint32_t xs_percentile(uint32_t *sorted, uint32_t n, double p) {
    if (n == 0) return 0;
    if (n > XS_LAT_SAMPLES) n = XS_LAT_SAMPLES;
    size_t i = (size_t)((double)(n - 1) * p + 0.5);
    return sorted[i];
}

/* GET /__metrics: JSON with process and per-route indicators. Always on; it costs one clock read per request. */
static void xs_metrics_json(xs_Server *s, xs_sb *b) {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "{\"uptime_s\":%.3f,\"requests\":%llu,\"rss_mb\":%.2f,\"allocations\":%lld,\"live_objects\":%lld,\"routes\":[",
             (xs_now() - xs_started) / 1e9, (unsigned long long)xs_total_requests, xs_max_rss_bytes() / 1048576.0, (long long)xs_allocs, (long long)xs_live);
    xs_sb_puts(b, tmp);
    for (int i = 0; i < s->nroutes; i++) {
        xs_route *r = &s->routes[i];
        xs_route_metrics *m = &r->m;
        uint32_t sorted[XS_LAT_SAMPLES];
        uint32_t ns = m->nsamples > XS_LAT_SAMPLES ? XS_LAT_SAMPLES : m->nsamples;
        memcpy(sorted, m->samples, ns * sizeof(uint32_t));
        qsort(sorted, ns, sizeof(uint32_t), xs_cmp_u32);
        snprintf(tmp, sizeof tmp, "%s{\"method\":\"%s\",\"path\":", i ? "," : "", r->method);
        xs_sb_puts(b, tmp);
        xs_sb_str(b, r->path);
        snprintf(tmp, sizeof tmp, ",\"count\":%llu,\"errors\":%llu,\"avg_us\":%.1f,\"p50_us\":%u,\"p99_us\":%u,\"max_us\":%.0f}",
                 (unsigned long long)m->count, (unsigned long long)m->errors, m->count ? m->total_ns / 1000.0 / (double)m->count : 0,
                 xs_percentile(sorted, m->nsamples, 0.5), xs_percentile(sorted, m->nsamples, 0.99), m->max_ns / 1000.0);
        xs_sb_puts(b, tmp);
    }
    xs_sb_puts(b, "]}");
}

/* ---- small helpers ---- */

static bool xs_ieq(const char *a, size_t alen, const char *b) {
    size_t blen = strlen(b);
    if (alen != blen) return false;
    for (size_t i = 0; i < alen; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

/* Finds a header value inside a raw header block (lines ending in \r\n). */
static bool xs_find_header(const char *hdrs, size_t len, const char *name, const char **val, size_t *vlen) {
    const char *p = hdrs, *end = hdrs + len;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        if (!eol) eol = end;
        const char *colon = memchr(p, ':', (size_t)(eol - p));
        if (colon && xs_ieq(p, (size_t)(colon - p), name)) {
            const char *v = colon + 1;
            while (v < eol && (*v == ' ' || *v == '\t')) v++;
            const char *ve = eol;
            while (ve > v && (ve[-1] == '\r' || ve[-1] == ' ')) ve--;
            *val = v;
            *vlen = (size_t)(ve - v);
            return true;
        }
        p = eol + 1;
    }
    return false;
}

static int xs_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static xs_str *xs_url_decode(const char *s, size_t len) {
    xs_str *r = xs_str_alloc((int64_t)len);
    int64_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '%' && i + 2 < len + 0 && xs_hexval(s[i + 1]) >= 0 && xs_hexval(s[i + 2]) >= 0) {
            r->data[n++] = (char)(xs_hexval(s[i + 1]) * 16 + xs_hexval(s[i + 2]));
            i += 2;
        } else if (s[i] == '+') {
            r->data[n++] = ' ';
        } else {
            r->data[n++] = s[i];
        }
    }
    r->len = n;
    r->data[n] = 0;
    return r;
}

/* req.search(): the query string as the URL had it, with its "?" ("" when there is none), as URL.search */
static xs_str *xs_HttpRequest_search(xs_HttpRequest *r) {
    if (!r->query_len) return xs_str_new("", 0);
    xs_str *s = xs_str_alloc((int64_t)r->query_len + 1);
    s->data[0] = '?';
    memcpy(s->data + 1, r->query, r->query_len);
    s->data[r->query_len + 1] = 0;
    return s;
}
static xs_str *xs_HttpRequest_query(xs_HttpRequest *r, xs_str *name) {
    const char *p = r->query, *end = r->query + r->query_len;
    while (p < end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        if (!amp) amp = end;
        const char *eq = memchr(p, '=', (size_t)(amp - p));
        size_t klen = eq ? (size_t)(eq - p) : (size_t)(amp - p);
        if ((int64_t)klen == name->len && memcmp(p, name->data, klen) == 0)
            return eq ? xs_url_decode(eq + 1, (size_t)(amp - eq - 1)) : xs_str_new("", 0);
        p = amp + 1;
    }
    return xs_str_new("", 0);
}

/* req.queryNames(): the names in the query string, in order (repeated names repeat), decoded */
static xs_arr_ref *xs_HttpRequest_queryNames(xs_HttpRequest *r) {
    xs_arr_ref *a = xs_arr_ref_new(8, xs_print_str_quoted_);
    const char *p = r->query, *end = r->query + r->query_len;
    while (p && p < end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        if (!amp) amp = end;
        const char *eq = memchr(p, '=', (size_t)(amp - p));
        size_t klen = eq ? (size_t)(eq - p) : (size_t)(amp - p);
        if (klen) xs_arr_ref_push(a, xs_url_decode(p, klen));
        p = amp + 1;
    }
    return a;
}
/* req.queryAll(name): every value of a name, in order */
static xs_arr_ref *xs_HttpRequest_queryAll(xs_HttpRequest *r, xs_str *name) {
    xs_arr_ref *a = xs_arr_ref_new(4, xs_print_str_quoted_);
    const char *p = r->query, *end = r->query + r->query_len;
    while (p && p < end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        if (!amp) amp = end;
        const char *eq = memchr(p, '=', (size_t)(amp - p));
        size_t klen = eq ? (size_t)(eq - p) : (size_t)(amp - p);
        xs_str *k = xs_url_decode(p, klen);
        if (k->len == name->len && memcmp(k->data, name->data, (size_t)k->len) == 0)
            xs_arr_ref_push(a, eq ? xs_url_decode(eq + 1, (size_t)(amp - eq - 1)) : xs_str_new("", 0));
        xs_release(k);
        p = amp + 1;
    }
    return a;
}
/* req.headerNames(): the names of the headers that came, lower case, in order (Express's req.headers keys) */
static xs_arr_ref *xs_HttpRequest_headerNames(xs_HttpRequest *r) {
    xs_arr_ref *a = xs_arr_ref_new(8, xs_print_str_quoted_);
    const char *p = r->headers, *end = r->headers + r->headers_len;
    while (p && p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        if (!eol) eol = end;
        const char *colon = memchr(p, ':', (size_t)(eol - p));
        if (colon && colon > p) {
            xs_str *k = xs_str_new(p, (size_t)(colon - p));
            for (int64_t i = 0; i < k->len; i++) k->data[i] = (char)tolower((unsigned char)k->data[i]);
            xs_arr_ref_push(a, k);
        }
        p = eol + 1;
    }
    return a;
}
static xs_str *xs_HttpRequest_header(xs_HttpRequest *r, xs_str *name) {
    const char *v;
    size_t vlen;
    if (xs_find_header(r->headers, r->headers_len, name->data, &v, &vlen)) return xs_str_new(v, vlen);
    return xs_str_new("", 0);
}

/* ---- buffers ---- */

typedef struct { char *data; size_t len, cap; } xs_buf;

/* Idle server connections give their 16 KB I/O buffers back to a per-worker pool (xs_buf_park), and the
   next read takes one from it: an idle keep-alive connection holds no buffer, a busy one pays no malloc. */
#define XS_BUFPOOL_SIZE 16384
static XS_TLS char *xs_bufpool[128];
static XS_TLS int xs_bufpool_n;
static void xs_buf_park(xs_buf *b) {
    if (!b->data) return;
    if (b->cap == XS_BUFPOOL_SIZE && xs_bufpool_n < 128) xs_bufpool[xs_bufpool_n++] = b->data;
    else free(b->data);
    b->data = NULL; b->len = b->cap = 0;
}

static void xs_buf_reserve(xs_buf *b, size_t extra) {
    if (b->len + extra <= b->cap) return;
    if (!b->cap && extra <= XS_BUFPOOL_SIZE && xs_bufpool_n > 0) { b->data = xs_bufpool[--xs_bufpool_n]; b->cap = XS_BUFPOOL_SIZE; return; }
    size_t cap = b->cap ? b->cap : 4096;
    while (cap < b->len + extra) cap *= 2;
    b->data = realloc(b->data, cap);
    if (!b->data) xs_panic("out of memory");
    b->cap = cap;
}

static void xs_buf_append(xs_buf *b, const void *p, size_t n) {
    xs_buf_reserve(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void xs_buf_consume(xs_buf *b, size_t n) {
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
}

/* Returns the length of the complete HTTP message at the start of buf, or 0 if incomplete.
   On success fills *hdr_len (through the blank line) and *body_len. Chunked bodies are decoded into *chunked_body. */
static size_t xs_http_message_len(xs_buf *buf, size_t *hdr_len, size_t *body_len, bool *is_chunked, xs_buf *chunked_body) {
    if (buf->len < 4) return 0;
    const char *blank = NULL;
    for (size_t i = 0; i + 3 < buf->len; i++)
        if (buf->data[i] == '\r' && buf->data[i + 1] == '\n' && buf->data[i + 2] == '\r' && buf->data[i + 3] == '\n') { blank = buf->data + i; break; }
    if (!blank) return 0;
    *hdr_len = (size_t)(blank - buf->data) + 4;
    const char *v;
    size_t vlen;
    *is_chunked = false;
    if (xs_find_header(buf->data, *hdr_len, "Transfer-Encoding", &v, &vlen) && xs_ieq(v, vlen, "chunked")) {
        *is_chunked = true;
        chunked_body->len = 0;
        size_t p = *hdr_len;
        for (;;) {
            const char *line_end = memchr(buf->data + p, '\n', buf->len - p);
            if (!line_end) return 0;
            size_t size = strtoul(buf->data + p, NULL, 16);
            p = (size_t)(line_end - buf->data) + 1;
            if (size == 0) {
                /* trailer: expect final CRLF */
                if (p + 2 > buf->len) return 0;
                return p + 2;
            }
            if (p + size + 2 > buf->len) return 0;
            xs_buf_append(chunked_body, buf->data + p, size);
            p += size + 2;
        }
    }
    *body_len = 0;
    if (xs_find_header(buf->data, *hdr_len, "Content-Length", &v, &vlen)) *body_len = strtoul(v, NULL, 10);
    if (*hdr_len + *body_len > buf->len) return 0;
    return *hdr_len + *body_len;
}

/* ---- client: fetch ---- */

/* keep-alive connections: an idle list per thread; a fetch takes one (or connects) and gives it back */
typedef struct xs_conn { char host[256]; int port; int fd; bool https; xs_tls *tls; struct xs_conn *next; } xs_conn;
static XS_TLS xs_conn *xs_idle_conns;
static XS_TLS int xs_idle_count;

static int xs_connect(const char *host, int port) {
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char ports[16];
    snprintf(ports, sizeof ports, "%d", port);
    if (getaddrinfo(host, ports, &hints, &res) != 0) return -1;
    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        fcntl(fd, F_SETFL, O_NONBLOCK);
        int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (r == 0) break;
        if (errno == EINPROGRESS) {
            xs_wait_fd(fd, EVFILT_WRITE);
            int err = 0; socklen_t len = sizeof err;
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err == 0) break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

XS_NOINLINE xs_conn *xs_conn_take(const char *host, int port, bool https) {
    for (xs_conn **pp = &xs_idle_conns; *pp; pp = &(*pp)->next) {
        xs_conn *c = *pp;
        if (c->port == port && c->https == https && strcmp(c->host, host) == 0) {
            *pp = c->next;
            xs_idle_count--;
            return c;
        }
    }
    xs_conn *c = calloc(1, sizeof *c);
    strncpy(c->host, host, sizeof c->host - 1);
    c->port = port;
    c->fd = xs_connect(host, port);
    if (c->fd < 0) { free(c); xs_panic("fetch: connection failed"); }
    c->https = https;
#ifdef XS_WITH_TLS
    if (https) c->tls = xs_tls_client_new(host);
#endif
    return c;
}

static void xs_conn_give(xs_conn *c) {
    if (xs_idle_count >= 512) { close(c->fd); XS_TLS_FREE(c->tls); free(c); return; }
    c->next = xs_idle_conns;
    xs_idle_conns = c;
    xs_idle_count++;
}

static void xs_conn_drop(xs_conn *c) { close(c->fd); XS_TLS_FREE(c->tls); free(c); }

static bool xs_write_all(int fd, const char *p, size_t n) {
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { xs_wait_fd(fd, EVFILT_WRITE); continue; }
            return false;
        }
        p += w;
        n -= (size_t)w;
    }
    return true;
}

static xs_Response *xs_fetch_full(xs_str *url, xs_str *method, xs_str *body, int nh, xs_str **hdrs, int64_t timeout_ms, xs_Uint8Array *abort);
XS_NOINLINE xs_Response *xs_fetch(xs_str *url) { return xs_fetch_full(url, NULL, NULL, 0, NULL, -1, NULL); }
/* fetch(url, { headers: h }) with h a Record<string, string> built before the call: its entries, in insertion order */
XS_NOINLINE xs_Response *xs_fetch_hmap(xs_str *url, xs_str *method, xs_str *body, xs_map *h, int64_t timeout_ms, xs_Uint8Array *abort) {
    size_t n = 0;
    xs_str **hd = (xs_str **)malloc(sizeof(xs_str *) * (h ? h->count * 2 : 0) + sizeof(xs_str *));
    xs_map_entry *e;
    if (h) XS_MAP_EACH(h, e) { hd[n++] = e->skey; hd[n++] = (xs_str *)e->val.p; }
    xs_Response *r = xs_fetch_full(url, method, body, (int)n, hd, timeout_ms, abort);
    free(hd);
    return r;
}

static bool xs_has_ctl(const char *p, size_t n) { /* control bytes other than tab */
    for (size_t i = 0; i < n; i++) { unsigned char ch = (unsigned char)p[i]; if ((ch < 0x20 && ch != '\t') || ch == 0x7f) return true; }
    return false;
}
/* what an aborted fetch rejects with, as Node's (a DOMException): its name and message */
XS_NOINLINE void xs_fetch_fail_named(const char *name, const char *msg) {
    xs_str *m = xs_str_new(msg, strlen(msg));
    xs_Error *e = xs_Error_new(m);
    xs_release(m);
    xs_release(e->f_name);
    e->f_name = xs_str_new(name, strlen(name));
    xs_throw(e);
}
XS_NOINLINE void xs_fetch_timeout(void) { xs_fetch_fail_named("TimeoutError", "The operation was aborted due to timeout"); }
/* the signal's byte: 1 abort() (AbortError), 2 AbortSignal.timeout as a value (TimeoutError) */
XS_NOINLINE void xs_fetch_aborted(int kind) {
    if (kind == 2) xs_fetch_timeout();
    xs_fetch_fail_named("AbortError", "This operation was aborted");
}

XS_NOINLINE xs_Response *xs_fetch_full(xs_str *url, xs_str *method, xs_str *body, int nh, xs_str **hdrs, int64_t timeout_ms, xs_Uint8Array *abort) {
    /* with an AbortSignal the waits are cut in slices of 20 ms, and the signal's byte read between them */
    #define XS_ABORTED() (abort && abort->data[0])
    if (XS_ABORTED()) xs_fetch_aborted(abort->data[0]);
    uint64_t deadline = timeout_ms >= 0 ? xs_now() + (uint64_t)timeout_ms * 1000000ull : 0;
    #define XS_LEFT_MS() (timeout_ms < 0 ? (int64_t)-1 : (int64_t)((deadline > xs_now() ? deadline - xs_now() : 0) / 1000000ull))
    const char *u = url->data;
    bool https = strncmp(u, "https://", 8) == 0;
    if (!https && strncmp(u, "http://", 7) != 0) xs_panic("fetch: the URL must start with http:// or https://");
#ifndef XS_WITH_TLS
    if (https) xs_panic("fetch: https is not available: the program was built with --tls=off");
#endif
    u += https ? 8 : 7;
    const char *path = strchr(u, '/');
    size_t hostport_len = path ? (size_t)(path - u) : strlen(u);
    if (!path) path = "/";
    char host[256];
    int port = https ? 443 : 80;
    if (hostport_len >= sizeof host) xs_panic("fetch: host too long");
    memcpy(host, u, hostport_len);
    host[hostport_len] = 0;
    char *colon = strrchr(host, ':');
    if (colon) {
        port = atoi(colon + 1);
        *colon = 0;
    }

    /* An empty host would connect to localhost and, over https, skip the name check: refuse it.
       CR, LF and other control bytes in the request line or a header would let the caller inject headers
       or a second request (Node's fetch refuses them too). */
    if (!host[0]) xs_panic("fetch: invalid URL (empty host)");
    if (xs_has_ctl(path, strlen(path)) || strchr(path, ' ')) xs_panic("fetch: invalid character in URL");
    if (method && (xs_has_ctl(method->data, (size_t)method->len) || strchr(method->data, ' '))) xs_panic("fetch: invalid method");
    for (int i = 0; i + 1 < nh; i += 2) {
        if (!hdrs[i]->len || xs_has_ctl(hdrs[i]->data, (size_t)hdrs[i]->len) || memchr(hdrs[i]->data, ':', (size_t)hdrs[i]->len))
            xs_panicf("fetch: invalid header name '%s'", hdrs[i]->data);
        if (xs_has_ctl(hdrs[i + 1]->data, (size_t)hdrs[i + 1]->len)) xs_panicf("fetch: invalid value for header '%s'", hdrs[i]->data);
    }

    xs_conn *c = xs_conn_take(host, port, https);

    xs_buf rq = {0};
    char line[1400];
    int ln = snprintf(line, sizeof line, "%s %s HTTP/1.1\r\nHost: %s:%d\r\nUser-Agent: xisto\r\nAccept: */*\r\nConnection: keep-alive\r\n",
                      method ? method->data : "GET", path, host, port);
    if (ln >= (int)sizeof line) xs_panic("fetch: URL too long");
    xs_buf_append(&rq, line, (size_t)ln);
    for (int i = 0; i + 1 < nh; i += 2) {
        xs_buf_append(&rq, hdrs[i]->data, (size_t)hdrs[i]->len);
        xs_buf_append(&rq, ": ", 2);
        xs_buf_append(&rq, hdrs[i + 1]->data, (size_t)hdrs[i + 1]->len);
        xs_buf_append(&rq, "\r\n", 2);
    }
    if (body) {
        ln = snprintf(line, sizeof line, "Content-Length: %lld\r\n", (long long)body->len);
        xs_buf_append(&rq, line, (size_t)ln);
    }
    xs_buf_append(&rq, "\r\n", 2);
    if (body) xs_buf_append(&rq, body->data, (size_t)body->len);
    const char *req = rq.data;
    int n = (int)rq.len;

    for (int attempt = 0; attempt < 2; attempt++) {
        bool wrote;
#ifdef XS_WITH_TLS
        if (c->tls) wrote = xs_tls_write_all(c->tls, c->fd, req, (size_t)n, timeout_ms, deadline);
        else
#endif
        wrote = xs_write_all(c->fd, req, (size_t)n);
        if (!wrote) {
#ifdef XS_WITH_TLS
            if (c->tls && xs_tls_last_error(c->tls) != 0) {
                static XS_TLS char msg[200];
                snprintf(msg, sizeof msg, "fetch failed: %s", xs_tls_err(c->tls));
                xs_conn_drop(c);
                free(rq.data);
                xs_panic(msg);
            }
#endif
            if (timeout_ms >= 0 && xs_now() >= deadline) { free(rq.data); xs_conn_drop(c); xs_fetch_timeout(); }
            xs_conn_drop(c);
            c = xs_conn_take(host, port, https);
            continue;
        }
        xs_buf buf = {0}, chunked = {0};
        size_t hdr_len = 0, body_len = 0, total;
        bool is_chunked;
        for (;;) {
            total = xs_http_message_len(&buf, &hdr_len, &body_len, &is_chunked, &chunked);
            if (total) break;
            xs_buf_reserve(&buf, 16384);
            ssize_t r;
#ifdef XS_WITH_TLS
            if (c->tls) {
                uint64_t dl = deadline;
                int64_t tm = timeout_ms;
                if (abort) { /* a slice */
                    uint64_t sl = xs_now() + 20000000ull;
                    if (tm < 0 || sl < dl) { dl = sl; tm = 20; }
                }
                r = xs_tls_read_some(c->tls, c->fd, buf.data + buf.len, buf.cap - buf.len, tm, dl);
                if (r <= 0 && XS_ABORTED()) { int k = abort->data[0]; free(buf.data); free(chunked.data); free(rq.data); xs_conn_drop(c); xs_fetch_aborted(k); }
                if (r <= 0 && timeout_ms >= 0 && xs_now() >= deadline) { free(buf.data); free(chunked.data); free(rq.data); xs_conn_drop(c); xs_fetch_timeout(); }
                if (r > 0) { buf.len += (size_t)r; continue; }
                if (abort && dl != deadline && xs_now() >= dl) continue; /* (the slice ended: read again) */
                break;
            }
#endif
            r = read(c->fd, buf.data + buf.len, buf.cap - buf.len);
            if (r < 0 && errno == EINTR) continue;
            if (r < 0 && errno == EAGAIN) { /* inside a handler this yields to the event loop */
                int64_t left = XS_LEFT_MS();
                bool sliced = abort && (left < 0 || left > 20);
                bool ready = xs_wait_fd_ms(c->fd, EVFILT_READ, sliced ? 20 : left);
                if (XS_ABORTED()) {
                    int k = abort->data[0];
                    free(buf.data); free(chunked.data); free(rq.data);
                    xs_conn_drop(c);
                    xs_fetch_aborted(k);
                }
                if ((!ready && !sliced) || (timeout_ms >= 0 && xs_now() >= deadline)) {
                    free(buf.data); free(chunked.data); free(rq.data);
                    xs_conn_drop(c); /* a late answer must not be read as the reply to the next request */
                    xs_fetch_timeout();
                }
                continue;
            }
            if (r <= 0) break; /* peer closed: retry once on a fresh connection */
            buf.len += (size_t)r;
        }
        if (!total) {
            free(buf.data);
            free(chunked.data);
            xs_conn_drop(c);
            c = xs_conn_take(host, port, https);
            continue;
        }
        int status = atoi(buf.data + 9);
        xs_Response *resp = xs_obj_alloc(sizeof *resp, xs_Response_free, xs_Response_print);
        resp->f_status = status;
        resp->f_ok = status >= 200 && status < 300;
        resp->f_url = xs_retain(url); /* (redirects are not followed: the URL asked for) */
        resp->body = is_chunked ? xs_str_new(chunked.data ? chunked.data : "", chunked.len) : xs_str_new(buf.data + hdr_len, body_len);
        { /* response.headers: names in lower case (what Headers.get and forEach give), in the order received */
            xs_map *hm = xs_map_new(1, 1, 3, xs_print_str_quoted_, xs_print_str_quoted_);
            const char *p = memchr(buf.data, '\n', hdr_len), *end = buf.data + hdr_len;
            while (p && p < end) {
                p++;
                const char *eol = memchr(p, '\n', (size_t)(end - p));
                if (!eol) eol = end;
                const char *colon = memchr(p, ':', (size_t)(eol - p));
                if (colon && colon > p) {
                    xs_str *k = xs_str_new(p, (size_t)(colon - p));
                    for (int64_t i = 0; i < k->len; i++) k->data[i] = (char)tolower((unsigned char)k->data[i]);
                    const char *hv = colon + 1, *he = eol;
                    while (hv < he && (*hv == ' ' || *hv == '\t')) hv++;
                    while (he > hv && (he[-1] == '\r' || he[-1] == ' ')) he--;
                    hm = xs_map_set_s(hm, k, (xs_val){.p = xs_str_new(hv, (size_t)(he - hv))});
                    xs_release(k);
                }
                p = eol;
            }
            resp->f_headers = hm;
        }
        const char *v;
        size_t vlen;
        if (xs_find_header(buf.data, hdr_len, "Connection", &v, &vlen) && xs_ieq(v, vlen, "close")) xs_conn_drop(c);
        else xs_conn_give(c);
        free(buf.data);
        free(chunked.data);
        free(rq.data);
        return resp;
    }
    free(rq.data);
    xs_conn_drop(c);
    xs_panic("fetch: no response");
    return NULL;
    #undef XS_LEFT_MS
    #undef XS_ABORTED
}

/* ---- server ---- */

struct xs_client { int kind; int fd; xs_buf in, out; bool closing; bool fast_ok; int inflight; bool closed; bool rd_paused; struct xs_client *next_dead; xs_tls *tls; };
#define XS_HAS_PENDING(c) ((c)->out.len > 0 || XS_TLS_PENDING(c))
/* clients are freed after the current batch of events: a later event in the same batch may still name them */
static XS_TLS xs_client *xs_clients_dead;
static void xs_client_free_later(xs_client *c) { c->next_dead = xs_clients_dead; xs_clients_dead = c; }
static void xs_clients_flush_dead(void) {
    while (xs_clients_dead) {
        xs_client *c = xs_clients_dead;
        xs_clients_dead = c->next_dead;
        free(c->in.data); free(c->out.data); XS_TLS_FREE(c->tls); free(c);
    }
}
typedef struct { xs_Server *s; xs_client *c; xs_HttpRequest *req; xs_route *route; bool keep_alive; } xs_request_job;

/* TLS backpressure: while the engine holds records it cannot write, it accepts no input either, so the
   peer's bytes would sit in the socket and a level-triggered read event would fire forever (a client that
   stops reading but keeps sending pinned a core). Read interest is dropped until the output drains. */
static void xs_client_pause_read(int kq, xs_client *c) {
    if (c->rd_paused || c->closed) return;
    struct kevent ev;
    EV_SET(&ev, c->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    kevent(kq, &ev, 1, NULL, 0, NULL);
    c->rd_paused = true;
}
static void xs_client_resume_read(int kq, xs_client *c) {
    if (!c->rd_paused || c->closed) return;
    struct kevent ev;
    EV_SET(&ev, c->fd, EVFILT_READ, EV_ADD, 0, 0, c);
    kevent(kq, &ev, 1, NULL, 0, NULL);
    c->rd_paused = false;
}

static void xs_client_close(int kq, xs_client *c) {
    struct kevent ev[2];
    EV_SET(&ev[0], c->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&ev[1], c->fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(kq, ev, 2, NULL, 0, NULL);
#ifdef XS_WITH_TLS
    if (c->tls) xs_tls_srv_close_notify(c->tls, c->fd);
#endif
    close(c->fd);
    c->fd = -1;
    c->closed = true;
    if (c->inflight > 0) return; /* a coroutine still refers to it; freed when it finishes */
    xs_client_free_later(c);
}

static const char *xs_status_text(int64_t s) {
    switch (s) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default: return "Status";
    }
}

#if !defined(_WIN32)
#include <sys/uio.h>
#endif
static void xs_server_respond_x(xs_client *c, int64_t status, const char *ctype, const char *body, size_t body_len, bool keep_alive, const xs_str *extra);
static void xs_server_respond(xs_client *c, int64_t status, const char *ctype, const char *body, size_t body_len, bool keep_alive) {
    xs_server_respond_x(c, status, ctype, body, body_len, keep_alive, NULL);
}
/* extra: header lines ("Name: value\r\n...") from HttpResponse.header */
static void xs_server_respond_x(xs_client *c, int64_t status, const char *ctype, const char *body, size_t body_len, bool keep_alive, const xs_str *extra) {
    /* the response head, assembled by hand: snprintf showed up in the profile */
    char head[320 + 2048];
    char *h = head;
    memcpy(h, "HTTP/1.1 ", 9); h += 9;
    h += xs_itoa(status, h);
    *h++ = ' ';
    const char *st = xs_status_text(status); size_t stl = strlen(st); memcpy(h, st, stl); h += stl;
    size_t ctl = strlen(ctype); if (ctl > 128) ctl = 128;
    if (ctl) { memcpy(h, "\r\nContent-Type: ", 16); h += 16; memcpy(h, ctype, ctl); h += ctl; } /* "" sends none */
    memcpy(h, "\r\nContent-Length: ", 18); h += 18;
    h += xs_itoa((int64_t)body_len, h);
    if (extra && extra->len) { memcpy(h, "\r\n", 2); h += 2; memcpy(h, extra->data, (size_t)extra->len - 2); h += extra->len - 2; }
    if (keep_alive) { memcpy(h, "\r\nConnection: keep-alive\r\n\r\n", 28); h += 28; }
    else { memcpy(h, "\r\nConnection: close\r\n\r\n", 23); h += 23; }
    int n = (int)(h - head);
    if (c->out.len == 0 && c->fast_ok && !c->tls) {
        /* nothing queued and no pipelined request behind: write straight from the response, no copy */
        struct iovec iov[2] = {{head, (size_t)n}, {(void *)body, body_len}};
        ssize_t w = writev(c->fd, iov, body_len ? 2 : 1);
        if (w < 0) w = 0;
        size_t done = (size_t)w;
        if (done >= (size_t)n + body_len) return;
        if (done < (size_t)n) xs_buf_append(&c->out, head + done, (size_t)n - done);
        else xs_buf_append(&c->out, body + (done - (size_t)n), body_len - (done - (size_t)n));
        return;
    }
    xs_buf_append(&c->out, head, (size_t)n);
    xs_buf_append(&c->out, body, body_len);
}

static bool xs_server_flush(xs_client *c);

/* writes to the client socket from inside the request coroutine, yielding when the socket is full */
static bool xs_client_write_all(xs_client *c, const char *p, size_t n) {
#ifdef XS_WITH_TLS
    if (c->tls) {
        while (n > 0 || xs_tls_srv_pending(c->tls)) {
            if (c->closed) return false;
            ssize_t w = xs_tls_srv_write(c->tls, c->fd, p, n);
            if (w < 0) return false;
            p += w; n -= (size_t)w;
            if (n > 0 || xs_tls_srv_pending(c->tls)) xs_wait_fd(c->fd, EVFILT_WRITE);
        }
        xs_client_resume_read(xs_kq, c);
        return true;
    }
#endif
    while (n > 0) {
        if (c->closed) return false;
        ssize_t w = write(c->fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { xs_wait_fd(c->fd, EVFILT_WRITE); continue; }
            return false;
        }
        p += w; n -= (size_t)w;
    }
    return true;
}

/* chunked transfer: the stream is pulled only as fast as the client reads, so the file behind it is too */
static void xs_server_respond_stream(xs_client *c, xs_HttpResponse *res, bool keep_alive) {
    if (!xs_server_flush(c)) return; /* anything queued before */
    char head[256];
    int n = snprintf(head, sizeof head, "HTTP/1.1 %lld %s\r\nContent-Type: %s\r\nTransfer-Encoding: chunked\r\nConnection: %s\r\n\r\n",
                     (long long)res->f_status, xs_status_text(res->f_status), res->content_type, keep_alive ? "keep-alive" : "close");
    if (!xs_client_write_all(c, head, (size_t)n)) return;
    /* lines are coalesced into 64 KB chunks: one write per chunk instead of three per line */
    xs_buf out = {0};
    xs_buf_reserve(&out, 65536 + 32);
    xs_val v;
    bool ok = true;
    while (ok && xs_stream_pull(res->stream, &v)) {
        xs_str *s = v.p;
        if (out.len + (size_t)s->len + 1 > 65536 && out.len > 0) {
            char sz[24];
            int m = snprintf(sz, sizeof sz, "%zx\r\n", out.len);
            ok = xs_client_write_all(c, sz, (size_t)m) && xs_client_write_all(c, out.data, out.len) && xs_client_write_all(c, "\r\n", 2);
            out.len = 0;
        }
        xs_buf_append(&out, s->data, (size_t)s->len);
        xs_buf_append(&out, "\n", 1);
        xs_release(s);
    }
    if (ok && out.len > 0) {
        char sz[24];
        int m = snprintf(sz, sizeof sz, "%zx\r\n", out.len);
        ok = xs_client_write_all(c, sz, (size_t)m) && xs_client_write_all(c, out.data, out.len) && xs_client_write_all(c, "\r\n", 2);
    }
    free(out.data);
    if (ok) xs_client_write_all(c, "0\r\n\r\n", 5);
}

/* Body of a request coroutine: runs the handler, writes the response, releases the request. */
XS_NOINLINE void xs_request_run(void *arg) {
    xs_request_job *job = arg;
    xs_client *c = job->c;
    xs_route *route = job->route;
    xs_HttpRequest *req = job->req;
    size_t mark = xs_ar_mark();
    jmp_buf rec;
    xs_recover = &rec;
    xs_recover_cln = xs_cln_n;
    if (xs_handler_lock) { pthread_mutex_lock(&xs_handler_mutex); xs_cur_coro->holds_lock = true; }
    xs_metrics_begin();
    if (_setjmp(rec) == 0) {
        xs_HttpResponse *res = ((xs_HttpResponse *(*)(void *, xs_HttpRequest *))route->fn->fn)(route->fn, req);
        xs_recover = NULL;
        xs_ar_drain(mark);
        xs_metrics_end(route, res->f_status);
        if (xs_handler_lock) { xs_cur_coro->holds_lock = false; pthread_mutex_unlock(&xs_handler_mutex); }
        if (!c->closed) {
            if (res->stream) xs_server_respond_stream(c, res, job->keep_alive);
            else xs_server_respond_x(c, res->f_status, res->content_type, res->f_body->data, (size_t)res->f_body->len, job->keep_alive, res->extra);
        }
        xs_release(res);
    } else {
        xs_recover = NULL;
        xs_ar_drain(mark);
        if (xs_handler_lock) { xs_cur_coro->holds_lock = false; pthread_mutex_unlock(&xs_handler_mutex); }
        /* a JSON error is the client's (400, and the message says what is wrong with the body); anything
           else is ours: 500 without details, as in Node, and the message with its stack trace on stderr */
        bool client = xs_recover_client;
        char body[1100];
        size_t n = 0;
        if (client) {
            n = (size_t)snprintf(body, sizeof body, "{\"error\":\"");
            for (const char *m = xs_recover_msg; *m && n < sizeof body - 8; m++) {
                unsigned char ch = (unsigned char)*m;
                if (ch == '"' || ch == '\\') { body[n++] = '\\'; body[n++] = (char)ch; }
                else if (ch < 0x20) n += (size_t)snprintf(body + n, sizeof body - n, "\\u%04x", ch);
                else body[n++] = (char)ch;
            }
            body[n++] = '"'; body[n++] = '}'; body[n] = 0;
        } else {
            n = (size_t)snprintf(body, sizeof body, "{\"error\":\"internal error\"}");
        }
        if (!c->closed) xs_server_respond(c, client ? 400 : 500, "application/json", body, n, job->keep_alive);
        xs_metrics_end(route, client ? 400 : 500);
        if (!client) {
            char tb[4096];
            xs_trace_format(&xs_recover_trace, tb, sizeof tb);
            fprintf(stderr, "xisto: handler failed: %s\n%s", xs_recover_msg, tb);
        }
    }
    xs_release(req);
}

/* After a coroutine yields or finishes: finish bookkeeping for the request it carried. */
static bool xs_server_process(xs_Server *s, xs_client *c);
XS_NOINLINE void xs_coro_after(xs_coro *co) {
    if (!co->done) return; /* parked on a socket; the event loop resumes it */
    if (co->on_done) { co->on_done(co); return; }
    xs_request_job *job = co->arg;
    xs_client *c = job->c;
    c->inflight--;
    __atomic_fetch_sub(&xs_inflight_total, 1, __ATOMIC_RELAXED);
    xs_Server *srv = job->s;
    bool keep_alive = job->keep_alive;
    free(job);
    xs_coro_recycle(co);
    if (c->closed) {
        if (c->inflight == 0) xs_client_free_later(c);
        return;
    }
    if (!keep_alive) c->closing = true;
    /* requests pipelined behind this one are already in the buffer: no read event will announce them */
    if (!c->closing && c->inflight == 0 && c->in.len > 0) {
        if (!xs_server_process(srv, c)) c->closing = true;
        if (c->closed || c->inflight > 0) return;
    }
    if (XS_HAS_PENDING(c) || c->closing) {
        if (!xs_server_flush(c)) { c->closing = true; }
        struct kevent ev;
        if (c->closing && !XS_HAS_PENDING(c)) {
            xs_client_close(xs_kq, c);
        } else if (XS_HAS_PENDING(c)) {
            EV_SET(&ev, c->fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, c);
            kevent(xs_kq, &ev, 1, NULL, 0, NULL);
        }
    }
}

/* Handles every complete request in the input buffer. Returns false if the connection must close. */
XS_NOINLINE bool xs_server_process(xs_Server *s, xs_client *c) {
    if (c->inflight > 0) return true; /* responses go out in order: wait for the request in flight */
    for (;;) {
        size_t hdr_len, body_len;
        bool is_chunked;
        xs_buf chunked = {0};
        size_t total = xs_http_message_len(&c->in, &hdr_len, &body_len, &is_chunked, &chunked);
        if (!total) {
            free(chunked.data);
            return c->in.len < (1 << 20); /* drop clients that send a megabyte without finishing a request */
        }
        c->fast_ok = c->in.len == total; /* no pipelined request behind this one */
        /* request line */
        const char *line = c->in.data;
        const char *sp1 = memchr(line, ' ', hdr_len);
        const char *sp2 = sp1 ? memchr(sp1 + 1, ' ', hdr_len - (size_t)(sp1 + 1 - line)) : NULL;
        const char *eol = memchr(line, '\n', hdr_len);
        if (!sp1 || !sp2 || !eol || sp2 > eol) {
            xs_server_respond(c, 400, "text/plain", "bad request", 11, false);
            free(chunked.data);
            return false;
        }
        size_t mlen = (size_t)(sp1 - line);
        const char *target = sp1 + 1;
        size_t tlen = (size_t)(sp2 - target);
        const char *q = memchr(target, '?', tlen);
        size_t plen = q ? (size_t)(q - target) : tlen;
        const char *headers = eol + 1;
        size_t headers_len = hdr_len - (size_t)(headers - line);
        bool http10 = memcmp(sp2 + 1, "HTTP/1.0", 8) == 0;
        const char *v;
        size_t vlen;
        bool keep_alive = !http10;
        if (xs_find_header(headers, headers_len, "Connection", &v, &vlen)) keep_alive = xs_ieq(v, vlen, "keep-alive") || (!http10 && !xs_ieq(v, vlen, "close"));

        if (mlen == 3 && plen == 10 && memcmp(target, "/__metrics", 10) == 0 && memcmp(line, "GET", 3) == 0) {
            xs_sb b = {0};
            xs_metrics_json(s, &b);
            xs_server_respond(c, 200, "application/json", b.data ? b.data : "{}", b.len, keep_alive);
            free(b.data);
            free(chunked.data);
            xs_buf_consume(&c->in, total);
            if (!keep_alive) return false;
            if (c->in.len == 0) return true;
            continue;
        }
        xs_HttpRequest *req = xs_obj_alloc(sizeof *req, xs_HttpRequest_free, xs_HttpRequest_print);
        xs_route *route = NULL;
        bool path_known = false;
        xs_route *fallback = NULL;
        for (int i = 0; i < s->nroutes; i++) {
            xs_route *r = &s->routes[i];
            if (r->method[0] == '*') { fallback = r; continue; }
            if (xs_route_match(r->path, target, plen, req)) {
                path_known = true;
                if (strlen(r->method) == mlen && memcmp(line, r->method, mlen) == 0) { route = r; break; }
            }
        }
        if (!route && fallback) route = fallback;
        if (!route) {
            if (path_known) xs_server_respond(c, 405, "text/plain", "method not allowed", 18, keep_alive);
            else xs_server_respond(c, 404, "text/plain", "not found", 9, keep_alive);
            xs_release(req);
        } else {
            static xs_str *m_get, *m_post, *m_put, *m_delete, *empty;
            if (!m_get) { m_get = xs_str_immortal("GET", 3); m_post = xs_str_immortal("POST", 4); m_put = xs_str_immortal("PUT", 3); m_delete = xs_str_immortal("DELETE", 6); empty = xs_str_immortal("", 0); }
            req->f_method = mlen == 3 && !memcmp(line, "GET", 3) ? m_get : mlen == 4 && !memcmp(line, "POST", 4) ? m_post
                          : mlen == 3 && !memcmp(line, "PUT", 3) ? m_put : mlen == 6 && !memcmp(line, "DELETE", 6) ? m_delete : xs_str_new(line, mlen);
            req->f_path = xs_str_new(target, plen);
            req->f_body = is_chunked ? xs_str_new(chunked.data ? chunked.data : "", chunked.len) : body_len ? xs_str_new(c->in.data + hdr_len, body_len) : empty;
            req->query = q ? q + 1 : "";
            req->query_len = q ? tlen - plen - 1 : 0;
            req->headers = headers;
            req->headers_len = headers_len;
            /* run the handler in its own coroutine: an `await` inside it yields to the event loop */
            xs_request_job *job = malloc(sizeof *job);
            job->s = s; job->c = c; job->req = req; job->route = route; job->keep_alive = keep_alive;
            c->inflight++;
            __atomic_fetch_add(&xs_inflight_total, 1, __ATOMIC_RELAXED);
            free(chunked.data);
            xs_buf_consume(&c->in, total);
            xs_coro *co = xs_coro_new(xs_request_run, job);
            xs_coro_resume(co);
            xs_coro_after(co);
            return !c->closed;
        }
        free(chunked.data);
        xs_buf_consume(&c->in, total);
        if (!keep_alive) return false;
        if (c->in.len == 0) return true;
    }
}

static bool xs_server_flush(xs_client *c) {
#ifdef XS_WITH_TLS
    if (c->tls) {
        ssize_t w = xs_tls_srv_write(c->tls, c->fd, c->out.data, c->out.len);
        if (w < 0) return false;
        xs_buf_consume(&c->out, (size_t)w);
        return true;
    }
#endif
    while (c->out.len > 0) {
        ssize_t w = write(c->fd, c->out.data, c->out.len);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) return true; /* wait for writable */
            return false;
        }
        xs_buf_consume(&c->out, (size_t)w);
    }
    return true;
}

typedef struct { xs_Server *s; int64_t port; int index; int lfd; } xs_worker_args;

/* ---- graceful shutdown: SIGTERM/SIGINT stop accepting, in-flight requests finish, then the process exits ---- */
static volatile sig_atomic_t xs_shutting_down;
static uint64_t xs_shutdown_t0;
static int xs_sig_pipe[2] = {-1, -1};
static int xs_wake_tag = 3; /* udata kind 3 */
static int xs_listen_tag = 4; /* udata kind 4: the listening socket (matched by tag, not by fd number, which a closed listener frees) */
#ifdef XS_DEBUG
static void xs_dbg_net_wake(void) { if (xs_sig_pipe[1] >= 0) { char b = 0; ssize_t w = write(xs_sig_pipe[1], &b, 1); (void)w; } }
#endif
static void xs_on_signal(int sig) {
    (void)sig;
    xs_shutting_down = 1;
    if (xs_sig_pipe[1] >= 0) { char b = 1; ssize_t w = write(xs_sig_pipe[1], &b, 1); (void)w; }
}
static void xs_server_signals(int kq) {
    static bool installed;
    if (!installed) {
        installed = true;
        if (pipe(xs_sig_pipe) == 0) { fcntl(xs_sig_pipe[0], F_SETFL, O_NONBLOCK); fcntl(xs_sig_pipe[1], F_SETFL, O_NONBLOCK); }
        struct sigaction sa = {0};
        sa.sa_handler = xs_on_signal;
        sigaction(SIGTERM, &sa, NULL);
#ifdef XS_DEBUG
        if (xs_dbg.fd < 0) sigaction(SIGINT, &sa, NULL); /* under the debugger Ctrl-C is a pause, not a shutdown */
        xs_dbg_waker = xs_dbg_net_wake;
#else
        sigaction(SIGINT, &sa, NULL);
#endif
    }
    struct kevent ev;
    EV_SET(&ev, xs_sig_pipe[0], EVFILT_READ, EV_ADD, 0, 0, &xs_wake_tag);
    kevent(kq, &ev, 1, NULL, 0, NULL);
}
static void xs_server_wake(xs_Server *s, int lfd) {
    (void)s;
    char b[16];
    while (read(xs_sig_pipe[0], b, sizeof b) > 0) {}
#ifdef XS_DEBUG
    xs_dbg_idle();
#endif
    if (xs_shutting_down) { /* stop accepting new connections; the loop exits once nothing is in flight */
        if (!xs_shutdown_t0) xs_shutdown_t0 = xs_now();
        struct kevent ev;
        EV_SET(&ev, lfd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(xs_kq, &ev, 1, NULL, 0, NULL);
        static int closed;
        if (__atomic_exchange_n(&closed, 1, __ATOMIC_SEQ_CST) == 0) { /* refuse new connections, once for all workers */
            close(lfd);
            fprintf(stderr, "xisto: shutting down, %lld request(s) in flight\n", (long long)xs_inflight_total);
        }
    }
}

XS_NOINLINE int xs_listen_socket(int64_t port) {
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) xs_panic("listen: socket failed");
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0) xs_panic("listen: bind failed (port in use?)");
    if (listen(lfd, 4096) < 0) xs_panic("listen: listen failed");
    fcntl(lfd, F_SETFL, O_NONBLOCK);
    return lfd;
}

static void *xs_server_worker(void *arg);

static void xs_Server_listen(xs_Server *s, int64_t port);
#ifdef XS_WITH_TLS
static void *xs_tls_cred_load_any(xs_str *cert, xs_str *key);
XS_NOINLINE void xs_Server_listen_tls(xs_Server *s, int64_t port, xs_str *cert, xs_str *key) {
    s->cred = xs_tls_cred_load_any(cert, key);
    xs_Server_listen(s, port);
}
#endif
/* The first DNS lookup of a process loads the resolver's libraries (macOS: libnetwork, whose initializers run Swift
   and Foundation; Linux: the NSS modules) on the stack of whoever makes it. A request runs on a 64 KB task stack,
   and a NestJS app's first query from a request (connect -> getaddrinfo of the database's name) overflowed it
   (SIGBUS). Loaded here once, on the thread's own stack, before any request, and without a lookup on the network
   (a name in /etc/hosts or a number does not load libnetwork: it is opened directly, 1.5 ms). */
static void xs_resolver_warm(void) {
    static XS_TLS bool warm;
    if (warm) return;
    warm = true;
#if defined(__APPLE__)
    (void)dlopen("/usr/lib/libnetwork.dylib", RTLD_LAZY | RTLD_LOCAL);
#else
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("localhost", NULL, &hints, &res) == 0 && res) freeaddrinfo(res);
#endif
}
XS_NOINLINE void xs_Server_listen(xs_Server *s, int64_t port) {
    xs_resolver_warm();
    signal(SIGPIPE, SIG_IGN);
    const char *w = getenv("XISTO_WORKERS");
    if (w && atoi(w) > 1) xs_workers = atoi(w) > 64 ? 64 : atoi(w);
#ifndef XS_THREADS
    if (xs_workers > 1) { /* built without per-thread state (xisto run --dev): one event loop */
        fprintf(stderr, "xisto: XISTO_WORKERS ignored: this build runs one event loop (xisto run --dev; build the program for workers)\n");
        xs_workers = 1;
    }
#endif
    const char *l = getenv("XISTO_LOCK");
    xs_handler_lock = xs_workers > 1 && (!l || atoi(l) != 0);
    /* one listening socket shared by every worker: each worker watches it in its own kqueue and
       accepts what it can (on macOS SO_REUSEPORT would not spread connections between sockets) */
    if (xs_workers > 1 && xs_channels_used) xs_panic("channels connect handlers of one event loop; run with one worker (XISTO_WORKERS=1)");
    int lfd = xs_listen_socket(port);
    if (xs_workers > 1) {
        fprintf(stderr, "xisto: %d workers, handlers %s\n", xs_workers, xs_handler_lock ? "serialized by a lock" : "running in parallel (handlers must not touch shared state)");
        pthread_t *threads = malloc(sizeof(pthread_t) * (size_t)xs_workers);
        for (int i = 0; i < xs_workers; i++) {
            xs_worker_args *a = malloc(sizeof *a);
            a->s = s; a->port = port; a->index = i; a->lfd = lfd;
            pthread_create(&threads[i], NULL, xs_server_worker, a);
        }
        for (int i = 0; i < xs_workers; i++) pthread_join(threads[i], NULL);
        return;
    }
    xs_worker_args a = {s, port, 0, lfd};
    xs_server_worker(&a);
}

XS_NOINLINE void *xs_server_worker(void *arg) {
    xs_worker_args *wa = arg;
    xs_Server *s = wa->s;
    int64_t port = wa->port;
    int lfd = wa->lfd;
    int one = 1;

    int kq = kqueue();
    xs_kq = kq;
    struct kevent ev;
    EV_SET(&ev, lfd, EVFILT_READ, EV_ADD, 0, 0, &xs_listen_tag);
    kevent(kq, &ev, 1, NULL, 0, NULL);
    if (wa->index == 0) fprintf(stderr, "xisto: listening on %s://127.0.0.1:%lld\n", s->cred ? "https" : "http", (long long)port);
    fflush(stdout);

    xs_server_signals(kq);
    struct kevent events[256];
    for (;;) {
        xs_coro_flush_dead();
        if (xs_shutting_down && xs_inflight_total == 0) {
            if (wa->index == 0) fprintf(stderr, "xisto: shutdown complete\n");
            fflush(stdout);
            _exit(0);
        }
        while (xs_ready_head) { /* coroutines woken by a channel */
            xs_coro *co = xs_ready_head;
            xs_ready_head = co->next;
            if (!xs_ready_head) xs_ready_tail = NULL;
            xs_coro_resume(co);
            xs_coro_after(co);
        }
        xs_coro_flush_dead();
        xs_clients_flush_dead();
        struct timespec zero = {0, 0}, tick = {0, 200000000}, tt;
        bool has_t = xs_tmr_timeout(&tt);
        if (xs_shutting_down && (!has_t || tt.tv_sec > 0 || tt.tv_nsec > tick.tv_nsec)) { tt = tick; has_t = true; }
        int n = kevent(kq, NULL, 0, events, 256, xs_ready_head ? &zero : has_t ? &tt : NULL);
        xs_batch++;
        if (xs_shutting_down && xs_shutdown_t0 && xs_now() - xs_shutdown_t0 > 10000000000ull) { /* handlers parked forever: give up after 10 s */
            if (wa->index == 0) fprintf(stderr, "xisto: shutdown timed out with %lld request(s) in flight\n", (long long)xs_inflight_total);
            fflush(stdout);
            _exit(0);
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            xs_panic("kevent failed");
        }
        xs_tmr_fire(xs_coro_after); /* due timers: sleeps and wait timeouts */
        for (int i = 0; i < n; i++) {
            if (events[i].udata == &xs_listen_tag) {
                if (xs_shutting_down) continue;
                for (;;) {
                    int fd = accept(lfd, NULL, NULL);
                    if (fd < 0) break;
                    fcntl(fd, F_SETFL, O_NONBLOCK);
                    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                    xs_client *c = calloc(1, sizeof *c);
                    c->fd = fd;
#ifdef XS_WITH_TLS
                    if (s->cred) c->tls = xs_tls_server_new(s->cred);
#endif
                    EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, c);
                    kevent(kq, &ev, 1, NULL, 0, NULL);
                }
                continue;
            }
            if (events[i].udata && *(int *)events[i].udata == 2) { /* a parked coroutine: its socket or its timer */
                xs_waiter *w = events[i].udata;
                if (!w->armed || w->armed_batch == xs_batch) continue; /* lost the race, stale, or re-armed during this batch */
                w->armed = false;
                w->fired = true;
                xs_coro *co = w->co;
                xs_coro_resume(co);
                xs_coro_after(co);
                continue;
            }
            if (events[i].udata && *(int *)events[i].udata == 3) { xs_server_wake(s, lfd); continue; } /* shutdown signal */
            xs_client *c = events[i].udata;
            if (c->closed) continue;
            if (events[i].filter == EVFILT_READ) {
                bool alive = true;
                for (;;) {
                    xs_buf_reserve(&c->in, 16384);
#ifdef XS_WITH_TLS
                    if (c->tls) {
                        ssize_t r = xs_tls_srv_read(c->tls, c->fd, c->in.data + c->in.len, c->in.cap - c->in.len);
                        if (r == 0) { if (xs_tls_srv_pending(c->tls)) xs_client_pause_read(kq, c); break; }
                        if (r < 0) { alive = false; break; }
                        c->in.len += (size_t)r;
                        continue;
                    }
#endif
                    ssize_t r = read(c->fd, c->in.data + c->in.len, c->in.cap - c->in.len);
                    if (r < 0 && errno == EINTR) continue;
                    if (r < 0 && errno == EAGAIN) break;
                    if (r <= 0) { alive = false; break; }
                    c->in.len += (size_t)r;
                }
                if (!alive && c->inflight > 0) { xs_client_close(kq, c); continue; } /* the peer left while its handler is parked */
                if (alive) alive = xs_server_process(s, c);
                if (c->closed) continue;
                if (c->inflight > 0) continue; /* the coroutine finishes the response */
                if (!xs_server_flush(c)) alive = false;
                if (!alive) {
                    if (XS_HAS_PENDING(c)) { /* let the response drain before closing */
                        c->closing = true;
                        EV_SET(&ev, c->fd, EVFILT_WRITE, EV_ADD, 0, 0, c);
                        kevent(kq, &ev, 1, NULL, 0, NULL);
                    } else {
                        xs_client_close(kq, c);
                    }
                } else if (XS_HAS_PENDING(c)) {
                    EV_SET(&ev, c->fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, c);
                    kevent(kq, &ev, 1, NULL, 0, NULL);
                } else if (c->in.len == 0) { /* idle: nothing buffered either way */
                    xs_buf_park(&c->in); xs_buf_park(&c->out);
                }
            } else if (events[i].filter == EVFILT_WRITE) {
                if (!xs_server_flush(c) || (c->closing && !XS_HAS_PENDING(c))) xs_client_close(kq, c);
                else if (XS_HAS_PENDING(c)) {
                    EV_SET(&ev, c->fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, c);
                    kevent(kq, &ev, 1, NULL, 0, NULL);
                }
                if (!c->closed && !XS_TLS_PENDING(c)) xs_client_resume_read(kq, c);
                if (!c->closed && c->inflight == 0 && c->in.len == 0 && !XS_HAS_PENDING(c)) { xs_buf_park(&c->in); xs_buf_park(&c->out); }
            }
        }
    }
    return NULL;
}


/* ---- generators: function* f(): Generator<T> runs its body on its own stack and `yield` switches back to whoever
   asked for the next value (a for...of, a stream stage), from any stack. The value travels in xs_val (owned).
   A generator dropped before its end is resumed once with `cancel` set: its yield returns true and the compiled
   code leaves the body the way a return does, releasing its locals. An exception inside the body is caught at
   its entry and thrown again where the next value was asked for. ---- */
#define XS_GEN_STACK (256 * 1024)
typedef struct xs_gen {
    void *sp, *caller_sp;
    char *stack;
    void (*body)(void *);
    void *args;
    void (*args_free)(void *);
    xs_val val;
    bool started, done, cancel;
    void *error;
    /* the body's runtime state, swapped with the caller's */
    void **ar_pool; size_t ar_len, ar_cap;
    jmp_buf **try_stack; int try_depth; void *thrown;
    void ***cln; size_t cln_n, cln_cap;
    struct xs_gen *outer;
} xs_gen;
static XS_TLS xs_gen *xs_cur_gen;
static char *xs_gen_shlow(char *lo, char *base, char *top) {
    for (xs_gen *g = xs_cur_gen; g; g = g->outer)
        if ((char *)g->caller_sp >= base && (char *)g->caller_sp < top && (char *)g->caller_sp < lo) lo = g->caller_sp;
    return lo;
}

static void xs_gen_entry(void *p) {
    xs_gen *g = p;
    jmp_buf jb;
    xs_try_push(&jb);
    if (_setjmp(jb) == 0) {
        g->body(g->args);
        xs_try_pop();
    } else {
        g->error = xs_caught();
    }
    xs_ar_drain(0); /* the body's own pool: nobody else drains it */
    g->done = true;
    xs_ctx_swap(&g->sp, g->caller_sp); /* never resumed again */
}

/* runs the body until its next yield or its end */
XS_NOINLINE void xs_gen_run(xs_gen *g) {
    void **ap = xs_ar_pool; size_t al = xs_ar_len, ac = xs_ar_cap;
    jmp_buf **ts = xs_try_stack; int td = xs_try_depth; void *th = xs_thrown;
    void ***cs = xs_cln; size_t cn = xs_cln_n, cc = xs_cln_cap;
    if (!g->started) {
        g->started = true;
        g->stack = mmap(NULL, XS_GEN_STACK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (g->stack == MAP_FAILED) xs_panic("cannot allocate a generator stack");
        mprotect(g->stack, XS_PAGE, PROT_NONE); /* guard page */
        uint64_t *top = (uint64_t *)(g->stack + XS_GEN_STACK);
        uint64_t *f = top - XS_FRAME_WORDS;
        memset(f, 0, XS_FRAME_WORDS * sizeof(uint64_t));
        XS_FRAME_INIT(f, xs_gen_entry, g);
        g->sp = f;
        g->ar_cap = 64;
        g->ar_pool = malloc(g->ar_cap * sizeof(void *));
        g->try_stack = NULL;
    }
    xs_ar_pool = g->ar_pool; xs_ar_len = g->ar_len; xs_ar_cap = g->ar_cap;
    xs_try_stack = g->try_stack; xs_try_depth = g->try_depth; xs_thrown = g->thrown;
    xs_cln = g->cln; xs_cln_n = g->cln_n; xs_cln_cap = g->cln_cap;
    g->outer = xs_cur_gen;
    xs_cur_gen = g;
    xs_ctx_swap(&g->caller_sp, g->sp);
    xs_cur_gen = g->outer;
    g->ar_pool = xs_ar_pool; g->ar_len = xs_ar_len; g->ar_cap = xs_ar_cap;
    g->try_stack = xs_try_stack; g->try_depth = xs_try_depth; g->thrown = xs_thrown;
    g->cln = xs_cln; g->cln_n = xs_cln_n; g->cln_cap = xs_cln_cap;
    xs_ar_pool = ap; xs_ar_len = al; xs_ar_cap = ac;
    xs_try_stack = ts; xs_try_depth = td; xs_thrown = th;
    xs_cln = cs; xs_cln_n = cn; xs_cln_cap = cc;
}
/* in the body: hands v (owned) to the consumer; true when the consumer is gone (leave the body) */
XS_NOINLINE bool xs_gen_yield(xs_val v) {
    xs_gen *g = xs_cur_gen;
    g->val = v;
    xs_ctx_swap(&g->sp, g->caller_sp);
    return g->cancel;
}
XS_NOINLINE bool xs_pull_gen(xs_stream *st, xs_val *out) {
    xs_gen *g = st->ext;
    if (g->done) return false;
    xs_gen_run(g);
    if (g->error) { void *e = g->error; g->error = NULL; xs_throw(e); }
    if (g->done) return false;
    *out = g->val;
    return true;
}
XS_NOINLINE void xs_gen_free(void *p) {
    xs_gen *g = p;
    if (g->started && !g->done) { g->cancel = true; xs_gen_run(g); xs_release(g->error); } /* leaves the body, releasing its locals */
    if (g->stack) munmap(g->stack, XS_GEN_STACK);
    free(g->ar_pool);
    free(g->cln);
    if (g->try_stack) free(g->try_stack - 1); /* allocated with its capacity before it */
    if (g->args_free) g->args_free(g->args);
    free(g);
}
XS_NOINLINE xs_stream *xs_gen_stream(void (*body)(void *), void *args, void (*args_free)(void *), int elem_kind, void (*print_elem)(void *)) {
    xs_gen *g = calloc(1, sizeof *g);
    if (!g) xs_panic("out of memory");
    g->body = body;
    g->args = args;
    g->args_free = args_free;
    xs_stream *st = xs_stream_new(xs_pull_gen, NULL, NULL, elem_kind);
    st->print_elem = print_elem;
    st->ext = g;
    st->ext_free = xs_gen_free;
    return st;
}

/* ---- worker threads (xisto:worker_threads) ----
   new Worker(__filename, { workerData }) runs this same program on a thread of its own (xs_wt_body, the
   program's body that main runs too), with its own heap and its own copy of the globals (XS_TLS), and
   isMainThread false. Messages are text: a queue per direction, guarded by a mutex, with a pipe that wakes the
   receiver's event loop. A message is text, a Uint8Array (its bytes, copied), or shared memory: a
   SharedArrayBuffer, or a Float64Array on one, goes as its block (xs_shblk), whose count the message holds, and
   arrives as a view on the same memory. On the receiving side a coroutine per listener waits on the pipe and calls the
   listener with each message; it keeps the loop (and so the process, at exit) alive until the sender is done,
   as Node waits for its workers. */
#ifdef XS_THREADS
enum { XS_WM_TEXT, XS_WM_BYTES, XS_WM_SAB, XS_WM_F64 }; /* what a message is, and what a listener takes */
typedef struct xs_wmsg { struct xs_wmsg *next; size_t n; uint8_t kind; bool shared; char *p; xs_shblk *blk; int64_t off; char s[]; } xs_wmsg;
/* XS_WM_BYTES: a Uint8Array, its bytes in p (malloc'd: the receiver's Uint8Array takes them over, no second copy);
   XS_WM_TEXT: text in s; XS_WM_SAB, XS_WM_F64: the block (a reference of the message's own), and for a
   Float64Array its first element (off) and its length (n); shared: a view on a SharedArrayBuffer (else a copy) */
typedef struct xs_wq {
    pthread_mutex_t mu;
    xs_wmsg *head, *tail;
    int fd[2];       /* a byte per message (or close) wakes the reader */
    bool closed;     /* the sender is done: nothing more will come */
    int refs;        /* the sender and the receiver */
} xs_wq;
XS_NOINLINE xs_wq *xs_wq_new(void) {
    xs_wq *q = calloc(1, sizeof *q);
    if (!q || pipe(q->fd) != 0) xs_panic("worker_threads: cannot make a message queue");
    fcntl(q->fd[0], F_SETFL, fcntl(q->fd[0], F_GETFL) | O_NONBLOCK);
    pthread_mutex_init(&q->mu, NULL);
    q->refs = 2;
    return q;
}
static void xs_wq_wake(xs_wq *q) {
    char c = 1;
    while (write(q->fd[1], &c, 1) < 0 && errno == EINTR) {}
}
static void xs_wq_push(xs_wq *q, xs_wmsg *m) {
    pthread_mutex_lock(&q->mu);
    bool closed = q->closed;
    if (!closed) { if (q->tail) q->tail->next = m; else q->head = m; q->tail = m; }
    pthread_mutex_unlock(&q->mu);
    if (closed) { free(m->p); xs_shblk_unref(m->blk); free(m); return; } /* (the receiver is gone, as a message to an ended worker in Node) */
    xs_wq_wake(q);
}
static void xs_wq_put(xs_wq *q, const char *s, size_t n, bool bytes) {
    xs_wmsg *m = malloc(sizeof *m + (bytes ? 1 : n + 1));
    if (!m) xs_panic("out of memory");
    m->next = NULL;
    m->n = n;
    m->kind = bytes ? XS_WM_BYTES : XS_WM_TEXT;
    m->p = NULL;
    m->blk = NULL;
    if (bytes) {
        m->p = malloc(n ? n : 1);
        if (!m->p) xs_panic("out of memory");
        memcpy(m->p, s, n);
    } else {
        memcpy(m->s, s, n);
        m->s[n] = 0;
    }
    xs_wq_push(q, m);
}
/* shared memory: blk (a reference the message takes over), and for a Float64Array where it starts and its length */
static void xs_wq_put_blk(xs_wq *q, uint8_t kind, xs_shblk *blk, int64_t off, int64_t n, bool shared) {
    xs_wmsg *m = malloc(sizeof *m + 1);
    if (!m) xs_panic("out of memory");
    m->next = NULL;
    m->n = (size_t)n;
    m->kind = kind;
    m->shared = shared;
    m->p = NULL;
    m->blk = blk;
    m->off = off;
    xs_wq_push(q, m);
}
static void xs_wq_put_sab(xs_wq *q, xs_SharedArrayBuffer *s) {
    xs_shblk_ref(s->blk);
    xs_wq_put_blk(q, XS_WM_SAB, s->blk, 0, s->f_byteLength, true);
}
/* a Uint8Array on a SharedArrayBuffer goes as its block (the receiver sees the same bytes, as in Node); bytes of
   its own, as a copy */
static void xs_wq_put_u8(xs_wq *q, xs_Uint8Array *b) {
    xs_shblk *k = xs_u8_shared(b);
    if (k) {
        xs_shblk_ref(k);
        xs_wq_put_blk(q, XS_WM_BYTES, k, (int64_t)((char *)b->data - xs_shblk_data(k)), b->f_length, true);
        return;
    }
    xs_wq_put(q, (const char *)b->data, (size_t)b->f_length, true);
}
static void xs_wq_put_f64(xs_wq *q, xs_Float64Array *a) {
    if (a->shared) { /* the same memory, as Node shares a typed array on a SharedArrayBuffer */
        xs_shblk_ref(a->blk);
        xs_wq_put_blk(q, XS_WM_F64, a->blk, a->data - (double *)xs_shblk_data(a->blk), a->f_length, true);
        return;
    }
    xs_shblk *b = xs_shblk_new(a->f_length * 8, false); /* memory of its own: cloned, as Node does */
    if (a->f_length) memcpy(xs_shblk_data(b), a->data, (size_t)a->f_length * sizeof(double));
    xs_wq_put_blk(q, XS_WM_F64, b, 0, a->f_length, false);
}
static void xs_wq_close(xs_wq *q) {
    pthread_mutex_lock(&q->mu);
    q->closed = true;
    pthread_mutex_unlock(&q->mu);
    xs_wq_wake(q);
}
static xs_wmsg *xs_wq_take(xs_wq *q, bool *closed) {
    pthread_mutex_lock(&q->mu);
    xs_wmsg *m = q->head;
    q->head = q->tail = NULL;
    *closed = q->closed;
    pthread_mutex_unlock(&q->mu);
    return m;
}
static void xs_wq_unref(xs_wq *q) {
    if (__atomic_sub_fetch(&q->refs, 1, __ATOMIC_ACQ_REL) != 0) return;
    for (xs_wmsg *m = q->head, *n; m; m = n) { n = m->next; free(m->p); xs_shblk_unref(m->blk); free(m); }
    close(q->fd[0]);
    close(q->fd[1]);
    pthread_mutex_destroy(&q->mu);
    free(q);
}

static void (*xs_wt_body)(void);         /* the program's body (emit.c, xs_prog_body) */
static XS_TLS bool xs_wt_worker;          /* this thread is a worker */
static XS_TLS const char *xs_wt_data_s;   /* its workerData */
static XS_TLS size_t xs_wt_data_n;
static XS_TLS xs_wq *xs_wt_in, *xs_wt_out; /* a worker's queues: from its parent, to its parent */
static XS_TLS int64_t xs_wt_tid;
static int64_t xs_wt_next_id = 1;

typedef struct xs_MessagePort { xs_hdr hdr; xs_wq *in, *out; xs_closure *on_msg; bool listening; uint8_t on_kind; } xs_MessagePort;
typedef struct xs_Worker { xs_hdr hdr; xs_wq *to_main, *to_worker; xs_closure *on_msg; bool listening; uint8_t on_kind; } xs_Worker;
static void xs_MessagePort_free(void *p) { xs_MessagePort *m = p; xs_release(m->on_msg); xs_obj_free(p); }
static void xs_MessagePort_print(void *p) { (void)p; fputs("MessagePort {}", XS_OUT); }
static void xs_Worker_free(void *p) {
    xs_Worker *w = p;
    xs_release(w->on_msg);
    xs_wq_close(w->to_worker); /* (the worker's parentPort listener, if any, ends) */
    xs_wq_unref(w->to_worker);
    xs_wq_unref(w->to_main);
    xs_obj_free(p);
}
static void xs_Worker_print(void *p) { (void)p; fputs("Worker {}", XS_OUT); }

static bool xs_wt_is_main(void) { return !xs_wt_worker; }
static xs_str *xs_wt_data(void) { return xs_wt_worker ? xs_str_new(xs_wt_data_s, xs_wt_data_n) : xs_str_new("", 0); }
static int64_t xs_wt_id(void) { return xs_wt_tid; }
static xs_MessagePort *xs_wt_port(void) { /* parentPort: on the main thread, a port that refuses to be used */
    xs_MessagePort *m = xs_obj_alloc(sizeof(xs_MessagePort), xs_MessagePort_free, xs_MessagePort_print);
    m->in = xs_wt_in;
    m->out = xs_wt_out;
    return m;
}
/* node:os: the machine's name, the system and the user's folders, as Node gives them */
XS_NOINLINE xs_str *xs_os_host_name(void) {
    char b[256];
    if (gethostname(b, sizeof b) != 0) b[0] = 0;
    b[sizeof b - 1] = 0;
    return xs_str_new(b, strlen(b));
}
XS_NOINLINE xs_str *xs_os_platform_name(void) {
#if defined(__APPLE__)
    return xs_str_new("darwin", 6);
#elif defined(_WIN32)
    return xs_str_new("win32", 5);
#else
    return xs_str_new("linux", 5);
#endif
}
XS_NOINLINE xs_str *xs_os_arch_name(void) {
#if defined(__aarch64__) || defined(__arm64__)
    return xs_str_new("arm64", 5);
#else
    return xs_str_new("x64", 3);
#endif
}
XS_NOINLINE xs_str *xs_os_tmp_dir(void) { /* $TMPDIR without its trailing slash (Node), else /tmp */
    const char *t = getenv("TMPDIR");
    if (!t || !*t) return xs_str_new("/tmp", 4);
    size_t n = strlen(t);
    while (n > 1 && t[n - 1] == '/') n--;
    return xs_str_new(t, n);
}
XS_NOINLINE xs_str *xs_os_home_dir(void) {
    const char *h = getenv("HOME");
    return h ? xs_str_new(h, strlen(h)) : xs_str_new("", 0);
}
static int64_t xs_os_parallelism(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? n : 1;
}

/* the listener's coroutine: each message, in order, to the closure; ends when the sender is done */
typedef struct { xs_wq *q; xs_closure **cb; uint8_t *kind; xs_hdr *owner; } xs_wlisten; /* kind: what the listener takes (XS_WM_...) */
XS_NOINLINE void xs_wt_listen(void *p) {
    xs_wlisten *L = p;
    static const char *const kinds[] = {"string", "Uint8Array", "SharedArrayBuffer", "Float64Array"};
    for (;;) {
        bool closed;
        for (xs_wmsg *m = xs_wq_take(L->q, &closed), *n; m; m = n) {
            n = m->next;
            xs_closure *cl = *L->cb;
            if (cl) {
                if (m->kind != *L->kind) {
                    char e[200];
                    snprintf(e, sizeof e, "worker_threads: a %s message reached a listener of %s: declare (m: %s)", kinds[m->kind], kinds[*L->kind], kinds[m->kind]);
                    xs_panic(e);
                }
                size_t mark = xs_ar_mark();
                void *v;
                if (m->kind == XS_WM_SAB) { v = xs_sab_wrap(m->blk); m->blk = NULL; } /* (the message's reference becomes the view's) */
                else if (m->kind == XS_WM_F64) { v = xs_f64_wrap(m->blk, m->off, (int64_t)m->n, m->shared); m->blk = NULL; }
                else if (m->kind == XS_WM_BYTES) { /* the message's bytes become the array's; a block: a view on it */
                    xs_Uint8Array *b = xs_obj_alloc(sizeof *b, xs_u8_free, xs_u8_print);
                    b->f_length = (int64_t)m->n;
                    if (m->blk) { b->data = (uint8_t *)xs_shblk_data(m->blk) + m->off; b->owner = xs_sab_wrap(m->blk); m->blk = NULL; }
                    else { b->data = (uint8_t *)m->p; b->owner = NULL; m->p = NULL; }
                    v = b;
                }
                else v = xs_str_new(m->s, m->n);
                ((void (*)(void *, void *))cl->fn)(cl, v);
                xs_release(v);
                xs_ar_drain(mark);
            }
            free(m->p);
            xs_shblk_unref(m->blk);
            free(m);
        }
        if (closed) return;
        xs_wait_fd(L->q->fd[0], EVFILT_READ);
        char buf[256];
        while (read(L->q->fd[0], buf, sizeof buf) > 0) {}
    }
}
static void xs_wt_listen_done(xs_coro *co) {
    xs_wlisten *L = co->arg;
    xs_tasks_live--;
    xs_coro_recycle(co);
    /* nothing more can arrive: the listener goes, and with it the cycle it usually makes (a closure that
       captures its worker, to postMessage the next task, held by that worker) */
    xs_closure *cb = *L->cb;
    *L->cb = NULL;
    xs_release(cb);
    xs_release(L->owner);
    free(L);
}
XS_NOINLINE void xs_wt_listen_start(xs_wq *q, xs_closure **cb, uint8_t *kind, xs_hdr *owner) {
    if (xs_kq < 0) xs_kq = kqueue();
    xs_wlisten *L = malloc(sizeof *L);
    L->q = q;
    L->cb = cb;
    L->kind = kind;
    L->owner = xs_retain(owner);
    xs_coro *co = xs_coro_new_shared(xs_wt_listen, L);
    co->on_done = xs_wt_listen_done;
    xs_tasks_live++;
    xs_exit_hook = xs_tasks_drain;
    xs_ready_push(co); /* it starts when the loop runs: after the code that set the listener up */
}
/* ---- setTimeout / setInterval (Node's globals): each timer is a coroutine on the event loop. It starts when the
   loop runs (a server's, or the one the program's exit waits on), sleeps, then calls its callback; an interval goes
   on until clearInterval. Like Node, a pending timer keeps the program alive. */
typedef struct xs_ttask { xs_closure *cb; int64_t ms; bool repeat, cancelled; int64_t id; struct xs_ttask *next; } xs_ttask;
static xs_ttask *xs_ttasks;
static int64_t xs_tnext = 1;
XS_NOINLINE void xs_ttask_run(void *p) {
    xs_ttask *t = p;
    for (;;) {
        xs_sleep_ms(t->ms);
        if (t->cancelled) return;
        size_t mark = xs_ar_mark();
        ((void (*)(void *))t->cb->fn)(t->cb);
        xs_ar_drain(mark);
        if (!t->repeat || t->cancelled) return;
    }
}
static void xs_ttask_done(xs_coro *co) {
    xs_ttask *t = co->arg;
    xs_tasks_live--;
    xs_coro_recycle(co);
    for (xs_ttask **pp = &xs_ttasks; *pp; pp = &(*pp)->next)
        if (*pp == t) { *pp = t->next; break; }
    xs_release(t->cb);
    free(t);
}
XS_NOINLINE int64_t xs_timer_start(xs_closure *cb, double ms, bool repeat) {
    if (xs_kq < 0) xs_kq = kqueue();
    xs_ttask *t = calloc(1, sizeof *t);
    t->cb = xs_retain(cb);
    t->ms = !(ms >= 1) ? 1 : ms > 2147483647.0 ? 1 : (int64_t)ms; /* (Node: below 1 or above 2^31-1 ms is 1) */
    t->repeat = repeat;
    t->id = xs_tnext++;
    t->next = xs_ttasks;
    xs_ttasks = t;
    xs_coro *co = xs_coro_new_shared(xs_ttask_run, t);
    co->on_done = xs_ttask_done;
    xs_tasks_live++;
    xs_exit_hook = xs_tasks_drain;
    xs_ready_push(co);
    return t->id;
}
XS_NOINLINE int64_t xs_set_timeout(xs_closure *cb, double ms) { return xs_timer_start(cb, ms, false); }
XS_NOINLINE int64_t xs_set_interval(xs_closure *cb, double ms) { return xs_timer_start(cb, ms, true); }
XS_NOINLINE void xs_clear_timer(double idn) { /* the timer's coroutine ends when it next wakes */
    int64_t id = (int64_t)idn;
    for (xs_ttask *t = xs_ttasks; t; t = t->next)
        if (t->id == id) t->cancelled = true;
}
XS_NOINLINE void xs_wt_check_event(xs_str *ev, const char *who) {
    if (ev->len != 7 || memcmp(ev->data, "message", 7) != 0)
        xs_panicf("%s.on: only \"message\" is supported (messages are text)", who);
}

typedef struct { char *data; size_t n; xs_wq *to_main, *to_worker; int64_t id; } xs_wstart;
XS_NOINLINE void *xs_wt_thread(void *p) {
    xs_wstart *w = p;
    xs_wt_worker = true;
    xs_wt_data_s = w->data;
    xs_wt_data_n = w->n;
    xs_wt_in = w->to_worker;
    xs_wt_out = w->to_main;
    xs_wt_tid = w->id;
    xs_wt_body();
    xs_tasks_drain(); /* its own listeners (parentPort.on) */
    fflush(stdout);
    xs_wq_close(w->to_main);
    xs_wq_unref(w->to_main);
    xs_wq_unref(w->to_worker);
    free(w->data);
    free(w);
    return NULL;
}
XS_NOINLINE xs_Worker *xs_Worker_new(xs_str *data) {
    if (!xs_wt_body) xs_panic("worker_threads: this program was not built for workers");
    xs_Worker *wk = xs_obj_alloc(sizeof(xs_Worker), xs_Worker_free, xs_Worker_print);
    wk->to_main = xs_wq_new();
    wk->to_worker = xs_wq_new();
    xs_wstart *w = malloc(sizeof *w);
    w->data = malloc((size_t)data->len + 1);
    memcpy(w->data, data->data, (size_t)data->len);
    w->data[data->len] = 0;
    w->n = (size_t)data->len;
    w->to_main = wk->to_main;
    w->to_worker = wk->to_worker;
    w->id = __atomic_fetch_add(&xs_wt_next_id, 1, __ATOMIC_RELAXED);
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, (size_t)64 << 20); /* deep recursion as on the main thread */
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t th;
    if (pthread_create(&th, &at, xs_wt_thread, w) != 0) xs_panic("worker_threads: cannot start a thread");
    pthread_attr_destroy(&at);
    return wk;
}
XS_NOINLINE xs_Worker *xs_Worker_on_k(xs_Worker *w, xs_str *ev, xs_closure *cb, uint8_t kind) {
    xs_wt_check_event(ev, "worker");
    xs_assign(w->on_msg, xs_retain(cb));
    w->on_kind = kind;
    if (!w->listening) { w->listening = true; xs_wt_listen_start(w->to_main, &w->on_msg, &w->on_kind, &w->hdr); }
    return xs_retain(w);
}
XS_NOINLINE xs_Worker *xs_Worker_on(xs_Worker *w, xs_str *ev, xs_closure *cb) { return xs_Worker_on_k(w, ev, cb, XS_WM_TEXT); }
XS_NOINLINE xs_Worker *xs_Worker_on_bytes(xs_Worker *w, xs_str *ev, xs_closure *cb) { return xs_Worker_on_k(w, ev, cb, XS_WM_BYTES); }
XS_NOINLINE xs_Worker *xs_Worker_on_sab(xs_Worker *w, xs_str *ev, xs_closure *cb) { return xs_Worker_on_k(w, ev, cb, XS_WM_SAB); }
XS_NOINLINE xs_Worker *xs_Worker_on_f64(xs_Worker *w, xs_str *ev, xs_closure *cb) { return xs_Worker_on_k(w, ev, cb, XS_WM_F64); }
XS_NOINLINE void xs_Worker_postMessage(xs_Worker *w, xs_str *m) { xs_wq_put(w->to_worker, m->data, (size_t)m->len, false); }
XS_NOINLINE void xs_Worker_postBytes(xs_Worker *w, xs_Uint8Array *b) { xs_wq_put_u8(w->to_worker, b); }
XS_NOINLINE void xs_Worker_postSab(xs_Worker *w, xs_SharedArrayBuffer *s) { xs_wq_put_sab(w->to_worker, s); }
XS_NOINLINE void xs_Worker_postF64(xs_Worker *w, xs_Float64Array *a) { xs_wq_put_f64(w->to_worker, a); }
/* the worker's parentPort gets no more messages: its listener ends, and the worker with it once it is idle */
XS_NOINLINE void xs_Worker_terminate(xs_Worker *w) { xs_wq_close(w->to_worker); }
XS_NOINLINE xs_MessagePort *xs_MessagePort_on_k(xs_MessagePort *p, xs_str *ev, xs_closure *cb, uint8_t kind) {
    xs_wt_check_event(ev, "parentPort");
    if (!p->in) xs_panic("parentPort is null on the main thread");
    xs_assign(p->on_msg, xs_retain(cb));
    p->on_kind = kind;
    if (!p->listening) { p->listening = true; xs_wt_listen_start(p->in, &p->on_msg, &p->on_kind, &p->hdr); }
    return xs_retain(p);
}
XS_NOINLINE xs_MessagePort *xs_MessagePort_on(xs_MessagePort *p, xs_str *ev, xs_closure *cb) { return xs_MessagePort_on_k(p, ev, cb, XS_WM_TEXT); }
XS_NOINLINE xs_MessagePort *xs_MessagePort_on_bytes(xs_MessagePort *p, xs_str *ev, xs_closure *cb) { return xs_MessagePort_on_k(p, ev, cb, XS_WM_BYTES); }
XS_NOINLINE xs_MessagePort *xs_MessagePort_on_sab(xs_MessagePort *p, xs_str *ev, xs_closure *cb) { return xs_MessagePort_on_k(p, ev, cb, XS_WM_SAB); }
XS_NOINLINE xs_MessagePort *xs_MessagePort_on_f64(xs_MessagePort *p, xs_str *ev, xs_closure *cb) { return xs_MessagePort_on_k(p, ev, cb, XS_WM_F64); }
XS_NOINLINE void xs_MessagePort_postMessage(xs_MessagePort *p, xs_str *m) {
    if (!p->out) xs_panic("parentPort is null on the main thread");
    xs_wq_put(p->out, m->data, (size_t)m->len, false);
}
XS_NOINLINE void xs_MessagePort_postBytes(xs_MessagePort *p, xs_Uint8Array *b) {
    if (!p->out) xs_panic("parentPort is null on the main thread");
    xs_wq_put_u8(p->out, b);
}
XS_NOINLINE void xs_MessagePort_postSab(xs_MessagePort *p, xs_SharedArrayBuffer *s) {
    if (!p->out) xs_panic("parentPort is null on the main thread");
    xs_wq_put_sab(p->out, s);
}
XS_NOINLINE void xs_MessagePort_postF64(xs_MessagePort *p, xs_Float64Array *a) {
    if (!p->out) xs_panic("parentPort is null on the main thread");
    xs_wq_put_f64(p->out, a);
}
#endif
