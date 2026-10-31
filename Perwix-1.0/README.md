# Perwix 1.0

Perwix is a new operating-system project with an x86-64 PC build and an
experimental ARM64 build. The x86-64 image boots with GRUB2; ARM64 currently
boots on QEMU's `virt` machine with a PL011 serial console.

## What boots today

- GRUB2 loads the Perwix ELF kernel from a hybrid BIOS/UEFI ISO.
- The kernel enters 64-bit mode, shows the supplied colored logo on a black
  VGA background, writes to COM1, and reads the Multiboot2 memory map.
- A bootstrap physical-page allocator reserves pages from usable RAM.
- The 100 Hz PIT interrupt drives a round-robin preemptive scheduler for
  ring-0 kernel threads.
- Two kernel threads exchange messages through bounded IPC queues.
- A polling PS/2 keyboard driver feeds the built-in command shell.
- A PS/2 wheel mouse can scroll through 256 lines of console history.
- PCI configuration mechanism #1 scans devices and identifies USB host
  controller classes, including UHCI, OHCI, EHCI and xHCI.
- A QEMU e1000 driver sends and receives Ethernet frames. ARP and ICMP support
  make IPv4 `ping` work through QEMU user-mode networking.
- The shell includes `help`, `about`, `version`, `mem`, `uptime`, `tasks`,
`arch`, `cpu`, `free`, `ps`, `pci`, `net`, `ping`, `usb`, `mouse`, `drivers`,
`fs`, `ls`, `pwd`, `whoami`, `echo`, `clear` and `reboot`. DNS and HTTP commands
were removed because their network support is not implemented.
- VFS and driver registries define extension points for future services.

This is an early kernel, not yet a complete desktop OS. The current scheduler
does not run user processes; all current tasks remain in ring 0. PCI scanning
only discovers USB controllers; USB transfers and USB HID devices are not
implemented yet. Networking is limited to QEMU's e1000 adapter, static QEMU
user-network addresses, ARP and ICMP ping; DNS, TCP and web access are not
implemented. VFS and driver registries are interfaces only. No disk,
graphics, audio, or filesystem drivers are claimed as implemented yet.

## Build on this Windows PC

The build script uses the installed Ubuntu WSL distribution for GCC, NASM,
GRUB2 and xorriso. QEMU is the Windows installation at
E:\Program Files\qemu.

From PowerShell in this project directory:

    wsl -d Ubuntu -- bash -lc 'cd /mnt/e/HybridOS/Perwix-1.0 && chmod +x build.sh && ./build.sh'

The ISO is written to build/Perwix-1.0-amd64.iso. Run it in QEMU with:

    .\run-qemu.ps1

Click the QEMU window to focus the emulated keyboard, then type `help` at the
`perwix@pc:/ $` prompt. The QEMU profile includes an emulated xHCI controller
so the `usb` command has a controller to report; USB device transfers are not
available yet. QEMU user-mode networking and an emulated e1000 NIC are enabled;
try `net` and `ping 10.0.2.2`.

The QEMU runner uses TCG because hardware virtualization is disabled in the
machine firmware. If virtualization is enabled later, the runner can use WHPX.

## Hardware direction

The target is the x86-64 PC architecture, not one specific computer. The
current image boots through GRUB2 and uses the legacy PIC/PIT, PS/2 keyboard,
PCI configuration ports, and serial/VGA output. ACPI/APIC, USB transfers,
storage, full Internet protocols, graphics and hardware-specific drivers
remain future work. The ARM64 preview is a separate minimal kernel, not yet a
port of the x86-64 scheduler, shell input, USB, or network drivers. Supporting
every PC device requires a growing set of tested drivers.

## ARM64 QEMU preview

Build the ARM64 ELF with:

    wsl -d Ubuntu -- bash -lc 'cd /mnt/e/HybridOS/Perwix-1.0 && ./build-arm64.sh'

Run it with `.\run-qemu-arm64.ps1`. This build currently has a PL011 serial
shell with `help`, `about`, `arch`, `version`, `uptime`, `drivers`,
`echo`, and `clear`.

## Roadmap

1. Finish kernel memory protection, user-mode tasks, system calls, and IPC.
2. Move device services out of the kernel and isolate them behind capabilities.
3. Add USB host transfers and HID keyboard/mouse, then storage drivers.
4. Add DHCP, DNS, TCP/HTTP and network drivers beyond the QEMU e1000 profile.
5. Port PCIe, USB, storage, and network drivers to ARM64.
6. Add VFS and filesystem services: memory filesystem, FAT32, ISO9660, ext2,
   and read-only NTFS support as separate milestones.
7. Add graphics, audio, and hardware-specific drivers.

Perwix starts with an x86-64 generic kernel profile. Driver coverage will grow
by standard interface and tested hardware family.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the build steps and pull request
workflow. Shell command descriptions and messages live in
`kernel/core/shell.c`; boot messages live in `kernel/core/main.c`; ARM64 serial
shell text lives in `kernel/arch/aarch64/main.c`. GitHub pull requests use the
template under `.github/`.
