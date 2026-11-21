# imgdiff.py a.bmp b.bmp diff.png label: mean absolute difference and share of differing pixels.
import sys
sys.path.insert(0, '/work/.toolchain/pylib')
from PIL import Image, ImageChops
a, b, out, label = sys.argv[1:5]
try:
    ia = Image.open(a).convert('RGB')
    ib = Image.open(b).convert('RGB')
except Exception as e:
    print(f'{label}: missing image ({e})')
    sys.exit(0)
d = ImageChops.difference(ia, ib)
px = list(d.get_flattened_data() if hasattr(d, "get_flattened_data") else d.getdata())
diff = sum(1 for p in px if max(p) > 8)
mean = sum(sum(p) for p in px) / (3 * len(px))
print(f'{label}: mean diff {mean:.2f}, pixels differing >8: {100 * diff / len(px):.2f} %')
side = Image.new('RGB', (ia.width * 3 // 2, ia.height // 2))
side.paste(ia.resize((ia.width // 2, ia.height // 2)), (0, 0))
side.paste(ib.resize((ia.width // 2, ia.height // 2)), (ia.width // 2, 0))
side.paste(d.point(lambda v: min(255, v * 4)).resize((ia.width // 2, ia.height // 2)), (ia.width, 0))
side.save(out)
