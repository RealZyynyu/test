# NOVA OS Native — ongoing development (no new ISO until final)

**NOVA OS** is an independent freestanding x86-64 C/assembly kernel, not
Debian/Linux, Windows or a browser app. It boots via Limine and draws its own
macOS-inspired desktop.

## Release policy — IMPORTANT

**There will be no more automatic ISOs on pushes.** The ISO publishing workflow
`.github/workflows/build-native-iso.yml` can now run **only** through a manual
GitHub Actions dispatch with an explicit `final_release` approval. Changing
source files must never create a new ISO or GitHub Release automatically.

The existing [0.9 prerelease](https://github.com/RealZyynyu/test/releases/tag/nova-native-v0.9)
remains a historical *VM developer build*, not the finished system.

For every normal code push, [source-checks.yml](.github/workflows/source-checks.yml)
performs host-side unit tests **and** compiles/links the freestanding kernel ELF
without generating an ISO.

## Current experimental kernel components

- Custom framebuffer desktop, floating dock, Finder-style RAM file browser,
  Notes app, Arcade, Settings, Terminal, Inter-derived anti-aliased fonts.
- Polling PS/2 keyboard and mouse for virtual machines.
- Bounded physical page allocator (up to 8 MiB, 4 KiB pages, self-tested).
- Cooperative in-kernel jobs, IDT and software interrupt diagnostics.
  Hardware PIT IRQ0 delivery was not verified across QEMU runs and has a
  fallback; there are **no protected processes or preemptive threads**.
- RTC and CPUID read-only inspection, limited read-only PCI scan.
- Volatile RAM file table: `TOUCH`, `WRITE`, `CAT`, `RM`, `LS`.
- **Read-only legacy IDE/ATA sectors**: probes an emulated primary-master disk,
  reads LBA28 sectors, never writes, never formats.
- **New: read-only FAT16**: mounts a FAT16 superfloppy or first primary MBR
  FAT16 partition, validates geometry, lists short 8.3 root filenames, and
  reads file bytes using checked FAT cluster chains. **No FAT12/FAT32, long
  filenames, subdirectory traversal or disk writes.**

### FAT16 shell commands (in development, not yet in an ISO)

`FATSTAT` — detected FAT16 volume information

`FATLS` — list a few root-directory FAT16 filenames

`FATCAT README.TXT` — show a short printable file (up to 90 bytes)

`DISK` and `SECTOR0` — read-only IDE drive / first sector diagnostics

**FAT16 support currently has host-side simulated-disk tests.** A source-only
freestanding compile check runs on GitHub; full in-VM FAT16 functional testing
remains required before any final release. Do not treat this as a fully proven
disk filesystem.

### Source files

- `kernel/main.c` — UI, shell, drivers and kernel orchestration
- `kernel/ata.h` — legacy IDE LBA28 sector **reads only**
- `kernel/fat16.h` — independent bounded FAT16 parser and file reader
- `kernel/ramfs.h` — RAM-only files
- `kernel/vfs.h` — explicit RAM:/ and read-only DISK:/ storage namespace
- `kernel/elf64.h` — validated ELF64 executable metadata (never executes)
- `kernel/pmm.h` — physical page allocator
- `kernel/scheduler.h` — cooperative in-kernel job runner
- `kernel/interrupts.h`, `kernel/irq.S` — experimental interrupts
- `kernel/ps2_mouse.h`, `kernel/rtc.h`, `kernel/cpu.h`, `kernel/pci.h`
- `tests/test_fat16.c` — synthetic MBR/superfloppy, FAT file, boundary and
  malformed-data tests, compiled with GCC and Clang/UBSan

## Unified VFS in development

The native kernel now has a small explicit virtual filesystem API
(`kernel/vfs.h`) connecting two very different storage volumes:

- `RAM:/` is writable, volatile, limited to 12 named files of at most 127
  content bytes each. Files vanish at reboot.
- `DISK:/` is a read-only FAT16 root-directory view when the virtual IDE
  disk contains a supported FAT16 volume. **No disk writes exist** in this API.

Development-only terminal commands (not yet distributed in any ISO):

```text
VLS RAM:/
VLS DISK:/
VCAT RAM:/NOTES.TXT
VCAT DISK:/README.TXT
VSTAT DISK:/README.TXT
VTOUCH RAM:/DEMO.TXT
VWRITE RAM:/DEMO.TXT HELLO
VCAT RAM:/DEMO.TXT
VRM RAM:/DEMO.TXT
```

All VFS paths require an explicit volume, do not allow parent-directory
traversal, and reject invalid or excessively long names. `VWRITE` and
`VRM` refuse disk paths. FAT16 rejects reserved, bad, and end-of-chain
cluster values where actual file content requires another cluster.

Automated source tests verify disk write protection, RAM-file operations,
boundary checks and the FAT16 parser on synthetic MBR/superfloppy images.
GitHub's independent native-kernel job also compiles and links the full
freestanding kernel **without ISO generation**.

This is not a full production VFS: there are no directories, file
descriptors, permissions, symbolic links, writeable disk filesystem,
transaction recovery or concurrent multi-process operations. FAT16 integration
inside a running QEMU guest remains to be tested before a final release.

## ELF64 application groundwork (source-only)

NOVA now includes `kernel/elf64.h`: a **non-executing ELF64 validator**.
It uses bounded read callbacks that can read a binary from `RAM:/` or
the strictly read-only `DISK:/` FAT16 VFS.

Developer shell commands (not available in the already released ISO):

```text
ELFCHK DISK:/APP.ELF
ELFPAGES DISK:/APP.ELF
```

- Accepts only little-endian x86-64 **static ET_EXEC** ELF64.
- Limits load segments, header count and memory pages.
- Checks canonical user-space address bounds, file bounds, entry point,
  section alignment, page overlaps and readable binary segments.
- Refuses executable writable segments (W+X), executable stacks,
  dynamic linking, TLS, unsupported architecture and malformed files.
- Returns validation status / estimated number of pages. It **never
  loads, maps, starts, or executes programs**.
- Host tests use synthetic ELF files with intentional malformed data.
  A second suite reads a synthetic ELF file from a FAT16 disk image
  through the VFS. GCC/Clang sanitizer checks and freestanding kernel
  compilation run in GitHub Actions without creating an ISO.

**This is not yet user-mode process support.** Remaining milestones include
real per-process page tables, ELF segment mapping, user/kernel privilege
separation, protected system calls, a correct scheduler and an application
runtime. Passing the ELF header tests is not evidence the OS can execute
third-party apps or Windows binaries.

## Important missing features / final-release blockers

NOVA still cannot replace Windows. Before a credible daily-use release we need
a robust CPU exception/interrupt/timer architecture, virtual address spaces,
process/user-mode isolation, real scheduling, file permissions, reliable
persistent read/write filesystem and modern AHCI/NVMe drivers, USB input,
Ethernet/Wi-Fi networking, graphics/audio, secure installation, updates,
recovery and a native software ecosystem. Windows apps/games require a separate
and very large compatibility project.

**Do not overwrite Windows, flash this ISO to a real disk, or run this
experimental kernel on your primary PC.** Use a disposable VirtualBox or QEMU
virtual machine only. The disk driver is intentionally read-only.

NOVA original code is MIT-licensed; the Inter glyph source is OFL-1.1,
and Limine/upstream dependencies retain their own licenses. No Apple-owned
original logos, wallpapers or fonts are bundled.
