# MyOS v0.5

Text-only operating system project for 32-bit x86, booted by GRUB Multiboot2.

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

```bash
make
make verify
make run
```

Everything is session-only for now; persistent filesystems and real user processes are not yet implemented.
