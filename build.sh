#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "$0")" && pwd)"
BUILD="$ROOT/build"
ISO_ROOT="$BUILD/iso"
KERNEL="$BUILD/perwix.elf"

for tool in gcc ld nasm grub-mkrescue xorriso; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'Perwix build: missing tool: %s\n' "$tool" >&2
        exit 1
    }
done

mkdir -p "$BUILD/obj" "$BUILD/modules" "$ISO_ROOT/boot/modules" \
    "$ISO_ROOT/boot/grub/fonts" "$ISO_ROOT/boot/grub/themes/perwix"

CFLAGS="-std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -fcf-protection=none -m64 -mcmodel=kernel -mno-red-zone -mgeneral-regs-only"
SOURCES="kernel/core/console.c kernel/drivers/framebuffer.c kernel/core/irq.c kernel/core/ipc.c kernel/core/logo.c kernel/core/main.c kernel/core/pmm.c kernel/core/scheduler.c kernel/core/shell.c kernel/core/commands.c kernel/exec/elf.c kernel/net/net.c kernel/fs/vfs.c kernel/fs/fat32.c kernel/lib/utf8.c kernel/storage/storage.c kernel/drivers/keyboard.c kernel/drivers/mouse.c kernel/drivers/pci.c kernel/drivers/registry.c kernel/drivers/usb.c"
OBJECTS=""

for source in $SOURCES; do
    object="$BUILD/obj/$(printf '%s' "$source" | tr '/' '_').o"
    gcc $CFLAGS -Ikernel/include -Ikernel/fs -Ikernel/drivers -Ikernel/net \
        -c "$ROOT/$source" -o "$object"
    OBJECTS="$OBJECTS $object"
done

nasm -f elf64 "$ROOT/kernel/boot/entry.asm" -o "$BUILD/obj/entry.o"
nasm -f elf64 "$ROOT/kernel/arch/x86_64/interrupts.asm" \
    -o "$BUILD/obj/interrupts.o"
OBJECTS="$OBJECTS $BUILD/obj/entry.o $BUILD/obj/interrupts.o"

ld -m elf_x86_64 -nostdlib --build-id=none -z max-page-size=0x1000 \
    -T "$ROOT/kernel/linker.ld" $OBJECTS -o "$KERNEL"

# Build NIC drivers as independent relocatable ELF modules (.ko), just like
# Linux kernel modules. A tiny module entry point registers each NIC driver.
build_nic_module() {
    local driver_source="$1"
    local wrapper_source="$2"
    local module_name="$3"
    local driver_object="$BUILD/obj/modules/${module_name}_driver.o"
    local wrapper_object="$BUILD/obj/modules/${module_name}_entry.o"
    gcc $CFLAGS -Ikernel/include -Ikernel/fs -Ikernel/drivers -Ikernel/net \
        -c "$ROOT/$driver_source" -o "$driver_object"
    gcc $CFLAGS -Ikernel/include -Ikernel/fs -Ikernel/drivers -Ikernel/net \
        -c "$ROOT/$wrapper_source" -o "$wrapper_object"
    ld -m elf_x86_64 -r "$driver_object" "$wrapper_object" \
        -o "$BUILD/modules/${module_name}.ko"
}

mkdir -p "$BUILD/obj/modules"
build_nic_module kernel/drivers/e1000.c kernel/modules/nic_e1000.c nic_e1000
build_nic_module kernel/drivers/rtl8139.c kernel/modules/nic_rtl8139.c nic_rtl8139
build_nic_module kernel/drivers/rtl8169.c kernel/modules/nic_rtl8169.c nic_rtl8169
build_nic_module kernel/drivers/virtio_net.c kernel/modules/nic_virtio_net.c nic_virtio_net
build_nic_module kernel/drivers/pcnet.c kernel/modules/nic_pcnet.c nic_pcnet
build_nic_module kernel/drivers/ne2k.c kernel/modules/nic_ne2k.c nic_ne2k

# User commands are also separate ELF relocatable programs. The kernel ELF
# loader applies their relocations and calls perwix_module_init().
for command in echo uname uptime meminfo hello diskpart; do
    gcc $CFLAGS -Ikernel/include -Ikernel/fs -Ikernel/drivers -Ikernel/net -Ikernel/storage \
        -c "$ROOT/userland/commands/${command}.c" \
        -o "$BUILD/obj/modules/cmd_${command}.o"
    ld -m elf_x86_64 -r "$BUILD/obj/modules/cmd_${command}.o" \
        -o "$BUILD/modules/cmd_${command}.elf"
done

if command -v grub-file >/dev/null 2>&1; then
    grub-file --is-x86-multiboot2 "$KERNEL"
fi

cp "$KERNEL" "$ISO_ROOT/boot/perwix.elf"
cp "$ROOT/grub/grub.cfg" "$ISO_ROOT/boot/grub/grub.cfg"
cp "$ROOT/grub/themes/perwix/theme.txt" \
    "$ISO_ROOT/boot/grub/themes/perwix/theme.txt"
cp "$ROOT/grub/themes/perwix/background.png" \
    "$ISO_ROOT/boot/grub/themes/perwix/background.png"
cp "$ROOT"/grub/themes/perwix/select_*.png \
    "$ISO_ROOT/boot/grub/themes/perwix/"
cp "$BUILD"/modules/* "$ISO_ROOT/boot/modules/"

# GRUB's Unicode font enables the branded graphical menu. Keep a plain
# text-mode fallback if a distribution packages the font at another path.
GRUB_UNICODE_FONT=""
for candidate in /usr/share/grub/unicode.pf2 \
                 /usr/share/grub2/unicode.pf2 \
                 /usr/lib/grub/unicode.pf2; do
    if [[ -f "$candidate" ]]; then
        GRUB_UNICODE_FONT="$candidate"
        break
    fi
done
if [[ -z "$GRUB_UNICODE_FONT" ]]; then
    GRUB_UNICODE_FONT="$(find /usr/share -name unicode.pf2 -print -quit 2>/dev/null || true)"
fi
if [[ -n "$GRUB_UNICODE_FONT" ]]; then
    cp "$GRUB_UNICODE_FONT" "$ISO_ROOT/boot/grub/fonts/unicode.pf2"
else
    printf 'Perwix build: warning: unicode.pf2 not found; GRUB will use its text menu.\n' >&2
fi

grub-mkrescue -o "$BUILD/Perwix-1.1-amd64.iso" "$ISO_ROOT"

printf '\nPerwix image created: %s\n' "$BUILD/Perwix-1.1-amd64.iso"
