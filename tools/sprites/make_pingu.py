#!/usr/bin/env python3
"""Generates the boot-screen penguins (original Pingu-style pixel-art homages, one picked at random per boot).

Each sprite has two frames: beak normal and "noot" (trumpet / O-lips). Only the rectangle where the
frames differ is stored twice, so the firmware animates by redrawing that patch.

Writes:
  firmware/controller/boot_sprite.h   PROGMEM sprites + SPLASH_SPRITES table
  docs/display/pingu_preview.png      every sprite, both frames, on its background
Run from the repo root:  python3 tools/sprites/make_pingu.py
Then copy boot_sprite.h into tests/11_ui_dev.
"""
import math, os

def ell(cx, cy, rx, ry, x, y, tilt=0.0):
    """Ellipse test; tilt (radians) rotates the ellipse around its centre."""
    dx, dy = x - cx, y - cy
    if tilt:
        c, s = math.cos(tilt), math.sin(tilt)
        dx, dy = dx * c + dy * s, -dx * s + dy * c
    return (dx / rx) ** 2 + (dy / ry) ** 2 <= 1.0

def seg(x0, y0, x1, y1, r, x, y):
    """Thick line segment test."""
    vx, vy = x1 - x0, y1 - y0
    t = max(0.0, min(1.0, ((x - x0) * vx + (y - y0) * vy) / (vx * vx + vy * vy)))
    return math.hypot(x - (x0 + t * vx), y - (y0 + t * vy)) <= r

def outline(g, color, w=1, inner_pairs=()):
    """Silhouette outline of `w` px in `color`; inner_pairs = (a, b): paint a-pixels touching b."""
    H, W = len(g), len(g[0])
    n4 = ((1, 0), (-1, 0), (0, 1), (0, -1))
    for a, b in inner_pairs:
        hit = [(x, y) for y in range(H) for x in range(W) if g[y][x] == a and any(
            0 <= x + dx < W and 0 <= y + dy < H and g[y + dy][x + dx] == b for dx, dy in n4)]
        for x, y in hit:
            g[y][x] = color
    for _ in range(w):
        add = [(x, y) for y in range(H) for x in range(W) if g[y][x] == 0 and any(
            0 <= x + dx < W and 0 <= y + dy < H and g[y + dy][x + dx] not in (0,)
            for dx, dy in n4)]
        for x, y in add:
            g[y][x] = color
    return g

def render(W, H, paint):
    g = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            g[y][x] = paint(x + 0.5, y + 0.5)
    return g

# ---------------------------------------------------------------- 1: hobo (stick + bundle, facing left)
HOBO_PAL = {0: None, 1: (24, 24, 28), 2: (84, 84, 92), 3: (250, 250, 250), 4: (210, 64, 40),
            5: (140, 36, 24), 6: (240, 150, 60), 7: (140, 44, 60), 8: (214, 96, 120), 9: (240, 214, 140)}

def hobo(noot):
    def p(x, y):
        c = 0
        if ell(30, 54.5, 10, 2.0, x, y) or ell(33, 57.5, 11, 2.0, x, y):           # feet
            c = 6
        if ell(35, 38, 11, 16, x, y) or ell(33, 46, 12, 8, x, y):                  # body
            c = 2
        if ell(30.5, 40, 7.4, 13.5, x, y):                                         # yellow seam
            c = 9
        if ell(29.5, 40, 6.6, 13, x, y):                                           # belly
            c = 3
        if seg(8, 32, 58, 18, 0.9, x, y):                                          # stick
            c = 1
        if ell(54.5, 27, 4.8, 6.6, x, y) or ell(53, 19.5, 2.2, 2.4, x, y):         # bundle
            c = 7
            if (ell(54.5, 27, 4.8, 6.6, x, y) and (int(x) in (52, 56) or int(y) in (24, 29))):
                c = 8
        if ell(21, 30.5, 6.5, 2.8, x, y, -0.25):                                   # arm holding the stick
            c = 2
        if ell(35, 19, 8.5, 8.5, x, y):                                            # head
            c = 2
        if ell(31.8, 19, 3.4, 3.4, x, y):                                          # eye
            c = 3
        if ell(30.6, 19.3, 1.6, 1.7, x, y):
            c = 1
        if not noot:
            if ell(25.5, 21.5, 3.0, 1.5, x, y):
                c = 4
        else:
            if 13 < x < 28 and abs(y - 21.5) <= 0.9:                               # tube
                c = 4
            if 9 < x <= 16 and abs(y - 21.5) <= 0.9 + (16 - x) * 0.55:             # flare
                c = 4
            if ell(10, 21.5, 1.6, 4.6, x, y):
                c = 5
        return c
    g = render(64, 61, p)
    return outline(g, 1, 1, inner_pairs=((3, 2), (6, 2), (7, 0)))

