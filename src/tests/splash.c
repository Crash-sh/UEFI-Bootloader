#include "../uefi/loader.h"
#include "../uefi/splash_effect.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

EFI_SYSTEM_TABLE *ST;
EFI_BOOT_SERVICES *BS;
static EFI_SYSTEM_TABLE system_table;
static EFI_BOOT_SERVICES services;
static EFI_SIMPLE_TEXT_IN_PROTOCOL input;
static EFI_GRAPHICS_OUTPUT_PROTOCOL graphics;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
static unsigned reads, stalls, resets, fallbacks, allocations, closes;
static unsigned key_after, blits;
static int failure;
static BOOLEAN input_error;

enum { NO_GOP = 1, BAD_MODE, NO_MEMORY, NO_EVENT, NO_TIMER, BAD_BLT, BAD_WAIT, FINAL_BLT };

VOID *AllocatePool(UINTN size)
{
    if (failure == NO_MEMORY) {
        return(NULL);
    }
    void *memory = malloc(size);
    if (memory) {
        ++allocations;
    }
    return(memory);
}

VOID FreePool(VOID *memory)
{
    assert(allocations > 0);
    --allocations;
    free(memory);
}

VOID show_boot_fallback(EFI_STATUS status)
{
    assert(EFI_ERROR(status));
    ++fallbacks;
}

VOID render_splash_frame(const EFI_GRAPHICS_OUTPUT_BLT_PIXEL *base,
                         EFI_GRAPHICS_OUTPUT_BLT_PIXEL *frame, UINTN width, UINTN height,
                         UINTN tick)
{
    (void)base;
    (void)frame;
    (void)width;
    (void)height;
    (void)tick;
}

static EFI_STATUS EFIAPI locate(EFI_GUID *guid, VOID *registration, VOID **interface)
{
    (void)guid;
    (void)registration;
    *interface = &graphics;
    return(failure == NO_GOP ? EFI_NOT_FOUND : EFI_SUCCESS);
}

static EFI_STATUS EFIAPI handle(EFI_HANDLE device, EFI_GUID *guid, VOID **interface)
{
    (void)device;
    return(locate(guid, NULL, interface));
}

static EFI_STATUS EFIAPI reset(EFI_SIMPLE_TEXT_IN_PROTOCOL *self, BOOLEAN extended)
{
    (void)self;
    (void)extended;
    ++resets;
    return(EFI_SUCCESS);
}

static EFI_STATUS EFIAPI read_key(EFI_SIMPLE_TEXT_IN_PROTOCOL *self, EFI_INPUT_KEY *key)
{
    (void)self;
    ++reads;
    key->ScanCode = 0;
    key->UnicodeChar = L'M';
    if (input_error) {
        return(EFI_DEVICE_ERROR);
    }
    return(reads >= key_after ? EFI_SUCCESS : EFI_NOT_READY);
}

static EFI_STATUS EFIAPI stall(UINTN microseconds)
{
    assert(microseconds == 20000);
    ++stalls;
    return(EFI_SUCCESS);
}

static EFI_STATUS EFIAPI create(UINT32 type, EFI_TPL tpl, EFI_EVENT_NOTIFY notify, VOID *context,
                                EFI_EVENT *event)
{
    (void)type;
    (void)tpl;
    (void)notify;
    (void)context;
    *event = (EFI_EVENT)1;
    return(failure == NO_EVENT ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS);
}

static EFI_STATUS EFIAPI timer(EFI_EVENT event, EFI_TIMER_DELAY delay, UINT64 time)
{
    (void)event;
    (void)delay;
    (void)time;
    return(failure == NO_TIMER ? EFI_DEVICE_ERROR : EFI_SUCCESS);
}

static EFI_STATUS EFIAPI close_event(EFI_EVENT event)
{
    assert(event == (EFI_EVENT)1);
    ++closes;
    return(EFI_SUCCESS);
}

static EFI_STATUS EFIAPI wait_event(UINTN count, EFI_EVENT *events, UINTN *index)
{
    (void)count;
    (void)events;
    *index = 1;
    return(failure == BAD_WAIT ? EFI_DEVICE_ERROR : EFI_SUCCESS);
}

static EFI_STATUS EFIAPI blt(EFI_GRAPHICS_OUTPUT_PROTOCOL *self,
                             EFI_GRAPHICS_OUTPUT_BLT_PIXEL *buffer,
                             EFI_GRAPHICS_OUTPUT_BLT_OPERATION operation, UINTN sx, UINTN sy,
                             UINTN dx, UINTN dy, UINTN width, UINTN height, UINTN delta)
{
    (void)self;
    (void)buffer;
    (void)operation;
    (void)sx;
    (void)sy;
    (void)dx;
    (void)dy;
    (void)width;
    (void)height;
    (void)delta;
    ++blits;
    return(failure == BAD_BLT || (failure == FINAL_BLT && blits == 3) ? EFI_DEVICE_ERROR
                                                                       : EFI_SUCCESS);
}

static void setup(int fault)
{
    failure = fault;
    reads = stalls = resets = fallbacks = allocations = closes = blits = 0;
    key_after = 1;
    input_error = FALSE;
    ST = &system_table;
    BS = &services;
    ST->ConIn = &input;
    ST->ConOut = NULL; /* No usable text output must not prevent recovery input. */
    input.Reset = reset;
    input.ReadKeyStroke = read_key;
    BS->HandleProtocol = handle;
    BS->LocateProtocol = locate;
    BS->Stall = stall;
    BS->CreateEvent = create;
    BS->SetTimer = timer;
    BS->CloseEvent = close_event;
    BS->WaitForEvent = wait_event;
    graphics.Mode = fault == BAD_MODE ? NULL : &mode;
    graphics.Blt = blt;
    mode.Info = &info;
    info.HorizontalResolution = 800;
    info.VerticalResolution = 600;
}

int main(void)
{
    BOOLEAN recovery;
    for (int fault = NO_GOP; fault <= BAD_WAIT; ++fault) {
        setup(fault);
        key_after = 4; /* Input arriving after graphics fails, not just queued M. */
        assert(EFI_ERROR(show_splash(&recovery)));
        assert(recovery && resets == 1 && fallbacks == 1 && stalls == 3);
        assert(allocations == 0);
        assert(closes == (unsigned)(fault >= NO_TIMER));
    }
    setup(NO_GOP);
    key_after = 101;
    assert(EFI_ERROR(show_splash(&recovery)));
    assert(!recovery && reads == 100 && stalls == 100);
    setup(NO_GOP);
    ST->ConIn = NULL;
    assert(EFI_ERROR(show_splash(&recovery)));
    assert(!recovery && reads == 0 && stalls == 100);
    setup(NO_GOP);
    input_error = TRUE;
    assert(EFI_ERROR(show_splash(&recovery)));
    assert(!recovery && reads == 1 && stalls == 0);
    setup(0);
    assert(show_splash(&recovery) == EFI_SUCCESS);
    assert(recovery && resets == 1 && fallbacks == 0 && allocations == 0 && closes == 1);
    setup(FINAL_BLT);
    assert(EFI_ERROR(show_splash(&recovery)));
    assert(recovery && reads == 1 && fallbacks == 0 && allocations == 0 && closes == 1);
    puts("splash graphics-failure recovery checks passed");
    return(0);
}
