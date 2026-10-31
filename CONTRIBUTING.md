# Contributing to Perwix

Thanks for helping with Perwix. It is an early hobby OS, so please keep pull
requests focused and describe what is implemented and how you checked it.

## Where to edit

- x86-64 shell commands and their user-visible text: `kernel/core/shell.c`.
- x86-64 boot messages: `kernel/core/main.c`.
- ARM64 serial shell and its text: `kernel/arch/aarch64/main.c`.
- Device status must match the actual implementation in `kernel/drivers/`.
- Project status, build steps and limitations: `README.md`.

Use plain English and ASCII in kernel console strings. The VGA console currently
uses the classic text mode character set, so arbitrary Unicode may display
incorrectly. Keep help output aligned and remove a command from help if it is
not implemented. Do not describe a device as supported just because PCI found
it; distinguish detection, initialization, and working transfers.

## Build and run

The Windows build uses Ubuntu in WSL and QEMU from `E:\Program Files\qemu`.

```powershell
wsl -d Ubuntu -- bash -lc 'cd /mnt/e/HybridOS/Perwix-1.0 && ./build.sh'
.\run-qemu.ps1
```

ARM64 preview:

```powershell
wsl -d Ubuntu -- bash -lc 'cd /mnt/e/HybridOS/Perwix-1.0 && ./build-arm64.sh'
.\run-qemu-arm64.ps1
```

In QEMU, type `help` and check the command or driver status you changed. Do not
commit generated `build/` files or ISO images.

## Pull request workflow

1. Fork the repository on GitHub.
2. Create a branch for one change, for example `fix/usb-status-text`.
3. Make the change and build the relevant target.
4. Commit with a short message, such as `shell: clarify network status`.
5. Push the branch to your fork and open a pull request against `main`.
6. Describe the change, target (x86-64 or ARM64), build command, and QEMU or
   hardware result. Mention any limitation that remains.

For a small typo or documentation fix, a concise pull request is enough.
