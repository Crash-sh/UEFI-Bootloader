#ifndef NEUROS_SPLASH_EFFECT_H
#define NEUROS_SPLASH_EFFECT_H

#include <efi.h>

#define SPLASH_FRAME_COUNT 58
#define SPLASH_FRAME_PERIOD 500000

VOID render_splash_frame(const EFI_GRAPHICS_OUTPUT_BLT_PIXEL *base,
                         EFI_GRAPHICS_OUTPUT_BLT_PIXEL *frame, UINTN width, UINTN height,
                         UINTN tick);

#endif
