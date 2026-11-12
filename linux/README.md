# MyOS Linux Edition

This directory is reserved for the next-generation Linux-based MyOS distribution. The current i386 hobby kernel remains in the repository root and is not replaced by this work.

The target is Debian Stable (amd64), a Debian-supported Linux kernel, systemd, Wayland/KWin, and a hybrid KDE Plasma + MyOS Qt 6/QML desktop.

An initial Debian live-build configuration is present. The live image is configured to log in to Plasma Wayland, manage wired/Wi-Fi networking with NetworkManager and include Firefox ESR as the default browser. A separate GitHub Actions job builds the ISO and checks desktop, browser and outbound HTTPS from QEMU. The job must pass before a build is considered validated.

See:

- [Migration plan](../docs/LINUX_MIGRATION_PLAN.md)
- [Desktop architecture](../docs/LINUX_DESKTOP_ARCHITECTURE.md)

Do not put Linux kernel source code in this repository. Build against the distribution kernel packages and record package versions in the image manifest.
