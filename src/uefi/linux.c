#include "loader.h"
#include "linux_format.h"
#include "boot_config.h"

#define LOW_MAX 0xffffffffULL
#define MAP_LIMIT (4U * 1024 * 1024)
#define MAP_SLACK (32U * 1024)
#define INITRD_LIMIT (512U * 1024 * 1024)

extern const UINT8 linux_handoff_start[], linux_handoff_end[];

struct allocation {
    EFI_PHYSICAL_ADDRESS address;
    UINTN pages;
};

static EFI_STATUS allocate(struct allocation *memory, UINTN bytes, EFI_PHYSICAL_ADDRESS maximum,
                           EFI_MEMORY_TYPE type)
{
    EFI_STATUS status;
    memory->address = maximum;
    memory->pages = EFI_SIZE_TO_PAGES(bytes);
    status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateMaxAddress, type, memory->pages,
                               &memory->address);
    if (EFI_ERROR(status)) {
        memory->pages = 0;
    } else if (memory->address < 0x100000) {
        uefi_call_wrapper(BS->FreePages, 2, memory->address, memory->pages);
        memory->pages = 0;
        status = EFI_OUT_OF_RESOURCES;
    } else {
        SetMem((VOID *)(UINTN)memory->address, memory->pages * EFI_PAGE_SIZE, 0);
    }
    return(status);
}

static EFI_STATUS open_file(EFI_FILE_HANDLE root, CHAR16 *path, UINTN limit, EFI_FILE_HANDLE *file,
                            UINTN *size)
{
    EFI_GUID info_guid = EFI_FILE_INFO_ID;
    union {
        EFI_FILE_INFO info;
        UINT8 bytes[512];
    } info = {0};
    UINTN info_size = sizeof(info);
    EFI_STATUS status = uefi_call_wrapper(root->Open, 5, root, file, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        return(status);
    }
    status = uefi_call_wrapper((*file)->GetInfo, 4, *file, &info_guid, &info_size, &info);
    if (!EFI_ERROR(status) &&
        (info_size > sizeof(info) || info_size < SIZE_OF_EFI_FILE_INFO ||
         info.info.Size < SIZE_OF_EFI_FILE_INFO || info.info.Size > info_size ||
         (info.info.Attribute & EFI_FILE_DIRECTORY) || info.info.FileSize > limit)) {
        status = EFI_BAD_BUFFER_SIZE;
    }
    if (EFI_ERROR(status)) {
        uefi_call_wrapper((*file)->Close, 1, *file);
        *file = NULL;
    } else {
        *size = info.info.FileSize;
    }
    return(status);
}

static EFI_STATUS read_exact(EFI_FILE_HANDLE file, VOID *buffer, UINTN size)
{
    while (size) {
        UINTN count = size > 1024 * 1024 ? 1024 * 1024 : size;
        EFI_STATUS status = uefi_call_wrapper(file->Read, 3, file, &count, buffer);
        if (EFI_ERROR(status)) {
            return(status);
        }
        if (!count || count > size || count > 1024 * 1024) {
            return(EFI_LOAD_ERROR);
        }
        buffer = (UINT8 *)buffer + count;
        size -= count;
    }
    return(EFI_SUCCESS);
}

static EFI_STATUS select_boot_set(EFI_FILE_HANDLE *root, BOOLEAN previous, BOOLEAN *configured)
{
    EFI_FILE_HANDLE file = NULL, directory = NULL;
    UINT8 data[BOOT_CONFIG_MAX];
    UINTN size;
    struct boot_config config;
    *configured = FALSE;
    EFI_STATUS status = open_file(*root, L"boot.conf", sizeof(data), &file, &size);
    if (status == EFI_NOT_FOUND) {
        return(previous ? EFI_NOT_FOUND : EFI_SUCCESS);
    }
    *configured = TRUE;
    if (EFI_ERROR(status)) {
        return(status);
    }
    status = read_exact(file, data, size);
    uefi_call_wrapper(file->Close, 1, file);
    if (EFI_ERROR(status)) {
        return(status);
    }
    if (boot_config_parse(data, size, &config) != 0) {
        return(EFI_LOAD_ERROR);
    }
    const char *id = previous ? config.previous : config.current;
    if (id[0] == '-') {
        return(EFI_NOT_FOUND);
    }
    if (id[0] == 'l') { /* The parser only accepts "legacy" here. */
        return(EFI_SUCCESS);
    }
    CHAR16 path[BOOT_SET_ID_MAX + 1];
    UINTN i = 0;
    do {
        path[i] = id[i];
    } while (id[i++]);
    status = uefi_call_wrapper((*root)->Open, 5, *root, &directory, path, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(status)) {
        uefi_call_wrapper((*root)->Close, 1, *root);
        *root = directory;
    }
    return(status);
}

