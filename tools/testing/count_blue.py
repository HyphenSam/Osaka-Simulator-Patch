# Count frames with Osaka's blue-hair glitch (pure 0,0,255 pixels) in a folder of PNG screenshots.
# Usage: python count_blue.py <folder>
import sys, glob
from PIL import Image
hits = []
files = sorted(glob.glob(sys.argv[1] + '/*.png'))
for f in files:
    im = Image.open(f).convert('RGB').resize((640, 512))
    c = sum(1 for p in im.getdata() if p[2] > 230 and p[0] < 30 and p[1] < 30)
    if c > 20: hits.append((__import__('os').path.basename(f), c))
print(f'{len(hits)}/{len(files)} frames with blue hair:', hits)
