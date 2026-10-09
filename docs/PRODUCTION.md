# `crash@nsd:~$ cat /etc/nsd/boot-procedure`

```text
[ NSD / PRODUCTION FIELD MANUAL ]

operator      : Crash
project       : NeurOS
target        : Intel / AMD x86_64 UEFI PCs
boot volume   : separate FAT32 ESP mounted at /boot
assignment    : make the machine boot, then make it do it again
```

The root filesystem can be ext4, Btrfs, or encrypted storage your kernel and initramfs understand. Btrfs is another Linux filesystem; NeurOS does not mount it or any other Linux root filesystem. Firmware reads the ESP, NeurOS hands off, and the initramfs sorts out root. An encrypted `/boot` is outside this setup.

QEMU/OVMF is our development evidence. Physical hardware still gets a vote. Boot the actual distro on the intended machines before promoting a build to default.

<a id="quick-start"></a>
## `crash@nsd:~$ quick-start`

From the repository root on Arch:

```sh
sudo pacman -S --needed base-devel linux-api-headers gnu-efi qemu-system-x86 qemu-ui-gtk edk2-ovmf
make
make test
```

`make` builds the EFI executable and host tools. `make test` runs the native checks and five isolated QEMU boot cases. No root privileges are needed for the build or VM tests. The output goes under `build/`; your real `/boot` is not involved.

<a id="how-to-run"></a>
## `crash@nsd:~$ run --inside qemu`

For the splash and recovery preview:

```sh
make run QEMU="qemu-system-x86_64 -display gtk"
```

A fresh build has no Linux payload staged. Recovery is the expected destination. The animation cannot manufacture a kernel through sheer fucking confidence.

For a direct Linux test, first find an installed kernel:

```sh
ls /usr/lib/modules/*/vmlinuz
```

Set this to one actual path from that output. Keep the same shell for the following commands:

```sh
NEUROS_KERNEL=/usr/lib/modules/REPLACE_WITH_INSTALLED_RELEASE/vmlinuz
make test-linux KERNEL="$NEUROS_KERNEL"
```

The test builds its own static `/init` and initramfs. Successful handoff prints:

```text
NEUROS: direct Linux init reached
NEUROS: initramfs and command line verified
```

To watch that test kernel boot through the normal animated loader, stage it in the VM ESP:

```sh
printf '%s\n' 'console=ttyS0,115200 neuros_test=direct panic=-1' > build/test-cmdline.txt
make update-linux KERNEL="$NEUROS_KERNEL" CMDLINE="$PWD/build/test-cmdline.txt" INITRD="$PWD/build/test-initramfs.cpio"
make run QEMU="qemu-system-x86_64 -display gtk -cpu max -serial file:$PWD/build/linux-serial.log"
```

Read serial output from another terminal in the repository:

```sh
tail -f build/linux-serial.log
```

The test `/init` stays running after printing its result. Close QEMU when finished. The GUI command assumes the repository path has no spaces. Use a serial-log path without spaces if yours does.

This proves the test kernel reached PID 1. It does not boot your installed Arch root filesystem. **Keep `build/test-initramfs.cpio` and `build/test-cmdline.txt` in the test setup.** Your production boot set needs the distro's actual root options and matching initramfs.

For a VM-compatible UKI instead, on a VM ESP with no direct boot set selected:

```sh
make stage-uki UKI=/path/to/arch-linux.efi
make run QEMU="qemu-system-x86_64 -display gtk"
```

If a direct set is already staged, press **M**, then **U** to explicitly choose the UKI. A full OS boot also needs its root disk attached to QEMU; the default `make run` attaches only the staged ESP. The normal preview uses the standard OVMF template. Use `test-secureboot` below for the enrolled Secure Boot test setup.

<a id="install-on-the-actual-machine"></a>
## `crash@nsd:~$ install --on the-actual-machine`

The supported installer route is **systemd-boot → NeurOS → Linux**. It requires an existing working systemd-boot installation and a real Arch UKI. Check the machine first:

```sh
bootctl status
findmnt /boot
bootctl list
```

The ESP must be FAT32, mounted directly at `/boot`, with these existing paths:

```text
/boot/EFI/systemd/systemd-bootx64.efi
/boot/EFI/Linux/arch-linux.efi
/boot/loader/entries/
```

If you use GRUB, have a different ESP layout, or have no UKI, prepare that setup before using this installer. It does not migrate GRUB, create a UKI, or install systemd-boot. Keep a working Arch entry and a rescue USB. Root access is not a recovery strategy.

With Secure Boot **disabled**:

```sh
make test package
sudo ./build/neuros-install --esp /boot
```

With Secure Boot **enabled**, use your own signing key and certificate:

```sh
make package-signed SIGN_KEY=/path/to/key.pem SIGN_CERT=/path/to/cert.pem
sudo ./build/neuros-install --esp /boot --signed
```