static EFI_STATUS allocate_kernel(struct allocation *memory, const struct setup_header *h,
                                  EFI_PHYSICAL_ADDRESS *kernel)
{
    EFI_STATUS status;
    UINT64 alignment = h->kernel_alignment;
    UINT64 preferred = h->pref_address;
    if (preferred >= 0x100000 && !(preferred & (alignment - 1)) &&
        preferred + h->init_size <= LOW_MAX + 1) {
        memory->address = preferred;
        memory->pages = EFI_SIZE_TO_PAGES(h->init_size);
        status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAddress, EfiLoaderCode,
                                   memory->pages, &memory->address);
        if (!EFI_ERROR(status)) {
            *kernel = memory->address;
            SetMem((VOID *)(UINTN)*kernel, memory->pages * EFI_PAGE_SIZE, 0);
            return(EFI_SUCCESS);
        }
        memory->pages = 0;
    }
    status = allocate(memory, (UINTN)h->init_size + alignment - 1, LOW_MAX, EfiLoaderCode);
    if (EFI_ERROR(status)) {
        return(status);
    }
    *kernel = (memory->address + alignment - 1) & ~(alignment - 1);
    /* Loading below pref_address makes the decompressor relocate outside our allocation. */
    if (*kernel < preferred) {
        return(EFI_OUT_OF_RESOURCES);
    }
    return(EFI_SUCCESS);
}

static VOID platform_info(struct boot_params *params)
{
    EFI_GUID acpi2 = ACPI_20_TABLE_GUID, acpi1 = ACPI_TABLE_GUID;
    EFI_GUID graphics = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
    struct screen_info *screen = &params->screen_info;

    for (UINTN i = 0; i < ST->NumberOfTableEntries; ++i) {
        EFI_CONFIGURATION_TABLE *table = &ST->ConfigurationTable[i];
        if (CompareMem(&table->VendorGuid, &acpi2, sizeof(acpi2)) == 0) {
            params->acpi_rsdp_addr = (UINTN)table->VendorTable;
            break;
        }
        if (CompareMem(&table->VendorGuid, &acpi1, sizeof(acpi1)) == 0) {
            params->acpi_rsdp_addr = (UINTN)table->VendorTable;
        }
    }
    EFI_STATUS status =
        uefi_call_wrapper(BS->HandleProtocol, 3, ST->ConsoleOutHandle, &graphics, (VOID **)&gop);
    if (EFI_ERROR(status)) {
        status = uefi_call_wrapper(BS->LocateProtocol, 3, &graphics, NULL, (VOID **)&gop);
    }
    if (EFI_ERROR(status) || !gop->Mode || !gop->Mode->Info) {
        return
    }
    info = gop->Mode->Info;
    UINT32 masks[4];
    if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
        masks[0] = 0xff;
        masks[1] = 0xff00;
        masks[2] = 0xff0000;
        masks[3] = 0xff000000;
    } else if (info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
        masks[0] = 0xff0000;
        masks[1] = 0xff00;
        masks[2] = 0xff;
        masks[3] = 0xff000000;
    } else if (info->PixelFormat == PixelBitMask) {
        masks[0] = info->PixelInformation.RedMask;
        masks[1] = info->PixelInformation.GreenMask;
        masks[2] = info->PixelInformation.BlueMask;
        masks[3] = info->PixelInformation.ReservedMask;
    } else {
        return
    }
    /* Never switch modes or touch pixels here: retain the completed splash. */
    linux_framebuffer(screen, gop->Mode->FrameBufferBase, gop->Mode->FrameBufferSize,
                      info->HorizontalResolution, info->VerticalResolution, info->PixelsPerScanLine,
                      masks);
}

