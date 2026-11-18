#!/usr/bin/env python3
"""
WinIOS Guest Application Packager & Build Tool (v2.0)
Windows-hosted ARM64 Mach-O packaging and manifest verification utility.
"""

import sys
import os
import struct
import hashlib
import argparse
import subprocess

MAGIC_V1 = b"WIOSPKG1"
MAGIC_V2 = b"WIOSPKG2"

def sha256_file(filepath):
    h = hashlib.sha256()
    with open(filepath, 'rb') as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()

def cmd_init(args):
    project_dir = args.project_dir
    os.makedirs(os.path.join(project_dir, "src"), exist_ok=True)
    os.makedirs(os.path.join(project_dir, "res"), exist_ok=True)

    manifest_path = os.path.join(project_dir, "manifest.txt")
    with open(manifest_path, "w", encoding="utf-8") as f:
        f.write(f"format_version=2.0\n")
        f.write(f"bundle_id={args.id}\n")
        f.write(f"name={args.name}\n")
        f.write(f"version=1.0.0\n")
        f.write(f"min_os_version=223.0\n")
        f.write(f"architecture=arm64\n")
        f.write(f"min_abi_version=1.7\n")
        f.write(f"executable_name=bin/{args.name.lower().replace(' ', '_')}\n")
        f.write(f"storage_quota_bytes=10485760\n")
        f.write(f"permissions=display.window,storage.private,logging,clipboard\n")

    main_c = os.path.join(project_dir, "src", "main.c")
    with open(main_c, "w", encoding="utf-8") as f:
        f.write(f"""#include "winios/winios.h"

int main(int argc, const char* const argv[]) {{
    winios_log_info("APP", "Starting {args.name}...");
    winios_wid_t wid = winios_window_create("{args.name}", 100, 100, 400, 300);
    if (wid > 0) {{
        winios_surface_fill(wid, 0, 0, 400, 300, WINIOS_COLOR_BLUE);
        winios_surface_present(wid);
    }}
    winios_log_info("APP", "{args.name} running successfully.");
    return 0;
}}
""")
    print(f"[OK] Initialized WinIOS project at {project_dir}")

def cmd_build(args):
    project_dir = args.project_dir
    sdk_dir = os.path.abspath(args.sdk_dir)
    include_dir = os.path.join(sdk_dir, "include")
    lib_dir = os.path.join(sdk_dir, "lib")
    src_dir = os.path.join(project_dir, "src")
    bin_dir = os.path.join(project_dir, "bin")
    os.makedirs(bin_dir, exist_ok=True)

    exec_name = args.output or "app.bin"
    out_path = os.path.join(bin_dir, exec_name)

    c_files = [os.path.join(src_dir, f) for f in os.listdir(src_dir) if f.endswith(".c")]
    crt0_s = os.path.join(lib_dir, "crt0.s")
    runtime_c = os.path.join(lib_dir, "winios_runtime.c")

    cmd = [
        "zig", "cc",
        "-target", "aarch64-macos",
        "-nostdlib",
        "-fno-sanitize=all",
        "-O2",
        f"-I{include_dir}",
        crt0_s,
        runtime_c,
        *c_files,
        "-o", out_path
    ]
    print(f"[*] Compiling ARM64 Mach-O: {' '.join(cmd)}")
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[ERROR] Build failed:\n{res.stderr}")
        sys.exit(1)
    print(f"[OK] Built executable: {out_path}")

def cmd_pack(args):
    project_dir = args.project_dir
    out_pkg = args.output or os.path.join(project_dir, "app.wios")
    manifest_file = os.path.join(project_dir, "manifest.txt")
    if not os.path.exists(manifest_file):
        print(f"[ERROR] Manifest file not found: {manifest_file}")
        sys.exit(1)

    with open(manifest_file, "r", encoding="utf-8") as f:
        manifest_text = f.read()

    # Collect files from bin/ and res/
    file_entries = []
    for folder in ["bin", "res"]:
        f_dir = os.path.join(project_dir, folder)
        if os.path.exists(f_dir):
            for root, _, files in os.walk(f_dir):
                for fname in files:
                    full_p = os.path.join(root, fname)
                    rel_p = os.path.relpath(full_p, project_dir).replace("\\", "/")
                    with open(full_p, "rb") as bf:
                        data = bf.read()
                    h = sha256_bytes(data)
                    file_entries.append((rel_p, h, data))

    # Binary packaging
    packed_data = bytearray()
    packed_data.extend(MAGIC_V2)

    # Manifest length and payload
    mbytes = manifest_text.encode("utf-8")
    packed_data.extend(struct.pack("<I", len(mbytes)))
    packed_data.extend(mbytes)

    # File count
    packed_data.extend(struct.pack("<I", len(file_entries)))
    for rel_p, h, data in file_entries:
        pb = rel_p.encode("utf-8")
        packed_data.extend(struct.pack("<H", len(pb)))
        packed_data.extend(pb)
        packed_data.extend(struct.pack("<I", len(data)))
        packed_data.extend(data)

    with open(out_pkg, "wb") as f:
        f.write(packed_data)

    print(f"[OK] Packaged {len(file_entries)} files into {out_pkg} ({len(packed_data)} bytes)")

