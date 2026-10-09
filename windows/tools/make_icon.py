"""Draw the screensaver icon: a bright mirrored katakana over a short green rain column."""
import sys
from PIL import Image, ImageDraw, ImageFont, ImageOps

ipag, dst = sys.argv[1], sys.argv[2]
S = 256
img = Image.new('RGBA', (S, S), (0, 6, 0, 255))
d = ImageDraw.Draw(img)
small = ImageFont.truetype(ipag, 44)
for col, (x, chars) in enumerate([(14, 'ｱﾐ7ﾂ'), (190, 'ｶ0ﾘﾈ')]):
    for i, ch in enumerate(chars):
        a = 90 + i * 40
        d.text((x, 20 + i * 52), ch, font=small, fill=(0, 200, 60, a))
big = Image.new('L', (S, S), 0)
ImageDraw.Draw(big).text((60, 24), 'ﾏ', font=ImageFont.truetype(ipag, 200), fill=255)
big = ImageOps.mirror(big)
img.paste((0, 255, 65, 255), (0, 0), big.point(lambda v: v))
img.paste((230, 255, 230, 255), (0, 0), big.point(lambda v: v * 6 // 10))
img.save(dst, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (256, 256)])
