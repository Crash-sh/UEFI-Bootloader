#include "boot_config.h"

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return(*a == *b);
}

static int identifier(const unsigned char *data, size_t size, size_t *offset, char *out,
                      int previous)
{
    size_t length = 0;
    while (*offset < size && data[*offset] != '\n') {
        unsigned char c = data[(*offset)++];
        if (length == BOOT_SET_ID_MAX ||
            !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return(-1);
        }
        out[length++] = c;
    }
    out[length] = 0;
    if (*offset == size || !length) {
        return(-1);
    }
    ++*offset;
    if (equal(out, "legacy") || (previous && equal(out, "-"))) {
        return(0);
    }
    if (length < 10 || out[0] != 's' || out[1] != 'e' || out[2] != 't' || out[3] != '-') {
        return(-1);
    }
    for (size_t i = 4; i < length; ++i) {
        if (!((out[i] >= 'a' && out[i] <= 'f') || (out[i] >= '0' && out[i] <= '9'))) {
            return(-1);
        }
    }
    return(0);
}

int boot_config_parse(const void *data, size_t size, struct boot_config *config)
{
    const unsigned char *bytes = data;
    const char magic[] = "NEUROS1\n";
    struct boot_config result = {{0}, {0}};
    size_t offset = sizeof(magic) - 1;
    if (size < offset || size > BOOT_CONFIG_MAX) {
        return(-1);
    }
    for (size_t i = 0; i < offset; ++i) {
        if (bytes[i] != (unsigned char)magic[i]) {
            return(-1);
        }
    }
    if (identifier(bytes, size, &offset, result.current, 0) ||
        identifier(bytes, size, &offset, result.previous, 1) || offset != size ||
        equal(result.current, result.previous)) {
        return(-1);
    }
    *config = result;
    return(0);
}
