# MyOS Linux live image (proof of concept)

This configuration builds an amd64 Debian 13 (Trixie) live ISO using Debian live-build. It auto-enters a Plasma Wayland desktop in the live session, with NetworkManager, Wi-Fi firmware and Firefox ESR preinstalled. It is separate from the existing root-level i386 kernel build.

## Build on Debian/Ubuntu

Install the host tools:

```sh
sudo apt-get update
sudo apt-get install live-build debootstrap squashfs-tools xorriso \
  grub-pc-bin grub-efi-amd64-bin grub2-common mtools dosfstools
```

Build:

```sh
cd linux/live-build
chmod +x auto/config config/hooks/normal/*.hook.chroot
sudo ./auto/config
sudo lb build
```

The resulting ISO is created in this directory. Exact output name depends on live-build metadata; CI locates the generated ISO and publishes its SHA-256 checksum.

## Initial scope and limitations

- amd64 only; legacy i386 remains separate.
- Debian Trixie package repositories; the Linux kernel is installed as a Debian package, not copied into this repository.
- Plasma Wayland/KWin is the initial desktop. MyOS-specific branding and Qt/QML shell components are subsequent milestones.
- This first image configuration is a build proof of concept, not an installer or stable release.
- CI builds the image and runs a QEMU smoke test. The smoke test verifies a Plasma session, Firefox ESR, NetworkManager and successful HTTPS access through the virtual network adapter. This validates a QEMU NAT/Ethernet path; Wi-Fi and physical-hardware coverage still require real-device testing.

## Live-session account and networking

The live system uses the standard temporary `user` account and SDDM first-boot autologin into Plasma Wayland. This is a live-image convenience, not an installed-system security policy. NetworkManager provides automatic wired connectivity and the Plasma network tray provides Wi-Fi selection. The image includes `wpasupplicant` and common Intel, Realtek, Broadcom and Atheros/MediaTek firmware packages; actual Wi-Fi support still depends on the specific laptop adapter and firmware compatibility.

Firefox ESR is installed and registered as the default web browser and is also available from the application launcher.

## Package policy

The package list is explicit and intentionally small relative to a full KDE metapackage. Debian dependencies and recommends may add packages. Review the final package manifest and image size from CI before promoting the configuration.
