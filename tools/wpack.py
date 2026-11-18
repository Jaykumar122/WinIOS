#!/usr/bin/env python3
"""
WinIOS Package & Build Tool (wpack)
Campaign 217 Developer Tooling
"""

import sys
import os
import argparse
import hashlib
import struct
import subprocess
from pathlib import Path

def compute_sha256(filepath):
    h = hashlib.sha256()
    with open(filepath, 'rb') as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def validate_macho(binary_path):
    if not os.path.isfile(binary_path):
        print(f"[ERROR] Binary not found: {binary_path}", file=sys.stderr)
        return False
    with open(binary_path, 'rb') as f:
        header = f.read(32)
        if len(header) < 32:
            print("[ERROR] Binary too small for Mach-O header", file=sys.stderr)
            return False
        magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = struct.unpack('<IIIIIIII', header)
        if magic != 0xfeedfacf:
            print(f"[ERROR] Invalid magic 0x{magic:08x} (expected 0xfeedfacf for 64-bit Mach-O)", file=sys.stderr)
            return False
        if cputype != 0x0100000c:
            print(f"[ERROR] Invalid CPU type 0x{cputype:08x} (expected 0x0100000c for ARM64)", file=sys.stderr)
            return False
        print(f"[VALID] 64-bit ARM64 Mach-O: ncmds={ncmds}, sizeofcmds={sizeofcmds}")
        return True

def compile_arm64(src_path, out_bin, entry="__start"):
    print(f"[*] Compiling {src_path} -> {out_bin} using Zig (entry={entry})...")
    cmd = [
        "zig", "cc",
        "-target", "aarch64-macos",
        "-nostdlib",
        "-fno-sanitize=undefined",
        "-O2",
        "-fno-vectorize",
        "-fno-slp-vectorize",
        f"-Wl,-e,{entry}",
        src_path,
        "-o", out_bin
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[ERROR] Compilation failed:\n{res.stderr}", file=sys.stderr)
        return False
    print(f"[SUCCESS] Compiled ARM64 binary: {out_bin} ({os.path.getsize(out_bin)} bytes)")
    return True

def pack_app(app_dir, out_wios, bundle_id, app_name, version="1.2.0"):
    print(f"[*] Packaging {app_dir} -> {out_wios}...")
    manifest_lines = [
        f"bundle_id={bundle_id}",
        f"name={app_name}",
        f"version={version}",
        "architecture=arm64",
        "min_os_version=218.0",
        "executable_name=bin/app"
    ]
    files = []
    seen_paths = set()
    for root, _, filenames in os.walk(app_dir):
        for fn in filenames:
            full_path = os.path.join(root, fn)
            rel_path = os.path.relpath(full_path, app_dir).replace("\\", "/")
            if ".." in rel_path or rel_path.startswith("/"):
                print(f"[ERROR] Path traversal attempted in package: {rel_path}", file=sys.stderr)
                return False
            if rel_path in seen_paths:
                print(f"[ERROR] Duplicate entry in package: {rel_path}", file=sys.stderr)
                return False
            seen_paths.add(rel_path)

            sz = os.path.getsize(full_path)
            if sz > 32 * 1024 * 1024:
                print(f"[ERROR] File size exceeds 32MB limit: {rel_path} ({sz} bytes)", file=sys.stderr)
                return False

            h = compute_sha256(full_path)
            files.append((rel_path, sz, h, full_path))

    with open(out_wios, 'wb') as out_f:
        out_f.write(b"WIOSPKG\0")
        manifest_text = "\n".join(manifest_lines) + "\n"
        manifest_bytes = manifest_text.encode('utf-8')
        out_f.write(struct.pack('<I', len(manifest_bytes)))
        out_f.write(manifest_bytes)
        out_f.write(struct.pack('<I', len(files)))
        for rel_path, sz, h, full_path in files:
            path_bytes = rel_path.encode('utf-8')
            out_f.write(struct.pack('<I', len(path_bytes)))
            out_f.write(path_bytes)
            out_f.write(struct.pack('<Q', sz))
            h_bytes = h.encode('utf-8')
            out_f.write(struct.pack('<I', len(h_bytes)))
            out_f.write(h_bytes)
            with open(full_path, 'rb') as in_f:
                out_f.write(in_f.read())

    pkg_hash = compute_sha256(out_wios)
    print(f"[SUCCESS] Created package: {out_wios} ({os.path.getsize(out_wios)} bytes, SHA-256: {pkg_hash})")
    return True

def verify_package(wios_path):
    print(f"[*] Verifying package {wios_path}...")
    if not os.path.isfile(wios_path):
        print(f"[ERROR] Package not found: {wios_path}", file=sys.stderr)
        return False
    with open(wios_path, 'rb') as f:
        magic = f.read(8)
        if magic != b"WIOSPKG\0":
            print(f"[ERROR] Invalid package magic: {magic}", file=sys.stderr)
            return False
        m_len = struct.unpack('<I', f.read(4))[0]
        manifest = f.read(m_len).decode('utf-8')
        num_files = struct.unpack('<I', f.read(4))[0]
        seen_files = set()
        for _ in range(num_files):
            p_len = struct.unpack('<I', f.read(4))[0]
            rel_p = f.read(p_len).decode('utf-8')
            if ".." in rel_p or rel_p.startswith("/"):
                print(f"[ERROR] Dangerous path in package: {rel_p}", file=sys.stderr)
                return False
            if rel_p in seen_files:
                print(f"[ERROR] Duplicate path in package: {rel_p}", file=sys.stderr)
                return False
            seen_files.add(rel_p)
            sz = struct.unpack('<Q', f.read(8))[0]
            h_len = struct.unpack('<I', f.read(4))[0]
            _ = f.read(h_len)
            f.seek(sz, os.SEEK_CUR)
        print(f"[VALID] Package verified: files={num_files}, manifest_len={m_len}")
    return True

def main():
    parser = argparse.ArgumentParser(description="WinIOS Developer Packaging Tool (wpack)")
    sub = parser.add_subparsers(dest="cmd")

    compile_p = sub.add_parser("compile", help="Compile C source to ARM64 Mach-O")
    compile_p.add_argument("src", help="Source file")
    compile_p.add_argument("-o", "--out", required=True, help="Output binary")
    compile_p.add_argument("--entry", default="__start", help="Entry point symbol (default: __start)")

    validate_p = sub.add_parser("validate", help="Validate Mach-O binary")
    validate_p.add_argument("bin", help="Binary path")

    pack_p = sub.add_parser("pack", help="Build .wios package")
    pack_p.add_argument("dir", help="App root directory")
    pack_p.add_argument("-o", "--out", required=True, help="Output .wios package")
    pack_p.add_argument("--id", required=True, help="Bundle ID")
    pack_p.add_argument("--name", required=True, help="Application Name")

    verify_p = sub.add_parser("verify", help="Verify .wios package")
    verify_p.add_argument("pkg", help="Package file")

    args = parser.parse_args()
    if args.cmd == "compile":
        ok = compile_arm64(args.src, args.out, entry=args.entry)
        sys.exit(0 if ok else 1)
    elif args.cmd == "validate":
        ok = validate_macho(args.bin)
        sys.exit(0 if ok else 1)
    elif args.cmd == "pack":
        ok = pack_app(args.dir, args.out, args.id, args.name)
        sys.exit(0 if ok else 1)
    elif args.cmd == "verify":
        ok = verify_package(args.pkg)
        sys.exit(0 if ok else 1)
    else:
        parser.print_help()
        sys.exit(1)

if __name__ == "__main__":
    main()