The signing certificate must already be enrolled in firmware trust. NeurOS, the UKI, and systemd-boot must all be trusted. Keep private keys off the ESP and out of the distro image. Installer checks cover signature containers; firmware checks authenticity and revocation. Key enrollment and rotation are separate provisioning jobs. These commands do not touch host keys.

The installer adds `EFI/NeurOS/neuros.efi` and `loader/entries/neuros.conf`. It refuses existing destinations and leaves the boot order and default alone. Keep `build/neuros-install` beside its `deploy/` or `deploy-signed/` package directory. Kernel and initramfs staging is separate.

Check the new entry, then select it for **one boot**:

```sh
bootctl list
sudo bootctl set-oneshot neuros.conf
sudo reboot
```

You can also hold **Space** during systemd-boot and select **NeurOS** manually. With no direct kernel staged, NeurOS launches the existing Arch UKI. After it boots your actual system and you have checked recovery, make it persistent:

```sh
sudo bootctl set-default neuros.conf
```

<a id="update-the-loader"></a>
## `crash@nsd:~$ replace old-firmware-business`

If the installed NeurOS is old, use the explicit update option. Do not delete it just to get past the installer's overwrite check.

With Secure Boot **disabled**:

```sh
make package
sudo ./build/neuros-install --esp /boot --update
```

With Secure Boot **enabled**:

```sh
make package-signed SIGN_KEY=/path/to/key.pem SIGN_CERT=/path/to/cert.pem
sudo ./build/neuros-install --esp /boot --update --signed
```

Both the installed executable and menu entry must exist. The updater locks the loader directory, validates a private staged executable, saves the old executable as `/boot/EFI/NeurOS/neuros.efi.previous`, then replaces the active one. It preserves the menu entry, boot order, and default.

The backup is replaced on each update. It requires manual restoration from a working OS or rescue media; it is not an automatically selected fallback. Updating NeurOS does not update Linux. They are separate pieces of the machine's morning problems.

<a id="boot-modes-and-recovery"></a>
## `crash@nsd:~$ cat escape.routes`

With Secure Boot off, the loader can use versioned direct Linux sets, legacy flat files, or the UKI. With Secure Boot on, it launches only firmware-authorized EFI images; the default is `/EFI/Linux/arch-linux.efi`. External unsigned kernels, command lines, and initramfs images do not get loaded by NeurOS in that mode. Trust comes from firmware `db`/`dbx`. Shim/MOK integration is not implemented.

Press **M** during the splash to stop before launching a kernel.

If graphics are unavailable or the splash fails, **M** remains available during
a two-second input window before boot. A working firmware keyboard/input console
is required; the fallback prompt appears when firmware text output is available.

| Recovery key | Action |
|---|---|
| Enter | Retry the default boot path |
| P | Boot the previous direct Linux set once; Secure Boot must be off |
| U | Boot the current UKI |
| B | Boot the previous UKI |
| R | Open systemd-boot |
| Esc | Return to the caller |

**P** does not persist a rollback. **R** opens systemd-boot with its existing configuration; choose your working Arch entry. The splash and recovery styling stay the same. If Linux panics after handoff, reset and press **M** on the next boot. Firmware recovery cannot rewind a kernel that already owns the machine.

<a id="direct-kernel-updates"></a>
## `crash@nsd:~$ update --subject linux`

Use direct boot with Secure Boot **disabled**. After your distro has generated a matching initramfs and the correct `/etc/kernel/cmdline`, you can stage the first real boot set from the repository:

```sh
make tools
sudo ./build/neuros-update-linux \
  /usr/lib/modules/KERNEL_RELEASE/vmlinuz \
  /etc/kernel/cmdline \
  /boot/initramfs-linux.img \
  /boot/EFI/NeurOS
```

Replace `KERNEL_RELEASE` with the release you are installing. `uname -r` reports the running kernel, which may be older. Adjust the initramfs path for your kernel package. The command line must contain the correct `root=`, filesystem, and encryption options. Include early microcode in the supplied initramfs as appropriate. Guessing a root UUID is an excellent way to spend the evening in an emergency shell.

For distro packaging, install `neuros-update-linux` and its sibling `stage-linux` together, for example under `/usr/lib/neuros/`. Run the updater only after mkinitcpio has successfully finished generating the image:

```sh
sudo /usr/lib/neuros/neuros-update-linux \
  /usr/lib/modules/KERNEL_RELEASE/vmlinuz \
  /etc/kernel/cmdline \
  /boot/initramfs-linux.img \
  /boot/EFI/NeurOS
```

A failed image-generation step must abort the packaging hook. No pacman hook is installed automatically: the distro's image names, root layout, and module-retention policy need to be defined first.

