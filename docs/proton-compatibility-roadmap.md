# NOVA OS — Linux guest and Steam Proton roadmap

**Selected architecture: option B — a Linux guest environment hosted by NOVA.**

**Status: planning only.** NOVA does not currently host virtual machines or run
Steam/Proton. No Linux guest, hypervisor, GPU passthrough, Wine, Proton, DXVK,
VKD3D-Proton, or Vulkan driver is implemented.

## Architecture decision

NOVA remains the host operating system with its own kernel and desktop. A
separate, isolated Linux virtual machine will provide the Linux kernel,
user-space ABI, drivers, and runtime expected by Steam and Proton.

Conceptually:

```text
NOVA desktop and host services
        |
NOVA virtual-machine manager (future)
        |
Hardware-assisted virtualization (Intel VT-x / AMD-V)
        |
Isolated Linux guest
        |
Linux graphics / audio / input drivers
        |
Steam + Proton + selected Windows games
```

This is not native Proton support in the NOVA kernel. Proton runs inside the
Linux guest. The guest must not receive unrestricted access to NOVA host
memory, files, devices, or administrative interfaces.

## Why this is the selected route

Proton is built around Wine and depends on a substantial Linux environment:
processes, threads, virtual memory, synchronization, file descriptors,
signals, filesystem behavior, runtime libraries, graphics, audio, and input.
A small collection of Linux-compatible system calls in NOVA would not be
enough.

A real Linux guest supplies that environment instead of requiring NOVA to
reimplement it. This is the most practical compatibility route, but it moves
the hard work to virtualization and device support.

## Current blockers

The current NOVA prototype is a freestanding x86-64 kernel. It does not yet
provide:

- A hypervisor or VM lifecycle manager.
- Intel VT-x / AMD-V initialization, VM control structures, VM exits, or
  second-level address translation (EPT/NPT).
- Isolated guest physical memory allocation and safe teardown.
- Virtual block, network, display, audio, USB, and input devices.
- A supported accelerated graphics path or GPU passthrough.
- A mature host storage and networking permission model.

A Linux guest with only basic emulated graphics may boot eventually, but that
does not establish acceptable Steam gaming performance. GPU acceleration is
a separate, substantial milestone.

## Staged implementation plan

1. **Validate the guest independently of NOVA**
   - In a disposable development VM on an existing supported host, boot a
     Linux guest and install Steam/Proton through the guest's normal,
     documented package and Steam workflows.
   - Confirm the guest can run a small, supported test title.
   - Record graphics mode, frame rate, audio, input, storage, and limitations.
   - This validates the Linux/Proton guest configuration only; it does not
     count as NOVA hosting a VM.

2. **Define NOVA's virtualization architecture**
   - Detect CPU virtualization capabilities without changing firmware or
     host settings automatically.
   - Specify guest memory ownership, VM exit handling, interrupt routing,
     lifecycle, and teardown before implementing execution.
   - Keep guest memory and device access isolated from host kernel memory.
   - Design explicit, least-privilege sharing for files, networking, and
     removable devices. Default to no host-folder or raw-disk passthrough.

3. **Build a minimal isolated VM prototype**
   - Implement virtualization support only after NOVA's kernel memory,
     exception, interrupt, and process foundations are reliable.
   - Start with a disposable Linux guest and minimal virtual devices.
   - Do not expose physical disks or unrestricted host devices to the guest.
   - Require repeatable guest-crash recovery and host-survival tests.

4. **Add practical guest devices**
   - Prioritize virtual storage, networking, display, keyboard, pointer,
     audio, and controller support.
   - Evaluate a supported paravirtualized graphics path before considering
     direct GPU passthrough.
   - Treat GPU passthrough as hardware-specific and optional, never assumed.
   - Measure performance and stability on documented hardware.

5. **Integrate the Steam/Proton experience**
   - Provide a NOVA launcher that starts and stops the Linux guest through
     the VM manager; it must not execute guest binaries in the host kernel.
   - Keep Steam libraries and game data inside guest-managed storage by
     default. Any shared folder must be explicit and permission-scoped.
   - Test a small list of single-player games first and publish per-game
     results for launch, graphics, audio, input, saves, performance, and
     stability.
   - Treat anti-cheat, DRM, launchers, and multiplayer support as separate
     title-specific compatibility questions.

## Security and release gates

- No kernel-mode Wine/Proton execution and no weakening of NOVA memory
  protections to make a game run.
- No automatic execution of unknown guest binaries or downloaded installers.
- No unrestricted guest access to NOVA memory, host disks, or devices.
- VM crashes must not crash NOVA or corrupt host data.
- Guest storage must be isolated and disposable during early development.
- No ISO, release, host installation, or real-disk modification during this
  research phase. Final ISO remains manually gated by explicit approval.

## First practical milestone

The first measurable result is a Linux guest running under QEMU on a separate,
disposable development host, with Steam and Proton tested on one supported
single-player title. After that, NOVA can be developed toward hosting an
isolated Linux guest itself. Passing the external QEMU test does not mean
NOVA's hypervisor or in-OS Proton support is complete.
