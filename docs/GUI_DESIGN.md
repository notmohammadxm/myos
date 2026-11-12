# MyOS GUI interaction model

This document describes the behavior contract for the current single-address-space desktop GUI. It is intentionally scoped to the existing fixed-window kernel GUI; it does not claim that MyOS has a process-isolated window server.

## Window lifecycle

Each desktop window has an active geometry and a saved resume geometry. Minimize and close are transitions toward the dock, not destructive geometry updates. The pre-transition location, size, maximize state, and snap state are saved before the transition begins.

- **Open/restore:** a closed or minimized window starts at its dock slot and animates toward its saved geometry.
- **Minimize:** the window is hidden after reaching its dock slot; its dock entry remains visible.
- **Taskbar activation:** clicking the focused running window's dock entry minimizes it; clicking it again restores it. Clicking a background entry focuses it.
- **Close:** the window is removed from the desktop after its close transition. Its pinned dock icon remains available to reopen it.
- **Interrupted transition:** selecting a window while it is closing or minimizing cancels the transition and restores its saved geometry instead of leaving a stale mini-window.

This follows the user-visible contract documented by Haiku: a minimized window is hidden while its Deskbar entry remains available, and selecting that entry restores it. ToaruOS also constructs its panel window list from the current window-advertisement list rather than treating taskbar buttons as a separate source of truth.

## Notification lifecycle

The kernel's bounded notification ring is the authoritative store. The GUI receives a display snapshot from that ring and never invents placeholder notification rows.

- New kernel notifications update the GUI snapshot immediately.
- The unread marker is a visual hint; opening the panel acknowledges the current snapshot.
- Escape, the close control, and clicking outside dismiss only the popup.
- Clear All queues a GUI action; the kernel clears the ring and publishes the empty snapshot.
- Turning notifications off closes the popup but does not silently delete stored messages.

The separation between dismissing a transient panel and deleting saved notifications is deliberate.

## Rendering and input constraints

MyOS currently uses a software renderer, a fixed backbuffer, and a single kernel-side event loop. UI actions therefore remain bounded and synchronous. Dirty-rectangle rendering remains the preferred path for localized changes. The action queue is finite; increasing its capacity reduces avoidable loss during bursts but does not replace future queue back-pressure and instrumentation.

## External references reviewed

- Haiku, [BWindow::Minimize / Show / Hide semantics](https://api.haiku.nz/kits/interface/window.html)
- Haiku, [Deskbar running-window list and minimized state](https://i18n.haiku-os.org/userguide/data/export/docs/userguide/en/deskbar.html)
- ToaruOS, [panel window-list construction](https://github.com/klange/toaruos/blob/master/apps/panel.c)
- ToaruOS, [window-list interactions and minimize action](https://github.com/klange/toaruos/blob/master/lib/panel_windowlist.c)
- ToaruOS, [release notes describing window minimization and screenshot toast notifications](https://github.com/klange/toaruos/releases)

These projects were used to compare interaction semantics and subsystem boundaries. Their source code was not copied into MyOS.
