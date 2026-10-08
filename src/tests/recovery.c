#include <efi.h>
#include <efilib.h>

static unsigned position;
static EFI_SIMPLE_TEXT_IN_PROTOCOL input;

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
    status = uefi_call_wrapper(BS->StartImage, 3, child, NULL, NULL);
    ST->ConIn = original;
    ST->Hdr.CRC32 = 0;
    uefi_call_wrapper(BS->CalculateCrc32, 3, ST, ST->Hdr.HeaderSize, &ST->Hdr.CRC32);
    uefi_call_wrapper(BS->CloseEvent, 1, input.WaitForKey);
    return(status);
}
