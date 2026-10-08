# NeurOS // before the kernel


![NeurOS animated boot splash](assets/neuros-splash.gif)


```text
NSD / FIRMWARE EXPERIMENTS

architecture : x86_64
interface    : UEFI / GNU-EFI
target       : \EFI\Linux\arch-linux.efi
job          : get Arch running. look suspicious doing it.
```

**As a thanks to Neuro for the DWM Setup**

Loads an existing Arch UKI from the same EFI System Partition. Linux handles the kernel handoff. NeurOS supplies 2.9 seconds of logo-only glitch: gold, warm fragments, cyan tearing, then a clean finish. The background stays black.

The logo stays until handoff. Enter or Esc skips the animation after a brief input guard against the boot-menu key. Graphics failures show a status for two seconds before continuing. If boot fails or returns, a separate recovery screen uses gold/cyan accents and muted diagnostics: **Enter** retries Arch, **R** opens systemd-boot, **Esc** returns to the caller.

**Compile. Interrogate.**

```sh
sudo pacman -S --needed base-devel gnu-efi qemu-system-x86 qemu-ui-gtk edk2-ovmf
make
make test
make run QEMU="qemu-system-x86_64 -display gtk"
```

Tests cover native tools and four isolated QEMU boot cases. Without a staged UKI, the preview lands on recovery. To supply one:

```sh
make stage-uki UKI=/path/to/arch-linux.efi
```

Your host's root partition does not magically appear inside QEMU. Use a VM-compatible UKI and root disk for a full OS boot.

**Put it on metal.**

Requires an existing Arch UKI, systemd-boot, and Secure Boot disabled. With the ESP mounted at `/boot`:

```sh
make test package
sudo ./build/neuros-install --esp /boot
```

Adds `EFI/NeurOS/neuros.efi` and `loader/entries/neuros.conf`. Refuses overwrites; leaves boot order and the default entry alone. Keep the installer beside `build/deploy/`.

Reboot, hold Space for systemd-boot, select **NeurOS**. Keep your working Arch entry as the default until hardware testing passes.

```text
kernel ownership : Linux
firmware patience: being tested
```
