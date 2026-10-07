#define _XOPEN_SOURCE 700
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int path_join(char *output, size_t size, const char *base, const char *name)
{
    int length = snprintf(output, size, "%s/%s", base, name);
    if (length < 0 || (size_t)length >= size) {
        fprintf(stderr, "Path too long\n");
        return (-1);
    }

    return (0);
}

static unsigned int le16(const unsigned char *data)
{
    return ((unsigned int)data[0] | (unsigned int)data[1] << 8);
}

static uint32_t le32(const unsigned char *data)
{
    return ((uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 |
            (uint32_t)data[3] << 24);
}

int validate_image(const char *path, int require_uki)
{
    const char names[4][9] = {".linux", ".initrd", ".cmdline", ".osrel"};

    unsigned char dos[64], pe[24], optional[70], section[40];

    struct stat info;

    FILE *stream = fopen(path, "rb");

    const char *error = "invalid or truncated PE header";

    unsigned int sections, optional_size, found = 0;
    uint64_t table, size;

    int result = -1;

    if (stream == NULL) {
        perror(path);
        return (-1);
    }

    if (fstat(fileno(stream), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 64) {
        goto done;
    }

    size = (uint64_t)info.st_size;

    if (fread(dos, 1, sizeof(dos), stream) != sizeof(dos) || memcmp(dos, "MZ", 2) != 0) {
        goto done;
    }

    table = le32(dos + 60);
    if (table + sizeof(pe) > size || fseeko(stream, (off_t)table, SEEK_SET) != 0 ||
        fread(pe, 1, sizeof(pe), stream) != sizeof(pe) || memcmp(pe, "PE\0\0", 4) != 0) {
        goto done;
    }

    sections = le16(pe + 6);
    optional_size = le16(pe + 20);
    error = "expected an x86-64 PE32+ EFI application";

    if (le16(pe + 4) != 0x8664 || optional_size < sizeof(optional) ||
        fread(optional, 1, sizeof(optional), stream) != sizeof(optional) ||
        le16(optional) != 0x20b || le16(optional + 68) != 10) {
        goto done;
    }

    table += sizeof(pe) + optional_size;
    error = "invalid or truncated section table";

    if (sections == 0 || table + (uint64_t)sections * sizeof(section) > size ||
        fseeko(stream, (off_t)table, SEEK_SET) != 0) {
        goto done;
    }

    for (unsigned int i = 0; i < sections; ++i) {
        uint32_t raw_size, offset;
        if (fread(section, 1, sizeof(section), stream) != sizeof(section)) {
            goto done;
        }

        raw_size = le32(section + 16);
        offset = le32(section + 20);
        error = "section extends past end of image";

        if (raw_size && (uint64_t)offset + raw_size > size) {
            goto done;
        }

        for (unsigned int j = 0; j < 4; ++j) {
            if (raw_size && memcmp(section, names[j], 8) == 0) {
                found |= 1u << j;
            }
        }
    }

    error = "UKI must contain kernel, initramfs, command line, and OS release sections";

    if (require_uki && found != 15) {
        goto done;
    }

    result = 0;
done:
    if (result != 0) {
        fprintf(stderr, "%s: %s\n", path, error);
    }

    fclose(stream);
    return (result);
}

int make_parents(const char *path)
{
    char buffer[PATH_MAX];
    struct stat info;

    if (strlen(path) >= sizeof(buffer)) {
        errno = ENAMETOOLONG;
        return (-1);
    }

    strcpy(buffer, path);

    for (char *cursor = buffer + 1; *cursor != '\0'; ++cursor) {
        if (*cursor != '/') {
            continue;
        }

        *cursor = '\0';

        if (mkdir(buffer, 0755) != 0 &&
            (errno != EEXIST || stat(buffer, &info) != 0 || !S_ISDIR(info.st_mode))) {
            perror(buffer);
            return (-1);
        }

        *cursor = '/';
    }

    return (0);
}

int copy_image(const char *source, const char *destination, int exclusive)
{
    unsigned char buffer[65536];

    char temporary[PATH_MAX];

    struct stat source_info, destination_info;

    int input = -1, output = -1, result = -1, created = 0;

    const char *output_path = destination;

    ssize_t count;

    input = open(source, O_RDONLY);

    if (input < 0 || fstat(input, &source_info) != 0) {
        goto done;
    }

    if (!S_ISREG(source_info.st_mode)) {
        errno = EINVAL;
        goto done;
    }

    if (stat(destination, &destination_info) == 0 &&
        source_info.st_dev == destination_info.st_dev &&
        source_info.st_ino == destination_info.st_ino) {
        errno = EINVAL;
        goto done;
    }

    if (make_parents(destination) != 0) {
        goto done;
    }

    if (exclusive) {
        output = open(destination, O_WRONLY | O_CREAT | O_EXCL, 0644);
    } else {
        int length = snprintf(temporary, sizeof(temporary), "%s.XXXXXX", destination);
        if (length < 0 || (size_t)length >= sizeof(temporary)) {
            errno = ENAMETOOLONG;
            goto done;
        }

        output_path = temporary;
        output = mkstemp(temporary);
    }

    if (output < 0) {
        goto done;
    }

    created = 1;

    for (;;) {
        count = read(input, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) {
            continue;
        }

        if (count < 0) {
            goto done;
        }

        if (count == 0) {
            break;
        }

        for (ssize_t written = 0; written < count;) {
            ssize_t amount = write(output, buffer + written, (size_t)(count - written));
            if (amount < 0 && errno == EINTR) {
                continue;
            }

            if (amount <= 0) {
                goto done;
            }

            written += amount;
        }
    }

    if (fsync(output) != 0) {
        goto done;
    }

    if (close(output) != 0) {
        output = -1;
        goto done;
    }

    output = -1;

    if (!exclusive && rename(temporary, destination) != 0) {
        goto done;
    }

    result = 0;

done:
    if (result != 0) {
        fprintf(stderr, "Copy %s -> %s: %s\n", source, destination, strerror(errno));
    }

    if (input >= 0) {
        close(input);
    }

    if (output >= 0) {
        close(output);
    }

    if (result != 0 && created) {
        unlink(output_path);
    }

    return (result);
}
