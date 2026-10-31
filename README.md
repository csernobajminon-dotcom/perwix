# Perwix 1.0

Perwix is an experimental operating system project with an x86-64 PC build and a minimal ARM64 preview. The x86-64 image boots with GRUB2. The ARM64 preview runs on QEMU's `virt` machine and uses a PL011 serial console.

## What works

- GRUB2 boots the x86-64 kernel through Multiboot2.
- Colored text output on a black VGA background and a COM1 serial console.
- Bootstrap physical-page allocator.
- 100 Hz PIT timer and a round-robin scheduler for kernel threads.
- Basic inter-process messaging queues.
- PS/2 keyboard input for the built-in shell.
- PS/2 wheel mouse support for scrolling through console history.
- PCI device scanning, including USB host controller detection.
- QEMU e1000 Ethernet driver with ARP and ICMP ping support.
- Basic x86-64 shell commands: `help`, `about`, `version`, `arch`, `cpu`,
  `mem`, `free`, `uptime`, `tasks`, `ps`, `pci`, `net`, `ping`, `usb`,
  `drivers`, `mouse`, `fs`, `ls`, `pwd`, `whoami`, `echo`, `clear`, and
  `reboot`.
- Separate ARM64 kernel preview with a PL011 serial shell.

## Current limitations

Perwix is an early kernel, not a complete desktop operating system. Current
kernel tasks run in ring 0; user processes and system calls are not implemented.
USB host controller detection and xHCI reset are present, but USB device
transfers and USB keyboard or mouse support are not implemented.

Networking currently supports the emulated QEMU e1000 adapter, static QEMU
user-mode networking, ARP, and IPv4 ICMP ping. DNS, TCP, and web access are not
implemented. The `net` command reports adapter status; `ping` sends an ICMP
echo request.

The filesystem registry is only an extension point. Disk storage, mounted
filesystems, graphics, and audio drivers are not implemented. The ARM64 build
is a separate minimal kernel and does not yet include the x86-64 scheduler,
keyboard shell, USB, or network drivers.

## Build on Windows

The build uses Ubuntu in WSL for GCC, NASM, GRUB2, and xorriso. QEMU is expected
at `E:\Program Files\qemu`.

Open PowerShell in the project directory and build the x86-64 ISO:

```powershell
wsl -d Ubuntu -- bash -lc 'cd /mnt/e/HybridOS/Perwix-1.0 && ./build.sh'
Run it in QEMU:
.\run-qemu.ps1
Click the QEMU window to focus the keyboard, then type help at the shell
prompt. Try net and ping 10.0.2.2 to check the QEMU network connection.

The QEMU runner uses software emulation (TCG). Hardware virtualization may need
to be enabled in your computer's firmware to use hardware acceleration.
ARM64 QEMU preview
Build the ARM64 kernel:
wsl -d Ubuntu -- bash -lc 'cd /mnt/e/HybridOS/Perwix-1.0 && ./build-arm64.sh'
Run it:
.\run-qemu-arm64.ps1
The ARM64 preview currently provides a PL011 serial shell with help, about,
arch, version, uptime, drivers, echo, and clear.
Contributing
Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for build
instructions and the pull request workflow.

Shell commands and their messages are in kernel/core/shell.c. x86-64 boot
messages are in kernel/core/main.c. ARM64 shell messages are in
kernel/arch/aarch64/main.c.

Please describe what changed, which architecture you worked on, how you built
it, and what you observed in QEMU or on hardware. Keep status messages accurate
to the features that are implemented.
Roadmap
- Add memory protection, user-mode processes, system calls, and stronger IPC.
- Implement USB transfers and USB HID keyboard and mouse support.
- Add storage drivers and a working virtual filesystem.
- Add DHCP, DNS, TCP, and broader network driver support.
- Port drivers and kernel services to ARM64.
- Add graphics, audio, and additional hardware support.
Perwix starts with generic x86-64 PC support. Hardware coverage will grow as
drivers are implemented and tested.
