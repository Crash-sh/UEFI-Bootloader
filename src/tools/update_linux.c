#define _GNU_SOURCE
#include "common.h"
#include "../uefi/boot_config.h"
#include "../uefi/linux_format.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int read_config(int directory, struct boot_config *config)
{
    unsigned char bytes[BOOT_CONFIG_MAX + 1];
    int fd = openat(directory, "boot.conf", O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        return(errno == ENOENT ? 1 : -1);
    }
    struct stat st;
    ssize_t count = -1;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        count = read(fd, bytes, sizeof(bytes));
    }
    close(fd);
    return(count >= 0 && boot_config_parse(bytes, count, config) == 0 ? 0 : -1);
}

static int complete_set(int directory, const char *id)
{
    int set = strcmp(id, "legacy") == 0
                  ? dup(directory)
                  : openat(directory, id, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (set < 0) {
        return(-1);
    }
    const char *files[] = {"vmlinuz", "cmdline.txt", "initrd"};
    int result = 0;
    for (size_t i = 0; i < 3; ++i) {
        struct stat st;
        if (fstatat(set, files[i], &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode) ||
            (i != 1 && st.st_size == 0) || (i == 2 && st.st_size > 512 * 1024 * 1024)) {
            result = -1;
        }
    }
    struct linux_image image;
    struct stat st;
    unsigned char header[4096], command[LINUX_CMDLINE_LIMIT + 3];
    int fd = openat(set, "vmlinuz", O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    ssize_t count = -1;
    if (fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        count = read(fd, header, sizeof(header));
    }
    if (fd >= 0) {
        close(fd);
    }
    if (count < 0 || linux_parse_header(header, count, st.st_size, &image) != 0) {
        result = -1;
    }
    if (!result) {
        fd = openat(set, "cmdline.txt", O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
        count = fd >= 0 ? read(fd, command, sizeof(command)) : -1;
        if (fd >= 0) {
            close(fd);
        }
        size_t size = count < 0 ? sizeof(command) : (size_t)count;
        if (linux_command_line(command, &size, sizeof(command), image.header.cmdline_size) != 0) {
            result = -1;
        }
    }
    close(set);
    return(result);
}

static int publish(int directory, const struct boot_config *config)
{
    char text[BOOT_CONFIG_MAX];
    int size = snprintf(text, sizeof(text), "NEUROS1\n%s\n%s\n", config->current, config->previous);
    struct boot_config checked;
    if (size < 0 || boot_config_parse(text, size, &checked) != 0) {
        return(-1);
    }
    /* A stale file means an interrupted update. Never replace it automatically. */
    int fd = openat(directory, ".boot.conf.new", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    if (fd < 0) {
        return(-1);
    }
    int result = -1;
    if (write(fd, text, size) != size || fsync(fd) != 0) {
        goto done;
    }
    if (close(fd) != 0) {
        fd = -1;
        goto done;
    }
    fd = -1;
    if (renameat(directory, ".boot.conf.new", directory, "boot.conf") != 0) {
        goto done;
    }
    if (fsync(directory) != 0) {
        fprintf(stderr, "Selection published but directory sync failed; inspect boot.conf.\n");
        return(-1);
    }
    result = 0;
done:
    if (fd >= 0) {
        close(fd);
    }
    if (result) {
        unlinkat(directory, ".boot.conf.new", 0);
    }
    return(result);
}

int main(int argc, char **argv)
{
    int rollback = argc == 3 && strcmp(argv[1], "--rollback") == 0;
    if (!rollback && argc != 5) {
        fprintf(stderr,
                "Usage: %s KERNEL CMDLINE INITRD DIRECTORY\n       %s --rollback DIRECTORY\n",
                argv[0], argv[0]);
        return(2);
    }
    const char *destination = argv[rollback ? 2 : 4];
    if (make_parents(destination) != 0 || (mkdir(destination, 0755) != 0 && errno != EEXIST)) {
        perror(destination);
        return(1);
    }
    int directory = open(destination, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (directory < 0 || flock(directory, LOCK_EX | LOCK_NB) != 0) {
        perror("Cannot lock boot directory");
        return(1);
    }
    struct boot_config old = {{0}, {0}}, next = {{0}, {0}};
    int result = read_config(directory, &old);
    if (result < 0) {
        fprintf(stderr, "Invalid boot.conf; refusing to replace it.\n");
        return(1);
    }
    if (rollback) {
        if (result != 0 || strcmp(old.previous, "-") == 0 ||
            complete_set(directory, old.previous) != 0) {
            fprintf(stderr, "No complete previous boot set is available.\n");
            return(1);
        }
        strcpy(next.current, old.previous);
        strcpy(next.previous, old.current);
    } else {
        if (result == 0) {
            if (complete_set(directory, old.current) != 0) {
                fprintf(stderr,
                        "Current set is damaged; recover it or roll back before updating.\n");
                return(1);
            }
            strcpy(next.previous, old.current);
        } else {
            struct stat st;
            if (fstatat(directory, "vmlinuz", &st, AT_SYMLINK_NOFOLLOW) == 0) {
                if (complete_set(directory, "legacy") != 0) {
                    fprintf(stderr, "Legacy boot files are incomplete; refusing migration.\n");
                    return(1);
                }
                strcpy(next.previous, "legacy");
            } else if (errno == ENOENT) {
                strcpy(next.previous, "-");
            } else {
                perror("legacy kernel");
                return(1);
            }
        }
        unsigned char random[16];
        size_t received = 0;
        while (received < sizeof(random)) {
            ssize_t count = getrandom(random + received, sizeof(random) - received, 0);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                perror("getrandom");
                return(1);
            }
            received += count;
        }
        strcpy(next.current, "set-");
        for (size_t i = 0; i < sizeof(random); ++i) {
            snprintf(next.current + 4 + 2 * i, 3, "%02x", random[i]);
        }
        char helper[PATH_MAX], temporary[80], path[PATH_MAX];
        ssize_t length = readlink("/proc/self/exe", helper, sizeof(helper) - 1);
        if (length <= 0 || length >= (ssize_t)sizeof(helper) - 1) {
            return(1);
        }
        helper[length] = 0;
        char *slash = strrchr(helper, '/');
        if (!slash || (size_t)(slash - helper) + sizeof("/stage-linux") > sizeof(helper)) {
            return(1);
        }
        strcpy(slash, "/stage-linux");
        snprintf(temporary, sizeof(temporary), ".new-%s", next.current);
        if (mkdirat(directory, temporary, 0700) != 0) {
            perror("private boot set");
            return(1);
        }
        /* Resolve destination through the locked directory, not a mutable pathname. */
        snprintf(path, sizeof(path), "/proc/self/fd/%d/%s", directory, temporary);
        pid_t child = fork();
        if (child == 0) {
            execl(helper, helper, argv[1], argv[2], argv[3], path, (char *)NULL);
            _exit(127);
        }
        int status = 0;
        pid_t waited;
        do {
            waited = child < 0 ? -1 : waitpid(child, &status, 0);
        } while (waited < 0 && child >= 0 && errno == EINTR);
        if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "Staging failed; active selection unchanged. Inspect %s/%s.\n",
                    destination, temporary);
            return(1);
        }
        if (complete_set(directory, temporary) != 0 ||
            renameat2(directory, temporary, directory, next.current, RENAME_NOREPLACE) != 0 ||
            fsync(directory) != 0) {
            perror("publish boot set");
            return(1);
        }
    }
    if (publish(directory, &next) != 0) {
        perror("publish boot selection");
        return(1);
    }
    close(directory);
    printf("Selected %s; previous %s. Older sets are retained.\n", next.current, next.previous);
    return(0);
}
