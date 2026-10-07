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
    CHAR16 *operation;
    BOOLEAN cursor;

    InitializeLib(image, system);
    cursor = ST->ConOut->Mode->CursorVisible;

    for (;;) {
        if (EFI_ERROR(show_splash())) {
            uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
            uefi_call_wrapper(ST->ConOut->EnableCursor, 2, ST->ConOut, cursor);
            Print(L"NeurOS\r\nBooting %s\r\n", path);
        }

        status = load_image(image, path, &operation);

        uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
        uefi_call_wrapper(ST->ConOut->EnableCursor, 2, ST->ConOut, cursor);
        Print(L"NeurOS\r\n%s\r\n%s: %r\r\n", path, operation, status);

        if (status == EFI_NOT_FOUND) {
            Print(L"Target missing on this loader's EFI System Partition.\r\n");
        }

        if (status == EFI_SECURITY_VIOLATION || status == EFI_ACCESS_DENIED) {
            Print(L"Firmware refused the image. Check Secure Boot signatures.\r\n");
        }

        Print(L"[Enter] Retry Arch  [R] systemd-boot  [Esc] Return to firmware\r\n");

        for (;;) {
            if (EFI_ERROR(wait_key(&key))) {
                return (status);
            }

            if (key.ScanCode == SCAN_ESC) {
                return (status);
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
}
