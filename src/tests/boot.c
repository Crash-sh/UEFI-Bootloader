#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct options {
    const char *build;
    const char *qemu;
    const char *firmware;
    const char *vars;
};

struct test_case {
    const char *name;
    const char *payload;
    const char *expected[3];
};

static volatile sig_atomic_t interrupted;

static void on_signal(int number)
{
    interrupted = number;
}

static int format(char *buffer, size_t size, const char *pattern, ...)
{
    va_list arguments;
    int length;

    va_start(arguments, pattern);
    length = vsnprintf(buffer, size, pattern, arguments);

    va_end(arguments);

    if (length < 0 || (size_t)length >= size) {
        fprintf(stderr, "Path exceeds supported length\n");
        return (-1);
    }

    return (0);
}

static int copy_file(const char *source, const char *destination)
{
    char buffer[16384];

    FILE *input = fopen(source, "rb");
    FILE *output;

    size_t count;

    int result = 0;

    if (input == NULL) {
        perror(source);
        return (-1);
    }

    output = fopen(destination, "wb");

    if (output == NULL) {
        perror(destination);
        fclose(input);
        return (-1);
    }

    while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0) {
        if (fwrite(buffer, 1, count, output) != count) {
            result = -1;

            break;
        }
    }

    if (ferror(input)) {
        result = -1;
    }

    if (fclose(output) != 0) {
        result = -1;
    }

    fclose(input);

    if (result != 0) {
        fprintf(stderr, "Could not copy %s to %s\n", source, destination);
    }

    return (result);
}

static void read_log(const char *path, char *buffer, size_t size)
{
    FILE *stream = fopen(path, "rb");

    long length;
    size_t count;

    buffer[0] = '\0';

    if (stream != NULL) {
        if (fseek(stream, 0, SEEK_END) == 0 && (length = ftell(stream)) >= 0) {
            long start = length > (long)size - 1 ? length - (long)size + 1 : 0;

            if (fseek(stream, start, SEEK_SET) == 0) {
                count = fread(buffer, 1, size - 1, stream);
                buffer[count] = '\0';
            }
        }

        fclose(stream);
    }
}

static double now(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return (-1.0);
    }

    return ((double)value.tv_sec + (double)value.tv_nsec / 1000000000.0);
}

static void pause_poll(void)
{
    struct timespec delay = {0, 100000000};

    nanosleep(&delay, NULL);
}

