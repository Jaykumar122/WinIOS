"""Convert a Zig/Clang aarch64 Mach-O binary's LC_DYLD_INFO_ONLY rebase/bind streams
into Apple LC_DYLD_CHAINED_FIXUPS (0x80000034) using DYLD_CHAINED_PTR_64 (format=2).

Used ONLY as a dev-time artifact post-processor because Zig 0.17.0's Mach-O linker
emits LC_DYLD_INFO_ONLY opcodes and lacks -Wl,-fixup_chains. Preserves all
compiler-generated __TEXT instructions, __DATA/__DATA_CONST layout, and load
commands while zeroing LC_DYLD_INFO_ONLY rebase/bind/lazy fields so dynamic
linkers must execute the chained-fixups path.
"""

import argparse
import pathlib
import struct

LC_SEGMENT_64 = 0x19
LC_CODE_SIGNATURE = 0x1D
LC_DYLD_INFO = 0x22
LC_DYLD_INFO_ONLY = 0x80000022
LC_DYLD_CHAINED_FIXUPS = 0x80000034

DYLD_CHAINED_PTR_64 = 2
DYLD_CHAINED_PTR_START_NONE = 0xFFFF
PAGE_SIZE = 0x4000


def uleb128(buf: bytes, idx: int):
    res, shift = 0, 0
    while idx < len(buf):
        b = buf[idx]
        idx += 1
        res |= (b & 0x7F) << shift
        if (b & 0x80) == 0:
            break
        shift += 7
    return res, idx


def sleb128(buf: bytes, idx: int):
    res, shift = 0, 0
    b = 0
    while idx < len(buf):
        b = buf[idx]
        idx += 1
        res |= (b & 0x7F) << shift
        shift += 7
        if (b & 0x80) == 0:
            break
    if (shift < 64) and (b & 0x40):
        res -= (1 << shift)
    return res, idx


def decode_rebases(rb: bytes, segs):
    records = []  # (seg_idx, seg_off)
    i = 0
    seg_idx = 0
    seg_off = 0
    while i < len(rb):
        b = rb[i]
        op = b & 0xF0
        imm = b & 0x0F
        i += 1
        if op == 0x00:
            continue
        elif op == 0x10:
            pass
        elif op == 0x20:
            seg_idx = imm
            seg_off, i = uleb128(rb, i)
            seg_off &= 0xFFFFFFFFFFFFFFFF
        elif op == 0x30:
            d, i = uleb128(rb, i)
            seg_off = (seg_off + d) & 0xFFFFFFFFFFFFFFFF
        elif op == 0x40:
            seg_off = (seg_off + imm * 8) & 0xFFFFFFFFFFFFFFFF
        elif op == 0x50:
            for _ in range(imm):
                records.append((seg_idx, seg_off))
                seg_off = (seg_off + 8) & 0xFFFFFFFFFFFFFFFF
        elif op == 0x60:
            cnt, i = uleb128(rb, i)
            for _ in range(cnt):
                records.append((seg_idx, seg_off))
                seg_off = (seg_off + 8) & 0xFFFFFFFFFFFFFFFF
        elif op == 0x70:
            cnt, i = uleb128(rb, i)
            skip, i = uleb128(rb, i)
            for _ in range(cnt):
                records.append((seg_idx, seg_off))
                seg_off = (seg_off + 8 + skip) & 0xFFFFFFFFFFFFFFFF
        elif op == 0x80:
            cnt = imm
            skip, i = uleb128(rb, i)
            for _ in range(cnt):
                records.append((seg_idx, seg_off))
                seg_off = (seg_off + 8 + skip) & 0xFFFFFFFFFFFFFFFF
        else:
            raise ValueError(f"Unsupported rebase opcode 0x{b:02x}")
    return records


