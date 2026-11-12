# MyOS Linux Migration Plan

**Status:** Phase 0 — architecture and migration baseline  
**Target:** x86_64 desktop ISO  
**Legacy line:** existing 32-bit i386 / Multiboot2 kernel on `main`

## Decision

The next-generation MyOS is a Linux distribution with a MyOS-branded hybrid desktop. The existing hobby kernel is retained as a separate legacy/learning track. This migration does not replace or rewrite the current kernel in place.

## Why this is a re-platform, not a kernel swap

The current kernel owns its boot path, interrupt handling, memory model, framebuffer renderer, input drivers, shell, and GUI event loop. Linux exposes different interfaces and expects user-space processes, a root filesystem, device management, graphics stack, and service supervision. Existing kernel GUI code cannot be linked on top of Linux as a desktop. User-facing behavior and design can be reimplemented in user space.

## Target platform

- **CPU:** x86_64 (amd64). The current i386 target remains available on the legacy line.
- **Base:** Debian Stable amd64, initially using Debian's supported kernel packages and security updates.
- **Boot/media:** hybrid BIOS/UEFI live ISO; installer support is a later milestone.
- **Init/service manager:** systemd.
- **Graphics session:** Wayland, with XWayland compatibility where needed.
- **Compositor/window manager:** KWin from KDE Plasma.
- **Desktop foundation:** KDE Plasma and Qt 6 / KDE Frameworks.
- **MyOS identity:** custom branding, theme, wallpaper, boot artwork, panel/launcher configuration and a growing set of MyOS Qt/QML applications.
- **Package management:** APT and Debian repositories; do not invent a separate package format.

## Hybrid desktop definition

“Hybrid” means reuse mature KDE components while replacing the visible experience incrementally with MyOS-owned components. It does **not** mean running two independent desktop shells or writing a Wayland compositor in the first release.

1. First boot into a customized Plasma Wayland session using KWin.
2. Apply MyOS theme, iconography, wallpaper, panel layout, launcher entries and defaults.
3. Implement MyOS panel/launcher/notification/settings applications with Qt 6 and QML, initially integrated as Plasma widgets or companion applications.
4. Replace selected Plasma-facing components only after equivalent MyOS components pass usability and accessibility tests.
5. Keep KWin as the compositor initially. Reconsider a custom compositor only if a demonstrated technical requirement justifies the maintenance cost.

## Migration phases and acceptance criteria

### Phase 0 — Architecture and safe branch (this change)

- Keep `main` and the i386 kernel build unchanged.
- Create a dedicated `migration/linux-desktop` branch.
- Record architecture decisions, repository layout and staged acceptance criteria.
- No claim of a bootable Linux ISO at this phase.

**Exit:** reviewed architecture docs; no changes to legacy build path.

### Phase 1 — Linux live-image proof of concept

- Add a reproducible Debian live-build configuration for amd64.
- Include Linux kernel, systemd, GRUB BIOS/EFI support, firmware where licensing permits, NetworkManager, SDDM, Plasma, KWin Wayland, Qt 6 and XWayland.
- Build in CI using pinned Debian release and package manifests.
- Boot under QEMU in both BIOS and UEFI modes where runner support permits.
- Verify a graphical login/session, not merely a kernel boot.

**Exit:** downloadable ISO boots to a working Plasma Wayland desktop in QEMU; build manifest and checksums are published.

### Phase 2 — MyOS visual identity

- MyOS Plymouth/boot branding, SDDM theme, Plasma theme, icons, cursor, wallpaper and sound defaults.
- MyOS desktop layout and curated application menu.
- Preserve a stock Plasma fallback session for troubleshooting.

**Exit:** consistent branding from boot/login to desktop; fallback session remains usable.

### Phase 3 — MyOS user-space shell components

- Qt 6/QML launcher and panel integration.
- Notification center using desktop notification standards (freedesktop notifications).
- Settings app backed by documented system interfaces (NetworkManager, systemd/logind, display settings).
- Window/task interaction via supported KDE/Wayland interfaces; do not manipulate compositor internals.
- File manager and terminal initially reuse mature applications, with MyOS branding/configuration.

**Exit:** components install as packages, start in the session, survive logout/login, and pass functional tests.

### Phase 4 — Installer, updates and hardware coverage

- Installer and disk partitioning with explicit destructive-operation safeguards.
- APT update policy, signed repository strategy and rollback/recovery documentation.
- Hardware matrix: graphics, Wi-Fi, audio, storage, suspend/resume, USB, touchpad and multiple displays.
- Test on real hardware before calling the release stable.

**Exit:** installation and upgrade are repeatable; recovery path and supported hardware are documented.

### Phase 5 — Release engineering

- Versioned ISO, SBOM/package manifest, checksums and reproducible build notes.
- Automated image boot tests and desktop smoke tests.
- Release notes distinguish Linux edition from the legacy hobby-kernel edition.

**Exit:** signed release artifacts and documented support policy.

## Repository layout target

```text
linux/
  live-build/
    auto/                  # live-build configuration entry points
    config/package-lists/  # explicit package selections
    config/hooks/          # minimal image customization
  desktop/
    themes/
    wallpapers/
    icons/
  apps/
    launcher/
    settings/
    notifications/
  packaging/
docs/
  LINUX_MIGRATION_PLAN.md
  LINUX_DESKTOP_ARCHITECTURE.md
src/                       # existing i386 kernel; preserved
iso/                       # existing legacy GRUB image
```

Only create directories/files when their first working implementation is added; avoid empty placeholders.

## Risks and controls

| Risk | Control |
|---|---|
| Existing i386 GUI cannot run as Linux desktop code | Reimplement behavior in user space; preserve source as legacy reference |
| Desktop image becomes too large | Start with a minimal package manifest; measure ISO and RAM footprint |
| GPU/Wayland incompatibility in VMs | Keep a documented fallback session and test QEMU display adapters |
| Upstream KDE/Qt churn | Use Debian Stable packages; avoid tracking rolling releases |
| Security and update burden | Use Debian security updates and signed packages; document image rebuild cadence |
| Project scope expands into a full distro | Reuse Debian installer, package manager, systemd and KDE infrastructure |

## Definition of done for the migration

MyOS Linux is not considered complete merely because Linux boots. It must boot to a usable graphical session, support login/logout, networking, storage, notifications, window management and software updates, and provide a tested installer/recovery path before a stable desktop release is announced.
