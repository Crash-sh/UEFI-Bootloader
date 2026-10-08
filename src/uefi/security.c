#include "loader.h"

EFI_STATUS secure_boot_state(BOOLEAN *enabled)
{
    EFI_GUID global = EFI_GLOBAL_VARIABLE;
    UINT8 value = 0xff;
    UINTN size = sizeof(value);
    *enabled = TRUE; /* A read error must never authorize unsigned direct boot. */
    EFI_STATUS status =
        uefi_call_wrapper(RT->GetVariable, 5, L"SecureBoot", &global, NULL, &size, &value);
    if (status == EFI_NOT_FOUND) {
        /* Pre-Secure-Boot firmware. */
        *enabled = FALSE;
        return (EFI_SUCCESS);
    }
    if (EFI_ERROR(status)) {
        return (status);
    }
    if (size != sizeof(value) || value > 1) {
        return (EFI_SECURITY_VIOLATION);
    }
    *enabled = value != 0;
    return (EFI_SUCCESS);
}