static EFI_STATUS memory_info(struct boot_params *params, VOID *map, UINTN size, UINTN stride,
                              UINT32 version, struct setup_data *extension, UINTN capacity)
{
    if (version != EFI_MEMORY_DESCRIPTOR_VERSION ||
        linux_memory_map(params, map, size, stride, extension, capacity) != 0) {
        return(EFI_LOAD_ERROR);
    }
    params->efi_info.efi_loader_signature = 0x34364c45; /* EL64 */
    params->efi_info.efi_systab = (UINT32)(UINTN)ST;
    params->efi_info.efi_systab_hi = (UINT64)(UINTN)ST >> 32;
    params->efi_info.efi_memmap = (UINT32)(UINTN)map;
    params->efi_info.efi_memmap_hi = (UINT64)(UINTN)map >> 32;
    params->efi_info.efi_memmap_size = size;
    params->efi_info.efi_memdesc_size = stride;
    params->efi_info.efi_memdesc_version = version;
    return(EFI_SUCCESS);
}

static VOID __attribute__((noreturn) stop_after_exit(VOID)
{
    /* Even a failed first ExitBootServices can partially shut down firmware.
     * Never returnto the recovery UI or call console/file protocols here. */
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

EFI_STATUS load_linux(EFI_HANDLE parent, BOOLEAN *present, CHAR16 **operation, BOOLEAN previous)
{
    EFI_LOADED_IMAGE *loaded;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *filesystem;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_FILE_HANDLE root = NULL, file = NULL;
    struct allocation memory[7] = {{0}};
    enum { KERNEL, PARAMS, CMDLINE, INITRD, TABLES, TRAMPOLINE, MAP };
    struct linux_image image;
    struct boot_params *params;
    UINT8 header[4096];
    UINTN size, header_size, cr4, map_size, key, stride, map_capacity = 0;
    BOOLEAN secure, configured;
    UINT32 version;
    EFI_PHYSICAL_ADDRESS kernel;
    EFI_STATUS status;

    *present = TRUE;
    *operation = L"Linux Secure Boot";
    status = secure_boot_state(&secure);
    if (!EFI_ERROR(status) && secure) {
        status = EFI_SECURITY_VIOLATION;
    }
    if (EFI_ERROR(status)) {
        return(status);
    }
    *operation = L"Linux filesystem";
    status =
        uefi_call_wrapper(BS->HandleProtocol, 3, parent, &LoadedImageProtocol, (VOID **)&loaded);
    if (EFI_ERROR(status)) {
        return(status);
    }
    status = uefi_call_wrapper(BS->HandleProtocol, 3, loaded->DeviceHandle, &fs_guid,
                               (VOID **)&filesystem);
    if (EFI_ERROR(status)) {
        return(status);
    }
    status = uefi_call_wrapper(filesystem->OpenVolume, 2, filesystem, &root);
    if (EFI_ERROR(status)) {
        return(status);
    }
    *operation = L"Linux boot directory";
    EFI_FILE_HANDLE directory;
    status =
        uefi_call_wrapper(root->Open, 5, root, &directory, L"\\EFI\\NeurOS", EFI_FILE_MODE_READ, 0);
    if (status == EFI_NOT_FOUND && !previous) {
        *present = FALSE;
    }
    if (EFI_ERROR(status)) {
        goto cleanup;
    }
    uefi_call_wrapper(root->Close, 1, root);
    root = directory;
    *operation = previous ? L"Previous Linux boot set" : L"Linux boot configuration";
    status = select_boot_set(&root, previous, &configured);
    if (EFI_ERROR(status)) {
        goto cleanup;
    }
    *operation = L"Linux kernel";
    status = open_file(root, L"vmlinuz", LINUX_KERNEL_LIMIT, &file, &size);
    if (status == EFI_NOT_FOUND && !configured && !previous) {
        *present = FALSE;
    }
    if (EFI_ERROR(status)) {
        goto cleanup;
    }
    *operation = L"Linux paging mode";
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if (cr4 & (1UL << 12)) { /* LA57 needs a different transition trampoline. */
        status = EFI_UNSUPPORTED;
        goto cleanup;
    }
    *operation = L"Linux header";
    header_size = size < sizeof(header) ? size : sizeof(header);
    status = read_exact(file, header, header_size);
    if (EFI_ERROR(status)) {
        goto cleanup;
    }
    if (linux_parse_header(header, header_size, size, &image) != 0) {
        status = EFI_LOAD_ERROR;
        goto cleanup;
    }
    *operation = L"Linux allocation";
    status = allocate_kernel(&memory[KERNEL], &image.header, &kernel);
    if (EFI_ERROR(status)) {
        goto cleanup;
    }
    status = allocate(&memory[PARAMS], sizeof(*params), LOW_MAX, EfiLoaderData);
    if (EFI_ERROR(status)) {
        goto cleanup;
    }
    params = (VOID *)(UINTN)memory[PARAMS].address;
    CopyMem(&params->hdr, header + 0x1f1, image.header_size);
    params->hdr.type_of_loader = 0xff;
    params->hdr.ext_loader_type = params->hdr.ext_loader_ver = 0;
    params->hdr.loadflags = (params->hdr.loadflags & LOADED_HIGH) | QUIET_FLAG;
    params->hdr.code32_start = kernel;
    params->hdr.vid_mode = 0xffff;
    params->hdr.setup_data = 0;
    params->hdr.ramdisk_image = params->hdr.ramdisk_size = 0;
    params->hdr.heap_end_ptr = 0;
    params->hdr.realmode_swtch = params->hdr.bootsect_kludge = 0;
    params->secure_boot = 2; /* Linux efi_secureboot_mode_disabled, checked above. */
    *operation = L"Linux payload";
    status = uefi_call_wrapper(file->SetPosition, 2, file, image.setup_size);
    if (!EFI_ERROR(status)) {
        status = read_exact(file, (VOID *)(UINTN)kernel, image.payload_size);
    }
    if (EFI_ERROR(status)) {
        goto cleanup;
    }
    uefi_call_wrapper(file->Close, 1, file);
    file = NULL;

    *operation = L"Linux cmdline.txt";
    status = open_file(root, L"cmdline.txt", LINUX_CMDLINE_LIMIT + 2, &file, &size);
    if (EFI_ERROR(status)) {
        goto cleanup;
    }
    status = allocate(&memory[CMDLINE], LINUX_CMDLINE_LIMIT + 3, LOW_MAX, EfiLoaderData);
    if (EFI_ERROR(status)) {
        goto cleanup;
    }
    UINT8 *command = (VOID *)(UINTN)memory[CMDLINE].address;
    status = read_exact(file, command, size);
    if (EFI_ERROR(status)) {
        goto cleanup;
    }
    size_t length = size;
    if (linux_command_line(command, &length, memory[CMDLINE].pages * EFI_PAGE_SIZE,
                           image.header.cmdline_size) != 0) {
        status = EFI_INVALID_PARAMETER;
        goto cleanup;
    }
    params->hdr.cmd_line_ptr = memory[CMDLINE].address;
    uefi_call_wrapper(file->Close, 1, file);
    file = NULL;

    *operation = L"Linux initrd";
    status = open_file(root, L"initrd", INITRD_LIMIT, &file, &size);
    /* Versioned sets always include an initrd. Never boot a damaged set without it. */
    if (status == EFI_NOT_FOUND && configured) {
        goto cleanup;
    }
    if (status != EFI_NOT_FOUND) {
        if (EFI_ERROR(status)) {
            goto cleanup;
        }
        if (!size) {
            status = EFI_LOAD_ERROR;
            goto cleanup;
        }
        status = allocate(&memory[INITRD], size, image.header.initrd_addr_max, EfiLoaderData);
        if (EFI_ERROR(status)) {
            goto cleanup;
        }
        status = read_exact(file, (VOID *)(UINTN)memory[INITRD].address, size);
        if (EFI_ERROR(status)) {
            goto cleanup;
        }
        params->hdr.ramdisk_image = memory[INITRD].address;
        params->hdr.ramdisk_size = size;
        uefi_call_wrapper(file->Close, 1, file);
        file = NULL;
    }
    uefi_call_wrapper(root->Close, 1, root);
    root = NULL;

    *operation = L"Linux handoff allocation";
    status = allocate(&memory[TABLES], 6 * EFI_PAGE_SIZE, LOW_MAX, EfiLoaderData);
    if (EFI_ERROR(status)) {
        goto cleanup;
    }
    /* First page is code/GDT; remaining four pages are the transition stack. */
    status = allocate(&memory[TRAMPOLINE], 5 * EFI_PAGE_SIZE, LOW_MAX, EfiLoaderCode);
    if (EFI_ERROR(status)) {
        goto cleanup;
    }
    if ((UINTN)(linux_handoff_end - linux_handoff_start) > EFI_PAGE_SIZE) {
        status = EFI_LOAD_ERROR;
        goto cleanup;
    }
    CopyMem((VOID *)(UINTN)memory[TRAMPOLINE].address, (VOID *)linux_handoff_start,
            linux_handoff_end - linux_handoff_start);
    linux_page_tables((VOID *)(UINTN)memory[TABLES].address, memory[TABLES].address);
    platform_info(params);
    *operation = L"Linux watchdog";
    status = uefi_call_wrapper(BS->SetWatchdogTimer, 4, 0, 0, 0, NULL);
    if (EFI_ERROR(status) && status != EFI_UNSUPPORTED) {
        goto cleanup;
    }

    *operation = L"Linux memory map";
    BOOLEAN exit_attempted = FALSE;
    for (UINTN attempt = 0; attempt < 8; ++attempt) {
        map_size = map_capacity;
        status = uefi_call_wrapper(BS->GetMemoryMap, 5, &map_size,
                                   (VOID *)(UINTN)memory[MAP].address, &key, &stride, &version);
        if (status == EFI_BUFFER_TOO_SMALL) {
            if (map_size > MAP_LIMIT - MAP_SLACK) {
                status = EFI_OUT_OF_RESOURCES;
                if (exit_attempted) {
                    stop_after_exit();
                }
                goto cleanup;
            }
            if (memory[MAP].pages) {
                uefi_call_wrapper(BS->FreePages, 2, memory[MAP].address, memory[MAP].pages);
                memory[MAP].pages = 0;
            }
            map_capacity = EFI_SIZE_TO_PAGES(map_size + MAP_SLACK) * EFI_PAGE_SIZE;
            /* Raw map plus enough sorted E820 storage for every possible descriptor. */
            status = allocate(&memory[MAP], 2 * map_capacity, LOW_MAX, EfiLoaderData);
            if (EFI_ERROR(status)) {
                if (exit_attempted) {
                    stop_after_exit();
                }
                goto cleanup;
            }
            continue;
        }
        if (!EFI_ERROR(status)) {
            struct setup_data *extension = (VOID *)(UINTN)(memory[MAP].address + map_capacity);
            status = memory_info(params, (VOID *)(UINTN)memory[MAP].address, map_size, stride,
                                 version, extension, map_capacity);
        }
        if (EFI_ERROR(status)) {
            if (exit_attempted) {
                stop_after_exit();
            }
            goto cleanup;
        }
        exit_attempted = TRUE;
        /* No firmware allocations, logging, or protocol calls between map and exit. */
        status = uefi_call_wrapper(BS->ExitBootServices, 2, parent, key);
        if (!EFI_ERROR(status)) {
            typedef VOID (*handoff_fn)(UINT64, VOID *, UINT64, UINT64);
            handoff_fn handoff = (handoff_fn)(UINTN)memory[TRAMPOLINE].address;
            handoff(kernel + 0x200, params, memory[TABLES].address,
                    memory[TRAMPOLINE].address + 5 * EFI_PAGE_SIZE);
            stop_after_exit();
        }
        if (status != EFI_INVALID_PARAMETER) {
            stop_after_exit();
        }
    }
    if (exit_attempted) {
        stop_after_exit();
    }
    status = EFI_ABORTED;

cleanup:
    if (file) {
        uefi_call_wrapper(file->Close, 1, file);
    }
    if (root) {
        uefi_call_wrapper(root->Close, 1, root);
    }
    for (UINTN i = 0; i < sizeof(memory) / sizeof(memory[0]); ++i) {
        if (memory[i].pages) {
            uefi_call_wrapper(BS->FreePages, 2, memory[i].address, memory[i].pages);
        }
    }
    return(status);
}
