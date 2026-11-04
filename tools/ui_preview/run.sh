#!/bin/sh
# Renders the real display code on the host into docs/display/preview170_*.png (1.9", screens170.cpp + splash.h)
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../.." && pwd); TMP=$(mktemp -d)
g++ -std=gnu++11 -O1 -DFB_W=320 -DFB_H=170 -I"$HERE/stub" -I"$ROOT/firmware/controller" "$HERE/preview170.cpp" \
    "$ROOT/firmware/controller/screens170.cpp" "$ROOT/firmware/controller/scr_gfx.cpp" -o "$TMP/preview170"
"$TMP/preview170" "$TMP"
python3 -I "$HERE/sheet.py" "$TMP" "$ROOT/docs/display"
rm -rf "$TMP"
