/* The operating system under the compiler: what `xisto build` and `xisto run` need from it, the same on
   macOS, Linux and Windows. On POSIX these are thin wrappers; on Windows the compiler runs natively (there is no
   sh, fork or /tmp), the C compiler is zig's (the same as --target uses), and paths keep forward slashes, which
   every Windows API and zig accept. */
#include "xisto.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

/* one argument for a command line: '...' for sh, "..." for cmd.exe (Windows paths hold no quote) */
const char *xs_q(const char *s) {
#if defined(_WIN32)
    return xs_sprintf("\"%s\"", s);
#else
    Buf b = {0};
    buf_putc(&b, '\'');
    for (; *s; s++) {
        if (*s == '\'') buf_puts(&b, "'\\''");
        else buf_putc(&b, *s);
    }
    buf_putc(&b, '\'');
    return b.data;
#endif
}

/* runs a command line through the system's shell; the exit code of the command (or -1) */
int xs_sh(const char *cmd) {
#if defined(_WIN32)
    /* cmd.exe drops the first and last quote of the line when it starts with one: wrap it once more */
    int rc = system(xs_sprintf("\"%s\"", cmd));
    return rc;
#else
    int rc = system(cmd);
    if (rc == -1) return -1;
    return (rc >> 8) & 0xff;
#endif
}

const char *xs_devnull(void) {
#if defined(_WIN32)
    return "NUL";
#else
    return "/dev/null";
#endif
}

bool xs_mkdirs(const char *path) {
    char *p = xs_strndup(path, strlen(path));
    for (char *s = p + 1; *s; s++) {
        if (*s != '/' && *s != '\\') continue;
        char c = *s;
        *s = 0;
#if defined(_WIN32)
        if (!(s == p + 2 && p[1] == ':')) _mkdir(p); /* (not "C:") */
#else
        mkdir(p, 0755);
#endif
        *s = c;
    }
#if defined(_WIN32)
    int r = _mkdir(p);
#else
    int r = mkdir(p, 0755);
#endif
    bool ok = r == 0 || errno == EEXIST;
    free(p);
    return ok;
}

/* rename that replaces the destination, as POSIX rename does (Windows' does not) */
bool xs_rename(const char *from, const char *to) {
#if defined(_WIN32)
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(from, to) == 0;
#endif
}

bool xs_copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb");
    if (!in) return false;
    FILE *out = fopen(to, "wb");
    if (!out) { fclose(in); return false; }
    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    fclose(in);
    if (fclose(out) != 0) ok = false;
#if !defined(_WIN32)
    if (ok) { struct stat st; if (stat(from, &st) == 0) chmod(to, st.st_mode & 0777); }
#endif
    return ok;
}

/* is there a file at path (the executable check: Windows has no execute bit) */
bool xs_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}
bool xs_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* prog in one of PATH's directories (Windows: also prog.exe) */
bool xs_on_path(const char *prog) {
    if (strchr(prog, '/') || strchr(prog, '\\')) return xs_exists(prog) || xs_exists(xs_sprintf("%s.exe", prog));
    const char *path = getenv("PATH");
    if (!path) return false;
#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif
    const char *s = path;
    while (*s) {
        const char *e = strchr(s, sep);
        size_t n = e ? (size_t)(e - s) : strlen(s);
        if (n) {
            char *dir = xs_strndup(s, n);
            if (xs_exists(xs_sprintf("%s/%s", dir, prog))) return true;
#if defined(_WIN32)
            if (xs_exists(xs_sprintf("%s/%s.exe", dir, prog))) return true;
#endif
        }
        if (!e) break;
        s = e + 1;
    }
    return false;
}

/* the directory for temporary files, without a trailing slash */
const char *xs_tmp_base(void) {
#if defined(_WIN32)
    static char buf[MAX_PATH + 1];
    DWORD n = GetTempPathA(sizeof buf, buf);
    if (n == 0 || n > MAX_PATH) return ".";
    for (char *c = buf; *c; c++) if (*c == '\\') *c = '/';
    size_t l = strlen(buf);
    if (l > 1 && buf[l - 1] == '/') buf[l - 1] = 0;
    return buf;
#else
    const char *t = getenv("TMPDIR");
    if (!t || !*t) return "/tmp";
    return t;
#endif
}

/* a new private directory under the temporary one: prefix-XXXXXX; NULL on failure */
char *xs_make_temp_dir(const char *prefix) {
#if defined(_WIN32)
    for (int k = 0; k < 100; k++) {
        char *d = xs_sprintf("%s/%s-%u-%u", xs_tmp_base(), prefix, (unsigned)GetCurrentProcessId(), (unsigned)(GetTickCount() + (DWORD)k * 7919u));
        if (_mkdir(d) == 0) return d;
    }
    return NULL;
#else
    char *d = xs_sprintf("%s/%s-XXXXXX", xs_tmp_base(), prefix);
    return mkdtemp(d) ? d : NULL;
#endif
}

