# NOVA OS Native 0.5 — interactive kernel preview

**Standalone, independent x86-64 C kernel** using the Limine bootloader. It is
NOT Debian, Linux, Windows, macOS, or a website. This is a developer
prototype, intended **only for VirtualBox/QEMU**.

## Download and test

Release: https://github.com/RealZyynyu/test/releases/tag/nova-native-v0.5

- `nova-os-native-x86_64.iso` — bootable image (~5.1 MB)
- `nova-os-native-preview.png` — screenshot captured during an emulated boot
- `nova-os-serial.log` — serial console boot diagnostics
- `nova-os-native-x86_64.iso.sha256` — file checksum

The GitHub Actions build compiled the kernel, generated an image, captured
a QEMU preview, and verified serial output contained
`NOVA_KERNEL_BOOT_OK` and `NOVA_RAMFS_READY`.

### VirtualBox (Windows 11)

Configure **Other/Unknown (64-bit)**, 1 CPU, **2048 MB RAM**, BIOS boot,
with 3D acceleration disabled. Use a virtual optical drive for the ISO.
No hard disk is required because persistence and installation aren't
supported. A PS/2 emulated mouse is required for cursor movement. If the
firmware says "This CPU does not support 64-bit mode", correct the VM's
64-bit CPU configuration and verify Windows host virtualization support.

## What's technical and functional in this release

- **PS/2 mouse driver**: controller initialization, stream packets, relative
  movement, left-click events, and a software-drawn cursor. Only legacy
  *emulated PS/2* input is supported; no USB mouse driver.
- **Mouse clickable desktop**: open apps from the dock, sidebar, desktop
  shortcuts and welcome tiles; close/minimize the content window; choose
  light/dark appearance in Settings; click RAM file tiles in Finder.
  Some visible chrome remains simplified (the green control opens Finder).
- **Small RAM-backed file table**: `NOTES.TXT`, `README.TXT` and
  `TODO.TXT`; bounded reads/writes and a Finder-like viewer. Editing Notes
  updates NOTES.TXT immediately, but **everything disappears after reboot**.
- **Tiny kernel command interpreter**: open Terminal with key **7** or the
  Tools/Terminal sidebar link. Commands: `HELP`, `LS`, `CAT NOTES.TXT`,
  `CAT README.TXT`, `CAT TODO.TXT`, `UNAME`, `MEM`, `CLEAR`.
- **Boot and memory reporting**: Limine memory map, basic usable memory
  figure, serial boot diagnostics for VM smoke tests.

### Keyboard controls
- Keys **1–6**: Welcome, Finder, Notes, Arcade, Settings, About
- **7**: Terminal (outside Notes and Terminal; switch using mouse too)
- **Escape**: Home
- **T**: switch appearance except inside text editors
- Notes: type / backspace; characters stored in NOTES.TXT in volatile RAM
- Arcade: WASD / arrow keys; collect golden tile
- Terminal: type commands and press Enter

### What NOVA still lacks

This is **not a replacement for Windows**. No persistent disk filesystem,
safe installer, process isolation, virtual memory manager, scheduler,
USB stack, real graphics acceleration, networking, audio drivers,
security policy or software update mechanism exists yet. The small terminal
runs in kernel mode and is NOT a secure sandbox. The RAM files are a
prototype, not a disk filesystem. Do not boot it on your primary PC or
overwrite a real disk.

### Appearance
An original macOS-inspired desktop, featuring a floating glass-like dock,
Finder-style navigation, rounded windows and smooth Inter-derived text.
No Apple branding or original proprietary assets. Inter is OFL-1.1
licensed: https://github.com/rsms/inter

## Source and build
- `kernel/main.c` — native renderer and system input loop
- `kernel/ps2_mouse.h` — PS/2 driver for QEMU/VirtualBox
- `kernel/ramfs.h` — bounded RAM-only file table
- `tools/generate_font.py` — Open Font License bitmap generation
- `.github/workflows/build-native-iso.yml` — Clang/Limine + QEMU tests
- `limine.conf` — boot configuration

The NOVA original source is MIT-licensed; dependencies retain their licenses.
