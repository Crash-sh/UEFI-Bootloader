#ifndef NEUROS_TOOLS_H
#define NEUROS_TOOLS_H

#include <stddef.h>

int path_join(char *output, size_t size, const char *base, const char *name);
int validate_image(const char *path, int require_uki);
int make_parents(const char *path);
int copy_image(const char *source, const char *destination, int exclusive);

#endif
