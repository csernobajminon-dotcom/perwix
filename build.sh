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

mkdir -p "$BUILD/obj" "$ISO_ROOT/boot/grub"

CFLAGS="-std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -fcf-protection=none -m64 -mcmodel=kernel -mno-red-zone -mgeneral-regs-only"
SOURCES="kernel/core/console.c kernel/core/irq.c kernel/core/ipc.c kernel/core/logo.c kernel/core/main.c kernel/core/pmm.c kernel/core/scheduler.c kernel/core/shell.c kernel/fs/vfs.c kernel/drivers/e1000.c kernel/drivers/keyboard.c kernel/drivers/mouse.c kernel/drivers/pci.c kernel/drivers/registry.c kernel/drivers/usb.c"
OBJECTS=""

for source in $SOURCES; do
    object="$BUILD/obj/$(printf '%s' "$source" | tr '/' '_').o"
    gcc $CFLAGS -Ikernel/include -Ikernel/fs -Ikernel/drivers \
        -c "$ROOT/$source" -o "$object"
    OBJECTS="$OBJECTS $object"
done

nasm -f elf64 "$ROOT/kernel/boot/entry.asm" -o "$BUILD/obj/entry.o"
nasm -f elf64 "$ROOT/kernel/arch/x86_64/interrupts.asm" \
    -o "$BUILD/obj/interrupts.o"
OBJECTS="$OBJECTS $BUILD/obj/entry.o $BUILD/obj/interrupts.o"

ld -m elf_x86_64 -nostdlib --build-id=none -z max-page-size=0x1000 \
    -T "$ROOT/kernel/linker.ld" $OBJECTS -o "$KERNEL"

if command -v grub-file >/dev/null 2>&1; then
    grub-file --is-x86-multiboot2 "$KERNEL"
fi

cp "$KERNEL" "$ISO_ROOT/boot/perwix.elf"
cp "$ROOT/grub/grub.cfg" "$ISO_ROOT/boot/grub/grub.cfg"
grub-mkrescue -o "$BUILD/Perwix-1.0-amd64.iso" "$ISO_ROOT"

printf '\nPerwix image created: %s\n' "$BUILD/Perwix-1.0-amd64.iso"
