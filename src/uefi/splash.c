#include "loader.h"

extern const UINT8 _binary_neuros_bmp_start[];
extern const UINT8 _binary_neuros_bmp_end[];

static UINT32 little32(const UINT8 *p)
{
    return ((UINT32)p[0] | (UINT32)p[1] << 8 | (UINT32)p[2] << 16 | (UINT32)p[3] << 24);
}

EFI_STATUS show_splash(VOID)
{
    EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *base = NULL, *frame = NULL;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL black = {0, 0, 0, 0};
    EFI_EVENT events[2];
    EFI_INPUT_KEY key;
    EFI_STATUS status;

    UINTN count = 1, event_index, width, height, x, y, stride, offset;
    UINTN source_width, source_height, size, left, top, screen_width, screen_height;

    const UINT8 *bmp = _binary_neuros_bmp_start;

    status = uefi_call_wrapper(BS->LocateProtocol, 3, &guid, NULL, (VOID **)&gop);

    if (EFI_ERROR(status) || gop->Mode == NULL || gop->Mode->Info == NULL) {
        return (EFI_UNSUPPORTED);
    }

    screen_width = gop->Mode->Info->HorizontalResolution;
    screen_height = gop->Mode->Info->VerticalResolution;

    if (screen_width < 80 || screen_height < 80) {
        return (EFI_UNSUPPORTED);
    }

    size = (UINTN)(_binary_neuros_bmp_end - _binary_neuros_bmp_start);

    if (size < 54 || bmp[0] != 'B' || bmp[1] != 'M' || bmp[28] != 24 || bmp[29] != 0 ||
        little32(bmp + 30) != 0) {
        return (EFI_UNSUPPORTED);
    }

    offset = little32(bmp + 10);
    source_width = little32(bmp + 18);

    source_height = little32(bmp + 22);

    if (!source_width || !source_height || source_width > 4096 || source_height > 4096) {
        return (EFI_UNSUPPORTED);
    }

    stride = (source_width * 3 + 3) & ~(UINTN)3;

    if (offset > size || source_height * stride > size - offset) {
        return (EFI_UNSUPPORTED);
    }

    width = screen_width - 40;

    if (width > 960) {
        width = 960;
    }

    height = width * source_height / source_width;

    if (height > screen_height - 40) {
        height = screen_height - 40;
        width = height * source_width / source_height;
    }

    if (!width || !height) {
        return (EFI_UNSUPPORTED);
    }

    base = AllocatePool(width * height * sizeof(*base));
    frame = AllocatePool(width * height * sizeof(*frame));

    if (base == NULL || frame == NULL) {
        status = EFI_OUT_OF_RESOURCES;
        goto release;
    }

    for (y = 0; y < height; ++y) {
        UINTN row = source_height - 1 - y * source_height / height;

        for (x = 0; x < width; ++x) {
            const UINT8 *pixel = bmp + offset + row * stride + (x * source_width / width) * 3;
            base[y * width + x].Blue = pixel[0];
            base[y * width + x].Green = pixel[1];
            base[y * width + x].Red = pixel[2];
            base[y * width + x].Reserved = 0;
        }
    }
    status =
        uefi_call_wrapper(BS->CreateEvent, 5, EVT_TIMER, TPL_APPLICATION, NULL, NULL, &events[0]);

    if (EFI_ERROR(status)) {
        goto release;
    }

    status = uefi_call_wrapper(BS->SetTimer, 3, events[0], TimerPeriodic, (UINT64)500000);

    if (EFI_ERROR(status)) {
        uefi_call_wrapper(BS->CloseEvent, 1, events[0]);
        goto release;
    }

    if (ST->ConIn != NULL) {
        events[1] = ST->ConIn->WaitForKey;
        count = 2;
    }

    left = (screen_width - width) / 2;
    top = (screen_height - height) / 2;

    uefi_call_wrapper(ST->ConOut->EnableCursor, 2, ST->ConOut, FALSE);
    uefi_call_wrapper(gop->Blt, 10, gop, &black, EfiBltVideoFill, 0, 0, 0, 0, screen_width,
                      screen_height, 0);

    for (UINTN tick = 0; tick < 58; ++tick) {
        BOOLEAN glitch = tick < 5 || (tick < 48 && tick % 19 < 3);

        for (y = 0; y < height; ++y) {
            INTN shift = glitch && (y / 7 + tick) % 5 == 0 ? (INTN)(tick % 3) * 12 - 12 : 0;

            for (x = 0; x < width; ++x) {
                INTN sample = (INTN)x + shift;
                EFI_GRAPHICS_OUTPUT_BLT_PIXEL pixel = black;

                if (sample >= 0 && (UINTN)sample < width) {
                    pixel = base[y * width + (UINTN)sample];

                    if (glitch && x + 4 < width) {
                        pixel.Blue = base[y * width + x + 4].Blue;
                        pixel.Green = base[y * width + x + 4].Green;
                    }

                    if (glitch && y % 4 == 0) {
                        pixel.Red /= 2;
                        pixel.Green /= 2;
                        pixel.Blue /= 2;
                    }
                }
                frame[y * width + x] = pixel;
            }
        }
        status = uefi_call_wrapper(gop->Blt, 10, gop, frame, EfiBltBufferToVideo, 0, 0, left, top,
                                   width, height, width * sizeof(*frame));
        if (EFI_ERROR(status)) {
            break;
        }

        do {
            status = uefi_call_wrapper(BS->WaitForEvent, 3, count, events, &event_index);

            if (EFI_ERROR(status)) {
                goto finish;
            }

            if (event_index == 1 &&
                !EFI_ERROR(uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key))) {
                if (key.UnicodeChar == L'\r' || key.ScanCode == SCAN_ESC) {
                    goto finish;
                }
            }
        } while (event_index != 0);
    }

finish:
    uefi_call_wrapper(BS->CloseEvent, 1, events[0]);
    /* Leave a clean, static logo visible, including when the animation is skipped. */
    if (!EFI_ERROR(status)) {
        status = uefi_call_wrapper(gop->Blt, 10, gop, base, EfiBltBufferToVideo, 0, 0, left, top,
                                   width, height, width * sizeof(*base));
    }

release:
    if (frame != NULL) {
        FreePool(frame);
    }
    if (base != NULL) {
        FreePool(base);
    }
    return (status);
}
