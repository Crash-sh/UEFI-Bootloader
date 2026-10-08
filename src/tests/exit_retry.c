/* QEMU-only proxy: force one stale map-key result and observe the real exit. */
#include <efi.h>
#include <efilib.h>

static EFI_EXIT_BOOT_SERVICES original_exit;
static unsigned attempts;
#if defined(TEST_LARGE_MAP) || defined(TEST_GROW_MAP)
static EFI_GET_MEMORY_MAP original_map;
static EFI_STATUS EFIAPI large_map(UINTN *size, EFI_MEMORY_DESCRIPTOR *map, UINTN *key,
                                   UINTN *stride, UINT32 *version)
{
    UINTN supplied = *size;
    EFI_STATUS status = uefi_call_wrapper(original_map, 5, size, map, key, stride, version);
    if (status != EFI_SUCCESS && status != EFI_BUFFER_TOO_SMALL) {
        return(status);
    }
    UINTN extra = 2048 * *stride;
#ifdef TEST_GROW_MAP
    if (!attempts) {
        return(status);
    }
#endif
    if (status == EFI_BUFFER_TOO_SMALL || supplied - *size < extra) {
        *size += extra;
        return(EFI_BUFFER_TOO_SMALL);
    }
    /* Reserved, disjoint ranges above real RAM force a large E820 extension. */
    for (UINTN i = 0; i < 2048; ++i) {
        EFI_MEMORY_DESCRIPTOR *d = (VOID *)((UINT8 *)map + *size + i * *stride);
        SetMem(d, *stride, 0);
        d->Type = EfiReservedMemoryType;
        d->PhysicalStart = 0x1000000000ULL + i * 8192;
        d->NumberOfPages = 1;
    }
    *size += extra;
    return(EFI_SUCCESS);
}
#endif

static void serial(const char *message)
{
    while (*message) {
        unsigned char ready;
        unsigned tries = 100000;
        do {
            __asm__ volatile("inb %1, %0" : "=a"(ready) : "Nd"((unsigned short)0x3fd));
        } while (!(ready & 0x20) && --tries);
        __asm__ volatile("outb %0, %1" : : "a"(*message++), "Nd"((unsigned short)0x3f8));
    }
}

static EFI_STATUS EFIAPI retry_exit(EFI_HANDLE image, UINTN key)
{
    if (++attempts == 1) {
        /* Let firmware reject a deliberately stale key, exercising partial shutdown. */
        EFI_STATUS status = uefi_call_wrapper(original_exit, 2, image, key ^ 1);
        if (status == EFI_INVALID_PARAMETER) {
            serial("NEUROS: stale map key rejected\r\n");
        }
        return(status);
    }
    EFI_STATUS status = uefi_call_wrapper(original_exit, 2, image, key);
    if (!EFI_ERROR(status)) {
        serial("NEUROS: ExitBootServices retry succeeded\r\n");
    }
    return(status);
}

static VOID update_crc(VOID)
{
    BS->Hdr.CRC32 = 0;
    uefi_call_wrapper(BS->CalculateCrc32, 3, BS, BS->Hdr.HeaderSize, &BS->Hdr.CRC32);
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    EFI_LOADED_IMAGE *loaded;
    EFI_HANDLE child;
    InitializeLib(image, system);
    EFI_STATUS status =
        uefi_call_wrapper(BS->HandleProtocol, 3, image, &LoadedImageProtocol, (VOID **)&loaded);
    if (EFI_ERROR(status)) {
        return(status);
    }
    EFI_DEVICE_PATH *path = FileDevicePath(loaded->DeviceHandle, L"\\EFI\\NeurOS\\loader.efi");
    if (!path) {
        return(EFI_OUT_OF_RESOURCES);
    }
    status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, image, path, NULL, 0, &child);
    FreePool(path);
    if (EFI_ERROR(status)) {
        return(status);
    }
    original_exit = BS->ExitBootServices;
    BS->ExitBootServices = retry_exit;
#if defined(TEST_LARGE_MAP) || defined(TEST_GROW_MAP)
    original_map = BS->GetMemoryMap;
    BS->GetMemoryMap = large_map;
#endif
    update_crc();
    status = uefi_call_wrapper(BS->StartImage, 3, child, NULL, NULL);
    /* The tested loader must not returnafter attempting to exit boot services. */
    if (attempts) {
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }
    BS->ExitBootServices = original_exit;
#if defined(TEST_LARGE_MAP) || defined(TEST_GROW_MAP)
    BS->GetMemoryMap = original_map;
#endif
    update_crc();
    return(status);
}