const char *xs_home(void) {
    const char *h = getenv("HOME");
#if defined(_WIN32)
    if (!h || !*h) h = getenv("USERPROFILE");
    if (h) { char *c = xs_strndup(h, strlen(h)); for (char *x = c; *x; x++) if (*x == '\\') *x = '/'; return c; }
#endif
    return h && *h ? h : xs_tmp_base();
}

/* the absolute path of path, or NULL */
char *xs_real_path(const char *path, char *out /* PATH_MAX */) {
#if defined(_WIN32)
    if (!_fullpath(out, path, 4096) || !xs_exists(out)) return NULL;
    for (char *c = out; *c; c++) if (*c == '\\') *c = '/';
    return out;
#else
    return realpath(path, out);
#endif
}

int xs_ncpus(void) {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}

int xs_pid(void) {
#if defined(_WIN32)
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}

/* the files under dir (recursively) whose name ends with one of the suffixes, as paths relative to dir */
void xs_list_files(const char *dir, const char *suf1, const char *suf2, char ***out, int *n) {
    const char **stack = malloc(sizeof(char *) * 64);
    int sp = 0, scap = 64;
    stack[sp++] = "";
    while (sp) {
        const char *rel = stack[--sp];
        char *full = *rel ? xs_sprintf("%s/%s", dir, rel) : xs_strndup(dir, strlen(dir));
#if defined(_WIN32)
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(xs_sprintf("%s/*", full), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const char *name = fd.cFileName;
            bool isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
        DIR *d = opendir(full);
        if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d))) {
            const char *name = de->d_name;
            struct stat st;
            bool isdir = stat(xs_sprintf("%s/%s", full, name), &st) == 0 && S_ISDIR(st.st_mode);
#endif
            if (!strcmp(name, ".") || !strcmp(name, "..")) continue; /* (dot files count: .git under a package removed whole) */
            char *r = *rel ? xs_sprintf("%s/%s", rel, name) : xs_strndup(name, strlen(name));
            if (isdir) {
                if (sp == scap) { scap *= 2; stack = realloc(stack, sizeof(char *) * (size_t)scap); }
                stack[sp++] = r;
                continue;
            }
            size_t l = strlen(name), a = strlen(suf1), b = suf2 ? strlen(suf2) : 0;
            if ((l > a && strcmp(name + l - a, suf1) == 0) || (suf2 && l > b && strcmp(name + l - b, suf2) == 0)) {
                *out = realloc(*out, sizeof(char *) * (size_t)(*n + 1));
                (*out)[(*n)++] = r;
            }
#if defined(_WIN32)
        } while (FindNextFileA(h, &fd));
        FindClose(h);
#else
        }
        closedir(d);
#endif
    }
}

/* removes a directory tree (temp dirs, packages): files first, then each directory on the way back up */
void xs_rmtree(const char *path) {
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(xs_sprintf("%s/*", path), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const char *name = fd.cFileName;
            if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
            char *f = xs_sprintf("%s/%s", path, name);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) xs_rmtree(f);
            else { _chmod(f, _S_IREAD | _S_IWRITE); unlink(f); } /* (git's objects are read-only: Windows keeps those) */
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    _rmdir(path);
#else
    DIR *d = opendir(path);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            const char *name = de->d_name;
            if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
            char *f = xs_sprintf("%s/%s", path, name);
            struct stat st;
            if (lstat(f, &st) == 0 && S_ISDIR(st.st_mode)) xs_rmtree(f); /* (a link to a directory: only the link goes) */
            else unlink(f);
        }
        closedir(d);
    }
    rmdir(path);
#endif
}

void xs_setenv(const char *name, const char *value) {
#if defined(_WIN32)
    _putenv(xs_sprintf("%s=%s", name, value ? value : ""));
#else
    if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}

/* a whole file, NUL-terminated (*len its size); NULL when it cannot be read */
char *xs_slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    Buf b = {0};
    char tmp[65536];
    size_t k;
    while ((k = fread(tmp, 1, sizeof tmp, f)) > 0) for (size_t i = 0; i < k; i++) buf_putc(&b, tmp[i]);
    fclose(f);
    if (!b.data) buf_puts(&b, "");
    if (len) *len = b.len;
    return b.data;
}

/* the files under dir and their total size in bytes */
long long xs_tree_size(const char *dir, int *count) {
    char **files = NULL;
    int n = 0;
    xs_list_files(dir, "", NULL, &files, &n);
    long long total = 0;
    for (int i = 0; i < n; i++) { struct stat st; if (stat(xs_sprintf("%s/%s", dir, files[i]), &st) == 0) total += (long long)st.st_size; }
    if (count) *count = n;
    return total;
}
