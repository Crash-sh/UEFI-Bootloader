#ifndef NEUROS_LOADER_H
#define NEUROS_LOADER_H

#include <efi.h>
#include <efilib.h>

#define LINUX_PATH L"\\EFI\\NeurOS\\vmlinuz"
#define UKI_PATH L"\\EFI\\Linux\\arch-linux.efi"
#define PREVIOUS_UKI_PATH L"\\EFI\\Linux\\arch-linux.efi.previous"

EFI_STATUS show_splash(BOOLEAN *recovery);
VOID show_boot_fallback(EFI_STATUS status);
VOID show_recovery(CHAR16 *path, CHAR16 *operation, EFI_STATUS status);
EFI_STATUS load_image(EFI_HANDLE parent, CHAR16 *path, CHAR16 **operation);
EFI_STATUS load_linux(EFI_HANDLE parent, BOOLEAN *present, CHAR16 **operation, BOOLEAN previous);
EFI_STATUS secure_boot_state(BOOLEAN *enabled);

#endif
