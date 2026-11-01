#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "$0")" && pwd)"
BUILD="$ROOT/build/arm64"
mkdir -p "$BUILD"

for tool in clang ld.lld; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'ARM64 build: missing tool: %s\n' "$tool" >&2
        exit 1
    }
done

CFLAGS="--target=aarch64-none-elf -mcpu=cortex-a57 -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -mgeneral-regs-only"
clang $CFLAGS -c "$ROOT/kernel/arch/aarch64/start.S" -o "$BUILD/start.o"
clang $CFLAGS -c "$ROOT/kernel/arch/aarch64/main.c" -o "$BUILD/main.o"
ld.lld -m aarch64elf -nostdlib --build-id=none \
    -T "$ROOT/kernel/arch/aarch64/linker.ld" \
    "$BUILD/start.o" "$BUILD/main.o" -o "$BUILD/perwix-arm64.elf"

printf '\nPerwix ARM64 kernel created: %s\n' "$BUILD/perwix-arm64.elf"
