#include "loader.h"

static EFI_STATUS wait_key(EFI_INPUT_KEY *key)
{
    EFI_STATUS status;
    UINTN index;

    if (ST->ConIn == NULL) {
        return(EFI_UNSUPPORTED);
    }

    do {
        status = uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &index);
        if (EFI_ERROR(status)) {
            return(status);
        }

        status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, key);
    } while (status == EFI_NOT_READY);

    return(status);
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    EFI_STATUS status;
    EFI_INPUT_KEY key;
    CHAR16 *path = UKI_PATH;
    EFI_STATUS splash_status;
    CHAR16 *operation;
    BOOLEAN cursor;
    UINTN attribute;
    BOOLEAN firmware_image = FALSE, direct, secure, previous = FALSE, recovery;

    InitializeLib(image, system);
    cursor = ST->ConOut && ST->ConOut->Mode ? ST->ConOut->Mode->CursorVisible : FALSE;
    attribute = ST->ConOut && ST->ConOut->Mode ? ST->ConOut->Mode->Attribute : 0;
    /* A recovery screen must not reset unexpectedly after the UEFI watchdog expires. */
    uefi_call_wrapper(BS->SetWatchdogTimer, 4, 0, 0, 0, NULL);

    for (;;) {
        splash_status = show_splash(&recovery);
        if (EFI_ERROR(splash_status)) {
            show_boot_fallback(splash_status);
            uefi_call_wrapper(BS->Stall, 1, 2000000);
        }

        if (recovery) {
            operation = L"Boot paused by user";
            status = EFI_NOT_READY;
        } else if (firmware_image) {
            status = load_image(image, path, &operation);
        } else if (previous) {
            path = L"\\EFI\\NeurOS\\boot.conf (previous)";
            status = load_linux(image, &direct, &operation, TRUE);
        } else {
            operation = L"Secure Boot state";
            status = secure_boot_state(&secure);
            if (!EFI_ERROR(status) && secure) {
                /* LoadImage enforces db/dbx. Do not read unsigned external boot data. */
                path = UKI_PATH;
                status = load_image(image, path, &operation);
            } else if (!EFI_ERROR(status)) {
                path = LINUX_PATH;
                status = load_linux(image, &direct, &operation, FALSE);
                if (!direct) {
                    path = UKI_PATH;
                    status = load_image(image, path, &operation);
                }
            }
        }

        show_recovery(path, operation, status);

        for (;;) {
            if (EFI_ERROR(wait_key(&key))) {
                goto finish;
            }

            if (key.ScanCode == SCAN_ESC) {
                goto finish;
            }

            if (key.UnicodeChar == L'\r') {
                firmware_image = FALSE;
                previous = FALSE;
                break;
            }

            if (key.UnicodeChar == L'r' || key.UnicodeChar == L'R') {
                path = L"\\EFI\\systemd\\systemd-bootx64.efi";
                firmware_image = TRUE;
                break;
            }

            if (key.UnicodeChar == L'u' || key.UnicodeChar == L'U') {
                path = UKI_PATH;
                firmware_image = TRUE;
                break;
            }
            if (key.UnicodeChar == L'b' || key.UnicodeChar == L'B') {
                path = PREVIOUS_UKI_PATH;
                firmware_image = TRUE;
                break;
            }
            if (key.UnicodeChar == L'p' || key.UnicodeChar == L'P') {
                previous = TRUE;
                firmware_image = FALSE;
                break;
            }
        }
    }

finish:
    if (ST->ConOut && ST->ConOut->Mode) {
        uefi_call_wrapper(ST->ConOut->SetAttribute, 2, ST->ConOut, attribute);
        uefi_call_wrapper(ST->ConOut->EnableCursor, 2, ST->ConOut, cursor);
    }

    return(status);
}
