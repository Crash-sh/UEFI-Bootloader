#include "../uefi/linux_format.h"
#include "../uefi/boot_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct descriptor {
    uint32_t type, padding;
    uint64_t address, virtual_address, pages, attributes;
    uint64_t extra; /* Test firmware strides larger than the standard prefix. */
};

static void platform_tests(void)
{
    struct boot_params params = {0};
    struct descriptor map[1024] = {0};
    size_t capacity = sizeof(struct setup_data) + 1024 * sizeof(struct boot_e820_entry);
    struct setup_data *extension = calloc(1, capacity);

    assert(extension);

    for (size_t i = 0; i < 1024; ++i) {
        map[i].type = i % 2 ? 7 : 9;
        map[i].address = (1024 - i) * 4096;
        map[i].pages = 1;
    }

    assert(linux_memory_map(&params, map, sizeof(map), sizeof(map[0]), extension, capacity) == 0);
    assert(params.e820_entries == 128 && params.hdr.setup_data == (uintptr_t)extension);
    assert(extension->type == SETUP_E820_EXT && extension->next == 0);
    assert(extension->len == 896 * sizeof(struct boot_e820_entry));

    struct boot_e820_entry *extra = (void *)extension->data;

    for (size_t i = 0; i < 1024; ++i) {
        struct boot_e820_entry *entry = i < 128 ? &params.e820_table[i] : &extra[i - 128];
        assert(entry->addr == (i + 1) * 4096 && entry->size == 4096);
        assert(entry->type == (i % 2 ? 3 : 1));
    }
    /* Reused storage must clear the previous setup_data link when no longer needed. */

    for (size_t i = 0; i < 1024; ++i) {
        map[i].type = 7;
    }
    assert(linux_memory_map(&params, map, sizeof(map), sizeof(map[0]), extension, capacity) == 0);
    assert(params.e820_entries == 1 && params.hdr.setup_data == 0);
    assert(params.e820_table[0].size == 1024 * 4096);

    map[1].address = map[0].address;
    assert(linux_memory_map(&params, map, sizeof(map), sizeof(map[0]), extension, capacity) != 0);

    map[1].address = 1023 * 4096;
    map[0].pages = UINT64_MAX;

    assert(linux_memory_map(&params, map, sizeof(map), sizeof(map[0]), extension, capacity) != 0);

    map[0].pages = 1;

    assert(linux_memory_map(&params, map, sizeof(map) - 1, sizeof(map[0]), extension, capacity) !=
           0);
    assert(linux_memory_map(&params, map, sizeof(map), 32, extension, capacity) != 0);
    assert(linux_memory_map(&params, map, sizeof(map), sizeof(map[0]), extension, capacity - 1) !=
           0);

    map[0].attributes = 1ULL << 63;
    map[1].attributes = 0x40000;
    map[2].pages = 0;

    assert(linux_memory_map(&params, map, 3 * sizeof(map[0]), sizeof(map[0]), extension,
                            capacity) == 0);
    assert(params.e820_entries == 2 && params.e820_table[0].type == 12 &&
           params.e820_table[1].type == 2);

    free(extension);

    struct screen_info screen = {0};

    const uint32_t rgb[] = {0xff, 0xff00, 0xff0000, 0xff000000};
    const uint32_t rgb565[] = {0xf800, 0x7e0, 0x1f, 0};
    const uint32_t overlap[] = {0xff, 0xff, 0xff0000, 0xff000000};
    const uint32_t gaps[] = {0x55, 0xff00, 0xff0000, 0xff000000};

    assert(linux_framebuffer(&screen, 0x100000000ULL, 4096 * 768, 1024, 768, 1024, rgb) == 0);
    assert(screen.ext_lfb_base == 1 && screen.lfb_depth == 32 && screen.red_pos == 0);
    assert(linux_framebuffer(&screen, 0xe0000000, 2048 * 768, 1024, 768, 1024, rgb565) == 0);
    assert(screen.lfb_depth == 16 && screen.green_size == 6 && screen.red_pos == 11);
    assert(linux_framebuffer(&screen, 0xe0000000, 2048 * 768 - 1, 1024, 768, 1024, rgb565) != 0);
    assert(linux_framebuffer(&screen, 0xe0000000, 4096 * 768, 1024, 768, 1024, overlap) != 0);
    assert(linux_framebuffer(&screen, 0xe0000000, 4096 * 768, 1024, 768, 1024, gaps) != 0);
    assert(linux_framebuffer(&screen, UINT64_MAX - 4096, 8192, 32, 32, 32, rgb) != 0);
    assert(linux_framebuffer(&screen, 0xe0000000, UINT32_MAX, 65536, 768, 65536, rgb) != 0);
}

static void fixture(uint8_t *bytes)
{
    struct setup_header h = {0};

    h.setup_sects = 4;
    h.boot_flag = 0xaa55;
    h.jump = 0x66eb;
    h.header = 0x53726448;
    h.version = 0x20c;
    h.loadflags = LOADED_HIGH;
    h.xloadflags = XLF_KERNEL_64;
    h.relocatable_kernel = 1;
    h.kernel_alignment = 0x200000;
    h.pref_address = 0x1000000;
    h.init_size = 0x400000;
    h.syssize = 256;
    h.cmdline_size = 2047;

    memset(bytes, 0, 4096);
    memcpy(bytes + 0x1f1, &h, sizeof(h));
}

