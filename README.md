# MyOS v0.5 — GUI development branch

MyOS is a 32-bit i386 / Multiboot2 operating-system project. The project keeps the existing command shell and adds a graphical desktop layer above it.

## Current capabilities

- Existing shell commands and line editing remain available.
- Calculator, themes, command history, completion, profile, system info, RTC tools, task manager, settings, notifications, logs and network-status commands remain in the kernel.
- Multiboot2 framebuffer detection.
- Software renderer with a bounded backbuffer to prevent visible full-screen redraw flicker.
- Graphical desktop chrome, windows, dock, notification panel and mouse cursor.
- PS/2 mouse input with IRQ12 and drag support.
- Graphical Terminal surface driven by the same shell output/input path.
- GUI fallback to VGA text mode when a supported framebuffer is unavailable.

## Build

```bash
make test
make
make verify
make run
```

`make test` runs host regression tests. `make` builds the ISO. `make verify` validates the resulting kernel as Multiboot2.

## GUI architecture

```text
Kernel services
    ├── keyboard / mouse / timer / RTC / PCI / ACPI
    ├── shell command engine
    └── settings / notifications / system information
                │
                ▼
            GUI layer
       ┌────────┼─────────┐
       │        │         │
    windows  terminal    dock
       │        │         │
       └────────┴─────────┘
                │
             renderer
                │
            backbuffer
                │
          framebuffer
```

The goal is to add GUI functionality without deleting or duplicating the existing command functionality.

## Current limitation

The GUI renderer intentionally uses a fixed static backbuffer because there is no heap allocator yet. The requested boot mode is 1024x768x32. A larger or unsupported framebuffer falls back to the text path.

## GUI Phase 3

- Full-screen PS/2 mouse bounds; cursor can enter top bar and footer.
- Mouse Y-axis follows the physical screen direction used by the current hardware setup.
- Topmost window ordering with focus-on-click and constrained dragging.
- Working window controls: minimize and close.
- Working dock targets: Terminal, System Information, Settings, Power.
- Power menu dispatches the existing `reboot` and `shutdown` shell paths.
- Settings interactions: cycle the existing Matrix/Ice/Amber/Mono themes; toggle notifications and animations.
- Terminal mouse focus, click-to-position within the active input line, and wheel scrolling.
- Embedded 8x14 DejaVu Sans Mono-derived bitmap font for cleaner, consistent kernel rendering.
- GUI rendering remains backbuffered; only the composed frame is copied to the hardware framebuffer.

The GUI remains layered over the existing command engine; existing commands are not removed.

## GUI Phase 4

The GUI now includes a dedicated Calculator window, a five-item dock, application focus/z-order integration, and clickable calculator controls. The original shell calculator remains available unchanged.

## GUI Phase 5

- Terminal keyboard navigation now supports Home/End and PageUp/PageDown.
- Shift+Left/Right selects text in the active command line; Delete/Backspace and normal typing replace the selection.
- Ctrl+L clears the terminal through the existing shell/GUI path.
- GUI terminal selection is rendered visibly without changing the existing shell parser.
- Existing mouse focus, command history, completion, calculator, settings, notifications, reboot and shutdown paths remain intact.


## Phase 6 mouse/input improvements

- Corrected PS/2 Y-axis conversion: physical mouse-up now moves the screen cursor up.
- Added optional PS/2 sample-rate/resolution tuning (200 Hz, 4 counts/mm); unsupported optional commands are non-fatal.
- Increased the mouse event queue to 64 entries.
- Mouse cursor is now rendered as a framebuffer overlay instead of forcing a full GUI redraw for every motion packet.
- Added partial renderer presentation for restoring only the old cursor rectangle.
- Mouse button transitions remain processed individually so fast clicks are not lost.

Full ISO/QEMU boot testing still requires NASM, GRUB and QEMU, which are not installed in the build environment used for this phase.
