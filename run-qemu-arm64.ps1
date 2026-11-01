$ErrorActionPreference = 'Stop'
$qemu = 'E:\Program Files\qemu\qemu-system-aarch64.exe'
$kernel = Join-Path $PSScriptRoot 'build\arm64\perwix-arm64.elf'

if (-not (Test-Path -LiteralPath $qemu)) {
    throw "ARM64 QEMU not found at $qemu"
}
if (-not (Test-Path -LiteralPath $kernel)) {
    throw "ARM64 kernel not found. Build it first with build-arm64.sh in WSL."
}

& $qemu -machine virt,gic-version=3 -accel tcg -cpu cortex-a57 -smp 1 -m 512M `
    -nographic -monitor none -serial stdio `
    -device "loader,file=$kernel,cpu-num=0"
