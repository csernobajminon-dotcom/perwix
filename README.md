# Perwix 1.1

Perwix is a new operating-system project with an x86-64 PC build and an
experimental ARM64 build. The x86-64 image boots with GRUB2; ARM64 currently
boots on QEMU's `virt` machine with a PL011 serial console.

## What boots today

- GRUB2 loads the Perwix ELF kernel from a hybrid BIOS/UEFI ISO. Its boot screen follows the classic FreeBSD text-loader layout: an ASCII `PERWIX` wordmark at the top left, the original colored artwork sourced from `kernel/core/logo.c` at the right, and a double-line `Welcome to Perwix` menu frame. The live menu offers Perwix multi-user, single-user, loader prompt, reboot, kernel, and boot-options entries. Use Up/Down and Enter; press `C` for the GRUB command prompt.
- The kernel enters 64-bit mode, writes its boot status to VGA and COM1, and
  reads the Multiboot2 memory map. The boot logo is not repeated during kernel
  startup; it remains available through `logo` and `neofetch`.
- A bootstrap physical-page allocator reserves pages from usable RAM.
- The 100 Hz PIT interrupt drives a round-robin preemptive scheduler for
  ring-0 kernel threads.
- Two kernel threads exchange messages through bounded IPC queues.
- A polling PS/2 keyboard driver feeds the built-in command shell.
- A PS/2 wheel mouse can scroll through 256 lines of console history.
- PCI configuration mechanism #1 scans devices and identifies USB host
  controller classes, including UHCI, OHCI, EHCI and xHCI. The USB bring-up
  path enables PCI I/O or memory decoding and attempts the controller-specific
  reset sequence for all four families. xHCI root-port status is sampled.
- A pluggable network layer (`kernel/net/`) with NIC drivers for e1000,
  RTL8139, RTL8169/8168, virtio-net, AMD PCnet32 and NE2000-PCI, plus a
  polling stack: ARP, IPv4, ICMP, UDP, DHCP client, DNS resolver, a minimal
  TCP client and HTTP/1.0 GET (no TLS/HTTPS).
- The shell includes system and network tools plus diagnostic commands such as
  `sysinfo`, `meminfo`, `tickinfo`, `usbinfo`, `usbports`, `usbgen`, `netstat`,
  `rand [COUNT]`, `beep [HZ]`, `pciinfo`, `drivercheck`, `logo`, `displayinfo`,
  `utf8test`, `uptimefmt`, `tickrate`, `pageinfo`, `kversion`, `drvcount`,
  `cpuidmax`, and `hex NUMBER`.
- Destructive power commands (`reboot`, `poweroff`, `shutdown`, `halt`) show
  an interactive safety prompt. Arrow keys move the selection, Enter confirms,
  Esc cancels, and the default selection is **NO**.
- VFS and driver registries define extension points for future services.
- Console output preserves UTF-8 byte sequences on the COM1 serial stream, so
  a serial terminal configured for UTF-8 can display Ukrainian and other
  Unicode text. Legacy VGA text mode cannot render arbitrary Unicode glyphs;
  non-ASCII code points are represented by `?` there to keep cursor layout sane.
- `displayinfo` documents the active display backend. The current console is
  still VGA text 80x25 plus serial, and the `logo` command downsamples the
  original artwork to a compact 24x10 character grid; `neofetch` uses a 20x9 grid. Dynamic framebuffer resolutions/scaling are not implemented
  yet.
- GRUB boot flags are read from the Multiboot2 command-line tag: `single`
  enters a maintenance shell with network/USB bring-up skipped, `safe` does
  the same hardware reduction without the maintenance label, and `verbose`
  prints detailed kernel startup status.

This is an early kernel, not yet a complete desktop OS. The current scheduler
does not run user processes; all current tasks remain in ring 0. The USB layer
recognizes UHCI, OHCI, EHCI and xHCI and attempts each controller's basic reset
sequence. USB transfer schedules/rings, device enumeration, USB HID and USB
mass storage are not implemented yet. USB4 router/tunneling management is
also not implemented. Networking uses one wired Ethernet adapter at a time
(first supported PCI NIC found). Wi-Fi, IPv6, HTTPS/TLS and TCP retransmission/congestion control
are not implemented. Only the e1000 path had been exercised on a running VM
before the extra NIC drivers were added; the other drivers are written from
the datasheets and need testing (RTL8169 especially on real hardware). VFS and driver registries are interfaces only. No disk,
graphics, audio, or filesystem drivers are claimed as implemented yet.

## Build on this Windows PC

The build script uses the installed Ubuntu WSL distribution for GCC, NASM,
GRUB2 and xorriso. QEMU is the Windows installation at
E:\Program Files\qemu.

From PowerShell in this project directory:

    wsl -d Ubuntu -- bash -lc 'cd /mnt/e/HybridOS/Perwix-1.1 && chmod +x build.sh && ./build.sh'

The ISO is written to build/Perwix-1.1-amd64.iso. Run it in QEMU with:

    .\run-qemu.ps1

Click the QEMU window to focus the emulated keyboard, then type `help` at the
`perwix@pc:/ $` prompt. The QEMU profile includes an emulated xHCI controller
so the `usb` command has a controller to report; USB device transfers are not
available yet. QEMU user-mode networking and an emulated e1000 NIC are enabled;
try `ifconfig`, `dhcp`, `ping 10.0.2.2`, `nslookup example.com`. The runner
accepts `-Nic e1000|rtl8139|virtio-net-pci|pcnet|ne2k_pci` to pick the emulated
adapter.

The QEMU runner uses TCG because hardware virtualization is disabled in the
machine firmware. If virtualization is enabled later, the runner can use WHPX.

## Hardware direction

The target is the x86-64 PC architecture, not one specific computer. The
current image boots through a custom Perwix-branded GRUB2 menu and uses the
legacy PIC/PIT, PS/2 keyboard, PCI configuration ports, and serial/VGA output. UHCI/OHCI provide basic USB
1.x controller reset, EHCI basic USB 2.0 controller reset, and xHCI basic USB
2.0/3.x controller reset. These are controller bring-up paths, not complete
USB device drivers. USB4 router/tunneling management, USB transfers, ACPI/APIC,
storage, full Internet protocols, graphics and hardware-specific drivers
remain future work. The ARM64 preview is a separate minimal kernel, not yet a
port of the x86-64 scheduler, shell input, USB, or network drivers. Supporting
every PC device requires a growing set of tested drivers.

## ARM64 QEMU preview

Build the ARM64 ELF with:

    wsl -d Ubuntu -- bash -lc 'cd /mnt/e/HybridOS/Perwix-1.1 && ./build-arm64.sh'

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