# ---------------------------------------------------------------- 2: sit (flat, long trumpet up-right)
SIT_PAL = {0: None, 1: (28, 28, 36), 2: (234, 226, 216), 3: (196, 120, 64), 4: (200, 64, 74),
           5: (150, 40, 52), 6: (250, 250, 250), 7: (60, 60, 70)}

def sit(noot):
    def p(x, y):
        c = 0
        if y > 39:
            return 0                                                               # flat floor
        if ell(10.5, 33.5, 7, 4.2, x, y, -0.42) or ell(50.5, 32.5, 8, 4.2, x, y, 0.38):      # flippers/feet out
            c = 3
        if ell(29, 31, 17.5, 13.5, x, y):                                          # body dome
            c = 1
        if ell(30.5, 31, 10, 10, x, y):                                            # belly
            c = 2
        if ell(29, 13, 7.5, 7.5, x, y) or ell(29, 20, 5.5, 4, x, y):               # head + neck
            c = 1
        if ell(33.5, 11.5, 2.3, 2.3, x, y):                                        # eye
            c = 6
        if ell(34.0, 11.6, 1.1, 1.2, x, y):
            c = 1
        if not noot:
            if ell(38, 13.5, 3.2, 1.7, x, y, -0.15):
                c = 4
        else:
            if seg(35.5, 13.5, 55, 7, 1.3, x, y):                                  # long tube
                c = 4
            if ell(57, 6, 2.2, 3.8, x, y, 0.3):                                    # flared end
                c = 4
            if ell(58, 5.6, 1.0, 2.6, x, y, 0.3):
                c = 5
        return c
    return render(66, 40, p)

# ---------------------------------------------------------------- 3: bow (leaning forward, O-lips)
BOW_PAL = {0: None, 1: (22, 24, 28), 2: (236, 236, 232), 3: (232, 212, 120), 4: (170, 40, 56),
           5: (90, 16, 24), 6: (156, 72, 48), 7: (250, 250, 250)}

def bow(noot):
    def p(x, y):
        c = 0
        if ell(17, 48, 7.5, 3.2, x, y, 0.15) or ell(29, 45.5, 9, 3.0, x, y, 0.1):  # feet
            c = 6
        if ell(27, 20, 20, 10, x, y, -0.22) or ell(21, 31, 12, 13, x, y):         # back (sloping down to the tail) + torso
            c = 1
        if ell(9.5, 27.5, 4, 3, x, y, -0.3):                                       # flipper bump
            c = 1
        if ell(24.5, 31, 11.3, 12.2, x, y) and c == 1:                             # yellow rim
            c = 3
        if ell(25.5, 32, 10, 11, x, y):                                            # belly
            c = 2
        if ell(40, 16, 6.5, 6.5, x, y) or ell(46, 18, 8, 8, x, y) or ell(39.5, 9.5, 3.5, 2.6, x, y):
            c = 1                                                                  # neck, head, bump
        if ell(45, 20.5, 3.0, 2.8, x, y):                                          # near eye
            c = 7
        if ell(45.7, 20.7, 1.3, 1.3, x, y):
            c = 1
        if ell(51.6, 13.8, 1.1, 1.8, x, y):                                        # far eye, edge-on
            c = 7
        if not noot:
            if ell(53.6, 18.5, 1.9, 2.4, x, y):
                c = 4
            if 52.5 < x < 55.5 and 18.6 < y < 19.4:
                c = 5
        else:
            if ell(54.6, 17.8, 3.0, 4.3, x, y):                                    # pursed "O"
                c = 4
            if ell(55.4, 17.8, 1.3, 2.6, x, y):
                c = 5
        return c
    return render(60, 52, p)

SPRITES = [
    ("HOBO", hobo, HOBO_PAL, (244, 244, 244)),
    ("SIT", sit, SIT_PAL, (240, 240, 240)),
    ("BOW", bow, BOW_PAL, (196, 224, 236)),
]

# ---------------------------------------------------------------- encoding
def bbox_diff(a, b):
    pts = [(x, y) for y in range(len(a)) for x in range(len(a[0])) if a[y][x] != b[y][x]]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1

