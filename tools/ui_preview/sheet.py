import sys, glob, os
from PIL import Image, ImageDraw, ImageFont
src, dst = sys.argv[1], sys.argv[2]
S = 2
font = ImageFont.truetype('/usr/share/fonts/truetype/freefont/FreeSansBold.ttf', 18)
def sheet(names, path, cols=3):
    ims = [(n, Image.open(os.path.join(src, n + '.ppm'))) for n in names]
    w, h = ims[0][1].size
    rows = (len(ims) + cols - 1) // cols
    out = Image.new('RGB', (cols * (w * S + 16) + 16, rows * (h * S + 40) + 8), (40, 40, 40))
    d = ImageDraw.Draw(out)
    for i, (n, im) in enumerate(ims):
        x, y = 16 + (i % cols) * (w * S + 16), 8 + (i // cols) * (h * S + 40)
        d.text((x, y), n, font=font, fill=(230, 230, 230))
        out.paste(im.resize((w * S, h * S), Image.NEAREST), (x, y + 28))
    out.save(os.path.join(dst, path))
    print('wrote', path)
sheet(['b170_0_noot', 'b170_1_noot', 'b170_2_noot', 'b170_0_crash_a', 'b170_1_crash_b', 'b170_2_crash_c',
       'b170_0_rog'], 'preview170_boot.png')
sheet(['s170_1', 's170_2', 's170_3', 's170_1_fault', 's170_2_fault', 's170_3_fault',
       's170_1_alarm', 's170_2_alarm', 's170_3_alarm', 's170_1_test',
       's170_1_lite', 's170_2_lite', 's170_3_lite'], 'preview170_modes.png')
