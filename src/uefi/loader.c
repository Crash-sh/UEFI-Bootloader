#include "loader.h"

EFI_STATUS load_image(EFI_HANDLE parent, CHAR16 *path, CHAR16 **operation)
{
    EFI_LOADED_IMAGE *loaded;
    EFI_DEVICE_PATH *device_path;
    EFI_HANDLE child = NULL;
    EFI_STATUS status;
    UINTN exit_size = 0;
    CHAR16 *exit_data = NULL;

    *operation = L"HandleProtocol";
    status =
        uefi_call_wrapper(BS->HandleProtocol, 3, parent, &LoadedImageProtocol, (VOID **)&loaded);
    if (EFI_ERROR(status)) {
        return (status);
    }
    *operation = L"FileDevicePath";
    device_path = FileDevicePath(loaded->DeviceHandle, path);
    if (device_path == NULL) {
        return (EFI_OUT_OF_RESOURCES);
    }

    *operation = L"LoadImage";
    status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, parent, device_path, NULL, 0, &child);

    FreePool(device_path);

    if (EFI_ERROR(status)) {
        if (child != NULL) {
            uefi_call_wrapper(BS->UnloadImage, 1, child);
        }

        return (status);
    }

    *operation = L"StartImage";
    status = uefi_call_wrapper(BS->StartImage, 3, child, &exit_size, &exit_data);

    if (exit_data != NULL) {
        FreePool(exit_data);
    }

    return (status);
}
