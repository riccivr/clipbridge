import os
import io
import struct
from PIL import Image

COLOR_SRC = "/mnt/c/Users/ricci/.gemini/antigravity/brain/30df34ec-5d4a-49e2-a886-a604571b4ba8/.user_uploaded/media_1788104590262.png"
WHITE_SRC = "/mnt/c/Users/ricci/.gemini/antigravity/brain/30df34ec-5d4a-49e2-a886-a604571b4ba8/.user_uploaded/media_1788104587948.png"
ASSETS_DIR = "assets"
os.makedirs(ASSETS_DIR, exist_ok=True)

def process_and_crop(src_path, pad_ratio=0.01):
    img = Image.open(src_path).convert("RGBA")
    bg_sample = img.getpixel((5, 5))
    print(f"Processing {src_path}: Size={img.size}, Mode={img.mode}")
    
    if bg_sample[3] == 0:
        cropped = img.crop(img.getbbox())
    else:
        bg_r, bg_g, bg_b = bg_sample[:3]
        datas = img.getdata()
        new_data = []
        for item in datas:
            r, g, b, a = item
            dist = ((r - bg_r)**2 + (g - bg_g)**2 + (b - bg_b)**2) ** 0.5
            if dist < 10:
                new_data.append((r, g, b, 0))
            elif dist < 32:
                alpha = int(255 * ((dist - 10) / (32 - 10)))
                new_data.append((r, g, b, alpha))
            else:
                new_data.append(item)
        img.putdata(new_data)
        bbox = img.getbbox()
        cropped = img.crop(bbox)

    w, h = cropped.size
    max_dim = max(w, h)
    pad = int(max_dim * pad_ratio)
    square_size = max_dim + pad * 2

    master = Image.new("RGBA", (square_size, square_size), (0, 0, 0, 0))
    paste_x = pad + (max_dim - w) // 2
    paste_y = pad + (max_dim - h) // 2
    master.paste(cropped, (paste_x, paste_y), cropped)
    return master

# 1. Process Color Master (Maximized edge-to-edge fill)
color_master = process_and_crop(COLOR_SRC, pad_ratio=0.01)
color_1024 = color_master.resize((1024, 1024), Image.Resampling.LANCZOS)
color_1024.save(os.path.join(ASSETS_DIR, "logo.png"), format="PNG", optimize=True)
color_256 = color_master.resize((256, 256), Image.Resampling.LANCZOS)
color_256.save(os.path.join(ASSETS_DIR, "clipbridge.png"), format="PNG", optimize=True)

# 2. Process White Master
white_master = process_and_crop(WHITE_SRC, pad_ratio=0.01)
white_1024 = white_master.resize((1024, 1024), Image.Resampling.LANCZOS)
white_1024.save(os.path.join(ASSETS_DIR, "clipbridge_white.png"), format="PNG", optimize=True)
white_256 = white_master.resize((256, 256), Image.Resampling.LANCZOS)
white_256.save(os.path.join(ASSETS_DIR, "tray_white.png"), format="PNG", optimize=True)

# 3. Process Grayscale / Dark Template Variant from White Master
gray_data = []
for r, g, b, a in white_master.getdata():
    if a > 0:
        gray_data.append((30, 30, 30, a))
    else:
        gray_data.append((0, 0, 0, 0))

gray_master = Image.new("RGBA", white_master.size)
gray_master.putdata(gray_data)
gray_256 = gray_master.resize((256, 256), Image.Resampling.LANCZOS)
gray_256.save(os.path.join(ASSETS_DIR, "tray_gray.png"), format="PNG", optimize=True)

# 4. Generate macOS / Linux Tray Template Icons (18x18, 36x36, 22x22, 44x44)
tray_18 = white_master.resize((18, 18), Image.Resampling.LANCZOS)
tray_18.save(os.path.join(ASSETS_DIR, "tray_template.png"), format="PNG")
tray_36 = white_master.resize((36, 36), Image.Resampling.LANCZOS)
tray_36.save(os.path.join(ASSETS_DIR, "tray_template@2x.png"), format="PNG")
print("Saved assets/tray_white.png, assets/tray_gray.png, assets/tray_template.png, and assets/tray_template@2x.png")

# 5. Windows .ico (9 resolutions)
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
save_windows_ico(os.path.join(ASSETS_DIR, "icon.ico"), color_master, ico_sizes)

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
    im = color_master.resize((sz, sz), Image.Resampling.LANCZOS)
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
