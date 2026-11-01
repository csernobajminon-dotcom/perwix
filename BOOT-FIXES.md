# Perwix GRUB boot-menu fixes

This package corrects three issues in the branded GRUB menu:

- Removes invalid percentage values from the optional terminal geometry settings.
- Uses GRUB-supported symbolic colors for `menu_color_normal` and `menu_color_highlight`.
- Copies all `select_*.png` theme slices into the ISO so the selected menu item frame is available.

The GRUB theme still uses the Perwix background artwork and hex colors for theme components, which are supported by the GRUB theme format.

Build from WSL using `./build.sh`, then launch QEMU with `./run-qemu.ps1` from PowerShell after the ISO has been created.
