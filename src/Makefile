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
OVMF_SECURE_CODE ?= /usr/share/edk2/x64/OVMF_CODE.secboot.4m.fd
SBSIGN ?= /usr/lib/systemd/systemd-sbsign
UKI_STUB ?= /usr/lib/systemd/boot/efi/linuxx64.efi.stub
PYTHON ?= python3

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

$(BUILD)/exit_retry.o: $(SOURCE)/tests/exit_retry.c | check-deps $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD)/large_map.o: $(SOURCE)/tests/exit_retry.c | check-deps $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DTEST_LARGE_MAP -MMD -MP -c "$<" -o "$@"

$(BUILD)/handoff.o: $(SOURCE)/uefi/handoff.S | $(BUILD)
	$(CC) -c "$<" -o "$@"

$(BUILD)/loader.so: $(BUILD)/main.o $(BUILD)/loader.o $(BUILD)/security.o $(BUILD)/linux.o $(BUILD)/linux_format.o $(BUILD)/handoff.o $(BUILD)/splash.o $(BUILD)/splash_effect.o $(BUILD)/ui.o $(BUILD)/logo.o
	$(LD) $(EFI_LINK) $(EFI_LIB)/crt0-efi-x86_64.o $^ -lefi -lgnuefi -o "$@"

$(BUILD)/%.so: $(BUILD)/%.o
	$(LD) $(EFI_LINK) $(EFI_LIB)/crt0-efi-x86_64.o $^ -lefi -lgnuefi -o "$@"

$(ESP)/EFI/BOOT/BOOTX64.EFI: $(BUILD)/loader.so
	mkdir -p "$(@D)"
	$(OBJCOPY) $(EFI_SECTIONS) -O efi-app-x86_64 "$<" "$@"

$(BUILD)/%.efi: $(BUILD)/%.so
	mkdir -p "$(@D)"
	$(OBJCOPY) $(EFI_SECTIONS) -O efi-app-x86_64 "$<" "$@"

check: all check-tools check-linux $(BUILD)/test.efi $(BUILD)/error.efi
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

.PHONY: check-linux test-linux
$(BUILD)/linux-tests: $(SOURCE)/tests/linux.c $(SOURCE)/uefi/linux_format.c $(SOURCE)/uefi/linux_format.h | $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) "$<" "$(SOURCE)/uefi/linux_format.c" -o "$@"

check-linux: $(BUILD)/linux-tests
	"$(BUILD)/linux-tests"

$(BUILD)/test-init: $(SOURCE)/tests/init.c | $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) -static "$<" -o "$@"

$(BUILD)/make-initramfs: $(SOURCE)/tests/initramfs.c | $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) "$<" -o "$@"

$(BUILD)/test-initramfs.cpio: $(BUILD)/test-init $(BUILD)/make-initramfs
	"$(BUILD)/make-initramfs" "$<" "$@"

test-linux: check $(BUILD)/boot-tests $(BUILD)/test-initramfs.cpio $(BUILD)/exit_retry.efi $(BUILD)/large_map.efi
	@test -n "$(KERNEL)" || { echo 'Usage: make test-linux KERNEL=/path/to/vmlinuz'; exit 1; }
	"$(BUILD)/boot-tests" --build "$(BUILD)" --qemu "$(QEMU)" --firmware "$(OVMF_CODE)" --vars "$(OVMF_VARS)" --kernel "$(KERNEL)"

.PHONY: test-secureboot sign package-signed
test-secureboot: check $(BUILD)/test-initramfs.cpio
	@test -n "$(KERNEL)" || { echo 'Usage: make test-secureboot KERNEL=/path/to/vmlinuz'; exit 1; }
	$(PYTHON) "$(SOURCE)/tests/secureboot.py" --build "$(BUILD)" --kernel "$(KERNEL)" --qemu "$(QEMU)" --firmware "$(OVMF_SECURE_CODE)" --vars "$(OVMF_VARS)" --signer "$(SBSIGN)" --stub "$(UKI_STUB)"

sign: all
	@test -n "$(SIGN_KEY)" -a -n "$(SIGN_CERT)" || { echo 'Set SIGN_KEY and SIGN_CERT to your signing key and PEM certificate'; exit 1; }
	"$(SBSIGN)" sign --private-key="$(SIGN_KEY)" --certificate="$(SIGN_CERT)" --output="$(BUILD)/neuros.signed.efi" "$(ESP)/EFI/BOOT/BOOTX64.EFI"

package-signed: sign
	mkdir -p "$(BUILD)/deploy-signed/EFI/NeurOS" "$(BUILD)/deploy-signed/loader/entries"
	cp "$(BUILD)/neuros.signed.efi" "$(BUILD)/deploy-signed/EFI/NeurOS/neuros.efi"
	printf 'title NeurOS\nefi /EFI/NeurOS/neuros.efi\n' > "$(BUILD)/deploy-signed/loader/entries/neuros.conf"

package: all
	mkdir -p "$(BUILD)/deploy/EFI/NeurOS" "$(BUILD)/deploy/loader/entries"
	cp "$(ESP)/EFI/BOOT/BOOTX64.EFI" "$(BUILD)/deploy/EFI/NeurOS/neuros.efi"
	printf 'title NeurOS\nefi /EFI/NeurOS/neuros.efi\n' > "$(BUILD)/deploy/loader/entries/neuros.conf"

stage-uki: all
	@test -n "$(UKI)" || { echo 'Usage: make stage-uki UKI=/path/to/arch-linux.efi'; exit 1; }
	"$(BUILD)/stage-uki" "$(UKI)" "$(ESP)/EFI/Linux/arch-linux.efi"

-include $(wildcard $(BUILD)/*.d)

tools: $(BUILD)/stage-uki $(BUILD)/stage-linux $(BUILD)/neuros-install

$(BUILD)/stage-linux: $(SOURCE)/tools/stage_linux.c $(SOURCE)/tools/common.c $(SOURCE)/tools/common.h $(SOURCE)/uefi/linux_format.c $(SOURCE)/uefi/linux_format.h | $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) "$<" "$(SOURCE)/tools/common.c" "$(SOURCE)/uefi/linux_format.c" -o "$@"

.PHONY: stage-linux
stage-linux: all
	@test -n "$(KERNEL)" -a -n "$(CMDLINE)" -a -n "$(INITRD)" || { echo 'Usage: make stage-linux KERNEL=... CMDLINE=... INITRD=...'; exit 1; }
	"$(BUILD)/stage-linux" "$(KERNEL)" "$(CMDLINE)" "$(INITRD)" "$(ESP)/EFI/NeurOS"

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
