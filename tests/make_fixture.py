from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont
root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=True)
im = Image.new("RGB", (480, 320), "white"); draw = ImageDraw.Draw(im)
font = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", 45)
draw.text((16, 8), "SPECTRUM", font=font, fill="black")
draw.text((18, 257), "PRINT 1.4.4", font=font, fill="black")
for x in range(20, 220, 12): draw.rectangle((x, 82, x+4, 226), fill="black")
for y in range(80, 232, 12): draw.rectangle((250, y, 450, y+4), fill="black")
draw.line((242, 237, 458, 75), fill="black", width=3)
im.save(root / "resolution-chart.png")
