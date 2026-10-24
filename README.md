# NOVA OS Native 0.4 — Ultra Close Desktop Preview

Independent x86-64 experimental operating-system kernel. This is not Debian,
Linux, Windows, or a browser simulation. It boots via Limine and draws the
desktop entirely with NOVA's own C framebuffer code.

## Version 0.4 release

- [Download the bootable ISO](https://github.com/RealZyynyu/test/releases/download/nova-native-v0.4/nova-os-native-x86_64.iso)
- [Open the GitHub release](https://github.com/RealZyynyu/test/releases/tag/nova-native-v0.4)
- [Actual QEMU screenshot](https://github.com/RealZyynyu/test/releases/download/nova-native-v0.4/nova-os-native-preview.png)

The ISO (~5.2 MB) was compiled by GitHub Actions. The VM screenshot
was captured automatically from QEMU (x86-64) during CI, but hardware
compatibility beyond that environment has not been established.

## Desktop appearance

This release explores the spacing, translucent appearance, dock-centered
layout, rounded windows, and Finder-inspired browsing conventions of
modern macOS while retaining original NOVA symbols, icons, and wallpaper.

- Independently drawn colorful panoramic wallpaper
- Thin top menu bar and status symbols
- Glass-like floating six-app dock with app indicators
- Three window-control dots, subtle shadows, unified window toolbar
- Finder-like folders, sidebar, location indicators and search field
- Notes, Arcade, Settings, About and Welcome screens
- Light/dark appearance toggling

### Typography and credits

The CI workflow rasterizes the open-source **Inter** typeface into glyph
bitmaps for anti-aliased, proportional text. The font is installed only
for the build; original font files are not committed to NOVA.
Inter © its authors, licensed under SIL Open Font License 1.1:
https://github.com/rsms/inter

The NOVA kernel code and original graphics are under the MIT license.
Limine and other upstream components retain their original licenses.
No original Apple icons, logos, system fonts, or wallpaper assets are included.

## Keyboard

- 1–6: switch between Welcome, Finder, Notes, Arcade, Settings, About
- Escape: home screen
- T: switch appearance, except when using Notes
- Notes: type letters/space, Backspace to delete; RAM only (not saved)
- Arcade: WASD or arrow keys

The dock and window buttons are **visual only**, because there is no
working mouse driver in NOVA Native yet. The search box is visual too.
Menu bar date/time is currently illustrative, not synchronized.

## Windows 11 / VirtualBox

Create an **Other/Unknown (64-bit)** virtual machine with BIOS firmware,
2048 MB RAM and one CPU. No hard drive is needed. Attach the ISO as a
virtual optical disk and boot.

If Limine says "This CPU does not support 64-bit mode", turn off the VM
and choose a 64-bit guest type; verify virtualization support on the
Windows 11 host. The ISO cannot fix incorrect virtual CPU settings.

## Do not install on real hardware

NOVA Native is not ready to replace Windows. It lacks a real filesystem,
durable data storage, an installer, modern storage/USB/GPU/network
drivers, usable multitasking, app execution, security mechanisms, updates,
and Windows game compatibility. Test exclusively in a virtual machine.
Never overwrite your Windows disk with this developer preview.

## Build

The GitHub Actions workflow installs Clang, xorriso, Python Pillow and
Inter, rasterizes glyphs via `tools/generate_font.py`, and copies NOVA's
native C kernel into the Limine C template before building the bootable ISO.
CI optionally launches QEMU and produces a screenshot, and publishes
the release assets to the private repository's GitHub Releases.
