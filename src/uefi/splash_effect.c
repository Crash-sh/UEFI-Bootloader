#include "splash_effect.h"

static UINT32 noise(UINT32 value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    return(value);
}

static EFI_GRAPHICS_OUTPUT_BLT_PIXEL sample(const EFI_GRAPHICS_OUTPUT_BLT_PIXEL *base, UINTN width,
                                            UINTN y, INTN x)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL black = {0, 0, 0, 0};

    if (x < 0 || (UINTN)x >= width) {
        return(black);
    }
    return(base[y * width + (UINTN)x]);
}

static UINT8 brightest(EFI_GRAPHICS_OUTPUT_BLT_PIXEL pixel)
{
    UINT8 level = pixel.Red > pixel.Green ? pixel.Red : pixel.Green;

    return(level > pixel.Blue ? level : pixel.Blue);
}

static EFI_GRAPHICS_OUTPUT_BLT_PIXEL tint(EFI_GRAPHICS_OUTPUT_BLT_PIXEL pixel, BOOLEAN hot)
{
    UINT8 level = brightest(pixel);

    if (hot) {
        pixel.Red = level;
        pixel.Green = level / 3 + pixel.Green / 4;
        pixel.Blue = level / 8;
    } else {
        pixel.Red = level / 5;
        pixel.Green = level * 3 / 4;
        pixel.Blue = level;
    }
    return(pixel);
}

VOID render_splash_frame(const EFI_GRAPHICS_OUTPUT_BLT_PIXEL *base,
                         EFI_GRAPHICS_OUTPUT_BLT_PIXEL *frame, UINTN width, UINTN height,
                         UINTN tick)
{
    /* Match the reference's 20 fps sequence, without its background effects. */
    if (tick < 8 || tick >= 36) {
        for (UINTN i = 0; i < width * height; ++i) {
            frame[i] = base[i];
        }
    } else {
        BOOLEAN hot = tick < 21;
        BOOLEAN tearing = tick < 11 || tick == 21 || tick == 22 || tick == 29;

        UINTN band_height = height / (tearing ? 48 : 90) + 1;
        UINTN amplitude = width / (tearing ? 14 : 100) + 1;
        UINTN separation = width / (tearing ? 55 : 180) + 1;
        UINTN settle = tick >= 30 ? tick - 29 : 0;

        for (UINTN y = 0; y < height; ++y) {
            UINT32 band = noise((UINT32)(y / band_height) + (UINT32)tick * 131U);

            INTN shift = (INTN)(band % (amplitude * 2 + 1)) - (INTN)amplitude;

            UINTN fragment_width = width / 5 + 1;
            UINTN fragment_left = noise(band) % width;
            UINTN fragment_length = fragment_width + noise(band + 1) % fragment_width;

            if (!tearing && band % 5 != 0) {
                shift = 0;
            }
            if (settle != 0) {
                shift = shift * (INTN)(7 - settle) / 7;
            }

            for (UINTN x = 0; x < width; ++x) {
                INTN source_x = (INTN)x + shift;
                BOOLEAN colored =
                    band % 3 != 0 && x >= fragment_left && x - fragment_left < fragment_length;
                EFI_GRAPHICS_OUTPUT_BLT_PIXEL original = sample(base, width, y, source_x);
                EFI_GRAPHICS_OUTPUT_BLT_PIXEL pixel = original;

                /* Keep the original palette outside short, scattered strip fragments. */
                if (colored) {
                    EFI_GRAPHICS_OUTPUT_BLT_PIXEL echo = sample(
                        base, width, y, source_x + (hot ? -(INTN)separation : (INTN)separation));
                    UINT8 ghost = brightest(echo) / 2;

                    pixel = tint(original, hot);
                    if (hot && ghost > pixel.Red) {
                        pixel.Red = ghost;
                    } else if (!hot && ghost > pixel.Blue) {
                        pixel.Blue = ghost;
                        if (ghost / 2 > pixel.Green) {
                            pixel.Green = ghost / 2;
                        }
                    }
                }

                if ((tick == 8 && band % 5 != 0) || (tick == 9 && band % 3 == 0) ||
                    (tearing && band % 11 == 0)) {
                    pixel.Red = 0;
                    pixel.Green = 0;
                    pixel.Blue = 0;
                } else if (colored && y % 3 == 0) {
                    pixel.Red = pixel.Red * 3 / 4;
                    pixel.Green = pixel.Green * 3 / 4;
                    pixel.Blue = pixel.Blue * 3 / 4;
                }

                if (settle != 0) {
                    pixel.Red = (pixel.Red * (7 - settle) + original.Red * settle) / 7;
                    pixel.Green = (pixel.Green * (7 - settle) + original.Green * settle) / 7;
                    pixel.Blue = (pixel.Blue * (7 - settle) + original.Blue * settle) / 7;
                }
                pixel.Reserved = 0;
                frame[y * width + x] = pixel;
            }
        }
    }
}
