#include "loader.h"

/* Firmware text colors approximate the logo's gold and cyan on black. */
#define UI_GOLD EFI_YELLOW
#define UI_CYAN EFI_LIGHTCYAN
#define UI_TEXT EFI_WHITE
#define UI_MUTED EFI_LIGHTGRAY

static VOID set_color(UINTN color)
{
    uefi_call_wrapper(ST->ConOut->SetAttribute, 2, ST->ConOut, EFI_TEXT_ATTR(color, EFI_BLACK));
}

static VOID begin_screen(VOID)
{
    set_color(UI_TEXT);
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
    uefi_call_wrapper(ST->ConOut->EnableCursor, 2, ST->ConOut, FALSE);
}

static VOID heading(CHAR16 *label)
{
    set_color(UI_GOLD);
    Print(L"\r\n  NeurOS\r\n");
    set_color(UI_CYAN);
    Print(L"  %s\r\n\r\n", label);
}

VOID show_boot_fallback(EFI_STATUS status)
{
    begin_screen();
    heading(L"BOOT");
    set_color(UI_MUTED);
    Print(L"  Splash unavailable: %r\r\n\r\n", status);
    set_color(UI_TEXT);
    Print(L"  Starting the boot image...\r\n");
}

VOID show_recovery(CHAR16 *path, CHAR16 *operation, EFI_STATUS status)
{
    begin_screen();
    heading(L"RECOVERY");
    set_color(UI_TEXT);

    if (status == EFI_NOT_FOUND) {
        Print(L"  The boot image was not found on this EFI System Partition.\r\n");
    } else if (status == EFI_SECURITY_VIOLATION || status == EFI_ACCESS_DENIED) {
        Print(L"  Firmware refused the image. Check Secure Boot signatures.\r\n");
    } else if (EFI_ERROR(status)) {
        Print(L"  The boot attempt failed. Retry or open systemd-boot.\r\n");
    } else {
        Print(L"  The boot image returned control to NeurOS.\r\n");
    }

    set_color(UI_MUTED);
    Print(L"\r\n  Target: %s\r\n  %s: %r\r\n\r\n", path, operation, status);

    set_color(UI_CYAN);
    Print(L"  [Enter] Retry Arch\r\n  [R] systemd-boot\r\n  [Esc] Return to caller\r\n");
    set_color(UI_TEXT);
}