int main(void)
{
    struct boot_config config;

    const char valid[] = "NEUROS1\nset-123abc\nlegacy\n";

    assert(boot_config_parse(valid, sizeof(valid) - 1, &config) == 0);
    assert(strcmp(config.current, "set-123abc") == 0 && strcmp(config.previous, "legacy") == 0);

    for (size_t i = 0; i < sizeof(valid) - 1; ++i) {
        assert(boot_config_parse(valid, i, &config) != 0);
    }

    const char *invalid[] = {"NEUROS1\n../boot\n-\n",         "NEUROS1\n-\nlegacy\n",
                             "NEUROS1\nset-ABCDEF\n-\n",      "NEUROS1\nlegacy\nlegacy\n",
                             "NEUROS1\nset-abcdef\n-\nextra", "NEUROS1\nset-12345g\n-\n",
                             "NEUROS1\r\nset-abcdef\r\n-\r\n"};

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(boot_config_parse(invalid[i], strlen(invalid[i]), &config) != 0);
    }

    platform_tests();

    uint8_t bytes[4096];

    struct linux_image image;
    struct boot_params params = {0};

    uint64_t tables[6 * 512];

    size_t length;

    fixture(bytes);
    assert(linux_parse_header(bytes, sizeof(bytes), 6656, &image) == 0);
    assert(image.setup_size == 2560 && image.payload_size == 4096);

    for (size_t i = 0; i < 0x268; ++i) {
        assert(linux_parse_header(bytes, i, 6656, &image) != 0);
    }

    assert(linux_parse_header(bytes, sizeof(bytes), 6655, &image) != 0);
    assert(linux_parse_header(bytes, sizeof(bytes), UINT64_MAX, &image) != 0);

    const size_t corrupt[] = {0x200, 0x201, 0x1fe, 0x202, 0x206, 0x211, 0x234, 0x236};

    for (size_t i = 0; i < sizeof(corrupt) / sizeof(corrupt[0]); ++i) {
        fixture(bytes);
        bytes[corrupt[i]] = 0;
        assert(linux_parse_header(bytes, sizeof(bytes), 6656, &image) != 0);
    }

    fixture(bytes);

    bytes[0x230] = 1; /* not a power-of-two alignment */

    assert(linux_parse_header(bytes, sizeof(bytes), 6656, &image) != 0);

    fixture(bytes);
    memset(bytes + 0x260, 0xff, 4); /* oversized decompression allocation */

    assert(linux_parse_header(bytes, sizeof(bytes), 6656, &image) != 0);

    fixture(bytes);
    memset(bytes + 0x1f4, 0xff, 4); /* payload size multiplication */

    assert(linux_parse_header(bytes, sizeof(bytes), 6656, &image) != 0);

    memcpy(bytes, "root=UUID=test quiet\r\n", 22);

    length = 22;

    assert(linux_command_line(bytes, &length, sizeof(bytes), 2047) == 0);
    assert(length == 20 && strcmp((char *)bytes, "root=UUID=test quiet") == 0);

    length = 20;
    assert(linux_command_line(bytes, &length, sizeof(bytes), 18) != 0);

    bytes[4] = 0;

    assert(linux_command_line(bytes, &length, sizeof(bytes), 2047) != 0);

    bytes[4] = '\n';

    assert(linux_command_line(bytes, &length, sizeof(bytes), 2047) != 0);

    length = sizeof(bytes);

    assert(linux_command_line(bytes, &length, sizeof(bytes), 4095) != 0);

    length = 0;

    assert(linux_command_line(bytes, &length, sizeof(bytes), 2047) == 0 && bytes[0] == 0);

    assert(linux_add_memory(&params, 0x1000, 0x2000, 1) == 0);
    assert(linux_add_memory(&params, 0x3000, 0x1000, 1) == 0);
    assert(params.e820_entries == 1 && params.e820_table[0].size == 0x3000);
    assert(linux_add_memory(&params, 0x2000, 0x1000, 2) != 0);
    assert(linux_add_memory(&params, UINT64_MAX - 0x1000, 0x2000, 1) != 0);
    assert(linux_add_memory(&params, 0x4000, 0, 1) != 0);

    for (unsigned i = 1; i < 128; ++i) {
        assert(linux_add_memory(&params, 0x4000 + i * 0x2000, 0x1000, 2) == 0);
    }

    assert(linux_add_memory(&params, 0x200000, 0x1000, 2) != 0);

    linux_page_tables(tables, 0x100000);

    for (uint64_t address = 0; address < (1ULL << 32); address += 0x200000) {
        uint64_t pdpt = tables[0] & ~0xfffULL;
        uint64_t pd = tables[(pdpt - 0x100000) / 8 + (address >> 30)] & ~0xfffULL;
        uint64_t leaf = tables[(pd - 0x100000) / 8 + ((address >> 21) & 511)];
        assert((leaf & ~0x1fffffULL) == address && (leaf & 0x83) == 0x83);
    }

    assert(tables[1] == 0 && tables[516] == 0);

    puts("Linux header, command line, memory map, and page table checks passed.");

    return(0);
}
