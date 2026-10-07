#ifndef NEUROS_LOADER_H
#define NEUROS_LOADER_H

#include <efi.h>
#include <efilib.h>

EFI_STATUS show_splash(VOID);
EFI_STATUS load_image(EFI_HANDLE parent, CHAR16 *path, CHAR16 **operation);

#endif
