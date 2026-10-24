# NOVA OS Native 0.1 — its own kernel, not Debian

**Independent, freestanding x86-64 hobby OS**, booted with Limine.
This is not based on Debian or Linux, and it is not a website.
Source code contains a framebuffer desktop, keyboard input (PS/2 emulator)
and a basic tile game. No adverts, tracking, or accounts.

**Important:** It cannot yet replace Windows. It has no modern hardware
drivers, general-purpose filesystem, process isolation, network stack,
installer, security updates, Windows app compatibility, or full desktop apps.

## Get ISO using GitHub Actions
Open Actions > Build NOVA Native ISO > the latest workflow run. If it succeeds,
download artifact "nova-os-native-iso"; extract nova-os-native-x86_64.iso.

## VirtualBox on Windows 11
Create a NEW VM: Other/Unknown (64-bit), 2 GB RAM, 1 CPU, BIOS firmware.
No hard disk is necessary. Attach the ISO as optical media. Boot and press
1, 2, 3, 4 or 5 to switch screens. Arcade controls: WASD or arrow keys.
Do not install or flash it onto your main PC.

## Build on Linux
The workflow clones the official Limine x86-64 C template to obtain proper
freestanding linker options and BIOS/UEFI boot-image construction, copies
our original kernel and font into its source tree, then runs make with Clang.

## Future development
Memory manager, paging, interrupts, USB input, NVMe/storage filesystems,
install/update/recovery, drivers, networking, process isolation,
compositor, apps, game acceleration, Windows compatibility and security
must be implemented and tested before it could plausibly replace Windows.

See https://github.com/Limine-Bootloader/limine-c-template-x86-64
for the boot template's own terms. The NOVA code is MIT licensed.
