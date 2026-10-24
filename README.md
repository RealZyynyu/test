# NOVA OS Native 0.2 — Independent macOS-inspired desktop preview

NOVA OS Native is a small, **independent x86-64 operating-system prototype**.
It is **not based on Debian or Linux** and does not run in a web browser.
The desktop is drawn directly by NOVA's freestanding C kernel, using the
Limine bootloader's framebuffer.

## Download the ISO
[NOVA Native v0.2 prerelease](https://github.com/RealZyynyu/test/releases/tag/nova-native-v0.2)

Download **nova-os-native-x86_64.iso** (about 4.9 MB). A SHA256 file is included.
GitHub Actions built and uploaded it successfully. VirtualBox boot success
is **not yet confirmed** on real user machines.

## Desktop features
- macOS-inspired menu bar, wallpaper, rounded windows and floating dock
- Six keyboard-selectable views: Welcome, Finder, Notes, Arcade, Settings, About
- Light and dark wallpaper: press **T** outside of Notes
- Editable Notes held in RAM (letters, space, backspace); NOT saved to disk
- Playable tile collecting game in Arcade (WASD or arrow keys)
- No NOVA analytics or advertising

Controls: keys **1–6** switch views, **Esc** returns Home, **T** changes
appearance (outside Notes), game uses **WASD / arrow keys**.
There is **no mouse driver** in this prototype; the dock and window controls
are graphical, not clickable.

## VirtualBox on Windows 11
Create a new VM with **Other/Unknown (64-bit)** or **Other Linux (64-bit)**,
one CPU, 2048 MB RAM, and BIOS firmware (EFI off). No hard disk is necessary.
Attach the ISO as the virtual optical drive. Do not install it on your PC.

If Limine says **"This CPU does not support 64-bit mode"**, the VM is not
exposing x86-64 support. In VirtualBox, shut down the VM and select a
**64-bit guest type**. Verify Task Manager > Performance > CPU >
Virtualization = Enabled. The OS image cannot override VM CPU capabilities.

## Important limits
This is **not safe to replace Windows**. It lacks persistent filesystems,
installers, USB input, modern GPU and storage drivers, networking, multitasking,
security updates, application execution and Windows game compatibility.
Test it inside a virtual machine only. Do not flash it onto your primary drive.
The graphics and keyboard are designed for simple emulator environments.

## Build pipeline
Pushes to `nova-os-native` trigger the private GitHub Actions ISO build.
The workflow clones the upstream Limine C kernel template and builds NOVA's
original C code with Clang. ISO assets are published under the GitHub
Releases tab; Actions artifacts were unavailable due to account storage quota.

Kernel source: `kernel/main.c`, `kernel/font.h`.
License: MIT for NOVA code; upstream dependencies retain their own licenses.
