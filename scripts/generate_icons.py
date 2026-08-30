#!/usr/bin/env python3
import os
import struct
import io
from PIL import Image, ImageDraw

SRC_LOGO = "/mnt/c/Users/ricci/.gemini/antigravity/brain/30df34ec-5d4a-49e2-a886-a604571b4ba8/clipbridge_logo_1788099993501.jpg"
ASSETS_DIR = "assets"
os.makedirs(ASSETS_DIR, exist_ok=True)

# 1. Color brand logo for README
if os.path.exists(SRC_LOGO):
    img = Image.open(SRC_LOGO).convert("RGBA")
    img.save(os.path.join(ASSETS_DIR, "logo.png"), format="PNG", optimize=True)
    img.resize((256, 256), Image.Resampling.LANCZOS).save(os.path.join(ASSETS_DIR, "clipbridge.png"), format="PNG")
    print("Saved assets/logo.png & assets/clipbridge.png")

# 2. Crisp Monochrome Vector SVG (macOS menu bar / Linux status / Windows tray)
svg_monochrome = '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
  <!-- Clipboard Board Outline -->
  <path d="M8 4H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V6a2 2 0 0 0-2-2h-2" />
  <!-- Clipboard Clip Top -->
  <rect x="8" y="2" width="8" height="4" rx="1" ry="1" />
  <!-- Bridge Flow Lines (Rich -> Plain Text Conversion) -->
  <path d="M4 11h4c2 0 3 2 5 2h7" />
  <path d="M4 14h4c2 0 3-2 5-2h7" />
  <!-- Clean Text Formatted Rows -->
  <line x1="14" y1="17" x2="18" y2="17" />
</svg>'''

with open(os.path.join(ASSETS_DIR, "tray_icon.svg"), "w") as f:
    f.write(svg_monochrome)
with open(os.path.join(ASSETS_DIR, "clipbridge.svg"), "w") as f:
    f.write(svg_monochrome)
print("Saved assets/tray_icon.svg & assets/clipbridge.svg")

# 3. Generate high-contrast monochrome tray icons for macOS / Linux / Windows
def create_monochrome_icon(size):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    s = size / 24.0
    sw = max(1, int(round(1.8 * s)))
    
    # Board outline
    draw.rounded_rectangle([4*s, 4*s, 20*s, 22*s], radius=int(2*s), outline=(255, 255, 255, 255), width=sw)
    # Clip cutout background
    draw.rectangle([7*s, 3*s, 17*s, 6*s], fill=(0, 0, 0, 0))
    # Clip top
    draw.rounded_rectangle([7.5*s, 2*s, 16.5*s, 5.5*s], radius=int(1*s), fill=(255, 255, 255, 255))
    
    # Bridge flow waves
    draw.line([(4*s, 11*s), (8*s, 11*s), (13*s, 13*s), (19*s, 13*s)], fill=(255, 255, 255, 255), width=sw)
    draw.line([(4*s, 15*s), (8*s, 15*s), (13*s, 13*s), (19*s, 13*s)], fill=(255, 255, 255, 255), width=sw)
    draw.line([(14*s, 17*s), (18*s, 17*s)], fill=(255, 255, 255, 255), width=sw)

    return img

# Generate macOS template icons
tray_16 = create_monochrome_icon(16)
tray_32 = create_monochrome_icon(32)
tray_16.save(os.path.join(ASSETS_DIR, "tray_template.png"))
tray_32.save(os.path.join(ASSETS_DIR, "tray_template@2x.png"))
print("Saved assets/tray_template.png & @2x.png")

# Generate Windows multi-resolution icon (.ico)
icon_sizes = [16, 24, 32, 48, 64, 128, 256]
ico_images = []
if os.path.exists(SRC_LOGO):
    base_logo = Image.open(SRC_LOGO).convert("RGBA")
    for sz in icon_sizes:
        ico_images.append(base_logo.resize((sz, sz), Image.Resampling.LANCZOS))
    
    ico_images[0].save(
        os.path.join(ASSETS_DIR, "icon.ico"),
        format="ICO",
        sizes=[(sz, sz) for sz in icon_sizes],
        append_images=ico_images[1:]
    )
    print("Saved assets/icon.ico")

# Generate macOS AppIcon.icns
if os.path.exists(SRC_LOGO):
    base_logo = Image.open(SRC_LOGO).convert("RGBA")
    icns_entries = [
        (b'icp4', 16),
        (b'icp5', 32),
        (b'icp6', 64),
        (b'ic07', 128),
        (b'ic08', 256),
        (b'ic09', 512),
        (b'ic10', 1024),
    ]
    icns_body = bytearray()
    for ostype, sz in icns_entries:
        resized = base_logo.resize((sz, sz), Image.Resampling.LANCZOS)
        buf = io.BytesIO()
        resized.save(buf, format="PNG")
        png_bytes = buf.getvalue()
        # Entry header: 4-byte OSType, 4-byte length (header + data)
        entry_len = 8 + len(png_bytes)
        icns_body.extend(ostype)
        icns_body.extend(struct.pack('>I', entry_len))
        icns_body.extend(png_bytes)
    
    total_len = 8 + len(icns_body)
    icns_data = b'icns' + struct.pack('>I', total_len) + icns_body
    with open(os.path.join(ASSETS_DIR, "AppIcon.icns"), "wb") as f:
        f.write(icns_data)
    print("Saved assets/AppIcon.icns")
