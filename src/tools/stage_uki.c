#define _XOPEN_SOURCE 700
#include "common.h"
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s SOURCE [DESTINATION]\n", argv[0]);
        return (2);
    }
    if (validate_image(argv[1], 1) != 0) {
        return (1);
    }

    if (argc == 3) {
        char parent[PATH_MAX], previous[PATH_MAX];
        struct stat info;
        if (strlen(argv[2]) >= sizeof(parent) || make_parents(argv[2]) != 0 ||
            snprintf(previous, sizeof(previous), "%s.previous", argv[2]) >= (int)sizeof(previous)) {
            return (1);
        }
        strcpy(parent, argv[2]);
        char *slash = strrchr(parent, '/');
        if (slash == parent) {
            slash[1] = 0;
        } else if (slash) {
            *slash = 0;
        } else {
            strcpy(parent, ".");
        }
        int directory = open(parent, O_RDONLY | O_DIRECTORY);
        if (directory < 0 || flock(directory, LOCK_EX | LOCK_NB) != 0) {
            perror("Cannot lock UKI directory");
            return (1);
        }
        if (lstat(argv[2], &info) == 0) {
            if (!S_ISREG(info.st_mode) || validate_image(argv[2], 1) != 0 ||
                copy_image(argv[2], previous, 0) != 0) {
                return (1);
            }
        } else if (errno != ENOENT) {
            return (1);
        }
        if (copy_image(argv[1], argv[2], 0) != 0) {
            return (1);
        }
        close(directory);
    }
    puts("UKI structure validated; signatures and boot configuration are not verified.");
    return (0);
}
