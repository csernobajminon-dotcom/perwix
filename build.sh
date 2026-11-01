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

mkdir -p "$BUILD/obj" "$ISO_ROOT/boot/grub/fonts" \
    "$ISO_ROOT/boot/grub/themes/perwix"

CFLAGS="-std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -fcf-protection=none -m64 -mcmodel=kernel -mno-red-zone -mgeneral-regs-only"
SOURCES="kernel/core/console.c kernel/core/irq.c kernel/core/ipc.c kernel/core/logo.c kernel/core/main.c kernel/core/pmm.c kernel/core/scheduler.c kernel/core/shell.c kernel/core/commands.c kernel/net/net.c kernel/fs/vfs.c kernel/drivers/e1000.c kernel/drivers/rtl8139.c kernel/drivers/rtl8169.c kernel/drivers/virtio_net.c kernel/drivers/pcnet.c kernel/drivers/ne2k.c kernel/drivers/keyboard.c kernel/drivers/mouse.c kernel/drivers/pci.c kernel/drivers/registry.c kernel/drivers/usb.c"
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
