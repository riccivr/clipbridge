#!/usr/bin/env python3
import os
import struct
import io
from PIL import Image, ImageDraw

ASSETS_DIR = "assets"
os.makedirs(ASSETS_DIR, exist_ok=True)

# 1. Color Master Icon Generator (Crisp vector on transparent background)
def draw_app_icon(size):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    s = size / 64.0  # reference coordinate system: 64x64

    # High-contrast color palette
    bg_fill = (15, 23, 42, 255)       # Dark slate
    border_col = (0, 210, 255, 255)   # Electric cyan
    clip_col = (255, 255, 255, 255)   # Bright white
    line_cyan = (56, 189, 248, 255)   # Sky cyan
    line_white = (248, 250, 252, 255) # Pure light

    if size <= 24:
        # Hand-tuned pixel-aligned design for 16x16 and 24x24
        draw.rounded_rectangle([2*s, 3*s, 61*s, 61*s], radius=int(6*s), fill=bg_fill, outline=border_col, width=max(1, int(4*s)))
        draw.rounded_rectangle([18*s, 0*s, 46*s, 10*s], radius=int(3*s), fill=clip_col, outline=border_col, width=max(1, int(2*s)))
        draw.line([(10*s, 22*s), (24*s, 22*s), (38*s, 30*s), (54*s, 30*s)], fill=line_cyan, width=max(1, int(5*s)))
        draw.line([(10*s, 34*s), (24*s, 34*s), (38*s, 30*s), (54*s, 30*s)], fill=border_col, width=max(1, int(5*s)))
        draw.line([(38*s, 42*s), (54*s, 42*s)], fill=line_white, width=max(1, int(5*s)))
        draw.line([(10*s, 50*s), (54*s, 50*s)], fill=line_cyan, width=max(1, int(5*s)))
    else:
        # High-res design with crisp depth and clean geometry
        draw.rounded_rectangle([6*s, 7*s, 58*s, 61*s], radius=int(6*s), fill=(10, 15, 30, 255), outline=border_col, width=max(1, int(3.5*s)))
        draw.rounded_rectangle([20*s, 2*s, 44*s, 11*s], radius=int(3*s), fill=clip_col, outline=(0, 180, 230, 255), width=max(1, int(2*s)))
        draw.ellipse([29*s, 4*s, 35*s, 9*s], fill=bg_fill)

        # Bridge Conversion Waves (Left: Raw -> Right: Clean formatted text)
        draw.line([(13*s, 22*s), (24*s, 22*s), (34*s, 28*s), (51*s, 28*s)], fill=border_col, width=max(1, int(3.5*s)))
        draw.line([(13*s, 32*s), (24*s, 32*s), (34*s, 28*s), (51*s, 28*s)], fill=line_cyan, width=max(1, int(3.5*s)))
        draw.line([(34*s, 38*s), (51*s, 38*s)], fill=line_white, width=max(1, int(3.5*s)))
        draw.line([(13*s, 46*s), (51*s, 46*s)], fill=line_cyan, width=max(1, int(3.5*s)))
        draw.line([(13*s, 53*s), (40*s, 53*s)], fill=line_white, width=max(1, int(3*s)))

    return img

# 2. Proper Binary Windows ICO encoder with multi-resolution PNG support
def save_windows_ico(filepath, size_list):
    entries = []
    png_blobs = []
    for sz in size_list:
        im = draw_app_icon(sz)
        buf = io.BytesIO()
        im.save(buf, format="PNG")
        blob = buf.getvalue()
        png_blobs.append(blob)
        entries.append({
            'width': 0 if sz >= 256 else sz,
            'height': 0 if sz >= 256 else sz,
            'size': len(blob)
        })

    header_size = 6 + 16 * len(entries)
    current_offset = header_size
    ico_bytes = bytearray()
    # ICONDIR: Reserved=0, Type=1 (ICO), Count
    ico_bytes.extend(struct.pack('<HHH', 0, 1, len(entries)))

    for entry in entries:
        ico_bytes.extend(struct.pack('<BBBBHHII',
            entry['width'],
            entry['height'],
            0,   # Color palette count (0 for 256+ colors)
            0,   # Reserved
            1,   # Color planes
            32,  # Bits per pixel
            entry['size'],
            current_offset
        ))
        current_offset += entry['size']

    for blob in png_blobs:
        ico_bytes.extend(blob)

    with open(filepath, 'wb') as f:
        f.write(ico_bytes)
    print(f"Saved {filepath} ({len(ico_bytes)} bytes with {len(entries)} resolutions)")

