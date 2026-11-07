/* Runs a program and writes its peak RSS (KB), user and system CPU seconds to a file: the runners' measurement
   on Linux. There a child's ru_maxrss starts at its parent's RSS when it calls exec (the kernel carries the old
   memory's high-water mark over), so a program started from Python reports Python's ~20 MB at least; started from
   this small launcher, the floor is the launcher's few hundred KB.
   Usage: rusage OUT_FILE PROGRAM [ARGS...]   (exits with the program's status; 127 when it could not start) */
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 3) { fputs("usage: rusage OUT_FILE PROGRAM [ARGS...]\n", stderr); return 2; }
    pid_t pid = fork();
    if (pid < 0) return 127;
    if (pid == 0) { execvp(argv[2], argv + 2); _exit(127); }
    int status; struct rusage ru;
    if (wait4(pid, &status, 0, &ru) < 0) return 127;
    FILE *f = fopen(argv[1], "w");
    if (!f) return 127;
#ifdef __APPLE__
    long kb = ru.ru_maxrss / 1024; /* bytes on macOS */
#else
    long kb = ru.ru_maxrss;        /* KB on Linux */
#endif
    fprintf(f, "%ld %.6f %.6f\n", kb, ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6, ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6);
    fclose(f);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