def decode_binds(bb: bytes):
    records = []  # (seg_idx, seg_off, lib_ord, weak, sym_name, addend)
    i = 0
    seg_idx = 0
    seg_off = 0
    lib_ord = 0
    weak = 0
    sym_name = ""
    addend = 0
    while i < len(bb):
        b = bb[i]
        op = b & 0xF0
        imm = b & 0x0F
        i += 1
        if op == 0x00:
            continue
        elif op == 0x10:
            lib_ord = imm
        elif op == 0x20:
            lib_ord, i = uleb128(bb, i)
        elif op == 0x30:
            lib_ord = imm - 16 if imm >= 13 else 0
        elif op == 0x40:
            weak = 1 if (imm & 1) else 0
            z = bb.index(0, i)
            sym_name = bb[i:z].decode("ascii")
            i = z + 1
        elif op == 0x50:
            pass
        elif op == 0x60:
            addend, i = sleb128(bb, i)
        elif op == 0x70:
            seg_idx = imm
            seg_off, i = uleb128(bb, i)
            seg_off &= 0xFFFFFFFFFFFFFFFF
        elif op == 0x80:
            d, i = uleb128(bb, i)
            seg_off = (seg_off + d) & 0xFFFFFFFFFFFFFFFF
        elif op == 0x90:
            records.append((seg_idx, seg_off, lib_ord, weak, sym_name, addend))
            seg_off = (seg_off + 8) & 0xFFFFFFFFFFFFFFFF
        elif op == 0xA0:
            records.append((seg_idx, seg_off, lib_ord, weak, sym_name, addend))
            d, i = uleb128(bb, i)
            seg_off = (seg_off + 8 + d) & 0xFFFFFFFFFFFFFFFF
        elif op == 0xB0:
            records.append((seg_idx, seg_off, lib_ord, weak, sym_name, addend))
            seg_off = (seg_off + 8 + imm * 8) & 0xFFFFFFFFFFFFFFFF
        elif op == 0xC0:
            cnt, i = uleb128(bb, i)
            skip, i = uleb128(bb, i)
            for _ in range(cnt):
                records.append((seg_idx, seg_off, lib_ord, weak, sym_name, addend))
                seg_off = (seg_off + 8 + skip) & 0xFFFFFFFFFFFFFFFF
        else:
            raise ValueError(f"Unsupported bind opcode 0x{b:02x}")
    return records


