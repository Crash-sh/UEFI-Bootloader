#ifndef NEUROS_BOOT_CONFIG_H
#define NEUROS_BOOT_CONFIG_H

#include <stddef.h>

#define BOOT_SET_ID_MAX 48
#define BOOT_CONFIG_MAX 128

struct boot_config {
    char current[BOOT_SET_ID_MAX + 1];
    char previous[BOOT_SET_ID_MAX + 1];
};

/* Strict ASCII: NEUROS1\n<current>\n<previous or ->\n. No paths or whitespace. */
int boot_config_parse(const void *data, size_t size, struct boot_config *config);

#endif