static void stop_child(pid_t child)
{
    int status;
    pid_t result;

    kill(child, SIGTERM);
    for (int i = 0; i < 50; ++i) {
        result = waitpid(child, &status, WNOHANG);
        if (result == child || (result < 0 && errno == ECHILD)) {
            break;
        }

        pause_poll();
    }
    result = waitpid(child, &status, WNOHANG);

    if (result == 0) {
        kill(child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
    }
}

static int remove_entry(const char *path, const struct stat *info, int type, struct FTW *walk)
{
    (void)info;
    (void)type;
    (void)walk;
    return (remove(path));
}

static int run_case(const struct options *options, const struct test_case *test)
{
    char directory[] = "/tmp/neuros-XXXXXX";

    const char *folders[] = {"esp", "esp/EFI", "esp/EFI/BOOT", "esp/EFI/Linux"};

    char source[PATH_MAX], target[PATH_MAX], path[PATH_MAX];
    char log[PATH_MAX], errors[PATH_MAX], variables[PATH_MAX];
    char firmware_drive[PATH_MAX + 64], vars_drive[PATH_MAX + 64];
    char esp_drive[PATH_MAX + 64], serial[PATH_MAX + 16];
    char output[65536];

    int matched[3] = {0};
    int result = -1, status;

    pid_t child = -1;

    double deadline, current;

    if (mkdtemp(directory) == NULL) {
        perror("mkdtemp");
        return (-1);
    }

    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); ++i) {
        if (format(path, sizeof(path), "%s/%s", directory, folders[i]) != 0) {
            goto cleanup;
        }

        if (mkdir(path, 0700) != 0) {
            perror(path);
            goto cleanup;
        }
    }

    if (format(source, sizeof(source), "%s/esp/EFI/BOOT/BOOTX64.EFI", options->build) != 0 ||
        format(target, sizeof(target), "%s/esp/EFI/BOOT/BOOTX64.EFI", directory) != 0 ||
        copy_file(source, target) != 0) {
        goto cleanup;
    }

    if (format(target, sizeof(target), "%s/esp/EFI/Linux/arch-linux.efi", directory) != 0) {
        goto cleanup;
    }

    if (test->payload != NULL) {
        if (format(source, sizeof(source), "%s/%s", options->build, test->payload) != 0 ||
            copy_file(source, target) != 0) {
            goto cleanup;
        }
    } else if (strcmp(test->name, "invalid UKI") == 0) {
        FILE *stream = fopen(target, "wb");

        if (stream == NULL) {
            perror(target);
            goto cleanup;
        }

        int written = fputs("not an EFI image", stream);
        int closed = fclose(stream);

        if (written == EOF || closed != 0) {
            goto cleanup;
        }
    }

    if (format(variables, sizeof(variables), "%s/vars.fd", directory) != 0 ||
        copy_file(options->vars, variables) != 0 ||
        format(log, sizeof(log), "%s/serial.log", directory) != 0 ||
        format(errors, sizeof(errors), "%s/stderr.log", directory) != 0 ||
        format(firmware_drive, sizeof(firmware_drive), "if=pflash,format=raw,readonly=on,file=%s",
               options->firmware) != 0 ||
        format(vars_drive, sizeof(vars_drive), "if=pflash,format=raw,file=%s", variables) != 0 ||
        format(esp_drive, sizeof(esp_drive), "format=raw,file=fat:rw:%s/esp", directory) != 0 ||
        format(serial, sizeof(serial), "file:%s", log) != 0) {
        goto cleanup;
    }

    current = now();

    if (current < 0) {
        goto cleanup;
    }

    deadline = current + 30.0;
    child = fork();

    if (child < 0) {
        perror("fork");
        goto cleanup;
    }

    if (child == 0) {
        int null_fd = open("/dev/null", O_RDWR);
        int error_fd = open(errors, O_WRONLY | O_CREAT | O_TRUNC, 0600);

        if (null_fd < 0 || error_fd < 0 || dup2(null_fd, STDIN_FILENO) < 0 ||
            dup2(null_fd, STDOUT_FILENO) < 0 || dup2(error_fd, STDERR_FILENO) < 0) {
            _exit(126);
        }

        if (null_fd > STDERR_FILENO) {
            close(null_fd);
        }

        if (error_fd > STDERR_FILENO) {
            close(error_fd);
        }

        execlp(options->qemu, options->qemu, "-accel", "tcg", "-m", "256", "-display", "none",
               "-monitor", "none", "-net", "none", "-no-reboot", "-drive", firmware_drive, "-drive",
               vars_drive, "-drive", esp_drive, "-serial", serial, (char *)NULL);
        perror(options->qemu);
        _exit(127);
    }

    while (!interrupted) {
        int complete = 1;

        read_log(log, output, sizeof(output));

        for (size_t i = 0; i < 3; ++i) {
            if (test->expected[i] != NULL && strstr(output, test->expected[i]) != NULL) {
                matched[i] = 1;
            }

            if (test->expected[i] != NULL && !matched[i]) {
                complete = 0;
            }
        }

        if (complete) {
            printf("%s: passed\n", test->name);
            fflush(stdout);
            result = 0;
            break;
        }

        pid_t waited = waitpid(child, &status, WNOHANG);

        if (waited == child) {
            child = -1;
            read_log(errors, output, sizeof(output));
            fprintf(stderr, "%s: QEMU exited early (wait status %d)\n%s", test->name, status,
                    output);
            break;
        }

        if (waited < 0 && errno != EINTR) {
            perror("waitpid");
            break;
        }

        current = now();

        if (current < 0 || current >= deadline) {
            size_t length = strlen(output);
            fprintf(stderr, "%s: timed out\n%s\n", test->name,
                    output + (length > 2000 ? length - 2000 : 0));
            break;
        }

        pause_poll();
    }

cleanup:
    if (child > 0) {
        stop_child(child);
    }

    if (nftw(directory, remove_entry, 16, FTW_DEPTH | FTW_PHYS) != 0) {
        perror("temporary directory cleanup");
        result = -1;
    }

    return (result);
}

int main(int argc, char **argv)
{
    struct options options = {0};
    struct sigaction action = {0};

    const struct test_case cases[] = {
        {"child success",
         "test.efi",
         {"NEUROS: test child reached", "StartImage: Success", "[Enter] Retry Arch"}},
        {"child error",
         "error.efi",
         {"NEUROS: test child reached", "StartImage: Aborted", "[Enter] Retry Arch"}},
        {"missing UKI", NULL, {"LoadImage: Not Found", "[Enter] Retry Arch", NULL}},
        {"invalid UKI", NULL, {"LoadImage:", "[Enter] Retry Arch", NULL}}};

    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            goto usage;
        }

        if (strcmp(argv[i], "--build") == 0) {
            options.build = argv[i + 1];
        } else if (strcmp(argv[i], "--qemu") == 0) {
            options.qemu = argv[i + 1];
        } else if (strcmp(argv[i], "--firmware") == 0) {
            options.firmware = argv[i + 1];
        } else if (strcmp(argv[i], "--vars") == 0) {
            options.vars = argv[i + 1];
        } else {
            goto usage;
        }
    }

    if (!options.build || !options.qemu || !options.firmware || !options.vars) {
        goto usage;
    }

    action.sa_handler = on_signal;

    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, NULL) != 0 || sigaction(SIGTERM, &action, NULL) != 0) {
        perror("sigaction");
        return (1);
    }

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (interrupted || run_case(&options, &cases[i]) != 0) {
            return (interrupted ? 128 + interrupted : 1);
        }
    }

    return (0);
usage:
    fprintf(stderr, "Usage: %s --build DIR --qemu PROGRAM --firmware FILE --vars FILE\n", argv[0]);
    return (2);
}
