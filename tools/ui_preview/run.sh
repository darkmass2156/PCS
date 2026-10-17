#!/bin/sh
# Renders the real display code on the host: docs/display/preview_*.png
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../.." && pwd); TMP=$(mktemp -d)
g++ -std=gnu++11 -O1 -I"$HERE/stub" -I"$ROOT/firmware/controller" "$HERE/preview.cpp" \
    "$ROOT/firmware/controller/screens.cpp" "$ROOT/firmware/controller/scr_gfx.cpp" -o "$TMP/preview"
"$TMP/preview" "$TMP"
python3 -I "$HERE/sheet.py" "$TMP" "$ROOT/docs/display"
rm -rf "$TMP"
