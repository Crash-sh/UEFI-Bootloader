#ifndef NEUROS_LINUX_FORMAT_H
#define NEUROS_LINUX_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <asm/bootparam.h>

#define LINUX_KERNEL_LIMIT (128U * 1024 * 1024)
#define LINUX_INIT_LIMIT (512U * 1024 * 1024)
#define LINUX_CMDLINE_LIMIT 4095U

struct linux_image {
    struct setup_header header;
    size_t header_size;
    size_t setup_size;
    size_t payload_size;
};

/* Parse only the initial header buffer; file_size describes the entire file. */
int linux_parse_header(const uint8_t *bytes, size_t size, uint64_t file_size,
                       struct linux_image *image);
int linux_command_line(uint8_t *bytes, size_t *size, size_t capacity, uint32_t limit);
/* Append sorted, non-overlapping firmware ranges, coalescing adjacent types. */
int linux_add_memory(struct boot_params *params, uint64_t address, uint64_t size,
                     uint32_t type);
void linux_page_tables(uint64_t *tables, uint64_t physical);
/* Firmware-independent conversion, including SETUP_E820_EXT overflow entries. */
int linux_memory_map(struct boot_params *params, const void *map, size_t size,
                     size_t stride, struct setup_data *extension, size_t capacity);
int linux_framebuffer(struct screen_info *screen, uint64_t base, uint64_t size,
                      uint32_t width, uint32_t height, uint32_t stride,
                      const uint32_t masks[4]);

#endif
