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


## Phase 7

- Fixed the main GUI performance bottleneck: physical framebuffer presentation is now limited to the requested dirty region instead of copying the entire framebuffer for every small interaction.
- Terminal editing requests only repaint the terminal window region.
- Window dragging repaints only the union of the old/new window rectangles.
- Cursor overlay is restored before partial redraws to prevent cursor trails.
- Added a live System Monitor GUI app with uptime, framebuffer mode, mouse position and kernel-service status.
- Expanded the Dock with the System Monitor.
- Existing shell and GUI functionality is preserved.
- Tests: `make test` PASS; freestanding i386 C compile with `-Wall -Wextra -Werror` PASS.
- Bare-metal ISO/QEMU boot was not verified in this environment because NASM/GRUB/QEMU are unavailable.

## Phase 8 UI/interaction overhaul

- Reworked the desktop header: the right-side status, live RTC clock and date now have explicit spacing and no overlap.
- Rebuilt the Dock layout with wider cells and dedicated Terminal, System, Settings, Calculator, Monitor and Power icons; long labels remain inside their buttons.
- Added a MYOS application launcher from the top-left OS/MENU control.
- Window close/minimize operations now explicitly repaint the exposed area, eliminating stale window images that previously disappeared only after hovering another control.
- Window dragging uses clipped repainting and preserves the underlying windows correctly when z-order changes.
- System Information was redesigned to a compact two-column layout so text stays inside the window while moving it.
- System Monitor was redesigned to fit its content cleanly and now shares the live RTC clock state with the desktop.
- The GUI Calculator now actually evaluates expressions with +, -, *, /, %, parentheses and unary signs; errors show as `ERR` instead of silently returning zero. `C` and `DEL` were added.
- Renderer now supports a clip rectangle, so a small GUI change only redraws the requested screen region in the backbuffer.
- Framebuffer presentation uses 32-bit pixel copies rather than byte-at-a-time copying.
- Mouse and keyboard processing are budgeted per scheduler pass so a busy input queue cannot starve the other input path.
- GUI clock synchronization is rate-limited to once per timer second, avoiding repeated RTC reads during the same tick.
- Added GUI window shortcuts from the shell input path: `Ctrl+1` Terminal, `Ctrl+2` System Information, `Ctrl+3` Settings, `Ctrl+4` Calculator, `Ctrl+5` System Monitor.
- Added unit coverage for the GUI calculator evaluator.