def convert(in_path: pathlib.Path, out_path: pathlib.Path) -> None:
    data = bytearray(in_path.read_bytes())
    magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = struct.unpack_from(
        "<IIIIIIII", data, 0
    )
    if magic != 0xFEEDFACF:
        raise ValueError("Not a 64-bit Mach-O binary")

    segs = []
    dyld_info_lc_off = None
    code_sig_lc_off = None
    linkedit_lc_off = None
    first_section_off = len(data)
    text_vmaddr = None

    off = 32
    for idx in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmd == LC_SEGMENT_64:
            name = data[off + 8 : off + 24].split(b"\x00", 1)[0].decode("ascii")
            vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects, sflags = struct.unpack_from(
                "<QQQQIIII", data, off + 24
            )
            seg_idx = len(segs)
            segs.append(
                {
                    "idx": seg_idx,
                    "lc_off": off,
                    "name": name,
                    "vmaddr": vmaddr,
                    "vmsize": vmsize,
                    "fileoff": fileoff,
                    "filesize": filesize,
                }
            )
            if name == "__TEXT":
                text_vmaddr = vmaddr
            if name == "__LINKEDIT":
                linkedit_lc_off = off
            sec_off = off + 72
            for _ in range(nsects):
                sec_fileoff = struct.unpack_from("<I", data, sec_off + 48)[0]
                if sec_fileoff > 0:
                    first_section_off = min(first_section_off, sec_fileoff)
                sec_off += 80
        elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
            dyld_info_lc_off = off
        elif cmd == LC_CODE_SIGNATURE:
            code_sig_lc_off = off
        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            raise ValueError("Binary already contains LC_DYLD_CHAINED_FIXUPS")
        off += cmdsize

    if dyld_info_lc_off is None or linkedit_lc_off is None or text_vmaddr is None:
        raise ValueError("Missing LC_DYLD_INFO_ONLY, __LINKEDIT, or __TEXT")

    roff, rsz, boff, bsz, woff, wsz, loff, lsz, eoff, esz = struct.unpack_from(
        "<10I", data, dyld_info_lc_off + 8
    )

    rebases = decode_rebases(bytes(data[roff : roff + rsz]), segs) if rsz > 0 else []
    binds = decode_binds(bytes(data[boff : boff + bsz])) if bsz > 0 else []
    lazy_binds = decode_binds(bytes(data[loff : loff + lsz])) if lsz > 0 else []
    all_binds = binds + lazy_binds

    if not rebases or not all_binds:
        raise ValueError(f"Expected NONZERO rebases and binds, got {len(rebases)} rebases, {len(all_binds)} binds")

    # Build deduplicated imports list + symbol pool
    imports = []
    import_map = {}
    sym_pool = bytearray(b"\x00")
    for (_, _, lib_ord, weak, sym_name, _) in all_binds:
        key = (lib_ord, weak, sym_name)
        if key not in import_map:
            name_off = len(sym_pool)
            sym_pool.extend(sym_name.encode("ascii") + b"\x00")
            import_map[key] = len(imports)
            imports.append((lib_ord, weak, name_off))

    # Group all fixups by seg_idx -> (seg_off, kind, payload)
    per_seg = {}
    for seg_idx, seg_off in rebases:
        foff = segs[seg_idx]["fileoff"] + seg_off
        target = struct.unpack_from("<Q", data, foff)[0]
        per_seg.setdefault(seg_idx, {})[seg_off] = ("rebase", target)

    for seg_idx, seg_off, lib_ord, weak, sym_name, addend in all_binds:
        ord_idx = import_map[(lib_ord, weak, sym_name)]
        per_seg.setdefault(seg_idx, {})[seg_off] = ("bind", (ord_idx, addend))

    # Encode DYLD_CHAINED_PTR_64 chains per page in each segment
    seg_blobs = {}
    for seg_idx, slot_map in sorted(per_seg.items()):
        seg = segs[seg_idx]
        # Round segment size to at least cover last page used
        max_off = max(slot_map.keys())
        page_count = (max_off // PAGE_SIZE) + 1
        by_page = {p: [] for p in range(page_count)}
        for seg_off in sorted(slot_map.keys()):
            by_page[seg_off // PAGE_SIZE].append(seg_off)

        page_starts = []
        for p in range(page_count):
            locs = by_page[p]
            if not locs:
                page_starts.append(DYLD_CHAINED_PTR_START_NONE)
                continue
            page_base = p * PAGE_SIZE
            page_starts.append(locs[0] - page_base)
            for idx_loc, cur_off in enumerate(locs):
                if idx_loc + 1 < len(locs):
                    delta = locs[idx_loc + 1] - cur_off
                    if delta % 4 != 0 or delta // 4 > 0xFFF:
                        raise ValueError(f"Chain stride invalid: delta={delta}")
                    next_stride = delta // 4
                else:
                    next_stride = 0
                kind, val = slot_map[cur_off]
                if kind == "rebase":
                    raw = ((next_stride & 0xFFF) << 51) | (val & 0xFFFFFFFFF)
                else:
                    ord_idx, addend = val
                    raw = (
                        (1 << 63)
                        | ((next_stride & 0xFFF) << 51)
                        | ((addend & 0xFF) << 24)
                        | (ord_idx & 0xFFFFFF)
                    )
                foff = seg["fileoff"] + cur_off
                struct.pack_into("<Q", data, foff, raw)

        raw_hdr_size = 22 + page_count * 2
        padded_size = (raw_hdr_size + 3) & ~3
        seg_blob = bytearray(padded_size)
        seg_offset_from_image = seg["vmaddr"] - text_vmaddr
        struct.pack_into(
            "<IHHQIH",
            seg_blob,
            0,
            padded_size,
            PAGE_SIZE,
            DYLD_CHAINED_PTR_64,
            seg_offset_from_image,
            0,
            page_count,
        )
        for p, ps in enumerate(page_starts):
            struct.pack_into("<H", seg_blob, 22 + p * 2, ps)
        seg_blobs[seg_idx] = bytes(seg_blob)

    # Assemble dyld_chained_fixups_header + starts_in_image + seg blobs + imports + symbols
    seg_count = len(segs)
    starts_in_image_hdr_size = 4 + 4 * seg_count
    starts_buf = bytearray(starts_in_image_hdr_size)
    struct.pack_into("<I", starts_buf, 0, seg_count)
    for s_idx in range(seg_count):
        if s_idx in seg_blobs:
            struct.pack_into("<I", starts_buf, 4 + 4 * s_idx, len(starts_buf))
            starts_buf.extend(seg_blobs[s_idx])
        else:
            struct.pack_into("<I", starts_buf, 4 + 4 * s_idx, 0)

    while len(starts_buf) % 4 != 0:
        starts_buf.append(0)

    starts_offset = 28
    imports_offset = starts_offset + len(starts_buf)
    imports_buf = bytearray()
    for lib_ord, weak, name_off in imports:
        word = (lib_ord & 0xFF) | ((weak & 1) << 8) | ((name_off & 0x7FFFFF) << 9)
        imports_buf.extend(struct.pack("<I", word))

    symbols_offset = imports_offset + len(imports_buf)
    payload = bytearray(28)
    struct.pack_into(
        "<7I",
        payload,
        0,
        0,  # fixups_version
        starts_offset,
        imports_offset,
        symbols_offset,
        len(imports),
        1,  # DYLD_CHAINED_IMPORT
        0,  # uncompressed symbols
    )
    payload.extend(starts_buf)
    payload.extend(imports_buf)
    payload.extend(sym_pool)
    while len(payload) % 8 != 0:
        payload.append(0)

    # Append payload into __LINKEDIT (aligned to 8 bytes)
    while len(data) % 8 != 0:
        data.append(0)
    fixups_fileoff = len(data)
    fixups_size = len(payload)
    data.extend(payload)

    # Update __LINKEDIT filesize & vmsize
    le_vmaddr, le_vmsize, le_fileoff, le_filesize = struct.unpack_from(
        "<QQQQ", data, linkedit_lc_off + 24
    )
    new_le_filesize = len(data) - le_fileoff
    new_le_vmsize = max(le_vmsize, ((new_le_filesize + PAGE_SIZE - 1) // PAGE_SIZE) * PAGE_SIZE)
    struct.pack_into("<Q", data, linkedit_lc_off + 32, new_le_vmsize)
    struct.pack_into("<Q", data, linkedit_lc_off + 48, new_le_filesize)

    # Zero LC_DYLD_INFO_ONLY rebase/bind/lazy fields (keep exports only)
    struct.pack_into("<8I", data, dyld_info_lc_off + 8, 0, 0, 0, 0, 0, 0, 0, 0)

    # Clear LC_CODE_SIGNATURE if present since __LINKEDIT grew
    if code_sig_lc_off is not None:
        struct.pack_into("<II", data, code_sig_lc_off + 8, 0, 0)

    # Append LC_DYLD_CHAINED_FIXUPS in header padding
    new_cmd_off = 32 + sizeofcmds
    if new_cmd_off + 16 > first_section_off:
        raise ValueError(f"Insufficient header padding: {new_cmd_off + 16} > {first_section_off}")
    struct.pack_into("<IIII", data, new_cmd_off, LC_DYLD_CHAINED_FIXUPS, 16, fixups_fileoff, fixups_size)
    struct.pack_into("<II", data, 16, ncmds + 1, sizeofcmds + 16)

    out_path.write_bytes(data)
    print(
        f"Converted {in_path.name} -> {out_path.name}: {len(rebases)} rebases, "
        f"{len(all_binds)} binds ({len(imports)} imports), blob={fixups_size}B @ 0x{fixups_fileoff:x}"
    )


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    args = ap.parse_args()
    convert(pathlib.Path(args.input), pathlib.Path(args.output))