The updater locks the boot directory, copies and validates a private set, flushes the files, publishes a unique `set-*` directory, then atomically renames and flushes `boot.conf`. Published sets are not edited or deleted. A first update preserves a complete legacy `vmlinuz`/`cmdline.txt`/`initrd` set as `legacy`; later updates preserve the prior selection.

The selection file is deliberately small:

```text
NEUROS1
set-<lowercase hexadecimal identifier>
set-<previous identifier>
```

The third line can be `legacy` or `-` for no previous set. `legacy` can also be current after rollback. Arbitrary paths, extra data, missing newlines, and duplicate selections are rejected. Versioned sets require all three files, including `initrd`. A missing file sends you to recovery; the loader does not borrow half of another kernel's boot set.

To persist a rollback from the repository:

```sh
sudo ./build/neuros-update-linux --rollback /boot/EFI/NeurOS
```

For packaged tools, use `/usr/lib/neuros/neuros-update-linux` instead. “Previous” means the prior selection. It does not mean “automatically proven to boot.” Verify an update by booting the distro before publishing another.

Retain the previous kernel's `/usr/lib/modules/<release>` tree too. A saved kernel without its loadable drivers is an incomplete escape route. Multiple installed kernel packages are preferable to overwriting the only working one.

Interrupted copies may leave `.new-set-*` directories or unselected published sets; boot ignores them. An interrupted selection write can leave `.boot.conf.new`, which the updater refuses to overwrite. Inspect `boot.conf` and its selected files before removing only that stale temporary file. Never delete current or previous sets. Old sets are not garbage-collected; watch ESP free space.

Rename plus `fsync` prevents mixed sets during ordinary updates and process termination. FAT32 has no journal. A power cut, broken firmware, or lying storage can still corrupt metadata. Process-kill tests do not prove power-loss safety. Keep independent recovery media and test the intended storage hardware.

<a id="signed-uki-updates"></a>
## `crash@nsd:~$ update --subject signed-uki`

Build a UKI containing the matching kernel, initramfs, command line, and OS release metadata with the distro's image-generation configuration. Sign the complete result with the distro's protected signing key, then publish it:

```sh
sudo ./build/stage-uki /path/to/signed-arch-linux.efi /boot/EFI/Linux/arch-linux.efi
```

For packaged tools, use `/usr/lib/neuros/stage-uki` instead. The previous complete image is saved as `arch-linux.efi.previous` and is available through recovery **B**.

Staging checks PE/UKI structure. Firmware enforces trust and revocation. Verify that current and previous UKIs both boot with the enrolled `db`/`dbx`. A revoked signature does not become valid because the file has “previous” in its name.

<a id="release-validation"></a>
## `crash@nsd:~$ prove it`

```sh
make check-tools
make test-linux KERNEL=/path/to/vmlinuz
make test-matrix KERNEL=/path/to/vmlinuz
make test-secureboot KERNEL=/path/to/vmlinuz
```

Or run the combined matrix and Secure Boot suite:

```sh
make test-production KERNEL=/path/to/vmlinuz
```

`test-matrix` runs the normal boot suite, then the direct Linux suite on a 256 MiB PC with an emulated Intel vendor and a 4 GiB Q35 machine with an emulated AMD vendor. Vendor strings are test inputs. They do not turn QEMU into a rack of physical motherboards.

Coverage includes memory-map expansion, E820 extensions, stale exit keys, versioned selection, and recovery before booting a previous set. Native checks cover interrupted/failed writes, locking, malformed selections, migration, rollback, loader replacement, and UKI preservation. Logs live in `build/test-logs/`.

All runners are compiled C. `test-secureboot` additionally needs OpenSSL, GNU objcopy, systemd-sbsign, QEMU/OVMF, and a systemd EFI stub. Supply a pristine raw authenticated OVMF variable-store template through `OVMF_VARS`; the default distribution template works. Populated or unsupported stores are rejected. The runner constructs disposable variable stores and test UKIs, generates disposable keys, and checks a trusted signed UKI plus rejection of unsigned, tampered, untrusted, and revoked images. Host firmware keys are not touched.

Before a distro release, boot the actual root filesystem on representative Intel and AMD machines with Secure Boot both on and off. Check cold boots, warm reboots, low ESP space, USB keyboard recovery, graphics handoff, kernel upgrades, previous-kernel module loading, and interrupted-update recovery. Record motherboard and firmware version, GPU, storage device, kernel release, and results.

```text
[ STILL ON THE BOARD ]

automatic boot counting          : not implemented
rollback after a hung kernel    : manual
physical power-loss testing     : still required
shim / MOK integration          : not implemented
motherboard mind-reading        : unlikely
```

Sources are in [QUICK_REF.md](../QUICK_REF.md). Read the contract before arguing with the silicon.
