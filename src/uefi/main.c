#include "loader.h"

static EFI_STATUS wait_key(EFI_INPUT_KEY *key)
{
    EFI_STATUS status;
    UINTN index;

    if (ST->ConIn == NULL) {
        return (EFI_UNSUPPORTED);
    }

    do {
        status = uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &index);
        if (EFI_ERROR(status)) {
            return (status);
        }

        status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, key);
    } while (status == EFI_NOT_READY);

    return (status);
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    EFI_STATUS status;
    EFI_INPUT_KEY key;
    CHAR16 *path = L"\\EFI\\Linux\\arch-linux.efi";
    EFI_STATUS splash_status;
    CHAR16 *operation;
    BOOLEAN cursor;
    UINTN attribute;

    InitializeLib(image, system);
    cursor = ST->ConOut->Mode->CursorVisible;
    attribute = ST->ConOut->Mode->Attribute;

    for (;;) {
        splash_status = show_splash();
        if (EFI_ERROR(splash_status)) {
            show_boot_fallback(splash_status);
            uefi_call_wrapper(BS->Stall, 1, 2000000);
        }

        status = load_image(image, path, &operation);

        show_recovery(path, operation, status);

        for (;;) {
            if (EFI_ERROR(wait_key(&key))) {
                goto finish;
            }

            if (key.ScanCode == SCAN_ESC) {
                goto finish;
            }

            if (key.UnicodeChar == L'\r') {
                path = L"\\EFI\\Linux\\arch-linux.efi";
                break;
            }

            if (key.UnicodeChar == L'r' || key.UnicodeChar == L'R') {
                path = L"\\EFI\\systemd\\systemd-bootx64.efi";
                break;
            }
        }
    }

finish:
    uefi_call_wrapper(ST->ConOut->SetAttribute, 2, ST->ConOut, attribute);
    uefi_call_wrapper(ST->ConOut->EnableCursor, 2, ST->ConOut, cursor);
    
    return (status);
}
