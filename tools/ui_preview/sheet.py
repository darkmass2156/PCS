import sys, glob, os
from PIL import Image, ImageDraw, ImageFont
src, dst = sys.argv[1], sys.argv[2]
S = 2
font = ImageFont.truetype('/usr/share/fonts/truetype/freefont/FreeSansBold.ttf', 18)
def sheet(names, path, cols=3):
    ims = [(n, Image.open(os.path.join(src, n + '.ppm'))) for n in names]
    rows = (len(ims) + cols - 1) // cols
    out = Image.new('RGB', (cols * (280 * S + 16) + 16, rows * (240 * S + 40) + 8), (40, 40, 40))
    d = ImageDraw.Draw(out)
    for i, (n, im) in enumerate(ims):
        x, y = 16 + (i % cols) * (280 * S + 16), 8 + (i // cols) * (240 * S + 40)
        d.text((x, y), n, font=font, fill=(230, 230, 230))
        out.paste(im.resize((280 * S, 240 * S), Image.NEAREST), (x, y + 28))
    out.save(os.path.join(dst, path))
    print('wrote', path)
sheet(['boot0_noot', 'boot0_glitch1', 'boot0_glitch2', 'boot0_glitch3', 'boot0_rog_sweep', 'boot0_rog',
       'boot1_glitch2', 'boot2_glitch1', 'boot2_glitch3'], 'preview_boot.png')
sheet(['mode1', 'mode2', 'mode3', 'mode1_changed', 'mode2_changed', 'mode3_changed',
       'mode1_fault', 'mode2_fault', 'mode3_fault', 'mode1_no12v', 'mode2_no12v', 'mode3_no12v'], 'preview_modes.png')
