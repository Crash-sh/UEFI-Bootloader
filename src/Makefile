.DEFAULT_GOAL := all
SOURCE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOT := $(abspath $(SOURCE)/..)
BUILD := $(ROOT)/build
ESP := $(BUILD)/esp

CC = gcc
HOSTCC ?= cc
HOSTCFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
LD = ld
OBJCOPY ?= objcopy
EFI_INCLUDE ?= /usr/include/efi
EFI_LIB ?= /usr/lib
QEMU ?= qemu-system-x86_64
QEMU_MEMORY ?= 1024
OVMF_CODE ?= /usr/share/edk2/x64/OVMF_CODE.4m.fd
OVMF_VARS ?= /usr/share/edk2/x64/OVMF_VARS.4m.fd

CPPFLAGS += -I$(EFI_INCLUDE) -I$(EFI_INCLUDE)/x86_64 -I$(EFI_INCLUDE)/protocol -DGNU_EFI_USE_MS_ABI
CFLAGS += -std=c11 -O2 -Wall -Wextra -Werror -fpic -ffreestanding -fshort-wchar -fno-stack-protector -fno-strict-aliasing -mno-red-zone -maccumulate-outgoing-args
EFI_LINK = -nostdlib -znocombreloc -T $(EFI_LIB)/elf_x86_64_efi.lds -shared -Bsymbolic -L$(EFI_LIB)
EFI_SECTIONS = -j .rodata -j .dynstr -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc

.PHONY: all check-deps check run clean test package stage-uki tools check-tools

all: $(ESP)/EFI/BOOT/BOOTX64.EFI tools

check-deps:
	@test -f "$(EFI_INCLUDE)/efi.h" -a -f "$(EFI_LIB)/crt0-efi-x86_64.o" -a -f "$(EFI_LIB)/elf_x86_64_efi.lds" -a -f "$(EFI_LIB)/libefi.a" -a -f "$(EFI_LIB)/libgnuefi.a" || { echo 'Missing GNU-EFI development files. Arch: sudo pacman -S gnu-efi'; exit 1; }

$(BUILD):
	mkdir -p "$@"

$(BUILD)/%.o: $(SOURCE)/uefi/%.c $(SOURCE)/uefi/loader.h | check-deps $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD)/error.o: $(SOURCE)/tests/test.c | check-deps $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DTEST_STATUS=EFI_ABORTED -MMD -MP -c "$<" -o "$@"

$(BUILD)/test.o: $(SOURCE)/tests/test.c | check-deps $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD)/loader.so: $(BUILD)/main.o $(BUILD)/loader.o $(BUILD)/splash.o $(BUILD)/ui.o $(BUILD)/logo.o
	$(LD) $(EFI_LINK) $(EFI_LIB)/crt0-efi-x86_64.o $^ -lefi -lgnuefi -o "$@"

$(BUILD)/%.so: $(BUILD)/%.o
	$(LD) $(EFI_LINK) $(EFI_LIB)/crt0-efi-x86_64.o $^ -lefi -lgnuefi -o "$@"

$(ESP)/EFI/BOOT/BOOTX64.EFI: $(BUILD)/loader.so
	mkdir -p "$(@D)"
	$(OBJCOPY) $(EFI_SECTIONS) -O efi-app-x86_64 "$<" "$@"

$(BUILD)/%.efi: $(BUILD)/%.so
	mkdir -p "$(@D)"
	$(OBJCOPY) $(EFI_SECTIONS) -O efi-app-x86_64 "$<" "$@"

check: all check-tools $(BUILD)/test.efi $(BUILD)/error.efi
	objdump -p "$(ESP)/EFI/BOOT/BOOTX64.EFI" | grep 'EFI application'
	objdump -p "$(BUILD)/test.efi" | grep 'EFI application'

$(BUILD)/OVMF_VARS.fd: | $(BUILD)
	cp "$(OVMF_VARS)" "$@"

run: all $(BUILD)/OVMF_VARS.fd
	$(QEMU) -accel tcg -m $(QEMU_MEMORY) -drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" -drive if=pflash,format=raw,file="$(BUILD)/OVMF_VARS.fd" -drive format=raw,file=fat:rw:"$(ESP)" -net none -debugcon stdio

clean:
	$(RM) -r "$(BUILD)"

$(BUILD)/boot-tests: $(SOURCE)/tests/boot.c | $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) "$<" -o "$@"

test: check $(BUILD)/boot-tests
	"$(BUILD)/boot-tests" --build "$(BUILD)" --qemu "$(QEMU)" --firmware "$(OVMF_CODE)" --vars "$(OVMF_VARS)"

package: all
	mkdir -p "$(BUILD)/deploy/EFI/NeurOS" "$(BUILD)/deploy/loader/entries"
	cp "$(ESP)/EFI/BOOT/BOOTX64.EFI" "$(BUILD)/deploy/EFI/NeurOS/neuros.efi"
	printf 'title NeurOS\nefi /EFI/NeurOS/neuros.efi\n' > "$(BUILD)/deploy/loader/entries/neuros.conf"

stage-uki: all
	@test -n "$(UKI)" || { echo 'Usage: make stage-uki UKI=/path/to/arch-linux.efi'; exit 1; }
	"$(BUILD)/stage-uki" "$(UKI)" "$(ESP)/EFI/Linux/arch-linux.efi"

-include $(wildcard $(BUILD)/*.d)

tools: $(BUILD)/stage-uki $(BUILD)/neuros-install

$(BUILD)/stage-uki: $(SOURCE)/tools/stage_uki.c $(SOURCE)/tools/common.c $(SOURCE)/tools/common.h | $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) "$<" "$(SOURCE)/tools/common.c" -o "$@"

$(BUILD)/neuros-install: $(SOURCE)/tools/install.c $(SOURCE)/tools/common.c $(SOURCE)/tools/common.h | $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) "$<" "$(SOURCE)/tools/common.c" -o "$@"

$(BUILD)/tools-tests: $(SOURCE)/tests/tools.c $(SOURCE)/tools/common.c $(SOURCE)/tools/common.h | $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) "$<" "$(SOURCE)/tools/common.c" -o "$@"

check-tools: tools $(BUILD)/tools-tests
	@if ! "$(BUILD)/tools-tests" 2>"$(BUILD)/tools-tests.log"; then cat "$(BUILD)/tools-tests.log" >&2; exit 1; fi

$(BUILD)/logo.o: $(ROOT)/assets/neuros.bmp | $(BUILD)
	cd "$(ROOT)/assets" && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.rodata,alloc,load,readonly,data,contents neuros.bmp "$@"
