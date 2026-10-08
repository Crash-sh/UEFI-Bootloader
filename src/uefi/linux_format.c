#include "linux_format.h"

_Static_assert(sizeof(struct boot_params) == 4096, "Linux zero page ABI");
_Static_assert(offsetof(struct boot_params, hdr) == 0x1f1, "Linux setup header ABI");
_Static_assert(offsetof(struct boot_params, e820_table) == 0x2d0, "Linux e820 ABI");

int linux_parse_header(const uint8_t *bytes, size_t size, uint64_t file_size,
                       struct linux_image *image)
{
    size_t end, copy;

    *image = (struct linux_image){0};

    if (size < 0x268 || file_size < size || file_size > LINUX_KERNEL_LIMIT ||
        bytes[0x200] != 0xeb) {
        return(-1);
    }

    end = 0x202 + bytes[0x201];

    if (end < 0x268 || end > size || end > 0x290) {
        return(-1);
    }

    image->header_size = end - 0x1f1;
    copy = image->header_size;

    if (copy > sizeof(image->header)) {
        copy = sizeof(image->header);
    }

    for (size_t i = 0; i < copy; ++i) {
        ((uint8_t *)&image->header)[i] = bytes[0x1f1 + i];
    }

    const struct setup_header *h = &image->header;

    if (h->boot_flag != 0xaa55 || h->header != 0x53726448 || h->version < 0x20c ||
        !(h->loadflags & LOADED_HIGH) || !(h->xloadflags & XLF_KERNEL_64) ||
        !h->relocatable_kernel || h->hardware_subarch != 0 || h->kernel_alignment < 4096 ||
        h->kernel_alignment > LINUX_INIT_LIMIT ||
        (h->kernel_alignment & (h->kernel_alignment - 1)) || h->pref_address > UINT32_MAX ||
        !h->init_size || h->init_size > LINUX_INIT_LIMIT || !h->cmdline_size) {
        return(-1);
    }

    image->setup_size = ((size_t)(h->setup_sects ? h->setup_sects : 4) + 1) * 512;
    image->payload_size = (uint64_t)h->syssize * 16;

    if (image->setup_size < end || image->setup_size >= file_size || image->payload_size < 0x201 ||
        image->payload_size > file_size - image->setup_size || image->payload_size > h->init_size) {
        return(-1);
    }

    return(0);
}

int linux_command_line(uint8_t *bytes, size_t *size, size_t capacity, uint32_t limit)
{
    size_t length = *size;

    if (length >= capacity) {
        return(-1);
    }

    while (length && (bytes[length - 1] == '\n' || bytes[length - 1] == '\r')) {
        --length;
    }

    if (length > limit || length > LINUX_CMDLINE_LIMIT) {
        return(-1);
    }

    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] != '\t' && (bytes[i] < 0x20 || bytes[i] > 0x7e)) {
            return(-1);
        }
    }

    bytes[length] = 0;

    *size = length;

    return(0);
}

int linux_add_memory(struct boot_params *params, uint64_t address, uint64_t size, uint32_t type)
{
    unsigned count = params->e820_entries;

    if (!size || size > UINT64_MAX - address) {
        return(-1);
    }

    if (count) {
        struct boot_e820_entry *last = &params->e820_table[count - 1];
        uint64_t end = last->addr + last->size;

        if (address < end) {
            return(-1);
        }

        if (address == end && last->type == type) {
            last->size += size;
            return(0);
        }
    }

    if (count == E820_MAX_ENTRIES_ZEROPAGE) {
        return(-1);
    }
    params->e820_table[count] = (struct boot_e820_entry){address, size, type};
    params->e820_entries = count + 1;

    return(0);
}

void linux_page_tables(uint64_t *tables, uint64_t physical)
{
    /* PML4, PDPT, and four page directories: identity-map the first 4 GiB. */

    for (size_t i = 0; i < 6 * 512; ++i) {
        tables[i] = 0;
    }

    tables[0] = (physical + 4096) | 3;

    for (size_t i = 0; i < 4; ++i) {
        tables[512 + i] = (physical + (2 + i) * 4096) | 3;
    }

    for (size_t i = 0; i < 4 * 512; ++i) {
        tables[1024 + i] = ((uint64_t)i << 21) | 0x83;
    }
}

/* UEFI descriptor prefix, independent of firmware's possibly larger stride. */
struct firmware_memory {
    uint32_t type, padding;
    uint64_t physical, virtual_address, pages, attributes;
};
_Static_assert(sizeof(struct firmware_memory) == 40, "UEFI memory descriptor ABI");

static uint32_t memory_type(const struct firmware_memory *memory)
{
    if (memory->attributes & (1ULL << 63)) {
        return(2); /* Runtime memory must never be reclaimed as RAM. */
    }

    switch (memory->type) {

    case 1:
    case 2:
    case 3:
    case 4:
    case 7:
        return(memory->attributes & 0x40000 ? 12 : 1);
    case 8:
        return(5);
    case 9:
        return(3);
    case 10:
        return(4);
    case 14:
        return(7);
    default:
        return(2); /* Includes unaccepted and unknown memory types. */
    }
}

static void sift(struct boot_e820_entry *ranges, size_t root, size_t count)
{
    while (root < count / 2) {

        size_t child = root * 2 + 1;

        if (child + 1 < count && ranges[child].addr < ranges[child + 1].addr) {
            ++child;
        }

        if (ranges[root].addr >= ranges[child].addr) {
            break;
        }

        struct boot_e820_entry temp = ranges[root];

        ranges[root] = ranges[child];
        ranges[child] = temp;
        root = child;
    }
}

