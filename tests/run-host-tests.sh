#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/perwix-host-tests"
mkdir -p "$OUT"

cc -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT" \
  -I"$ROOT/kernel/storage" -I"$ROOT/kernel/fs" -I"$ROOT/kernel/lib" \
  "$ROOT/tests/fat32_smoke.c" "$ROOT/kernel/fs/fat32.c" \
  "$ROOT/kernel/fs/vfs.c" "$ROOT/kernel/lib/utf8.c" \
  -o "$OUT/fat32_smoke"
"$OUT/fat32_smoke"

cc -std=c11 -O2 -Wall -Wextra -Werror \
  "$ROOT/tests/utf8_test.c" "$ROOT/kernel/lib/utf8.c" \
  -o "$OUT/utf8_test"
"$OUT/utf8_test"

cc -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/kernel/include" \
  "$ROOT/tests/keyboard_utf8_test.c" "$ROOT/kernel/drivers/keyboard.c" \
  -o "$OUT/keyboard_utf8_test"
"$OUT/keyboard_utf8_test"

cc -std=c11 -O2 -Wall -Wextra -Werror \
  -I"$ROOT/kernel/drivers" -I"$ROOT/kernel/core" \
  "$ROOT/tests/framebuffer_pixel_test.c" "$ROOT/kernel/drivers/framebuffer.c" \
  -o "$OUT/framebuffer_pixel_test"
"$OUT/framebuffer_pixel_test"
