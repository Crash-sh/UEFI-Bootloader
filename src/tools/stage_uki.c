#include "common.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s SOURCE [DESTINATION]\n", argv[0]);
        return (2);
    }
    if (validate_image(argv[1], 1) != 0) {
        return (1);
    }

    if (argc == 3 && copy_image(argv[1], argv[2], 0) != 0) {
        return (1);
    }
    puts("UKI structure validated; signatures and boot configuration are not verified.");
    return (0);
}
