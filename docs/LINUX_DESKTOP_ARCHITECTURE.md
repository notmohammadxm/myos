# MyOS Linux Desktop Architecture

## Runtime stack

```text
MyOS applications (Qt 6 / QML)
  ├── MyOS Settings
  ├── MyOS Launcher / Panel components
  └── MyOS Notification Center
            │
            ▼
KDE Plasma session (initial shell and integration host)
            │
            ▼
KWin Wayland compositor ── XWayland compatibility
            │
            ▼
Wayland protocol / libinput / Mesa
            │
            ▼
Linux kernel (Debian-supported amd64 kernel)
            │
            ▼
Firmware + physical / virtual hardware
```

System services run alongside the graphical stack:

- systemd: boot, service supervision, logind and session lifecycle.
- NetworkManager: network configuration.
- udev (systemd-udevd): device discovery and permissions.
- PipeWire: audio and media routing.
- APT/dpkg: signed package installation and updates.
- polkit: privileged desktop actions with explicit authorization.

## Component ownership

| Component | Initial implementation | MyOS ownership |
|---|---|---|
| Kernel and drivers | Debian Linux kernel | Configuration and release selection |
| Bootloader | GRUB from Debian | Theme/configuration |
| Login manager | SDDM | Theme and defaults |
| Wayland compositor | KWin | Configuration; no fork initially |
| Desktop shell | KDE Plasma | Layout, theme, widgets, gradual replacement |
| UI toolkit | Qt 6 / QML | MyOS applications and components |
| Notifications | Plasma/freedesktop notification service | MyOS presentation and settings integration |
| File manager | Dolphin initially | Branding/configuration; replace only if justified |
| Terminal | Konsole initially | Branding/configuration; reuse shell ecosystem |
| Package management | APT/dpkg | MyOS repository policy and curated package set |

## Important interface boundaries

- Desktop applications run as unprivileged user-space processes.
- UI applications must not access hardware ports or kernel memory directly.
- Hardware operations go through kernel drivers and established user-space APIs.
- Privileged actions use system services and polkit authorization, never ad-hoc root GUI processes.
- Notifications use the freedesktop desktop notification interface.
- Window state and activation use supported Wayland/KDE interfaces; Wayland clients cannot assume unrestricted global window enumeration or control.
- MyOS-specific configuration must live in version-controlled package assets and user configuration templates, not patches to the Linux kernel.

## Initial session

The first working Linux edition should start a standard Plasma Wayland session. This provides a complete fallback desktop while MyOS applications are developed. The login screen should expose the MyOS session as the default only after it is tested; a stock Plasma fallback must remain available.

## Security model

- Keep Secure Boot support as a later, explicit release decision; do not imply it works until signed boot artifacts are tested.
- Use Debian security repositories and normal package signature verification.
- Do not ship a root password or enable passwordless administrative access.
- Keep application privileges separated from desktop presentation.
- Avoid bundling proprietary firmware by default unless redistribution terms are verified; document optional firmware packages.

## Performance targets (initial, to be measured)

These are engineering targets, not current guarantees:

- Idle RAM and boot time are measured in QEMU and on reference hardware before optimization.
- The ISO should avoid development tools, compilers and debug symbols in the runtime image.
- Prefer Debian Stable packages and hardware acceleration when available; provide a software-rendering fallback for virtual machines.
- Publish actual ISO size, idle memory use and boot measurements with each milestone.
