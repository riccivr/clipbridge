import os
import io
import struct
from PIL import Image

SRC = "/mnt/c/Users/ricci/.gemini/antigravity/brain/30df34ec-5d4a-49e2-a886-a604571b4ba8/.user_uploaded/media_1788103524397.jpg"
ASSETS_DIR = "assets"
os.makedirs(ASSETS_DIR, exist_ok=True)

img = Image.open(SRC).convert("RGBA")
print("Size:", img.size, "Corner pixel:", img.getpixel((5, 5)))

# Background removal with smooth anti-aliased alpha thresholding
bg_r, bg_g, bg_b = img.getpixel((5, 5))[:3]
datas = img.getdata()
new_data = []

for item in datas:
    r, g, b, a = item
    dist = ((r - bg_r)**2 + (g - bg_g)**2 + (b - bg_b)**2) ** 0.5
    if dist < 12:
        new_data.append((r, g, b, 0))
    elif dist < 35:
        alpha = int(255 * ((dist - 12) / (35 - 12)))
        new_data.append((r, g, b, alpha))
    else:
        new_data.append(item)

img.putdata(new_data)
bbox = img.getbbox()
print("BBox:", bbox)
cropped = img.crop(bbox)

# Add 6% margin and make square
w, h = cropped.size
max_dim = max(w, h)
pad = int(max_dim * 0.06)
square_size = max_dim + pad * 2

master_icon = Image.new("RGBA", (square_size, square_size), (0, 0, 0, 0))
paste_x = pad + (max_dim - w) // 2
paste_y = pad + (max_dim - h) // 2
master_icon.paste(cropped, (paste_x, paste_y), cropped)

# 1. Save high-resolution PNGs
master_1024 = master_icon.resize((1024, 1024), Image.Resampling.LANCZOS)
master_1024.save(os.path.join(ASSETS_DIR, "logo.png"), format="PNG", optimize=True)

master_256 = master_icon.resize((256, 256), Image.Resampling.LANCZOS)
master_256.save(os.path.join(ASSETS_DIR, "clipbridge.png"), format="PNG", optimize=True)

# 2. Save 9-resolution Windows .ico
def save_windows_ico(filepath, master, size_list):
    entries = []
    png_blobs = []
    for sz in size_list:
        im = master.resize((sz, sz), Image.Resampling.LANCZOS)
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
    ico_bytes.extend(struct.pack('<HHH', 0, 1, len(entries)))

    for entry in entries:
        ico_bytes.extend(struct.pack('<BBBBHHII',
            entry['width'],
            entry['height'],
            0,
            0,
            1,
            32,
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
save_windows_ico(os.path.join(ASSETS_DIR, "icon.ico"), master_icon, ico_sizes)

# 3. Save macOS AppIcon.icns
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
    im = master_icon.resize((sz, sz), Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, format="PNG")
    blob = buf.getvalue()
    entry_len = 8 + len(blob)
    icns_body.extend(ostype)
    icns_body.extend(struct.pack('>I', entry_len))
    icns_body.extend(blob)

total_len = 8 + len(icns_body)
icns_data = b'icns' + struct.pack('>I', total_len) + icns_body
with open(os.path.join(ASSETS_DIR, "AppIcon.icns"), "wb") as f:
    f.write(icns_data)
print(f"Saved {os.path.join(ASSETS_DIR, 'AppIcon.icns')} ({len(icns_data)} bytes)")
