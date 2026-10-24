# NOVA OS Native 0.3 — macOS-inspired desktop

This is a **real, independent x86-64 hobby kernel** (not Debian, not
Linux, and not a website) with an original desktop rendered to the
Limine framebuffer.

## Download
Version 0.3: https://github.com/RealZyynyu/test/releases/tag/nova-native-v0.3

Download `nova-os-native-x86_64.iso`, then attach it to VirtualBox as
a virtual optical drive. A checksum and optional VM screenshot are
included in the release.

## Appearance
- Panoramic original ribbon-style wallpaper with daytime/nighttime versions
- Frosted-looking top menu bar with app menu, status glyphs, and clock
- A centered six-app dock with custom NOVA app icons
- Rounded Finder-like window, subtle shadows, red/yellow/green controls
- Sidebar locations and content, original illustrated folder icons
- Familiar Notes, Arcade, Settings and About screens

This is an **original visual interpretation** of a macOS-like desktop,
not a pixel-perfect Apple desktop: no Apple logos, system fonts or
copyrighted macOS wallpapers are included.

## Controls / limitations
- **Keys 1 through 6** choose Welcome, Finder, Notes, Arcade, Settings,
  About. The default screen is Finder.
- **Esc** returns Home; **T** toggles light/dark theme outside Notes.
- Arcade: **WASD** or arrow keys move your tile.
- Notes: type letters, space or Backspace. Stored in RAM only.
- The dock and window controls are decorative for now. **No mouse driver**.

## VirtualBox on Windows 11
Set a 64-bit guest type (**Other/Unknown (64-bit)** or equivalent),
1 CPU, 2048 MB RAM, VMSVGA, BIOS boot (EFI off). No virtual hard disk
is necessary. Mount the ISO and start the machine.

If the Limine bootloader says
`This CPU does not support 64-bit mode`, shut the VM down, verify that
you selected a **64-bit VM guest** and check in Windows Task Manager >
Performance > CPU that virtualization is Enabled. Changing the kernel
UI cannot fix an incorrectly configured CPU.

## Safety
This prototype CANNOT replace Windows 11. There is no persistent
filesystem, usable installer, multitasking, USB or GPU driver stack,
networking, security maintenance or native app ecosystem. Test in
VirtualBox only. **Do not install it on your real disk.**

## Building
GitHub Actions compiles the project on branch `nova-os-native` using
Clang, Limine's official C template and xorriso, then publishes the ISO
to a private GitHub prerelease (Actions artifact storage quota was full).
Source: `kernel/main.c`, `kernel/font.h`; MIT license for original
NOVA OS code. Upstream boot components retain their own licenses.
