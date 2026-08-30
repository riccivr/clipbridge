import os
from PIL import Image

SRC = "assets/logo.png"
ASSETS_DIR = "package/Assets"
os.makedirs(ASSETS_DIR, exist_ok=True)

img = Image.open(SRC).convert("RGBA")
w, h = img.size
cropped = img.crop(img.getbbox())

def make_padded_icon(target_w, target_h, pad_ratio=0.08):
    cw, ch = cropped.size
    scale = min((target_w * (1 - pad_ratio * 2)) / cw, (target_h * (1 - pad_ratio * 2)) / ch)
    new_w = int(cw * scale)
    new_h = int(ch * scale)
    scaled = cropped.resize((new_w, new_h), Image.Resampling.LANCZOS)

    canvas = Image.new("RGBA", (target_w, target_h), (0, 0, 0, 0))
    paste_x = (target_w - new_w) // 2
    paste_y = (target_h - new_h) // 2
    canvas.paste(scaled, (paste_x, paste_y), scaled)
    return canvas

# 1. Store and Tile Assets required by Microsoft Store
sizes = {
    "StoreLogo.png": (50, 50),
    "Square44x44Logo.png": (44, 44),
    "Square44x44Logo.targetsize-44.png": (44, 44),
    "Square44x44Logo.targetsize-24.png": (24, 24),
    "Square44x44Logo.targetsize-16.png": (16, 16),
    "Square150x150Logo.png": (150, 150),
    "Wide310x150Logo.png": (310, 150),
    "Square310x310Logo.png": (310, 310),
    "SplashScreen.png": (620, 300)
}

for name, (tw, th) in sizes.items():
    icon = make_padded_icon(tw, th)
    out_path = os.path.join(ASSETS_DIR, name)
    icon.save(out_path, format="PNG", optimize=True)
    print(f"Generated {out_path} ({tw}x{th})")
