/* Build a newc fixture without root privileges or host device nodes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void entry(FILE *out, unsigned ino, const char *name, unsigned mode,
                  const void *data, unsigned size, unsigned major, unsigned minor)
{
    unsigned namesize = strlen(name) + 1;
    fprintf(out, "070701%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
            ino, mode, 0, 0, 1, 0, size, 0, 0, major, minor, namesize, 0);
    fwrite(name, 1, namesize, out);
    for (unsigned i = 110 + namesize; i % 4; ++i) {
        fputc(0, out);
    }
    if (size) {
        fwrite(data, 1, size, out);
    }
    for (unsigned i = size; i % 4; ++i) {
        fputc(0, out);
    }
}

int main(int argc, char **argv)
{
    struct stat st;
    if (argc != 3 || stat(argv[1], &st) != 0 || st.st_size <= 0 || st.st_size > 16 * 1024 * 1024) {
        return (1);
    }
    FILE *in = fopen(argv[1], "rb");
    void *data = malloc(st.st_size);
    if (!in || !data || fread(data, 1, st.st_size, in) != (size_t)st.st_size) {
        return (1);
    }
    fclose(in);
    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        return (1);
    }
    entry(out, 1, "dev", 0040755, NULL, 0, 0, 0);
    entry(out, 2, "dev/console", 0020600, NULL, 0, 5, 1);
    entry(out, 3, "proc", 0040755, NULL, 0, 0, 0);
    entry(out, 4, "sys", 0040755, NULL, 0, 0, 0);
    entry(out, 5, "init", 0100755, data, st.st_size, 0, 0);
    entry(out, 6, "TRAILER!!!", 0, NULL, 0, 0, 0);
    free(data);
    int error = ferror(out);
    return (fclose(out) != 0 || error ? 1 : 0);
}
