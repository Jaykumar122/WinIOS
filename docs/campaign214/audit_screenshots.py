import hashlib
import struct
import json

files = [
    ("screenshot_01_shell_homescreen.bmp", "artifacts/campaign-213/screenshot_01_shell_homescreen.bmp"),
    ("screenshot_02_counter_app_launched.bmp", "artifacts/campaign-213/screenshot_02_counter_app_launched.bmp"),
    ("screenshot_03_counter_app_incremented.bmp", "artifacts/campaign-213/screenshot_03_counter_app_incremented.bmp"),
    ("screenshot_04_return_to_shell.bmp", "artifacts/campaign-213/screenshot_04_return_to_shell.bmp"),
    ("screenshot_05_notes_app_launched.bmp", "artifacts/campaign-213/screenshot_05_notes_app_launched.bmp"),
    ("screenshot_06_notes_app_checked.bmp", "artifacts/campaign-213/screenshot_06_notes_app_checked.bmp"),
    ("screenshot_07_shell_closeout.bmp", "artifacts/campaign-213/screenshot_07_shell_closeout.bmp")
]

bmps = []
for idx, (name, path) in enumerate(files):
    full_path = "WinIOS/" + path
    with open(full_path, "rb") as f:
        data = f.read()
    sha = hashlib.sha256(data).hexdigest()
    magic = data[:2].decode('ascii', errors='replace')
    file_size, offset = struct.unpack('<II', data[2:6] + data[10:14])
    info_size, w, h, planes, bpp = struct.unpack('<IiiHH', data[14:30])
    pixels = data[offset:]
    bmps.append({
        "index": idx + 1,
        "name": name,
        "path": path,
        "sha256": sha,
        "magic": magic,
        "file_size": file_size,
        "offset": offset,
        "width": w,
        "height": h,
        "bpp": bpp,
        "pixels": pixels
    })
    print(f"[{idx+1}] {name}: {w}x{h} bpp={bpp} size={file_size} offset={offset} sha256={sha}")

print("\n=== Pairwise Pixel Differences ===")
comparisons = []
for i in range(len(bmps) - 1):
    a = bmps[i]
    b = bmps[i+1]
    same_bytes = (a["pixels"] == b["pixels"])
    diff_pixels = 0
    min_x, max_x, min_y, max_y = 99999, -1, 99999, -1
    if not same_bytes:
        w = a["width"]
        h = a["height"]
        stride = w * 4
        for y in range(h):
            for x in range(w):
                off = (y * stride) + (x * 4)
                if a["pixels"][off:off+4] != b["pixels"][off:off+4]:
                    diff_pixels += 1
                    if x < min_x: min_x = x
                    if x > max_x: max_x = x
                    if y < min_y: min_y = y
                    if y > max_y: max_y = y
        bbox = {"min_x": min_x, "min_y": min_y, "max_x": max_x, "max_y": max_y, "width": max_x - min_x + 1, "height": max_y - min_y + 1}
    else:
        bbox = None

    cmp_record = {
        "from": a["name"],
        "to": b["name"],
        "identical_sha256": (a["sha256"] == b["sha256"]),
        "changed_pixels": diff_pixels,
        "bounding_box": bbox
    }
    comparisons.append(cmp_record)
    print(f"Diff {a['index']} -> {b['index']} ({a['name']} -> {b['name']}): identical={cmp_record['identical_sha256']}, changed_pixels={diff_pixels}, bbox={bbox}")

with open("WinIOS/docs/campaign214/214_screenshot_forensics.json", "w") as fp:
    json.dump({"screenshots": [{k: v for k, v in b.items() if k != "pixels"} for b in bmps], "comparisons": comparisons}, fp, indent=2)
print("\nWrote WinIOS/docs/campaign214/214_screenshot_forensics.json")
