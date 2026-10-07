#define _XOPEN_SOURCE 700
#include "../tools/common.h"

#undef NDEBUG
#include <assert.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put16(unsigned char *p, unsigned int value)
{
    p[0] = value & 255;
    p[1] = (value >> 8) & 255;
}

static void put32(unsigned char *p, unsigned int value)
{
    for (int i = 0; i < 4; ++i) {
        p[i] = (value >> (8 * i)) & 255;
    }
}

static void fixture(unsigned char *data)
{
    const char *names[] = {".linux", ".initrd", ".cmdline", ".osrel"};

    memset(data, 0, 1024);
    memcpy(data, "MZ", 2);

    put32(data + 60, 64);

    memcpy(data + 64, "PE\0\0", 4);

    put16(data + 68, 0x8664);
    put16(data + 70, 4);
    put16(data + 84, 240);
    put16(data + 88, 0x20b);
    put16(data + 156, 10);

    for (int i = 0; i < 4; ++i) {
        unsigned char *section = data + 328 + 40 * i;
        memcpy(section, names[i], strlen(names[i]));
        put32(section + 16, 1);
        put32(section + 20, 512 + (unsigned int)i);
    }
}

static void write_file(const char *path, const unsigned char *data, size_t size)
{
    FILE *stream = fopen(path, "wb");
    assert(stream != NULL);
    assert(fwrite(data, 1, size, stream) == size);
    assert(fclose(stream) == 0);
}

static int remove_entry(const char *path, const struct stat *info, int type, struct FTW *walk)
{
    (void)info;
    (void)type;
    (void)walk;
    return (remove(path));
}

int main(void)
{
    char directory[] = "/tmp/neuros-tools-XXXXXX";
    char input[256], output[256], short_path[2];

    unsigned char data[1024], copied[1024];

    FILE *stream;

    assert(mkdtemp(directory) != NULL);
    assert(path_join(input, sizeof(input), directory, "source.efi") == 0);
    assert(path_join(output, sizeof(output), directory, "nested/output.efi") == 0);
    assert(path_join(short_path, sizeof(short_path), directory, "long") != 0);

    fixture(data);

    write_file(input, data, sizeof(data));

    assert(validate_image(input, 1) == 0);
    assert(copy_image(input, output, 0) == 0);

    stream = fopen(output, "rb");

    assert(stream != NULL);
    assert(fread(copied, 1, sizeof(copied), stream) == sizeof(copied));
    assert(fclose(stream) == 0);
    assert(memcmp(data, copied, sizeof(data)) == 0);
    assert(copy_image(input, output, 1) != 0);
    assert(copy_image(input, input, 0) != 0);

    data[900] = 42;

    write_file(input, data, sizeof(data));

    assert(copy_image(input, output, 0) == 0);
    assert(unlink(output) == 0);
    assert(copy_image(input, output, 1) == 0);
    assert(copy_image("/nonexistent/neuros-source", output, 0) != 0);
    assert(validate_image(output, 1) == 0);

    for (size_t size = 0; size < 488; ++size) {
        write_file(input, data, size);
        assert(validate_image(input, 1) != 0);
    }

    fixture(data);

    memcpy(data + 328, ".other\0\0", 8);

    write_file(input, data, sizeof(data));

    assert(validate_image(input, 1) != 0);
    assert(validate_image(input, 0) == 0);

    fixture(data);

    put16(data + 68, 0x14c);

    write_file(input, data, sizeof(data));

    assert(validate_image(input, 0) != 0);

    fixture(data);

    put16(data + 156, 3);

    write_file(input, data, sizeof(data));

    assert(validate_image(input, 0) != 0);

    fixture(data);
    put32(data + 348, 0xfffffff0);
    put32(data + 344, 32);

    write_file(input, data, sizeof(data));

    assert(validate_image(input, 1) != 0);

    fixture(data);

    put32(data + 60, 0xffffffff);

    write_file(input, data, sizeof(data));

    assert(validate_image(input, 1) != 0);
    assert(nftw(directory, remove_entry, 16, FTW_DEPTH | FTW_PHYS) == 0);

    puts("Native tool validation and file-copy checks passed.");

    return (0);
}
