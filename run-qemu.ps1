param(
    # Emulated network adapter: e1000, rtl8139, virtio-net-pci, pcnet, ne2k_pci
    [string]$Nic = 'e1000',
    # Optional raw disk image used to test `diskpart` (legacy IDE/ATA PIO).
    [string]$Disk = ''
)
$ErrorActionPreference = 'Stop'
$qemu = 'E:\Program Files\qemu\qemu-system-x86_64.exe'
$iso = Join-Path $PSScriptRoot 'build\Perwix-1.1-amd64.iso'

if (-not (Test-Path -LiteralPath $qemu)) {
    throw "QEMU not found at $qemu"
}
if (-not (Test-Path -LiteralPath $iso)) {
    throw "Perwix ISO not found. Build it first with build.sh in WSL."
}

$qemuArgs = @(
    '-machine', 'pc', '-accel', 'tcg,thread=multi', '-cpu', 'max',
    '-smp', '2', '-m', '2048', '-device', 'qemu-xhci',
    '-netdev', 'user,id=net0', '-device', "$Nic,netdev=net0",
    '-cdrom', $iso, '-boot', 'd', '-display', 'sdl', '-serial', 'stdio'
)
if ($Disk -ne '') {
    if (-not (Test-Path -LiteralPath $Disk)) {
        throw "Disk image not found: $Disk"
    }
    $qemuArgs += @('-drive', "file=$Disk,if=ide,index=0,media=disk,format=raw")
}
& $qemu @qemuArgs
