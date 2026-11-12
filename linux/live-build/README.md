# MyOS Linux live image (proof of concept)

This configuration builds an amd64 Debian 13 (Trixie) live ISO using Debian live-build. It is separate from the existing root-level i386 kernel build.

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
- The first CI job validates image creation and artifact integrity. A graphical QEMU login/session smoke test is still required before Phase 1 can be marked complete.

## Package policy

The package list is explicit and intentionally small relative to a full KDE metapackage. Debian dependencies and recommends may add packages. Review the final package manifest and image size from CI before promoting the configuration.
