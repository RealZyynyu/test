# NOVA OS Native — ongoing development (no new ISO until final)

**NOVA OS** is an independent freestanding x86-64 C/assembly kernel, not
Debian/Linux, Windows or a browser app. It boots via Limine and draws its own
macOS-inspired desktop.

## What NOVA should become

NOVA is being designed around three non-negotiable product goals:

1. **Security first:** isolated and permission-controlled apps, non-writable
   executable pages, protected game saves, verified updates, safe recovery,
   privacy-friendly defaults and no dangerous installer before real tests.
2. **Gaming without false promises:** a stable native game runtime, predictable
   timing, controller support, audio, graphics acceleration and measured
   performance **before** attempting the much larger job of Windows game
   compatibility. No Steam, Proton, anti-cheat or 3D game support exists yet.
3. **Mac-like ease of use:** a polished independent desktop with Dock,
   Finder-style navigation, consistent windows, helpful settings, search,
   accessibility and straightforward installation once safe.

Read the actionable [NOVA product vision and final-release criteria](docs/product-vision.md)
for priorities, current limitations and acceptance tests.

**Current reality:** NOVA has a basic Arcade game (WASD/arrows, P to pause),
a tested game-input abstraction and an experimental framebuffer UI.
It is **not yet secure enough for daily use** and cannot run commercial games.

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
- Normalized keyboard game actions for the Arcade prototype; controller input
  is an unconnected future-driver interface, not hardware gamepad support.
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
- `kernel/process_image.h` — staged image frames, cleanup and resource limits
- `kernel/paging.h` — offline PML4 user-page mapping constructor (never CR3)
- `kernel/user_space.h` — planned RW/NX user stack, guard page and cleanup
- `kernel/capabilities.h` — staged-app default-deny file-access policy
- `kernel/pmm.h` — physical page allocator
- `kernel/scheduler.h` — cooperative in-kernel job runner
- `kernel/interrupts.h`, `kernel/irq.S` — experimental interrupts
- `kernel/ps2_mouse.h`, `kernel/rtc.h`, `kernel/cpu.h`, `kernel/pci.h`
- `kernel/game_input.h` — keyboard game actions, hold/release states and controller adapter
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

## Staged program images — next process milestone

NOVA now has **bounded staged-process image support** (`kernel/process_image.h`).
After ELF64 validation, it can use the current physical page allocator to
copy ELF program segments into separate memory frames, zero-fill BSS,
record intended page permissions and release every page when the image is
discarded. All file reads go through the existing VFS (including read-only
FAT16). This groundwork is deliberately limited to **4 staged images**, each
using at most **32 × 4 KiB pages (128 KiB)**.

New **development-only** terminal commands:

```text
PSTAGE DISK:/APP.ELF   # stage validated ELF segments in separate RAM pages
PLIST                 # count staged images
PINFO 1               # memory-page and segment count for image ID 1
PDROP 1               # release all pages of staged image ID 1
```

These commands and files are in source control; no new ISO has been generated.
Host-based tests check independent frame allocation, file-byte copying,
zero-filled BSS, page-count bounds, rollback on I/O or allocation errors,
four-slot capacity, PID reuse safety and memory release. An integration test
stages an ELF from a simulated FAT16 disk through the VFS into memory.

**Critical limitation:** These are *not running processes*. All pages are
still kernel-addressable; virtual addresses and RX/RW flags are metadata only.
NOVA does not yet create protected CPU page tables, enforce privileges,
use a TSS, switch to ring 3, perform context switching, support syscalls,
or execute the bytes of a program. It would be inaccurate and unsafe to
claim that this work provides actual isolation or multitasking.

## Offline four-level x86-64 page tables (development only)

A new `kernel/paging.h` implements a bounded constructor for candidate
x86-64 **PML4 → PDPT → PD → PT** structures belonging to staged ELF images.
It obtains physical page frames from NOVA's existing PMM, verifies ownership,
encodes virtual→physical translations, and sets page-table USER, RW and NX
bits from validated ELF segment metadata. It rejects W+X mappings,
overlapping virtual pages, invalid physical frames, invalid user addresses,
and insufficient memory. Independent images receive different PML4 roots.
All owned page-table pages are released on `PDROP` or on failure.

Development terminal commands, **not available in any released ISO**:

```text
PSTAGE DISK:/APP.ELF  # validates + stages ELF + prepares OFFLINE page tables
PLIST
PINFO 1
PTINFO 1              # number of page tables and mapped pages (offline)
PDROP 1               # releases page tables and image frames
```

**Do not activate these tables or switch CR3.** They intentionally contain
*no kernel mappings* and no user stack, process state, TSS, or safe fault
handling. The NX bit is only data in memory, not verified in the CPU;
enforcement requires correct EFER.NXE and secure CR3/ring-3 transition.
There is still **no actual isolation, app execution or preemptive scheduling**.

Host tests construct and inspect physical-address tables with synthetic
page-aligned frames, including W^X, NX, address validation, separate PML4
roots, allocation-failure rollback, double-mapping refusal and full
cleanup. GitHub Actions additionally compiles the complete freestanding
kernel without building an ISO.

## Guarded user-stack preparation (still offline)

A new `kernel/user_space.h` assembles the **non-running** program image,
its candidate four-level x86-64 page tables, and a dedicated zeroed
16-KiB user stack:

- Four writable **non-executable (NX)** 4-KiB pages are mapped at a fixed
  high user-space virtual address; the immediately preceding 4-KiB
  **guard page is left unmapped**.
- The future initial stack pointer is 16-byte aligned.
- Preparing an image rejects virtual-address overlap between program
  segments and the stack or its guard page.
- Each staged image receives independent page tables and physical
  stack frames. Allocation or mapping failures release all new resources.
- `PDROP` releases both the stack and page-table frames before releasing
  the program image. `PSTACK <pid>` reports the planned stack and guard.
- GCC/Clang tests verify guard behavior, RW/NX permissions, separate
  stack frames, zeroed contents, rollback, bounds and full cleanup.

These are **software-constructed structures only**. No CR3 switch, NXE
configuration, TSS, ring-3 privilege change, syscall ABI, actual user
stack access, exception recovery, or program execution is implemented.
Thus the guard page is not yet an *enforced* protection boundary in a
running user process. The ISO remains a manually approved final-only
artifact; no new ISO was created for this source change.

## Experimental application permission model (default deny)

The current source code includes a **preparatory** permission policy for
staged app images (`kernel/capabilities.h`). New images have **zero rights**
and `PACCESS <PID>` reports the assigned rights bitmask. `PDROP` unregisters
rights before freeing an image. A kernel-controlled path can explicitly
grant and revoke **RAM read, RAM write or FAT16 disk read**, but **disk write
is never permitted**, protected RAM examples are not app-writable, and
network/microphone/controller permissions are disabled until real
drivers exist. There is intentionally no command to grant permissions
from the Terminal. Source tests cover default deny, revoked grants,
protected files and obsolete PIDs.

**This is not currently a sandbox or runtime security boundary.**
Kernel-mode code can still bypass the policy, and there is no Ring-3
process execution, syscall broker, per-app private storage, or
real permission prompt. See the [app permission architecture and tests](docs/app-permissions.md) for precise implementation status and next steps.

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
