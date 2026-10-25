# NOVA OS Native 0.7 — interrupt foundations

NOVA Native is a freestanding, independent x86-64 OS **developer preview**
built in C and assembly. It is not Debian, Linux or a web simulation.
It uses the Limine bootloader and draws its own macOS-inspired desktop.

## Download and testing

When the automated workflow succeeds, visit the
[NOVA Native 0.7 prerelease](https://github.com/RealZyynyu/test/releases/tag/nova-native-v0.7)
and download `nova-os-native-x86_64.iso`.
Release assets also include a SHA256 checksum, VM screenshot and serial log.

**Only boot this ISO in QEMU/VirtualBox.** In Windows 11 create a VM:
64-bit guest type, BIOS/legacy boot (EFI disabled initially), 1 CPU,
2 GB RAM, and optical drive with ISO. No virtual hard disk is required.
If Limine reports "This CPU does not support 64-bit mode", correct
VirtualBox's 64-bit virtual CPU support; this is not an ISO setting.

## New in 0.7

- **x86-64 Interrupt Descriptor Table:** 256 kernel interrupt gates,
  CPU exceptions routed to a fail-stop handler, and explicit
  save/restore of integer registers in assembler IRQ stubs.
- **Legacy PIC and PIT setup:** basic PIC remapping and PIT channel 0
  configuration for nominal 100 Hz. This is *experimental*;
  **hardware IRQ0 delivery was NOT confirmed** in automated QEMU runs
  as of this release. The x86-64 IDT and IRQ handler are exercised by
  a direct software interrupt, and boot tests check the return path.
- **Cooperative job fallback:** three in-kernel jobs continue to run
  using a polling fallback when hardware PIT IRQ0 is not delivered.
  Timer-driven callbacks also work on machines where the IRQ arrives.
- **Terminal command TICKS:** shows the count of hardware timer
  interrupts actually observed, excluding one software test interrupt.
  The value can remain zero depending on the virtual platform.
- **Harder boot checks:** serial logs verify the existing physical
  page allocator, PCI inventory, RAM files and software interrupt
  self-test before releasing the ISO.

### Previously implemented
- Custom framebuffer desktop, Finder-like RAM file view, settings,
  Arcade, Notes, original icons, Inter font rasterization
- PS/2 keyboard and mouse input with clickable app buttons
- Small bounded volatile RAM files (Notes changes disappear at reboot)
- Minimal kernel-mode command interpreter
- 4-KiB page allocator over a bounded 8-MiB Limine HHDM physical pool
- Limited read-only PCI configuration enumeration
- Simple cooperative kernel callbacks

### Keyboard and terminal

1–6 switch main apps, 7 opens Terminal, Esc returns Home, T changes
appearance outside text entry. Arcade: WASD or arrow keys.
Terminal: `HELP`, `LS`, `CAT NOTES.TXT`, `UNAME`, `MEM`,
`PMM`, `PCI`, `TASKS`, `TICKS` and `CLEAR`.

## What is STILL MISSING

**NOVA cannot yet replace Windows 11.** There is no safe installer,
persistent SSD/HDD filesystem, modern storage/USB/network/audio/GPU
driver stack, user-mode app runtime, Windows game compatibility,
complete virtual memory/address-space management, proper
privilege separation, user processes, preemptive scheduler, security
update system or recovery tools.

The IDT is a kernel-level foundation, not a production-grade
interrupt or fault-handling subsystem. The PIT support is unverified
for hardware IRQ delivery; the scheduler is NOT preemptive.

**Never overwrite your Windows installation with a NOVA preview.**

## Source and licenses

`kernel/main.c`: UI and kernel integration
`kernel/interrupts.h`: x86-64 IDT, PIC and PIT setup
`kernel/irq.S`: x86-64 interrupt stubs
`kernel/pmm.h`: physical page allocator
`kernel/scheduler.h`: cooperative jobs
`kernel/pci.h`: PCI enumeration
`kernel/ramfs.h`: volatile RAM file table
`kernel/ps2_mouse.h`: emulated PS/2 mouse
`tools/generate_font.py`: open-source Inter raster font generation

The original NOVA source is MIT licensed, Inter is OFL-1.1
(https://github.com/rsms/inter), and upstream bootloader and
freestanding components retain their own licenses.
