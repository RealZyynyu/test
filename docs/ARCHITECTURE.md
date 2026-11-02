# NOVA OS Architecture

> Status: source-tree architecture overview for the `nova-os-native` branch. This
> document describes the implementation currently present in the repository;
> planned features are explicitly marked as such.

## 1. Project and boot model

NOVA is an independent, freestanding x86-64 C/assembly operating-system
project. It is not a Linux distribution, Windows derivative, or browser
application. The kernel is loaded through the Limine boot protocol and uses
the bootloader-provided memory map and higher-half direct-map (HHDM)
information during early initialization.

The kernel currently contains an experimental framebuffer desktop and
in-kernel utilities. It is a development system, not a secure daily-use OS.
The repository's release policy is deliberately conservative: ordinary
source pushes run source checks and compile/link the kernel ELF; they do not
publish an ISO. The ISO workflow remains manually dispatched and gated by
the explicit `final_release` approval.

## 2. Kernel organization

The main implementation is in `kernel/main.c`. It coordinates boot-time
initialization, the framebuffer UI, shell commands, device probing, memory
management, jobs, and interrupt setup.

Important subsystems:

| Area | Source | Current responsibility |
| --- | --- | --- |
| Boot and kernel orchestration | `kernel/main.c` | Limine entry path and subsystem initialization |
| Physical memory | `kernel/pmm.h` | Bounded 4 KiB frame allocator with ownership checks and zeroing |
| Interrupts | `kernel/interrupts.h`, `kernel/irq.S` | IDT setup, legacy PIC/PIT setup, IRQ and CPU exception entry |
| Fault reporting | `kernel/fault.h`, `kernel/main.c` | Bounded serial diagnostics; fatal exceptions halt |
| GDT/TSS construction | `kernel/gdt.h`, `kernel/tss.h` | Candidate descriptor and TSS data construction only |
| Scheduling | `kernel/scheduler.h` | Cooperative in-kernel jobs, not protected threads |
| Storage | `kernel/vfs.h`, `kernel/ramfs.h`, `kernel/fat16.h`, `kernel/ata.h` | Volatile RAM files and read-only FAT16/IDE access |
| Program groundwork | `kernel/elf64.h`, `kernel/process_image.h`, `kernel/paging.h`, `kernel/user_space.h` | Validate and stage ELF images and construct inactive candidate mappings |
| App policy/API groundwork | `kernel/capabilities.h`, `kernel/broker.h`, `kernel/user_buffer.h` | Default-deny policy and simulated broker; not a hardware syscall boundary |
| Game input | `kernel/game_input.h` | Normalized keyboard actions for the Arcade prototype |

## 3. Memory management and address spaces

The physical memory manager uses bounded 4 KiB frames and validates boot
memory-map ranges and HHDM translations. It tracks ownership, rejects
invalid frees, and clears frames on allocation and release.

NOVA also has code that validates static ELF64 images, stages their load
segments into separate physical frames, and constructs candidate four-level
page-table structures with USER, RW, and NX permission bits. A planned user
stack includes an unmapped guard page. These structures are currently
offline data structures only.

**The candidate page tables must not be loaded into CR3.** They do not yet
contain the complete kernel mappings, a live process context, or a verified
user-mode transition. The kernel does not currently execute staged programs,
enter ring 3, provide a real syscall ABI, or enforce per-process isolation.

## 4. Interrupts, exceptions, GDT, and TSS

The IDT contains 256 64-bit interrupt gates. The current initialization
captures the active kernel code selector from the Limine-provided CS,
installs exception stubs for vectors 0–31, installs legacy PIC IRQ stubs,
loads the IDT with `lidt`, and configures the PIC/PIT. Interrupt enabling
is conditional on successful IDT initialization.

The assembly exception stubs normalize the stack layout by preserving
general-purpose registers and accounting for CPU-pushed error codes. The
common fatal handler reports the vector, category, error code, RIP, CS,
RFLAGS, and CR2 for page faults, then halts. It does not attempt recovery.

