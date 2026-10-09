#define _GNU_SOURCE
#include "common.h"
#include "../uefi/linux_format.h"
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

static int read_header(const char *path, unsigned char *data, size_t capacity, size_t *read_size,
                       struct stat *info)
{
    FILE *stream = fopen(path, "rb");

    if (!stream) {
        perror(path);
        return(-1);
    }

    int result = fstat(fileno(stream), info);

    if (result == 0 && S_ISREG(info->st_mode) && info->st_size >= 0) {
        *read_size = fread(data, 1, capacity, stream);
        result = ferror(stream) ? -1 : 0;
    } else {
        result = -1;
    }

    fclose(stream);

    return(result);
}

int main(int argc, char **argv)
{
    unsigned char header[4096], command[LINUX_CMDLINE_LIMIT + 3];

    struct stat info;
    struct linux_image image;

    size_t size;

    char target[PATH_MAX], temporary[PATH_MAX], staged[PATH_MAX];

    if (argc != 5) {
        fprintf(stderr, "Usage: %s KERNEL CMDLINE INITRD NEW_DIRECTORY\n", argv[0]);
        return(2);
    }

    if (read_header(argv[1], header, sizeof(header), &size, &info) != 0 ||
        linux_parse_header(header, size, info.st_size, &image) != 0) {

        fprintf(stderr, "Expected a relocatable x86-64 bzImage (boot protocol 2.12+).\n");

        return(1);
    }

    if (read_header(argv[2], command, sizeof(command), &size, &info) != 0 ||
        info.st_size > LINUX_CMDLINE_LIMIT + 2 ||

        linux_command_line(command, &size, sizeof(command), image.header.cmdline_size) != 0) {
        fprintf(stderr, "Expected a single ASCII kernel command line within the kernel limit.\n");

        return(1);
    }

    if (stat(argv[3], &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0 ||

        info.st_size > 512 * 1024 * 1024) {

        fprintf(stderr, "Expected a nonempty initramfs no larger than 512 MiB.\n");

        return(1);
    }
    if (make_parents(argv[4]) != 0 || (mkdir(argv[4], 0755) != 0 && errno != EEXIST)) {
        perror(argv[4]);

        return(1);
    }

    int directory = open(argv[4], O_RDONLY | O_DIRECTORY | O_NOFOLLOW);

    if (directory < 0 || flock(directory, LOCK_EX | LOCK_NB) != 0) {
        perror("Cannot lock staging directory");
        return(1);
    }

    /* Existing loader binaries are allowed, but never replace a live boot set. */
    const char *names[] = {"cmdline.txt", "initrd", "vmlinuz"};
    const char *sources[] = {argv[2], argv[3], argv[1]};

    for (size_t i = 0; i < 3; ++i) {
        if (fstatat(directory, names[i], &info, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT) {
            fprintf(stderr, "Refusing to replace existing boot file: %s\n", names[i]);
            return(1);
        }
    }

    if (path_join(temporary, sizeof(temporary), argv[4], ".staging-XXXXXX") != 0 ||
        !mkdtemp(temporary)) {
        perror("staging directory");
        return(1);
    }

    int result = 1;

    for (size_t i = 0; i < 3; ++i) {
        if (path_join(staged, sizeof(staged), temporary, names[i]) != 0 ||

            copy_image(sources[i], staged, 1) != 0) {

            goto cleanup;
        }
    }
    /* Validate the staged snapshots, not just the original paths. */

    if (path_join(staged, sizeof(staged), temporary, "vmlinuz") != 0 ||
        read_header(staged, header, sizeof(header), &size, &info) != 0 ||

        linux_parse_header(header, size, info.st_size, &image) != 0 ||

        path_join(staged, sizeof(staged), temporary, "cmdline.txt") != 0 ||

        read_header(staged, command, sizeof(command), &size, &info) != 0 ||

        info.st_size > LINUX_CMDLINE_LIMIT + 2 ||

        linux_command_line(command, &size, sizeof(command), image.header.cmdline_size) != 0) {
        fprintf(stderr, "Staged files failed validation.\n");

        goto cleanup;
    }

    /* Each rename publishes complete data; vmlinuz is the activation point. */
    if (path_join(staged, sizeof(staged), temporary, "initrd") != 0 || stat(staged, &info) != 0 ||
        !S_ISREG(info.st_mode) || info.st_size <= 0 || info.st_size > 512 * 1024 * 1024) {

        fprintf(stderr, "Staged initramfs failed validation.\n");

        goto cleanup;
    }

    for (size_t i = 0; i < 3; ++i) {

        if (path_join(staged, sizeof(staged), temporary, names[i]) != 0 ||
            path_join(target, sizeof(target), argv[4], names[i]) != 0 ||
            renameat2(AT_FDCWD, staged, AT_FDCWD, target, RENAME_NOREPLACE) != 0 ||

            fsync(directory) != 0) {
            perror("publish staged file");
            fprintf(stderr, "Staging incomplete; inspect the directory before retrying.\n");

            goto cleanup;
        }
    }

    result = 0;

cleanup:
    for (size_t i = 0; i < 3; ++i) {
        if (path_join(staged, sizeof(staged), temporary, names[i]) == 0) {
            unlink(staged); /* Only files in the private staging directory. */
        }
    }
    rmdir(temporary);

    fsync(directory);

    close(directory);

    if (result) {
        return(result);
    }

    puts("Direct Linux files staged. Kernel/initramfs compatibility is not verified.");

    return(0);
}
