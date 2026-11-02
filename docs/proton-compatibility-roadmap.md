# NOVA OS — Windows game compatibility / Proton feasibility

**Status: research and architecture plan only. No Proton, Wine, Steam, DXVK,
VKD3D-Proton, Vulkan driver, or Windows game runtime is implemented.**

## Goal

Investigate a future path for running selected Windows games on NOVA without
claiming broad Windows compatibility. The first target would be a small,
explicitly tested compatibility set, not every Steam title.

## Why Proton cannot be dropped into the current kernel

Proton is a Windows-game compatibility stack built around Wine and additional
components. It is not a standalone emulator or a single executable. Typical
deployments rely on a Linux user-space environment and Linux kernel interfaces.
Depending on the game and configuration, those include:

- Process creation, signals, threads, futex synchronization and robust
  virtual-memory operations.
- File descriptors and Linux-style I/O, polling, epoll/eventfd, timers and
  shared memory.
- Filesystem conventions and runtime libraries expected by Wine and games.
- Device discovery and access for graphics, audio, input and controllers.
- A working graphics stack. DXVK translates Direct3D 9/10/11 to Vulkan;
  VKD3D-Proton translates Direct3D 12 to Vulkan. Both still require a
  compatible Vulkan implementation and functioning GPU drivers.
- Audio, windowing, display synchronization, controller and sometimes
  anti-cheat integrations.

NOVA currently has a freestanding x86-64 kernel, no Linux syscall ABI,
no POSIX user-space runtime, no Vulkan driver, no modern GPU/audio/USB stack,
and no protected user-mode process execution. Its ELF staging and offline
page-table structures do not constitute a usable application runtime.

Therefore copying Proton files into NOVA or adding a launcher would not make
Windows games run.

## Architectural options

### A. Native NOVA kernel plus a Linux compatibility ABI

Keep NOVA as the host kernel and implement a sufficiently complete Linux
user-space ABI, then port/build the required runtime stack for it. This is
the most direct way to preserve the independent-kernel goal, but it is a
very large project: syscall semantics, threading, memory management,
signals, file descriptors, process lifecycle, dynamic linking, graphics,
audio, and device interfaces all need to be correct and secure.

A handful of Linux syscalls is not enough. Wine and game engines exercise
large, evolving portions of the platform.

### B. Linux guest environment

Run a real Linux kernel in a VM and use Proton inside that guest. This is
the most realistic early experiment because it delegates the Linux ABI and
driver ecosystem to Linux. It is not native Proton support in the NOVA
kernel, and performance depends on virtualization and GPU passthrough or
virtual GPU support. The current NOVA prototype does not have the device
virtualization, hypervisor, or GPU stack needed to host this itself.

### C. Port Wine and dependencies to a NOVA/POSIX environment

Build a POSIX compatibility layer and port Wine plus its dependencies.
This avoids implementing the entire Linux kernel ABI, but still requires a
substantial, coherent POSIX runtime and native graphics/audio backends.
Proton-specific patches and components may need additional porting. This is
not a shortcut around the missing drivers.

## Recommended staged plan

1. **Stabilize the kernel execution model**
   - Complete protected Ring-3 execution, correct kernel mappings, TSS,
     syscall/exception entry, context switching and process teardown.
   - Verify that a crashing or malformed user process cannot crash the kernel.
   - Keep staged ELF images non-executable until these tests pass.

2. **Build a small native user-space foundation**
   - Define a stable NOVA syscall ABI and libc/runtime contract.
   - Implement process/thread lifecycle, virtual memory, synchronization,
     file descriptors, pipes, timers, polling and shared memory.
   - Add a dynamic loader and documented executable/package format.
   - Test all interfaces with host-side conformance tests and VM integration.

3. **Deliver native gaming essentials**
   - Stable display compositor/windowing, GPU abstraction and a real
     supported graphics driver.
   - Audio output/mixing, USB HID/gamepad input, controller hotplug and
     reliable frame/timer APIs.
   - Demonstrate a native 2D game and then a native 3D test before
     attempting Windows compatibility.

4. **Choose and prototype a compatibility route**
   - Compare the cost of a Linux syscall ABI with a POSIX/Wine port.
   - Prototype one bounded component in an isolated development VM.
   - Do not import the whole Proton stack until its dependencies and
     licensing/build requirements have been audited.

5. **Compatibility testing**
   - Maintain a per-game matrix: launch, rendering API, audio, input,
     save persistence, performance, stability and known limitations.
   - Test single-player titles first. Anti-cheat, DRM, launchers and
     multiplayer compatibility are separate problems and must never
     be promised without title-specific verification.

## Non-goals and safety gates

- No claim that NOVA currently runs Windows applications or games.
- No kernel-mode Wine/Proton execution and no disabling memory protections.
- No automatic downloading or execution of unknown game binaries.
- No enabling disk writes, installation, or changes to the host OS as part
  of this research phase.
- No ISO or release from ordinary source commits. Final release remains
  manually gated.

## First practical milestone

Before any Proton source is integrated, NOVA must boot reproducibly in a
disposable QEMU/VirtualBox environment, execute a harmless native Ring-3
test program with enforced isolation, and pass process-crash, memory,
filesystem and syscall tests. After that, build the native runtime and
graphics/audio/input foundations. Proton compatibility remains a long-term
goal rather than a near-term feature.
