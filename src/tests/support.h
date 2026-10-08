#ifndef NEUROS_TEST_SUPPORT_H
#define NEUROS_TEST_SUPPORT_H
/* Host-only utilities for disposable integration tests. */
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "../tools/common.h"

static char test_root[PATH_MAX];
static pid_t test_child;
static const char *test_log = "/dev/null";
static volatile sig_atomic_t test_interrupted;

static inline void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s (errno: %s); log: %s\n", message, strerror(errno), test_log);
    exit(1);
}
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition))                                                                          \
            fail(#condition);                                                                      \
    } while (0)

static inline char *fmt(const char *pattern, ...)
{
    static char paths[512][PATH_MAX];
    static size_t used;
    CHECK(used < 512);
    char *result = paths[used++];
    va_list args;
    va_start(args, pattern);
    int length = vsnprintf(result, PATH_MAX, pattern, args);
    va_end(args);
    CHECK(length >= 0 && length < PATH_MAX);
    return(result);
}

static inline double seconds(void)
{
    struct timespec ts;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return(ts.tv_sec + ts.tv_nsec / 1e9);
}

static inline void poll_pause(void)
{
    struct timespec ts = {0, 20000000};
    nanosleep(&ts, NULL);
    CHECK(!test_interrupted);
}

static inline void stop_child(void)
{
    if (test_child > 0) {
        kill(-test_child, SIGKILL);
        while (waitpid(test_child, NULL, 0) < 0 && errno == EINTR) {
        }
        test_child = 0;
    }
}

static inline int remove_test_path(const char *path, const struct stat *info, int type,
                                   struct FTW *walk)
{
    (void)info;
    (void)type;
    (void)walk;
    return(remove(path));
}

static inline void cleanup_test(void)
{
    stop_child();
    if (*test_root) {
        nftw(test_root, remove_test_path, 32, FTW_DEPTH | FTW_PHYS);
    }
}

static inline void test_signal(int sig)
{
    test_interrupted = sig;
}

static inline void start_test(const char *prefix)
{
    CHECK(snprintf(test_root, sizeof(test_root), "/tmp/%s-XXXXXX", prefix) <
          (int)sizeof(test_root));
    CHECK(mkdtemp(test_root) != NULL);
    CHECK(atexit(cleanup_test) == 0);
    struct sigaction action = {0};
    action.sa_handler = test_signal;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGINT, &action, NULL) == 0);
    CHECK(sigaction(SIGTERM, &action, NULL) == 0);
}

static inline unsigned char *read_bytes(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    CHECK(length >= 0 && length <= 512 * 1024 * 1024);
    CHECK(fseek(file, 0, SEEK_SET) == 0);
    unsigned char *data = malloc((size_t)length + 1);
    CHECK(data != NULL);
    CHECK(fread(data, 1, (size_t)length, file) == (size_t)length && !ferror(file));
    CHECK(fclose(file) == 0);
    data[length] = 0;
    *size = (size_t)length;
    return(data);
}

static inline void write_bytes(const char *path, const void *data, size_t size)
{
    CHECK(make_parents(path) == 0);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fwrite(data, 1, size, file) == size);
    CHECK(fclose(file) == 0);
}

static inline void write_text(const char *path, const char *text)
{
    write_bytes(path, text, strlen(text));
}

static inline void equal_files(const char *a, const char *b)
{
    size_t as, bs;
    unsigned char *ad = read_bytes(a, &as), *bd = read_bytes(b, &bs);
    CHECK(as == bs && memcmp(ad, bd, as) == 0);
    free(ad);
    free(bd);
}

static inline void equal_text(const char *path, const char *expected)
{
    size_t size;
    unsigned char *data = read_bytes(path, &size);
    CHECK(size == strlen(expected) && memcmp(data, expected, size) == 0);
    free(data);
}

static inline pid_t spawn(const char *const args[], const char *log)
{
    CHECK(test_child == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        if (setpgid(0, 0) != 0) {
            _exit(126);
        }
        int input = open("/dev/null", O_RDONLY);
        int output = open(log, O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (input < 0 || output < 0 || dup2(input, 0) < 0 || dup2(output, 1) < 0 ||
            dup2(output, 2) < 0) {
            _exit(126);
        }
        close(input);
        close(output);
        execvp(args[0], (char *const *)args);
        _exit(127);
    }
    test_child = child;
    /* Either side can win the setpgid race before exec. */
    if (setpgid(child, child) != 0 && errno != EACCES && errno != ESRCH) {
        fail("setpgid");
    }
    return(child);
}

static inline int wait_command(double timeout)
{
    double deadline = seconds() + timeout;
    int status;
    for (;;) {
        pid_t result = waitpid(test_child, &status, WNOHANG);
        if (result == test_child) {
            test_child = 0;
            return(status);
        }
        CHECK(result == 0 || (result < 0 && errno == EINTR));
        CHECK(seconds() < deadline);
        poll_pause();
    }
}

static inline void run_command(const char *const args[], int success)
{
    spawn(args, test_log);
    int status = wait_command(60);
    CHECK((WIFEXITED(status) && WEXITSTATUS(status) == 0) == success);
}

static inline uint16_t get16(const unsigned char *p)
{
    return((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static inline uint32_t get32(const unsigned char *p)
{
    return((uint32_t)get16(p) | (uint32_t)get16(p + 2) << 16);
}
static inline uint64_t get64(const unsigned char *p)
{
    return((uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32);
}
static inline void put_le(unsigned char *p, uint64_t value, unsigned int bytes)
{
    for (unsigned int i = 0; i < bytes; ++i) {
        p[i] = (unsigned char)(value >> (8 * i));
    }
}
#endif
