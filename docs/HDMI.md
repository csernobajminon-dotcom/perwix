# HDMI / pixel framebuffer console

Perwix uses the RGB framebuffer supplied by GRUB through Multiboot2. The
firmware and GRUB choose the active display mode; if that framebuffer is
visible on the HDMI-connected monitor, the Perwix console is visible there.
The kernel does not yet program HDMI hardware or the GPU display engine.

## Boot flow

1. The x86-64 Multiboot2 header requests any available 32-bpp RGB mode
   (width and height are zero, leaving mode choice to GRUB).
2. GRUB uses `gfxmode=auto` and keeps its selected mode with
   `gfxpayload=keep`, instead of forcing a fixed 720p signal.
3. The kernel parses Multiboot2 framebuffer tag 8. It accepts RGB direct-color
   framebuffers with 16, 24 or 32 bits per pixel whose complete memory range is
   inside the current 4-GiB identity map.
4. The console writes the embedded 8x16 bitmap font directly to physical
   framebuffer pixels. A glyph bit maps to exactly one framebuffer pixel;
   there is no font stretching, interpolation, bilinear filtering or
   antialiasing in the kernel renderer.
5. The text grid adapts to the selected mode, up to 160 columns by 60 rows.
   At 1280x720 this produces a 160x45 grid; at other resolutions it uses
   as many complete native 8x16 cells as fit, centered on the screen.
6. If no supported framebuffer is supplied, the kernel uses VGA text mode
   (80x25) and COM1 serial output.

## Supported display path

- Multiboot2 RGB direct-color framebuffer (type 1).
- 16-, 24- and 32-bpp modes with channel masks no larger than 8 bits.
- GRUB/firmware-selected resolution; the kernel does not resize glyphs.
- Embedded 8x16 glyphs for ASCII, U+0400..U+045F, U+0490 and U+0491. Valid
  UTF-8 outside this glyph set is decoded but displayed using the `?` fallback.
- `displayinfo` reports the active mode; `utf8test` checks Ukrainian glyphs.

## Sharpness notes

For the crispest result, keep GRUB's `gfxmode=auto` and `gfxpayload=keep` so
it can choose a preferred display mode instead of forcing a fixed 720p signal.
Perwix writes exact RGB pixel values and does not soften the font. A monitor
or GPU scaling a non-native signal can still blur the whole image; that
scaling happens outside the kernel. Check `displayinfo` to see the mode passed
to Perwix. If GRUB's menu itself looks soft, the selected mode may still be
scaled by firmware or the display.

## What is not implemented

This is a firmware-configured framebuffer scanout path, not a universal HDMI
hardware driver. The kernel does not yet read EDID over DDC, detect hot-plug
events, train HDMI links, configure a GPU display engine, route among multiple
connectors, drive HDMI audio or HDCP, or provide GPU acceleration. Those
features require hardware-specific display drivers for each GPU family.
