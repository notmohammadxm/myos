# MyOS Linux Edition

This directory is reserved for the next-generation Linux-based MyOS distribution. The current i386 hobby kernel remains in the repository root and is not replaced by this work.

The target is Debian Stable (amd64), a Debian-supported Linux kernel, systemd, Wayland/KWin, and a hybrid KDE Plasma + MyOS Qt 6/QML desktop.

No bootable Linux image is claimed yet. The first implementation milestone is a reproducible Debian live-build image that reaches a working Plasma Wayland session in QEMU.

See:

- [Migration plan](../docs/LINUX_MIGRATION_PLAN.md)
- [Desktop architecture](../docs/LINUX_DESKTOP_ARCHITECTURE.md)

Do not put Linux kernel source code in this repository. Build against the distribution kernel packages and record package versions in the image manifest.