def rle(g, x0, y0, x1, y1):
    """Bytes: high nibble = palette index, low nibble = run length - 1. Rows packed back to back."""
    out = []
    for y in range(y0, y1):
        x = x0
        while x < x1:
            c, n = g[y][x], 1
            while x + n < x1 and g[y][x + n] == c and n < 16:
                n += 1
            out.append((c << 4) | (n - 1))
            x += n
    return out

def c565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def arr(name, data):
    lines = [f"static const uint8_t {name}[] PROGMEM = {{"]
    for i in range(0, len(data), 16):
        lines.append("  " + ", ".join(f"0x{v:02X}" for v in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)

def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    h = [
        "#pragma once",
        "// GENERATED by tools/sprites/make_pingu.py — edit the script, not this file.",
        "// Original Pingu-style pixel art for the boot splash. RLE: hi nibble = palette index",
        "// (0 = background), lo nibble = run - 1; rows back to back, left to right.",
        "#include <Arduino.h>",
        "",
        "struct SplashSprite {",
        "  uint8_t w, h;                 // sprite size in pixels",
        "  uint8_t px, py, pw, ph;       // patch where the two frames differ",
        "  uint16_t bg;                  // screen background (RGB565)",
        "  const uint16_t *pal;          // 16 entries, PROGMEM",
        "  const uint8_t *base;  uint16_t baseLen;",
        "  const uint8_t *beak;  uint16_t beakLen;   // patch, normal beak",
        "  const uint8_t *noot;  uint16_t nootLen;   // patch, noot",
        "};",
        "",
    ]
    table, frames = [], []
    for name, fn, pal, bg in SPRITES:
        a, b = fn(False), fn(True)
        W, H = len(a[0]), len(a)
        x0, y0, x1, y1 = bbox_diff(a, b)
        base, pa, pb = rle(a, 0, 0, W, H), rle(a, x0, y0, x1, y1), rle(b, x0, y0, x1, y1)
        p = [0 if pal.get(i) is None else c565(pal[i]) for i in range(16)]
        p[0] = c565(bg)
        h.append(f"static const uint16_t {name}_PAL[16] PROGMEM = {{" + ", ".join(f"0x{v:04X}" for v in p) + "};")
        h.append(arr(f"{name}_BASE", base))
        h.append(arr(f"{name}_BEAK", pa))
        h.append(arr(f"{name}_NOOT", pb))
        h.append("")
        table.append(f"  {{{W}, {H}, {x0}, {y0}, {x1 - x0}, {y1 - y0}, 0x{c565(bg):04X}, {name}_PAL, "
                     f"{name}_BASE, {len(base)}, {name}_BEAK, {len(pa)}, {name}_NOOT, {len(pb)}}},")
        frames.append((name, a, b, pal, bg))
        print(f"{name}: {W}x{H}, base {len(base)} B, patch {x1 - x0}x{y1 - y0} at {x0},{y0} ({len(pa)}/{len(pb)} B)")
    h.append("static const SplashSprite SPLASH_SPRITES[] = {")
    h += table
    h.append("};")
    h.append("#define SPLASH_COUNT (sizeof(SPLASH_SPRITES) / sizeof(SPLASH_SPRITES[0]))")
    h.append("")
    out = os.path.join(root, "firmware/controller/boot_sprite.h")
    open(out, "w").write("\n".join(h))
    print("wrote", out)

    try:
        from PIL import Image
    except ImportError:
        return
    S = 4
    cw = max(len(a[0]) for _, a, _, _, _ in frames) * S + 16
    ch = max(len(a) for _, a, _, _, _ in frames) * S + 16
    img = Image.new("RGB", (2 * cw, len(frames) * ch), (60, 60, 60))
    for r, (name, a, b, pal, bg) in enumerate(frames):
        for i, g in enumerate((a, b)):
            ox, oy = i * cw, r * ch
            for y in range(ch):
                for x in range(cw):
                    img.putpixel((ox + x, oy + y), bg)
            for y in range(len(g)):
                for x in range(len(g[0])):
                    if g[y][x]:
                        for dy in range(S):
                            for dx in range(S):
                                img.putpixel((ox + 8 + x * S + dx, oy + 8 + y * S + dy), pal[g[y][x]])
    os.makedirs(os.path.join(root, "docs/display"), exist_ok=True)
    p = os.path.join(root, "docs/display/pingu_preview.png")
    img.save(p)
    print("wrote", p)

if __name__ == "__main__":
    main()