int linux_memory_map(struct boot_params *params, const void *map, size_t size, size_t stride,
                     struct setup_data *extension, size_t capacity)
{
    params->e820_entries = 0;
    params->hdr.setup_data = 0;

    if (stride < sizeof(struct firmware_memory) || !size || size % stride ||
        capacity < sizeof(*extension)) {
        return(-1);
    }

    size_t count = size / stride, used = 0;

    if (count > (capacity - sizeof(*extension)) / sizeof(struct boot_e820_entry)) {
        return(-1);
    }

    struct boot_e820_entry *ranges = (void *)extension->data;

    for (size_t i = 0; i < count; ++i) {
        struct firmware_memory memory;
        /* Descriptor stride need not preserve C alignment. */

        for (size_t j = 0; j < sizeof(memory); ++j) {
            ((uint8_t *)&memory)[j] = ((const uint8_t *)map)[i * stride + j];
        }

        if (!memory.pages) {
            continue;
        }

        if ((memory.physical & 4095) || memory.pages > (UINT64_MAX >> 12)) {
            return(-1);
        }

        uint64_t bytes = memory.pages << 12;

        if (bytes > UINT64_MAX - memory.physical) {
            return(-1);
        }

        ranges[used++] = (struct boot_e820_entry){memory.physical, bytes, memory_type(&memory)};
    }

    if (!used) {
        return(-1);
    }
    /* Heap sort: bounded O(n log n), no allocations and no recursion. */

    for (size_t i = used / 2; i; --i) {
        sift(ranges, i - 1, used);
    }

    for (size_t i = used; i > 1; --i) {
        struct boot_e820_entry temp = ranges[0];
        ranges[0] = ranges[i - 1];
        ranges[i - 1] = temp;
        sift(ranges, 0, i - 1);
    }

    size_t merged = 0;

    for (size_t i = 0; i < used; ++i) {
        if (merged) {
            struct boot_e820_entry *last = &ranges[merged - 1];
            uint64_t end = last->addr + last->size;
            if (ranges[i].addr < end) {
                return(-1);
            }
            if (ranges[i].addr == end && ranges[i].type == last->type) {
                last->size += ranges[i].size;
                continue;
            }
        }
        ranges[merged++] = ranges[i];
    }

    size_t normal = merged < E820_MAX_ENTRIES_ZEROPAGE ? merged : E820_MAX_ENTRIES_ZEROPAGE;

    for (size_t i = 0; i < normal; ++i) {
        params->e820_table[i] = ranges[i];
    }

    params->e820_entries = normal;

    extension->next = 0;
    extension->type = SETUP_E820_EXT;
    extension->len = (merged - normal) * sizeof(*ranges);

    if (extension->len) {
        for (size_t i = normal; i < merged; ++i) {
            ranges[i - normal] = ranges[i];
        }
        params->hdr.setup_data = (uintptr_t)extension;
    }

    return(0);
}

int linux_framebuffer(struct screen_info *screen, uint64_t base, uint64_t size, uint32_t width,
                      uint32_t height, uint32_t stride, const uint32_t masks[4])
{
    struct screen_info result = {0};
    uint8_t positions[4] = {0}, widths[4] = {0};
    uint32_t combined = 0, depth = 0;

    for (unsigned i = 0; i < 4; ++i) {
        uint32_t mask = masks[i];

        if ((!mask && i < 3) || (combined & mask)) {
            return(-1);
        }

        combined |= mask;

        if (!mask) {
            continue;
        }

        while (!(mask & 1)) {
            ++positions[i];
            mask >>= 1;
        }

        while (mask & 1) {
            ++widths[i];
            mask >>= 1;
        }

        if (mask) {
            return(-1); /* Non-contiguous channel. */
        }
        depth += widths[i];
    }

    /* Only unambiguous packed 16/24/32-bit formats, with no unnaed gaps */

    if ((depth != 16 && depth != 24 && depth != 32) ||
        combined != (uint32_t)(UINT32_MAX >> (32 - depth)) || !base || !width || !height ||
        width > UINT16_MAX || height > UINT16_MAX || stride < width ||
        stride > UINT16_MAX / (depth / 8)) {
        return(-1);
    }

    uint32_t pitch = stride * (depth / 8);
    uint64_t visible = (uint64_t)pitch * height;

    if (visible > size || size > UINT64_MAX - base) {
        return(-1);
    }

    result.orig_video_isVGA = VIDEO_TYPE_EFI;
    result.lfb_width = width;
    result.lfb_height = height;
    result.lfb_depth = depth;
    result.lfb_linelength = pitch;
    result.lfb_base = base;
    result.ext_lfb_base = base >> 32;
    result.lfb_size = visible;
    result.pages = 1;
    result.capabilities = VIDEO_CAPABILITY_SKIP_QUIRKS;

    if (result.ext_lfb_base) {
        result.capabilities |= VIDEO_CAPABILITY_64BIT_BASE;
    }

    result.red_pos = positions[0];
    result.red_size = widths[0];
    result.green_pos = positions[1];
    result.green_size = widths[1];
    result.blue_pos = positions[2];
    result.blue_size = widths[2];
    result.rsvd_pos = positions[3];
    result.rsvd_size = widths[3];

    *screen = result;

    return(0);
}
