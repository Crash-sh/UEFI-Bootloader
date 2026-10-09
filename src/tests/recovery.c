#include <efi.h>
#include <efilib.h>

static unsigned position;
static EFI_SIMPLE_TEXT_IN_PROTOCOL input;

#ifdef TEST_NO_GRAPHICS
static EFI_HANDLE_PROTOCOL original_handle;
static EFI_LOCATE_PROTOCOL original_locate;
static EFI_GUID graphics_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

static EFI_STATUS EFIAPI handle_protocol(EFI_HANDLE handle, EFI_GUID *guid, VOID **interface)
{
    if (CompareMem(guid, &graphics_guid, sizeof(*guid)) == 0) {
        return(EFI_NOT_FOUND);
    }
    return(uefi_call_wrapper(original_handle, 3, handle, guid, interface));
}

static EFI_STATUS EFIAPI locate_protocol(EFI_GUID *guid, VOID *registration, VOID **interface)
{
    if (CompareMem(guid, &graphics_guid, sizeof(*guid)) == 0) {
        return(EFI_NOT_FOUND);
    }
    return(uefi_call_wrapper(original_locate, 3, guid, registration, interface));
}
#endif

static EFI_STATUS EFIAPI read_key(EFI_SIMPLE_TEXT_IN_PROTOCOL *self, EFI_INPUT_KEY *key)
{
    (void)self;
    if (position >= 2) {
        return(EFI_NOT_READY);
    }
    key->ScanCode = 0;
    key->UnicodeChar = position++ == 0 ? L'm' : L'p';
    return(EFI_SUCCESS);
}

static EFI_STATUS EFIAPI reset(EFI_SIMPLE_TEXT_IN_PROTOCOL *self, BOOLEAN extended)
{
    (void)self;
    (void)extended;
    return(EFI_SUCCESS);
}

static VOID EFIAPI ready(EFI_EVENT event, VOID *context)
{
    (void)context;
    if (position < 2) {
        uefi_call_wrapper(BS->SignalEvent, 1, event);
    }
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
    EFI_SIMPLE_TEXT_IN_PROTOCOL *original = ST->ConIn;
    input.Reset = reset;
    input.ReadKeyStroke = read_key;
    status = uefi_call_wrapper(BS->CreateEvent, 5, EVT_NOTIFY_WAIT, TPL_CALLBACK, ready, NULL,
                               &input.WaitForKey);
    if (EFI_ERROR(status)) {
        return(status);
    }
    ST->ConIn = &input;
    ST->Hdr.CRC32 = 0;
    uefi_call_wrapper(BS->CalculateCrc32, 3, ST, ST->Hdr.HeaderSize, &ST->Hdr.CRC32);
#ifdef TEST_NO_GRAPHICS
    original_handle = BS->HandleProtocol;
    original_locate = BS->LocateProtocol;
    BS->HandleProtocol = handle_protocol;
    BS->LocateProtocol = locate_protocol;
    BS->Hdr.CRC32 = 0;
    uefi_call_wrapper(BS->CalculateCrc32, 3, BS, BS->Hdr.HeaderSize, &BS->Hdr.CRC32);
#endif
    status = uefi_call_wrapper(BS->StartImage, 3, child, NULL, NULL);
#ifdef TEST_NO_GRAPHICS
    BS->HandleProtocol = original_handle;
    BS->LocateProtocol = original_locate;
    BS->Hdr.CRC32 = 0;
    uefi_call_wrapper(BS->CalculateCrc32, 3, BS, BS->Hdr.HeaderSize, &BS->Hdr.CRC32);
#endif
    ST->ConIn = original;
    ST->Hdr.CRC32 = 0;
    uefi_call_wrapper(BS->CalculateCrc32, 3, ST, ST->Hdr.HeaderSize, &ST->Hdr.CRC32);
    uefi_call_wrapper(BS->CloseEvent, 1, input.WaitForKey);
    return(status);
}
