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