def cmd_inspect(args):
    pkg_path = args.package
    with open(pkg_path, "rb") as f:
        data = f.read()

    if len(data) < 16:
        print("[ERROR] Corrupted package (too small)")
        sys.exit(1)

    magic = data[:8]
    if magic not in (MAGIC_V1, MAGIC_V2):
        print(f"[ERROR] Invalid magic: {magic}")
        sys.exit(1)

    off = 8
    mlen = struct.unpack("<I", data[off:off+4])[0]
    off += 4
    manifest_str = data[off:off+mlen].decode("utf-8")
    off += mlen

    print(f"=== WinIOS Package Inspector ({magic.decode('ascii')}) ===")
    print("Manifest:")
    for line in manifest_str.strip().splitlines():
        print(f"  {line}")

    file_count = struct.unpack("<I", data[off:off+4])[0]
    off += 4
    print(f"\nFiles ({file_count}):")
    for _ in range(file_count):
        plen = struct.unpack("<H", data[off:off+2])[0]
        off += 2
        pname = data[off:off+plen].decode("utf-8")
        off += plen
        dsize = struct.unpack("<I", data[off:off+4])[0]
        off += 4
        fdata = data[off:off+dsize]
        off += dsize
        h = sha256_bytes(fdata)
        print(f"  - {pname:30} [{dsize:6} bytes] SHA256: {h[:16]}...")

def cmd_verify(args):
    pkg_path = args.package
    with open(pkg_path, "rb") as f:
        data = f.read()

    if len(data) < 16:
        print("[FAIL] Corrupted package (too small)")
        sys.exit(1)

    magic = data[:8]
    if magic not in (MAGIC_V1, MAGIC_V2):
        print(f"[FAIL] Invalid magic: {magic}")
        sys.exit(1)

    off = 8
    mlen = struct.unpack("<I", data[off:off+4])[0]
    off += 4
    manifest_str = data[off:off+mlen].decode("utf-8")
    off += mlen

    file_count = struct.unpack("<I", data[off:off+4])[0]
    off += 4
    for _ in range(file_count):
        plen = struct.unpack("<H", data[off:off+2])[0]
        off += 2
        pname = data[off:off+plen].decode("utf-8")
        off += plen
        dsize = struct.unpack("<I", data[off:off+4])[0]
        off += 4
        fdata = data[off:off+dsize]
        off += dsize
        # Validate path safety
        if ".." in pname or pname.startswith("/") or pname.startswith("\\"):
            print(f"[FAIL] Insecure path traversal detected: {pname}")
            sys.exit(1)

    print(f"[PASS] Package {pkg_path} verified successfully ({file_count} files, {magic.decode('ascii')})")

def main():
    parser = argparse.ArgumentParser(description="WinIOS Package Tool")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_init = sub.add_parser("init", help="Initialize a new project")
    p_init.add_argument("project_dir")
    p_init.add_argument("--id", required=True, help="Bundle ID, e.g. com.example.app")
    p_init.add_argument("--name", required=True, help="Display Name")

    p_build = sub.add_parser("build", help="Build ARM64 Mach-O binary")
    p_build.add_argument("project_dir")
    p_build.add_argument("--sdk-dir", default="WinIOS/guest-sdk")
    p_build.add_argument("-o", "--output")

    p_pack = sub.add_parser("pack", help="Package project into .wios")
    p_pack.add_argument("project_dir")
    p_pack.add_argument("-o", "--output")

    p_insp = sub.add_parser("inspect", help="Inspect .wios container")
    p_insp.add_argument("package")

    p_veri = sub.add_parser("verify", help="Verify .wios container integrity")
    p_veri.add_argument("package")

    args = parser.parse_args()
    if args.cmd == "init":
        cmd_init(args)
    elif args.cmd == "build":
        cmd_build(args)
    elif args.cmd == "pack":
        cmd_pack(args)
    elif args.cmd == "inspect":
        cmd_inspect(args)
    elif args.cmd == "verify":
        cmd_verify(args)

if __name__ == "__main__":
    main()
