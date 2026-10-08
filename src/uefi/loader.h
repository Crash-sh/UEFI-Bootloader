#ifndef NEUROS_LOADER_H
#define NEUROS_LOADER_H

#include <efi.h>
#include <efilib.h>

EFI_STATUS show_splash(VOID);
VOID show_boot_fallback(EFI_STATUS status);
VOID show_recovery(CHAR16 *path, CHAR16 *operation, EFI_STATUS status);
EFI_STATUS load_image(EFI_HANDLE parent, CHAR16 *path, CHAR16 **operation);

#endif
