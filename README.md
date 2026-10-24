# NOVA OS Native 0.6 — memory and device foundations

NOVA Native is an independent freestanding x86-64 operating-system **kernel
prototype**, not Debian/Linux or a macOS theme. It uses the Limine
bootloader and paints its own macOS-inspired desktop into a framebuffer.

## Download

[NOVA Native 0.6 prerelease](https://github.com/RealZyynyu/test/releases/tag/nova-native-v0.6)

Download `nova-os-native-x86_64.iso` for a virtual machine.
Also provided: a boot screenshot, SHA256 checksum and serial boot logs.
The release workflow requires QEMU serial startup and subsystem self-test
markers; it fails if any required marker is absent.

## 0.6: actual kernel subsystems

- **Physical page allocator:** obtains suitable USABLE memory from the
  Limine memory map; uses the Limine higher-half direct mapping (HHDM).
  A bitmap guards a maximum pool of **2,048 × 4 KiB pages (8 MiB)**.
  Allocation zero-fills pages. Free validates address/alignment, occupancy
  and prevents double free. The boot self-test allocates two separate
  pages, writes/reads values, frees them and verifies the counts.
  This is NOT a virtual memory manager, heap or process isolation.
- **Cooperative kernel jobs:** bounded round-robin runner for three
  in-kernel callbacks (heartbeat, free page check and RAM-file health).
  A boot self-test verifies initial dispatch. No threads, scheduler
  interrupts, context switching, or user processes.
- **PCI configuration enumeration:** read-only scan of PCI buses
  0–7 with device classes (network, storage, display, USB).
  It is a *partial hardware inventory*, not working device drivers.
- **Terminal diagnostics:** `PMM`, `TASKS`, `PCI`, `MEM`,
  `HELP`, `LS`, `CAT NOTES.TXT`, `CAT README.TXT`,
  `CAT TODO.TXT`, `UNAME` and `CLEAR`.
- Existing **PS/2 mouse and keyboard**, clickable dock and sidebar,
  scratchpad with bounded RAM-only files, Arcade game, dark mode and
  original macOS-inspired user interface are retained.

### Testing

Boot from the ISO under QEMU or VirtualBox.
Use a **64-bit** guest CPU (Other/Unknown 64-bit), BIOS firmware
(UEFI off initially), 1 CPU and 2 GB RAM. No virtual hard disk required.
The earlier VirtualBox error `This CPU does not support 64-bit mode`
requires fixing hardware virtualization/VM guest settings on the host;
the ISO cannot provide missing CPU features.

- Press **1–6** for Welcome, Finder, Notes, Arcade, Settings, About;
  **7** for the Terminal (while outside Notes/Terminal).
- Press **Esc** for Home and **T** for light/dark mode outside text editors.
- Use mouse clicks to open apps and RAM file entries.
- Notes and the small `NOVA RAM FILES` system are stored **only in RAM**.
  All changes disappear after reboot.

### Safety and limitations

**DO NOT install NOVA 0.6 over Windows 11 or on your real PC.**
The project does not contain a safe disk installer and is unsuitable
for normal computing. There is still **no** persistent filesystem,
disk-write driver, networking/Wi-Fi stack, GPU acceleration, reliable
USB support, preemptive multitasking, user-mode process isolation,
general native-app/game runtime, update or security infrastructure.
This OS cannot replace Windows in its current form.

The scheduler only executes callback jobs in kernel context; it is
not a multiprocessing system. The PCI scan detects device headers
but does not provide functional network or disk drivers.

### Sources
`kernel/main.c` — input, graphics, serial and shell integration
`kernel/pmm.h` — bounded physical memory allocator
`kernel/scheduler.h` — cooperative kernel jobs
`kernel/pci.h` — legacy read-only PCI configuration scan
`kernel/ps2_mouse.h` — PS/2 input
`kernel/ramfs.h` — RAM file table
`tools/generate_font.py` — Inter font glyph generation

NOVA code is MIT-licensed. The Inter font remains OFL-1.1 licensed
by its authors. Upstream Limine and freestanding dependencies retain
their own licenses. No Apple trademarks or original proprietary assets
are bundled.