ico_sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
save_windows_ico(os.path.join(ASSETS_DIR, "icon.ico"), ico_sizes)

# 3. High-Res PNGs for Linux / Web / Desktop
logo_512 = draw_app_icon(512)
logo_512.save(os.path.join(ASSETS_DIR, "logo.png"), format="PNG", optimize=True)
logo_256 = draw_app_icon(256)
logo_256.save(os.path.join(ASSETS_DIR, "clipbridge.png"), format="PNG", optimize=True)
print("Saved assets/logo.png and assets/clipbridge.png")

# 4. Scalable Vector SVGs
svg_content = '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="64" height="64" fill="none">
  <rect x="8" y="9" width="48" height="51" rx="6" fill="#0F172A" stroke="#00D2FF" stroke-width="4"/>
  <rect x="20" y="3" width="24" height="10" rx="3" fill="#FFFFFF" stroke="#00D2FF" stroke-width="2"/>
  <circle cx="32" cy="7.5" r="2.5" fill="#0F172A"/>
  <path d="M14 23h12c5 0 9 5 14 5h10" stroke="#00D2FF" stroke-width="4" stroke-linecap="round"/>
  <path d="M14 33h12c5 0 9-5 14-5h10" stroke="#38BDF8" stroke-width="4" stroke-linecap="round"/>
  <line x1="40" y1="38" x2="50" y2="38" stroke="#FFFFFF" stroke-width="4" stroke-linecap="round"/>
  <line x1="14" y1="46" x2="50" y2="46" stroke="#38BDF8" stroke-width="4" stroke-linecap="round"/>
  <line x1="14" y1="53" x2="40" y2="53" stroke="#FFFFFF" stroke-width="3.5" stroke-linecap="round"/>
</svg>'''

with open(os.path.join(ASSETS_DIR, "clipbridge.svg"), "w") as f:
    f.write(svg_content)
with open(os.path.join(ASSETS_DIR, "tray_icon.svg"), "w") as f:
    f.write(svg_content)
print("Saved assets/clipbridge.svg and assets/tray_icon.svg")

# 5. Monochrome Tray Icons for macOS Menu Bar & Linux Status
def draw_monochrome_tray_icon(size):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    s = size / 24.0
    sw = max(1, int(round(1.6 * s)))

    draw.rounded_rectangle([3.5*s, 3*s, 20.5*s, 22*s], radius=int(2*s), outline=(255, 255, 255, 255), width=sw)
    draw.rectangle([7*s, 1.5*s, 17*s, 5.5*s], fill=(0, 0, 0, 0))
    draw.rounded_rectangle([7.5*s, 1*s, 16.5*s, 5*s], radius=int(1.5*s), fill=(255, 255, 255, 255))
    draw.line([(5.5*s, 10.5*s), (9*s, 10.5*s), (13*s, 12.5*s), (18.5*s, 12.5*s)], fill=(255, 255, 255, 255), width=sw)
    draw.line([(5.5*s, 14.5*s), (9*s, 14.5*s), (13*s, 12.5*s), (18.5*s, 12.5*s)], fill=(255, 255, 255, 255), width=sw)
    draw.line([(13*s, 16.5*s), (18.5*s, 16.5*s)], fill=(255, 255, 255, 255), width=sw)
    draw.line([(5.5*s, 19.5*s), (15*s, 19.5*s)], fill=(255, 255, 255, 255), width=sw)

    return img

tray_16 = draw_monochrome_tray_icon(16)
tray_32 = draw_monochrome_tray_icon(32)
tray_16.save(os.path.join(ASSETS_DIR, "tray_template.png"))
tray_32.save(os.path.join(ASSETS_DIR, "tray_template@2x.png"))
print("Saved assets/tray_template.png & @2x.png")

# 6. macOS AppIcon.icns
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
    icon_img = draw_app_icon(sz)
    buf = io.BytesIO()
    icon_img.save(buf, format="PNG")
    png_bytes = buf.getvalue()
    entry_len = 8 + len(png_bytes)
    icns_body.extend(ostype)
    icns_body.extend(struct.pack('>I', entry_len))
    icns_body.extend(png_bytes)

total_len = 8 + len(icns_body)
icns_data = b'icns' + struct.pack('>I', total_len) + icns_body
with open(os.path.join(ASSETS_DIR, "AppIcon.icns"), "wb") as f:
    f.write(icns_data)
print("Saved assets/AppIcon.icns")
