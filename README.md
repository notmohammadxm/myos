# MyOS v0.5

32-bit x86 operating system project, booted by GRUB Multiboot2, with a graphical framebuffer desktop and PS/2 mouse support. VGA text mode remains the safe fallback when a usable framebuffer is unavailable.

## Added features

- Calculator: `calc (12+8)*3^2-5`
- Themes: `theme matrix`, `theme ice`, `theme amber`, `theme mono`
- Command history: Up/Down arrows and `history`
- Tab completion
- User profile: `profile`, `whoami`, `hostname`
- System info: `sysinfo`, `mem`
- RTC date/time: `time`, `date`, `clock`, `calendar`
- Task manager: `taskmgr`
- Help center: `help`, `help shell`, `help system`, `help network`
- Settings: `settings`, `set username=NAME`, `set hostname=NAME`
- Notifications: `notify MESSAGE`, `notifications`
- Logs: `logs`
- Network tools: `net`, `ping HOST` (PCI NIC discovery is available; full TCP/IP is not yet implemented)

## Build

Run the host-side regression tests first, then build and verify the Multiboot2 kernel.

```bash
make test
make
make verify
make run
```

The `shutdown` command now uses validated ACPI S5 data when available and otherwise halts the CPU safely. Reboot uses ACPI reset, the 8042 reset path, and finally the chipset reset port as fallbacks.

Everything is session-only for now; persistent filesystems and real user processes are not yet implemented.

## GUI architecture

The GUI is layered over the existing shell command engine instead of replacing it. The first GUI phase adds:

- Multiboot2 framebuffer discovery and validation (32-bit direct RGB).
- A small freestanding renderer for rectangles, borders, lines, and bitmap text.
- A desktop layout with Terminal, System Info, Settings, notification, and dock surfaces.
- PS/2 mouse initialization and IRQ12 event delivery.
- A graphical terminal backend so the existing shell commands continue to run through the same command executor.
- VGA text fallback when the framebuffer is not usable.

The initial GUI target is a 1024x600-or-larger 32-bit direct-RGB framebuffer.
