# Perwix storage and DiskPart

The current x86-64 storage path is intentionally read-only.

## Hardware supported

- Legacy ATA/IDE primary and secondary channels.
- Master and slave devices on each channel.
- ATA IDENTIFY and PIO `READ SECTORS` using LBA28 (512-byte sectors).
- Maximum addressable capacity is limited to the LBA28 range (128 GiB).
- Primary MBR entries and basic GPT entry arrays (up to 32 partitions are kept
  per disk; GPT entry size must be the standard 128 bytes).

AHCI/SATA, NVMe, USB mass storage, ATAPI optical drives, LBA48, extended/logical
MBR chains, GPT CRC validation and all disk writes are not implemented yet.

## Filesystem status

The partition scanner recognizes common signatures for FAT12/16/32, exFAT,
NTFS, ext2/3/4, ISO9660, Btrfs, XFS, F2FS, HFS+, APFS, UFS, JFS, ReiserFS,
SquashFS, Linux swap, LUKS and LVM2. Recognition is not equivalent to file
access.

**Implemented file access:** the VFS currently registers a FAT32 driver and
automatically mounts the first valid FAT32 partition as `/`. It supports
read-only directory enumeration, FAT32 cluster-chain traversal, file reads,
UTF-16 long-file-name decoding to UTF-8 (including surrogate pairs), and
case-insensitive ASCII filename lookup. The shell provides `mount`, `ls`,
`cd`, `pwd` and `cat`. `cat` displays up to the first 64 KiB of a text file.

**Detected only:** FAT12/16, exFAT, NTFS, ext2/3/4, ISO9660, Btrfs, XFS, F2FS,
HFS+, APFS, UFS, JFS, ReiserFS, SquashFS, Linux swap, LUKS and LVM2 do not yet
have mounted read/write filesystem drivers. Journal recovery, encryption,
LVM activation, permissions, timestamps, symlinks and file creation are not
implemented. FAT32 writing and formatting are also not implemented.

## Shell filesystem commands

```text
mount                 show the root mount
ls [path]             list a directory
cd [path]             change the current directory
pwd                   print the current directory
cat path/to/file      display a text file
fs                    show detected formats and mounted filesystem
```

## DiskPart commands

`diskpart` is an independently loaded ELF command. It accepts:

```text
diskpart help
diskpart list disk
diskpart select disk 0
diskpart detail disk
diskpart list partition
diskpart list volume
diskpart rescan
```

The utility is read-only. It intentionally has no `clean`, `format`, `create`,
`delete`, `convert`, or other destructive partitioning commands. Disk numbers
are assigned in the order ATA devices are discovered at boot; a rescan resets
the selection.

## QEMU testing

Create an empty raw test image and attach it through the emulated legacy IDE
controller:

```powershell
qemu-img create -f raw .\test-disk.img 256M
.\run-qemu.ps1 -Disk .\test-disk.img
```

To test FAT32 file access, prepare a FAT32 partition on that image and add text
files with long Unicode filenames, then boot Perwix and try `mount`, `ls`,
`cd`, `pwd` and `cat`.

Do not attach a real host disk to this experimental kernel. The ATA driver and
FAT32 reader have not been validated against physical controllers or damaged
filesystems. The file system is strictly read-only in this release.
