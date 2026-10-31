$ErrorActionPreference = 'Stop'
$qemu = 'E:\Program Files\qemu\qemu-system-x86_64.exe'
$iso = Join-Path $PSScriptRoot 'build\Perwix-1.0-amd64.iso'

if (-not (Test-Path -LiteralPath $qemu)) {
    throw "QEMU not found at $qemu"
}
if (-not (Test-Path -LiteralPath $iso)) {
    throw "Perwix ISO not found. Build it first with build.sh in WSL."
}

& $qemu -machine q35 -accel tcg,thread=multi -cpu max -smp 2 -m 2048 -device qemu-xhci -netdev user,id=net0 -device e1000,netdev=net0 -cdrom $iso -boot d -display sdl -serial stdio
