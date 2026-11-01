# UTF-8 console and keyboard

The console uses a strict UTF-8 decoder. It validates continuation bytes,
rejects overlong encodings, surrogate values and code points above U+10FFFF,
and replaces malformed input with U+FFFD both on the framebuffer and on
COM1. Valid UTF-8 bytes are preserved for a configured UTF-8 serial terminal.

The shell line editor accepts Unicode code points from the keyboard driver,
encodes them to UTF-8 in its command buffer, and removes complete UTF-8
sequences when Backspace is pressed. FAT32 long filenames are decoded from
UTF-16 to UTF-8 when directories are listed and paths are resolved.

## Keyboard layouts

- US QWERTY is selected at boot.
- Press **Alt+Shift** in either order to toggle between US and Ukrainian
  ЙЦУКЕН. One held chord toggles only once, even if the key repeats.
- Run `layout` to see the active layout.
- Shift and Caps Lock produce uppercase Ukrainian letters, including Є, І,
  Ї and Ґ.

## Glyph coverage

The built-in 8x16 bitmap contains ASCII, Cyrillic U+0400..U+045F and the
Ukrainian letters Ґ/ґ (U+0490/U+0491). The UTF-8 implementation itself accepts
all valid Unicode scalar values, but a character that is not present in the
embedded bitmap font is drawn as `?`. VGA text fallback is ASCII-only.

Run `utf8test` to display a Ukrainian sample. This is UTF-8 text support, not
a full Unicode shaping engine: bidirectional layout, combining-mark shaping,
complex scripts and font fallback chains are not implemented.

## Host-side tests

From the repository root, run `./tests/run-host-tests.sh` on Linux x86-64. The
tests exercise strict UTF-8 edge cases (including four-byte characters and
malformed sequences), keyboard layout switching in both Alt/Shift orders,
repeat handling and uppercase Ukrainian input, exact 1:1 framebuffer pixels
at 1280x720, and FAT32 mounting with a synthetic disk containing a Unicode
long filename and UTF-8 file contents.
