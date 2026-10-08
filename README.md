# NeurOS // before the kernel


![NeurOS animated boot splash](assets/neuros-splash.gif)


```text
NSD / FIRMWARE EXPERIMENTS

architecture : x86_64
interface    : UEFI / GNU-EFI
direct       : \EFI\NeurOS\vmlinuz
fallback     : \EFI\Linux\arch-linux.efi
job          : get Arch running. look suspicious doing it.
```

**As a thanks to Neuro for the DWM Setup**

Boots a staged x86-64 Linux bzImage directly from the same EFI System Partition. NeurOS loads the kernel and initramfs, builds Linux boot parameters and page tables, exits UEFI boot services, and jumps to the kernel's 64-bit entry. When no direct kernel is present, it launches the existing Arch UKI. NeurOS supplies 2.9 seconds of logo-only glitch: gold, warm fragments, cyan tearing, then a clean finish. The background stays black.

The logo stays until handoff. Enter or Esc skips the animation after a brief input guard against the boot-menu key. Graphics failures show a status for two seconds before continuing. Before boot services exit, failures reach a separate recovery screen with gold/cyan accents and muted diagnostics: **Enter** retries Arch, **R** opens systemd-boot, **Esc** returns to the caller. Linux controls the display after handoff.

**Compile. Interrogate.**

```sh
sudo pacman -S --needed base-devel linux-api-headers gnu-efi qemu-system-x86 qemu-ui-gtk edk2-ovmf
make
make test
make run QEMU="qemu-system-x86_64 -display gtk"
```

Tests cover native tools, Linux header/command-line/memory-map validation, page tables, and five isolated QEMU boot cases. Without a staged kernel or UKI, the preview lands on recovery. To supply a UKI:

```sh
make stage-uki UKI=/path/to/arch-linux.efi
```

Your host's root partition does not magically appear inside QEMU. Use a VM-compatible UKI and root disk for a full OS boot.

**Direct Linux development.**

```sh
make test-linux KERNEL=/path/to/vmlinuz
make stage-linux KERNEL=/path/to/vmlinuz CMDLINE=/path/to/cmdline.txt INITRD=/path/to/initramfs.img
make run
```

`test-linux` builds a small static `/init` and a newc initramfs, then boots your supplied kernel in an isolated QEMU ESP. It verifies PID 1, the command line, EFI/ACPI tables, and recovery from a stale `ExitBootServices` map key. It also tests missing command-line and empty-initrd recovery. The test kernel needs built-in initramfs, ELF, procfs, sysfs, EFI, ACPI, and serial-console support. No host root disk is attached.

`stage-linux` creates `build/esp/EFI/NeurOS/` and refuses an existing directory. `cmdline.txt` contains one ASCII line with the kernel options for your target system, including its root-device configuration. The initramfs must match that kernel. Microcode and main initramfs archives can be concatenated beforehand in the order required by Linux. The loader passes the buffer intact for Linux to unpack.

The direct path reads `EFI/NeurOS/vmlinuz`, requires `EFI/NeurOS/cmdline.txt`, and optionally loads `EFI/NeurOS/initrd`. A missing initrd is supported for kernels with built-in early userspace or root drivers. A present but invalid kernel goes to recovery; it does not silently switch to the UKI. Rename or remove the staged `vmlinuz` to select UKI boot again.

Initial scope: relocatable x86-64 bzImage, boot protocol 2.12+, Secure Boot disabled, and firmware using four-level paging. Boot allocations remain below 4 GiB. Limits are 128 MiB for the kernel file, 512 MiB each for decompression space and initramfs, 4095 command-line bytes (or the kernel's smaller limit), a 64 KiB EFI memory map, and 128 coalesced E820 entries. Unsupported inputs fail before handoff. Raw ELF kernels, signature verification, encrypted/confidential-computing guests, and five-level firmware paging are outside this initial backend.

The current graphics mode is preserved; standard RGB/BGR GOP framebuffer metadata is passed to Linux. Bitmask and firmware-only blit modes retain the pre-kernel splash but are not advertised as a Linux framebuffer yet. Once `ExitBootServices` has been attempted, firmware recovery is no longer safe: stale memory-map keys are retried, and an unrecoverable exit failure halts until reset. Hardware validation is still required.

Sources: [QUICK_REF.md](QUICK_REF.md).

**Put it on metal.**

Requires an existing Arch UKI, systemd-boot, and Secure Boot disabled. With the ESP mounted at `/boot`:

```sh
make test package
sudo ./build/neuros-install --esp /boot
```

Adds `EFI/NeurOS/neuros.efi` and `loader/entries/neuros.conf`. Refuses overwrites; leaves boot order and the default entry alone. Keep the installer beside `build/deploy/`.

Packaging and installation still deploy only the EFI executable and boot entry. They do not copy a direct kernel, command line, or initramfs to your real ESP.

Reboot, hold Space for systemd-boot, select **NeurOS**. Keep your working Arch entry as the default until hardware testing passes.

```text
kernel handoff   : NeurOS direct / UKI fallback
firmware patience: being tested
```
