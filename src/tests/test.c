#include <efi.h>
#include <efilib.h>

#ifndef TEST_STATUS
#define TEST_STATUS EFI_SUCCESS
#endif

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    InitializeLib(image, system);
    Print(L"NEUROS: test child reached\r\n");

    return(TEST_STATUS);
}