The repository contains helpers to construct a candidate GDT and a 104-byte
long-mode TSS. The TSS helper can configure one-based IST slots with
nonzero, 16-byte-aligned stack tops. **At the time this document was written,
the candidate GDT is not installed, TR is not loaded, no IST stack is
activated, and IDT gates use IST index zero.** The active GDT remains the
one supplied by Limine. In particular, vector 8 (double fault) does not yet
use a dedicated IST stack.

The next interrupt-safety milestone is to audit the entry assembly and
boot assumptions, install a NOVA GDT with a valid far control transfer and
segment reload, load the TSS with `ltr`, and verify this in an emulator.
Only after a dedicated IST stack is allocated and mapped, with its guard
page left unmapped, should IST1 be set in the TSS and the double-fault gate
be changed to use IST1. A separate intentional-fault emulator test should
verify the double-fault serial diagnostic. Do not enable the IST gate before
the TSS and stack are live.

## 5. Scheduling and execution model

The current scheduler runs cooperative in-kernel jobs. It is not a
preemptive scheduler and does not switch between protected processes.
Hardware timer delivery has had verification limitations in QEMU and has
a fallback. There is no general-purpose user process execution model yet.

## 6. Storage model

`RAM:/` is a small writable volatile file namespace whose contents disappear
at reboot. `DISK:/` is a read-only FAT16 view over a supported IDE disk
layout. The VFS requires explicit volume paths and rejects parent-directory
traversal. No disk-write, formatting, or installation path is implemented
through this storage interface.

FAT16 currently supports a limited root-directory 8.3 view. It does not
provide FAT12/FAT32, long filenames, subdirectory traversal, or writes.
Host-side synthetic disk tests do not replace full guest integration tests.

## 7. Device and desktop layer

The current UI is an experimental framebuffer desktop with a dock, file
browser, Notes, Settings, Terminal, and a basic Arcade prototype. Keyboard
and mouse support uses polling PS/2 paths intended for virtual machines.
There is limited read-only PCI inspection and RTC/CPUID reporting.

There is no production graphics stack, GPU acceleration, audio subsystem,
USB stack, network stack, or general hardware-driver model. The Arcade
prototype's controller interface is not connected to a physical gamepad
driver.

## 8. Application security status

The ELF validator, staged image manager, offline page-table builder,
candidate user stack, permission policy, and broker are preparation work.
They do not collectively constitute a sandbox. Kernel-mode code can bypass
the simulated policy; no ring-3 execution, protected syscall boundary,
per-process address-space activation, or enforced app identity exists yet.

Security claims must be based on CPU-enforced mechanisms and tests, not on
metadata or host-side simulations alone.

## 9. Build and verification

The normal source-check workflow runs host-side tests (including sanitizer
builds where configured) and compiles/links a freestanding kernel ELF against
the Limine build environment. This does not by itself prove that the kernel
boots correctly in QEMU or on physical hardware.

Emulator tests should be treated as a distinct verification layer:
- normal boot test: assert expected serial milestones and a bounded timeout;
- exception test: trigger a controlled fault in a dedicated QEMU run and
  assert the expected serial diagnostic and emulator exit/timeout behavior;
- double-fault/IST test: only after GDT, TSS, and guarded IST stack setup are
  implemented and independently verified.

Do not interpret a missing CI status or an unrun workflow as a passing test.

## 10. Release and safety constraints

- No automatic ISO creation or release on ordinary source pushes.
- Preserve the manual `final_release` approval gate.
- Do not add automatic installation, disk formatting, or disk-write behavior.
- Do not test experimental kernels on the user's primary machine or disk;
  use a disposable virtual machine.
- Keep claims aligned with verified implementation and test evidence.

## 11. Near-term architecture sequence

1. Keep this document aligned with source changes.
2. Audit the current boot entry, GDT selectors, exception stubs, and linker
   memory layout before installing a replacement GDT.
3. Load and verify the GDT and TSS in QEMU while leaving IST gates disabled.
4. Allocate/map a dedicated kernel IST stack and preserve an unmapped guard
   page; configure TSS IST1 only after verifying the mapping and stack top.
5. Point vector 8 at IST1 only after the TSS is loaded and the stack is live.
6. Add separate normal-boot and intentional-fault QEMU CI jobs.
7. Keep host unit tests and emulator tests as complementary verification.

This document is an overview, not a substitute for the detailed subsystem
specifications and tests linked from the README.
