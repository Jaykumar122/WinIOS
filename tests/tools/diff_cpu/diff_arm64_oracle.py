#!/usr/bin/env python3
"""
Campaign 191-V Stage V2: Differential ARM64 CPU Verifier
Compares WinIOS CPU (16KB Stage-1 Normal-Memory MMU EL0) step-for-step
against Unicorn Engine 2.1.4 (QEMU TCG AArch64 external oracle).
"""

import argparse
import json
import os
import random
import struct
import subprocess
import sys
from typing import Dict, List, Optional, Tuple

from unicorn import Uc, UcError, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import (
    UC_ARM64_REG_CPACR_EL1,
    UC_ARM64_REG_NZCV,
    UC_ARM64_REG_PC,
    UC_ARM64_REG_Q0,
    UC_ARM64_REG_SP,
    UC_ARM64_REG_X0,
    UC_ARM64_REG_X29,
    UC_ARM64_REG_X30,
)

UC_X_REGS = [UC_ARM64_REG_X0 + i for i in range(29)] + [
    UC_ARM64_REG_X29,
    UC_ARM64_REG_X30,
]
UC_Q_REGS = [UC_ARM64_REG_Q0 + i for i in range(32)]

CODE_BASE = 0x10000
DATA_BASE = 0x40000
DATA_COMPARE_LEN = 0x8000  # 32 KB window spanning 16 KB page boundary at 0x44000!


def pack_case(
    steps: int,
    pc: int,
    sp: int,
    nzcv: int,
    x_regs: List[int],
    q_regs: List[Tuple[int, int]],
    code: bytes,
    data_addr: int,
    data_bytes: bytes,
) -> str:
    hdr = f"CASE {steps} {pc:x} {sp:x} {nzcv:x} {data_addr:x} {len(data_bytes)}\n"
    x_str = "X " + " ".join(f"{v:x}" for v in x_regs) + "\n"
    q_str = "Q " + " ".join(f"{lo:x}:{hi:x}" for (lo, hi) in q_regs) + "\n"
    c_str = "CODE " + (code.hex() if code else "-") + "\n"
    d_str = "DATA " + (data_bytes.hex() if data_bytes else "-") + "\n"
    return hdr + x_str + q_str + c_str + d_str


def run_unicorn_case(
    steps: int,
    pc: int,
    sp: int,
    nzcv: int,
    x_regs: List[int],
    q_regs: List[Tuple[int, int]],
    code: bytes,
    data_addr: int,
    data_bytes: bytes,
) -> Dict:
    mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    mu.mem_map(0x00000, 0x20000, 5)  # RX code
    mu.mem_map(DATA_BASE, 0x20000, 3)  # RW data
    mu.reg_write(UC_ARM64_REG_CPACR_EL1, 0x00300000)

    mu.mem_write(pc, code)
    if data_bytes:
        mu.mem_write(data_addr, data_bytes)

    for i, val in enumerate(x_regs):
        mu.reg_write(UC_X_REGS[i], val)
    for i, (lo, hi) in enumerate(q_regs):
        mu.reg_write(UC_Q_REGS[i], ((hi & 0xFFFFFFFFFFFFFFFF) << 64) | (lo & 0xFFFFFFFFFFFFFFFF))
    mu.reg_write(UC_ARM64_REG_SP, sp)
    mu.reg_write(UC_ARM64_REG_NZCV, (nzcv & 0xF) << 28)
    mu.reg_write(UC_ARM64_REG_PC, pc)

    status = "OK"
    try:
        mu.emu_start(pc, 0x1FFFF, count=steps)
    except UcError as exc:
        status = f"FAULT:{exc.errno}"

    out_x = [mu.reg_read(r) for r in UC_X_REGS]
    out_q = []
    for r in UC_Q_REGS:
        qv = mu.reg_read(r)
        out_q.append((qv & 0xFFFFFFFFFFFFFFFF, (qv >> 64) & 0xFFFFFFFFFFFFFFFF))
    out_data = bytes(mu.mem_read(data_addr, len(data_bytes))) if data_bytes else b""
    return {
        "status": status,
        "pc": mu.reg_read(UC_ARM64_REG_PC),
        "sp": mu.reg_read(UC_ARM64_REG_SP),
        "nzcv": (mu.reg_read(UC_ARM64_REG_NZCV) >> 28) & 0xF,
        "x": out_x,
        "q": out_q,
        "data": out_data,
    }


def parse_winios_response(proc: subprocess.Popen) -> Dict:
    rline = proc.stdout.readline().strip().split()
    xline = proc.stdout.readline().strip().split()
    qline = proc.stdout.readline().strip().split()
    dline = proc.stdout.readline().strip().split()
    if not rline or rline[0] != "RESULT":
        raise RuntimeError(f"Bad runner output: {rline}")
    status = rline[1]
    pc = int(rline[3], 16)
    sp = int(rline[4], 16)
    nzcv = int(rline[5], 16)
    esr = int(rline[6], 16)
    out_x = [int(tok, 16) for tok in xline[1:]]
    out_q = []
    for tok in qline[1:]:
        lo_s, hi_s = tok.split(":")
        out_q.append((int(lo_s, 16), int(hi_s, 16)))
    out_data = b"" if dline[1] == "-" else bytes.fromhex(dline[1])
    return {
        "status": status,
        "pc": pc,
        "sp": sp,
        "nzcv": nzcv,
        "esr": esr,
        "x": out_x,
        "q": out_q,
        "data": out_data,
    }


# --- Instruction Encoding Builders -----------------------------------------

def u32_bytes(insns: List[int]) -> bytes:
    return b"".join(struct.pack("<I", w & 0xFFFFFFFF) for w in insns)


def enc_add_sub_imm(sf: int, op: int, s: int, sh: int, imm12: int, rn: int, rd: int) -> int:
    return (sf << 31) | (op << 30) | (s << 29) | (0b100010 << 23) | (sh << 22) | ((imm12 & 0xFFF) << 10) | ((rn & 31) << 5) | (rd & 31)


def enc_add_sub_shifted(sf: int, op: int, s: int, shift: int, rm: int, imm6: int, rn: int, rd: int) -> int:
    return (sf << 31) | (op << 30) | (s << 29) | (0b01011 << 24) | (shift << 22) | (rm << 16) | ((imm6 & 0x3F) << 10) | (rn << 5) | rd


def enc_add_sub_ext(sf: int, op: int, s: int, rm: int, option: int, imm3: int, rn: int, rd: int) -> int:
    return (sf << 31) | (op << 30) | (s << 29) | (0b01011001 << 21) | (rm << 16) | (option << 13) | ((imm3 & 7) << 10) | (rn << 5) | rd


def enc_adc_sbc(sf: int, op: int, s: int, rm: int, rn: int, rd: int) -> int:
    return (sf << 31) | (op << 30) | (s << 29) | (0b11010000 << 21) | (rm << 16) | (0b000000 << 10) | (rn << 5) | rd


def enc_logical_shifted(sf: int, opc: int, shift: int, N: int, rm: int, imm6: int, rn: int, rd: int) -> int:
    return (sf << 31) | (opc << 29) | (0b01010 << 24) | (shift << 22) | (N << 21) | (rm << 16) | ((imm6 & 0x3F) << 10) | (rn << 5) | rd


def enc_mov_wide(sf: int, opc: int, hw: int, imm16: int, rd: int) -> int:
    return (sf << 31) | (opc << 29) | (0b100101 << 23) | (hw << 21) | ((imm16 & 0xFFFF) << 5) | rd


def enc_bitfield(sf: int, opc: int, n: int, immr: int, imms: int, rn: int, rd: int) -> int:
    return (sf << 31) | (opc << 29) | (0b100110 << 23) | (n << 22) | ((immr & 0x3F) << 16) | ((imms & 0x3F) << 10) | (rn << 5) | rd


def enc_extr(sf: int, n: int, rm: int, imms: int, rn: int, rd: int) -> int:
    return (sf << 31) | (0b00100111 << 23) | (n << 22) | (rm << 16) | ((imms & 0x3F) << 10) | (rn << 5) | rd


def enc_dp2(sf: int, rm: int, opcode: int, rn: int, rd: int) -> int:
    return (sf << 31) | (0b0011010110 << 21) | (rm << 16) | (opcode << 10) | (rn << 5) | rd


def enc_dp1(sf: int, opcode: int, rn: int, rd: int) -> int:
    return (sf << 31) | (0b1011010110 << 21) | (0b00000 << 16) | (opcode << 10) | (rn << 5) | rd


def enc_dp3(sf: int, op31: int, rm: int, o0: int, ra: int, rn: int, rd: int) -> int:
    return (sf << 31) | (0b0011011 << 24) | (op31 << 21) | (rm << 16) | (o0 << 15) | (ra << 10) | (rn << 5) | rd


def enc_csel(sf: int, op: int, rm: int, cond: int, op2: int, rn: int, rd: int) -> int:
    return (sf << 31) | (op << 30) | (0b011010100 << 21) | (rm << 16) | (cond << 12) | (op2 << 10) | (rn << 5) | rd


def enc_ccmp_reg(sf: int, op: int, rm: int, cond: int, rn: int, nzcv: int) -> int:
    return (sf << 31) | (op << 30) | (0b111010010 << 21) | (rm << 16) | (cond << 12) | (0b00 << 10) | (rn << 5) | nzcv


def enc_ldr_str_uimm(size: int, v: int, opc: int, imm12: int, rn: int, rt: int) -> int:
    return (size << 30) | (0b111 << 27) | (v << 26) | (0b01 << 24) | (opc << 22) | ((imm12 & 0xFFF) << 10) | (rn << 5) | rt


def enc_ldur_stur(size: int, v: int, opc: int, simm9: int, mode: int, rn: int, rt: int) -> int:
    return (size << 30) | (0b111 << 27) | (v << 26) | (0b00 << 24) | (opc << 22) | ((simm9 & 0x1FF) << 12) | (mode << 10) | (rn << 5) | rt


def enc_ldr_str_reg(size: int, opc: int, rm: int, option: int, s_bit: int, rn: int, rt: int) -> int:
    return (size << 30) | (0b111000 << 24) | (opc << 22) | (1 << 21) | (rm << 16) | (option << 13) | (s_bit << 12) | (0b10 << 10) | (rn << 5) | rt


def enc_ldp_stp(opc: int, v: int, mode: int, l_bit: int, simm7: int, rt2: int, rn: int, rt: int) -> int:
    return (opc << 30) | (0b101 << 27) | (v << 26) | (mode << 23) | (l_bit << 22) | ((simm7 & 0x7F) << 15) | (rt2 << 10) | (rn << 5) | rt


def generate_cases(seed: int) -> List[Dict]:
    rng = random.Random(seed)
    cases: List[Dict] = []

    edge_vals = [
        0,
        1,
        2,
        0x7FFFFFFF,
        0x80000000,
        0xFFFFFFFF,
        0x100000000,
        0x7FFFFFFFFFFFFFFF,
        0x8000000000000000,
        0xFFFFFFFFFFFFFFFF,
        0xDEADBEEFCAFEBABE,
        0x0123456789ABCDEF,
    ]

    def rand_u64() -> int:
        if rng.random() < 0.35:
            return rng.choice(edge_vals)
        return rng.getrandbits(64)

    def base_regs() -> Tuple[List[int], List[Tuple[int, int]]]:
        xs = [rand_u64() for _ in range(31)]
        qs = [(rng.getrandbits(64), rng.getrandbits(64)) for _ in range(32)]
        return xs, qs

    # Category 1: Integer arithmetic + flags + mul/div (300 cases)
    for i in range(300):
        xs, qs = base_regs()
        sf = rng.choice([0, 1])
        rd = rng.choice([0, 1, 2, 3, 4, 31])
        rn = rng.randint(0, 5)
        rm = rng.randint(0, 5)
        nzcv = rng.randint(0, 15)
        kind = i % 6
        if kind == 0:
            op = rng.choice([0, 1])
            s = rng.choice([0, 1])
            sh = rng.choice([0, 1])
            imm12 = rng.randint(0, 0xFFF)
            if rd == 31 and s == 0:
                rd = 2  # keep sp test separate
            if rn == 31:
                rn = 1
            insn = enc_add_sub_imm(sf, op, s, sh, imm12, rn, rd)
            name = f"add_sub_imm_sf{sf}_op{op}_s{s}"
        elif kind == 1:
            op = rng.choice([0, 1])
            s = rng.choice([0, 1])
            shift = rng.choice([0, 1, 2])
            imm6 = rng.randint(0, 63 if sf else 31)
            insn = enc_add_sub_shifted(sf, op, s, shift, rm, imm6, rn, rd)
            name = f"add_sub_shift_sf{sf}_op{op}_s{s}"
        elif kind == 2:
            op = rng.choice([0, 1])
            s = rng.choice([0, 1])
            opt = rng.randint(0, 7)
            imm3 = rng.randint(0, 4)
            insn = enc_add_sub_ext(sf, op, s, rm, opt, imm3, rn, rd)
            name = f"add_sub_ext_sf{sf}_opt{opt}"
        elif kind == 3:
            op = rng.choice([0, 1])
            s = rng.choice([0, 1])
            insn = enc_adc_sbc(sf, op, s, rm, rn, rd)
            name = f"adc_sbc_sf{sf}_op{op}_s{s}"
        elif kind == 4:
            # UDIV / SDIV
            opcode = rng.choice([0b000010, 0b000011])
            insn = enc_dp2(sf, rm, opcode, rn, rd)
            name = f"div_sf{sf}_opc{opcode}"
        else:
            # MADD / MSUB / SMADDL / UMADDL
            if sf == 1 and rng.random() < 0.4:
                op31 = rng.choice([0b001, 0b101])  # SMADDL / UMADDL
            else:
                op31 = 0b000
            o0 = rng.choice([0, 1])
            ra = rng.choice([0, 1, 2, 31])
            insn = enc_dp3(sf, op31, rm, o0, ra, rn, rd)
            name = f"dp3_sf{sf}_op31_{op31}_o0_{o0}"
        cases.append({
            "cat": "int_alu",
            "name": f"{name}_{i}",
            "steps": 1,
            "pc": CODE_BASE,
            "sp": DATA_BASE + 0x400,
            "nzcv": nzcv,
            "x": xs,
            "q": qs,
            "code": u32_bytes([insn]),
            "data_addr": DATA_BASE,
            "data": b"",
        })

    # Category 2: Logical, shifts, bitfield, MOVZ/N/K, RBIT/REV/CLZ (250 cases)
    for i in range(250):
        xs, qs = base_regs()
        sf = rng.choice([0, 1])
        rd = rng.choice([0, 1, 2, 3, 31])
        rn = rng.randint(0, 5)
        rm = rng.randint(0, 5)
        nzcv = rng.randint(0, 15)
        kind = i % 6
        if kind == 0:
            opc = rng.randint(0, 3)
            shift = rng.randint(0, 3)
            N = rng.choice([0, 1])
            imm6 = rng.randint(0, 63 if sf else 31)
            insn = enc_logical_shifted(sf, opc, shift, N, rm, imm6, rn, rd)
            name = f"log_shift_sf{sf}_opc{opc}"
        elif kind == 1:
            opc = rng.choice([0, 2, 3])  # MOVN, MOVZ, MOVK
            hw = rng.randint(0, 3 if sf else 1)
            imm16 = rng.randint(0, 0xFFFF)
            insn = enc_mov_wide(sf, opc, hw, imm16, rd)
            name = f"movwide_sf{sf}_opc{opc}"
        elif kind == 2:
            opc = rng.choice([0, 1, 2])  # SBFM, BFM, UBFM
            lim = 63 if sf else 31
            immr = rng.randint(0, lim)
            imms = rng.randint(0, lim)
            insn = enc_bitfield(sf, opc, sf, immr, imms, rn, rd)
            name = f"bf_sf{sf}_opc{opc}"
        elif kind == 3:
            imms = rng.randint(0, 63 if sf else 31)
            insn = enc_extr(sf, sf, rm, imms, rn, rd)
            name = f"extr_sf{sf}"
        elif kind == 4:
            # LSLV / LSRV / ASRV / RORV
            opcode = rng.choice([0b001000, 0b001001, 0b001010, 0b001011])
            insn = enc_dp2(sf, rm, opcode, rn, rd)
            name = f"varshift_sf{sf}_opc{opcode}"
        else:
            # RBIT, REV16, REV32/REV64, CLZ, CLS
            valid_dp1 = [0b000000, 0b000001, 0b000100, 0b000101]
            if sf:
                valid_dp1 += [0b000010, 0b000011]
            else:
                valid_dp1 += [0b000010]
            opcode = rng.choice(valid_dp1)
            insn = enc_dp1(sf, opcode, rn, rd)
            name = f"dp1_sf{sf}_opc{opcode}"
        cases.append({
            "cat": "logic_bit",
            "name": f"{name}_{i}",
            "steps": 1,
            "pc": CODE_BASE,
            "sp": DATA_BASE + 0x400,
            "nzcv": nzcv,
            "x": xs,
            "q": qs,
            "code": u32_bytes([insn]),
            "data_addr": DATA_BASE,
            "data": b"",
        })

    # Category 3: Conditional CSEL/CSINC/CSINV/CSNEG & CCMP/CCMN (150 cases)
    for i in range(150):
        xs, qs = base_regs()
        sf = rng.choice([0, 1])
        rd = rng.choice([0, 1, 2, 31])
        rn = rng.randint(0, 5)
        rm = rng.randint(0, 5)
        cond = i % 16
        nzcv = rng.randint(0, 15)
        if i % 3 == 2:
            op = rng.choice([0, 1])  # CCMN / CCMP
            insn = enc_ccmp_reg(sf, op, rm, cond, rn, rng.randint(0, 15))
            name = f"ccmp_sf{sf}_op{op}_c{cond}"
        else:
            op = rng.choice([0, 1])
            op2 = rng.choice([0, 1])
            insn = enc_csel(sf, op, rm, cond, op2, rn, rd)
            name = f"csel_sf{sf}_op{op}_op2_{op2}_c{cond}"
        cases.append({
            "cat": "cond",
            "name": f"{name}_{i}",
            "steps": 1,
            "pc": CODE_BASE,
            "sp": DATA_BASE + 0x400,
            "nzcv": nzcv,
            "x": xs,
            "q": qs,
            "code": u32_bytes([insn]),
            "data_addr": DATA_BASE,
            "data": b"",
        })

    # Category 4: Branches & PC-relative (ADR/ADRP/B/BL/B.cond/CBZ/CBNZ/TBZ/TBNZ/BR/BLR/RET) (200 cases)
    for i in range(200):
        xs, qs = base_regs()
        nzcv = rng.randint(0, 15)
        pc = CODE_BASE
        steps = 2
        landing_fall = enc_mov_wide(1, 2, 0, 0x1111, 10)  # MOVZ X10, #0x1111
        landing_tgt = enc_mov_wide(1, 2, 0, 0x2222, 10)   # MOVZ X10, #0x2222
        kind = i % 7
        if kind == 0:
            # ADR / ADRP (step=1)
            op = rng.choice([0, 1])
            immhi = rng.randint(0, 0x7FFFF)
            immlo = rng.randint(0, 3)
            rd = rng.randint(0, 5)
            insn = (op << 31) | (immlo << 29) | (0b10000 << 24) | (immhi << 5) | rd
            code = u32_bytes([insn])
            steps = 1
            name = f"adr_op{op}"
        elif kind == 1:
            # B / BL (+8 bytes -> insns[2])
            op = rng.choice([0, 1])
            insn = (op << 31) | (0b00101 << 26) | 2
            code = u32_bytes([insn, landing_fall, landing_tgt])
            name = f"b_op{op}"
        elif kind == 2:
            # B.cond (exclude reserved 0b1111 NV which WinIOS deliberately traps per branch_trace_tests.cpp:479)
            cond = i % 15
            insn = (0b01010100 << 24) | (2 << 5) | cond
            code = u32_bytes([insn, landing_fall, landing_tgt])
            name = f"bcond_{cond}"
        elif kind == 3:
            # CBZ / CBNZ
            sf = rng.choice([0, 1])
            op = rng.choice([0, 1])
            rt = 0
            if rng.random() < 0.5:
                xs[0] = 0 if sf else (xs[0] & 0xFFFFFFFF00000000)
            insn = (sf << 31) | (0b011010 << 25) | (op << 24) | (2 << 5) | rt
            code = u32_bytes([insn, landing_fall, landing_tgt])
            name = f"cbz_sf{sf}_op{op}"
        elif kind == 4:
            # TBZ / TBNZ
            bit = rng.randint(0, 63)
            b5 = (bit >> 5) & 1
            b40 = bit & 31
            op = rng.choice([0, 1])
            insn = (b5 << 31) | (0b011011 << 25) | (op << 24) | (b40 << 19) | (2 << 5) | 0
            code = u32_bytes([insn, landing_fall, landing_tgt])
            name = f"tbz_b{bit}_op{op}"
        elif kind == 5:
            # BR / BLR
            op = rng.choice([0b00, 0b01])
            xs[6] = pc + 8
            insn = (0b1101011 << 25) | (op << 21) | (0b11111 << 16) | (6 << 5)
            code = u32_bytes([insn, landing_fall, landing_tgt])
            name = f"br_op{op}"
        else:
            # RET (default X30)
            xs[30] = pc + 8
            insn = 0xD65F03C0
            code = u32_bytes([insn, landing_fall, landing_tgt])
            name = "ret"
        cases.append({
            "cat": "branch",
            "name": f"{name}_{i}",
            "steps": steps,
            "pc": pc,
            "sp": DATA_BASE + 0x400,
            "nzcv": nzcv,
            "x": xs,
            "q": qs,
            "code": code,
            "data_addr": DATA_BASE,
            "data": b"",
        })

    # Category 5: Loads & Stores including UNALIGNED and 16KB PAGE CROSSING (250 cases)
    fixed_pattern = bytes(((j * 73 + 19) & 0xFF) for j in range(DATA_COMPARE_LEN))
    for i in range(250):
        xs, qs = base_regs()
        nzcv = rng.randint(0, 15)
        # Alternate intra-page offsets and cross-16KB-page-boundary offsets (0x44000)!
        if i % 3 == 0:
            base = DATA_BASE + 0x3FF0 + rng.randint(0, 15)  # spans 0x43FF0..0x4400F!
        else:
            base = DATA_BASE + 0x200 + rng.randint(0, 31)
        rn = 1 if (i % 7 != 0) else 31  # include SP-based accesses!
        sp_val = base if rn == 31 else (DATA_BASE + 0x400)
        if rn != 31:
            xs[rn] = base
        rt = rng.choice([0, 2, 3, 31])
        rt2 = 4 if rt != 4 else 5
        kind = i % 6
        if kind == 0:
            # Unsigned imm scalar LDR/STR B/H/W/X
            size = rng.randint(0, 3)
            opc = rng.choice([0, 1])  # STR / LDR
            imm12 = rng.randint(0, 4)
            insn = enc_ldr_str_uimm(size, 0, opc, imm12, rn, rt)
            name = f"ldst_uimm_sz{size}_opc{opc}"
        elif kind == 1:
            # LDUR / STUR / Pre-index / Post-index (32/64 + signed loads)
            size = rng.randint(0, 3)
            opc = rng.choice([0, 1, 2, 3]) if size < 3 else rng.choice([0, 1])
            mode = rng.choice([0, 1, 3])
            if opc >= 2 and size == 2 and mode != 0:
                opc = 2  # LDRSW signed
            simm9 = rng.randint(-32, 32)
            # avoid writeback when rn==rt on load (architecturally UNPREDICTABLE!)
            if mode != 0 and rt == rn:
                rt = 7
            insn = enc_ldur_stur(size, 0, opc, simm9, mode, rn, rt)
            name = f"ldstur_sz{size}_opc{opc}_m{mode}"
        elif kind == 2:
            # Register-offset LDR/STR
            size = rng.randint(0, 3)
            opc = rng.choice([0, 1])
            rm = 5
            xs[rm] = rng.randint(0, 16)
            opt = rng.choice([0b010, 0b011, 0b110, 0b111])
            s_bit = rng.choice([0, 1]) if size > 0 else 0
            insn = enc_ldr_str_reg(size, opc, rm, opt, s_bit, rn, rt)
            name = f"ldst_reg_sz{size}_opc{opc}"
        elif kind == 3:
            # LDP / STP (32/64 + LDPSW)
            opc = rng.choice([0b00, 0b10])
            l_bit = rng.choice([0, 1])
            if l_bit and rng.random() < 0.25:
                opc = 0b01  # LDPSW
            mode = rng.choice([1, 2, 3])
            simm7 = rng.randint(-4, 4)
            if l_bit and (rt == rt2 or (mode != 2 and (rt == rn or rt2 == rn))):
                rt, rt2 = 6, 7
            insn = enc_ldp_stp(opc, 0, mode, l_bit, simm7, rt2, rn, rt)
            name = f"ldpstp_opc{opc}_l{l_bit}_m{mode}"
        elif kind == 4:
            # SIMD/FP LDR/STR B/H/S/D
            size = rng.randint(0, 3)
            opc = rng.choice([0, 1])
            insn = enc_ldr_str_uimm(size, 1, opc, rng.randint(0, 4), rn, rt & 31)
            name = f"fp_ldst_sz{size}_opc{opc}"
        else:
            # 128-bit SIMD LDR Q / STR Q (size=0, v=1, opc=2 STR Q / opc=3 LDR Q)
            opc = rng.choice([2, 3])
            insn = enc_ldr_str_uimm(0, 1, opc, rng.randint(0, 3), rn, rt & 31)
            name = f"fp_ldst_q_opc{opc}"
        cases.append({
            "cat": "ldst",
            "name": f"{name}_{i}",
            "steps": 1,
            "pc": CODE_BASE,
            "sp": sp_val,
            "nzcv": nzcv,
            "x": xs,
            "q": qs,
            "code": u32_bytes([insn]),
            "data_addr": DATA_BASE,
            "data": fixed_pattern,
        })

    # Category 6: SIMD & FP ops (including scalar FP, FCMP, SCVTF/FCVTZS, ADDV, UMAXV, SSHR/USHR, CNT) (350 cases)
    fp_s_bits = [0x00000000, 0x3F800000, 0xBF800000, 0x40490FDB, 0xC1200000, 0x47000000, 0x80000000, 0x00800000, 0x7F7FFFFF]
    fp_d_bits = [0x0000000000000000, 0x3FF0000000000000, 0xBFF0000000000000, 0x400921FB54442D18, 0xC059000000000000, 0x43E0000000000000, 0x8000000000000000]
    for i in range(350):
        xs, qs = base_regs()
        # Seed clean finite floats in lower bits for scalar FP categories
        for r_idx in range(6):
            lo = rng.choice(fp_d_bits) if (i % 10 >= 5) else (rng.choice(fp_s_bits) | (rng.getrandbits(32) << 32))
            qs[r_idx] = (lo, rng.getrandbits(64))
        nzcv = rng.randint(0, 15)
        rd = rng.randint(0, 5)
        rn = rng.randint(0, 5)
        rm = rng.randint(0, 5)
        q_bit = rng.choice([0, 1])
        kind = i % 10
        if kind == 0:
            opc_pair = rng.choice([(0, 0), (0, 1), (0, 2), (1, 0)])
            u_bit, opc2 = opc_pair
            insn = (q_bit << 30) | (u_bit << 29) | (0b01110 << 24) | (opc2 << 22) | (1 << 21) | (rm << 16) | (0b000111 << 10) | (rn << 5) | rd
            name = f"vec_log_q{q_bit}_u{u_bit}_o{opc2}"
        elif kind == 1:
            sz = rng.randint(0, 2 if q_bit == 0 else 3)
            u_bit = rng.choice([0, 1])
            insn = (q_bit << 30) | (u_bit << 29) | (0b01110 << 24) | (sz << 22) | (1 << 21) | (rm << 16) | (0b100001 << 10) | (rn << 5) | rd
            name = f"vec_addsub_q{q_bit}_sz{sz}_u{u_bit}"
        elif kind == 2:
            if rng.random() < 0.5:
                idx = rng.choice([0, 1])
                imm5 = 0b01000 | (idx << 4)
                insn = 0x4E001C00 | (imm5 << 16) | (rn << 5) | rd
                name = f"ins_d_{idx}"
            else:
                q_u = rng.choice([0, 1])
                imm5 = 0b01000 if q_u else 0b00100
                insn = (q_u << 30) | 0x0E003C00 | (imm5 << 16) | (rn << 5) | rd
                name = f"umov_q{q_u}"
        elif kind == 3:
            sz = rng.randint(0, 2 if q_bit == 0 else 3)
            imm5 = 1 << sz
            insn = (q_bit << 30) | 0x0E000C00 | (imm5 << 16) | (rn << 5) | rd
            name = f"dup_q{q_bit}_sz{sz}"
        elif kind == 4:
            if rng.random() < 0.5:
                insn = 0x9E670000 | (rn << 5) | rd
            else:
                insn = 0x9E660000 | (rn << 5) | rd
            name = "fmov_gpr_fpr"
        elif kind == 5:
            # Scalar FP 2-source: FMUL / FDIV / FADD / FSUB (S or D)
            ftype = rng.choice([0, 1])  # 0=S, 1=D
            opc = rng.choice([0, 2, 3])  # skip 1 (FDIV by 0) unless safe; actually include 1 when rm != 0
            if rng.random() < 0.2:
                opc = 1
                qs[rm] = (0x3FF8000000000000 if ftype else 0x3FC00000, 0)
            insn = (0b00011110 << 24) | (ftype << 22) | (1 << 21) | (rm << 16) | (opc << 12) | (0b10 << 10) | (rn << 5) | rd
            name = f"fp_2src_t{ftype}_op{opc}"
        elif kind == 6:
            # FCMP Sn, Sm / Dn, Dm
            ftype = rng.choice([0, 1])
            insn = (0b00011110 << 24) | (ftype << 22) | (1 << 21) | (rm << 16) | (0b001000 << 10) | (rn << 5)
            name = f"fcmp_t{ftype}"
        elif kind == 7:
            # SCVTF Dd/Sd, Xn/Wn  or FCVTZS Xd/Wd, Dn/Sn (keep values in safe range for int truncation)
            sf = rng.choice([0, 1])
            ftype = rng.choice([0, 1])
            if rng.random() < 0.5:
                # SCVTF
                insn = (sf << 31) | (0b0011110 << 24) | (ftype << 22) | (1 << 21) | (0b00010 << 16) | (rn << 5) | rd
                name = f"scvtf_sf{sf}_t{ftype}"
            else:
                # FCVTZS (moderate values inside range)
                qs[rn] = (0x4059400000000000 if ftype else 0x42C80000, 0)
                insn = (sf << 31) | (0b0011110 << 24) | (ftype << 22) | (1 << 21) | (0b11000 << 16) | (rn << 5) | rd
                name = f"fcvtzs_sf{sf}_t{ftype}"
        elif kind == 8:
            # Vector ADDV / SMAXV / UMAXV (across vector)
            sz = rng.randint(0, 2)  # 8B/16B, 4H/8H, 4S
            if sz == 2:
                q_bit = 1
            if rng.random() < 0.5:
                insn = (q_bit << 30) | (0 << 29) | (0b01110 << 24) | (sz << 22) | (0b11000 << 17) | (0b11011 << 12) | (0b10 << 10) | (rn << 5) | rd
                name = f"addv_q{q_bit}_sz{sz}"
            else:
                u_bit = rng.choice([0, 1])
                insn = (q_bit << 30) | (u_bit << 29) | (0b01110 << 24) | (sz << 22) | (0b11000 << 17) | (0b01010 << 12) | (0b10 << 10) | (rn << 5) | rd
                name = f"maxv_q{q_bit}_u{u_bit}_sz{sz}"
        else:
            # Vector CNT (.8B/.16B) or USHR / SSHR shift
            if rng.random() < 0.4:
                insn = (q_bit << 30) | 0x0E205800 | (rn << 5) | rd  # CNT
                name = f"cnt_q{q_bit}"
            else:
                # Vector USHR / SSHR: immh != 0 (0001xxxx=B, 001xxxxx=H, 01xxxxxx=S, 1xxxxxxx=D)
                u_bit = rng.choice([0, 1])
                esize_idx = rng.randint(0, 2 if q_bit == 0 else 3)
                width_bits = 8 << esize_idx
                shift_amt = rng.randint(1, width_bits)
                immh_immb = (width_bits * 2) - shift_amt
                insn = (q_bit << 30) | (u_bit << 29) | (0b011110 << 23) | (immh_immb << 16) | (0b000001 << 10) | (rn << 5) | rd
                name = f"vec_shr_q{q_bit}_u{u_bit}_e{esize_idx}"
        cases.append({
            "cat": "simd_fp",
            "name": f"{name}_{i}",
            "steps": 1,
            "pc": CODE_BASE,
            "sp": DATA_BASE + 0x400,
            "nzcv": nzcv,
            "x": xs,
            "q": qs,
            "code": u32_bytes([insn]),
            "data_addr": DATA_BASE,
            "data": b"",
        })

    # Category 7: Multi-instruction chains & LDR literal & Logical immediates (300 cases)
    # Valid N:immr:imms patterns for 64-bit / 32-bit logical immediates
    valid_log_imms_64 = [
        (1, 0, 0), (1, 0, 7), (1, 0, 15), (1, 0, 31), (1, 3, 11), (1, 60, 7),
        (0, 0, 0x07), (0, 4, 0x23), (0, 2, 0x11), (0, 0, 0x3C),
    ]
    for i in range(300):
        xs, qs = base_regs()
        nzcv = rng.randint(0, 15)
        if i % 3 == 0:
            # Logical immediate AND / ORR / EOR / ANDS (64-bit & 32-bit)
            sf = rng.choice([0, 1])
            opc = rng.randint(0, 3)
            n_bit, immr, imms = rng.choice(valid_log_imms_64 if sf else [x for x in valid_log_imms_64 if x[0] == 0])
            rd = rng.choice([0, 1, 2, 31])
            rn = rng.randint(0, 5)
            insn = (sf << 31) | (opc << 29) | (0b100100 << 23) | (n_bit << 22) | (immr << 16) | (imms << 10) | (rn << 5) | rd
            cases.append({
                "cat": "chains_imm",
                "name": f"log_imm_sf{sf}_opc{opc}_{i}",
                "steps": 1,
                "pc": CODE_BASE,
                "sp": DATA_BASE + 0x400,
                "nzcv": nzcv,
                "x": xs,
                "q": qs,
                "code": u32_bytes([insn]),
                "data_addr": DATA_BASE,
                "data": b"",
            })
        elif i % 3 == 1:
            # LDR literal (W, X, SW) reading constant words placed right after branch
            opc = rng.choice([0, 1, 2])
            rt = rng.randint(0, 5)
            insn = (opc << 30) | (0b011000 << 24) | (2 << 5) | rt  # PC + 8
            cases.append({
                "cat": "chains_imm",
                "name": f"ldr_lit_opc{opc}_{i}",
                "steps": 1,
                "pc": CODE_BASE,
                "sp": DATA_BASE + 0x400,
                "nzcv": nzcv,
                "x": xs,
                "q": qs,
                "code": u32_bytes([insn, 0xD503201F, 0x89ABCDEF, 0xFEDCBA98]),
                "data_addr": DATA_BASE,
                "data": b"",
            })
        else:
            # 6-instruction random arithmetic + memory chain spanning hazards & flags
            xs[5] = DATA_BASE + 0x3FFA  # cross-16KB boundary base!
            seq = [
                enc_add_sub_shifted(1, 0, 1, 0, 1, 3, 0, 2),  # ADDS X2, X0, X1, LSL #3
                enc_csel(1, 0, 3, 2, 1, 4, 6),                # CSINC X6, X4, X3, CS
                enc_ldr_str_uimm(3, 0, 0, 0, 5, 6),           # STR X6, [X5] (crosses 0x44000!)
                enc_ldr_str_uimm(2, 0, 1, 0, 5, 7),           # LDR W7, [X5] (unaligned tail/head)
                enc_bitfield(1, 2, 1, 4, 19, 7, 8),           # UBFX X8, X7, #4, #16
                enc_dp3(1, 0, 8, 1, 6, 2, 9),                 # MSUB X9, X2, X8, X6
            ]
            cases.append({
                "cat": "chains_imm",
                "name": f"chain6_{i}",
                "steps": 6,
                "pc": CODE_BASE,
                "sp": DATA_BASE + 0x400,
                "nzcv": nzcv,
                "x": xs,
                "q": qs,
                "code": u32_bytes(seq),
                "data_addr": DATA_BASE,
                "data": fixed_pattern,
            })

    # Category 8: adc_sbc_stress (500 cases)
    for i in range(500):
        xs, qs = base_regs()
        sf = rng.choice([0, 1])
        op = rng.choice([0, 1])
        s = rng.choice([0, 1])
        rd = rng.choice([0, 1, 2, 3, 4, 31])
        rn = rng.choice([0, 1, 2, 3, 4, 31])
        rm = rng.choice([0, 1, 2, 3, 4, 31])
        nzcv = rng.randint(0, 15)
        if rn < 31 and rng.random() < 0.6:
            xs[rn] = rng.choice(edge_vals)
        if rm < 31 and rng.random() < 0.6:
            xs[rm] = rng.choice(edge_vals)
        insn = enc_adc_sbc(sf, op, s, rm, rn, rd)
        name = f"adc_sbc_stress_sf{sf}_op{op}_s{s}_{i}"
        cases.append({
            "cat": "adc_sbc_stress",
            "name": name,
            "steps": 1,
            "pc": CODE_BASE,
            "sp": DATA_BASE + 0x400,
            "nzcv": nzcv,
            "x": xs,
            "q": qs,
            "code": u32_bytes([insn]),
            "data_addr": DATA_BASE,
            "data": b"",
        })

    # Category 9: shifted_extended_arith_stress (500 cases)
    for i in range(500):
        xs, qs = base_regs()
        sf = rng.choice([0, 1])
        op = rng.choice([0, 1])
        s = rng.choice([0, 1])
        rd = rng.choice([0, 1, 2, 3, 4, 31])
        rn = rng.randint(0, 5)
        rm = rng.randint(0, 5)
        nzcv = rng.randint(0, 15)
        if rng.random() < 0.5:
            shift = rng.choice([0, 1, 2])
            imm6 = rng.choice([0, 1, 2, 15, 31, 32, 63] if sf else [0, 1, 2, 15, 31])
            insn = enc_add_sub_shifted(sf, op, s, shift, rm, imm6, rn, rd)
            name = f"shift_arith_sf{sf}_sh{shift}_imm{imm6}_{i}"
        else:
            opt = rng.randint(0, 7)
            imm3 = rng.choice([0, 1, 2, 3, 4])
            insn = enc_add_sub_ext(sf, op, s, rm, opt, imm3, rn, rd)
            name = f"ext_arith_sf{sf}_opt{opt}_imm{imm3}_{i}"
        cases.append({
            "cat": "shifted_extended_arith_stress",
            "name": name,
            "steps": 1,
            "pc": CODE_BASE,
            "sp": DATA_BASE + 0x400,
            "nzcv": nzcv,
            "x": xs,
            "q": qs,
            "code": u32_bytes([insn]),
            "data_addr": DATA_BASE,
            "data": b"",
        })

    # Category 10: cond_edge_cases (500 cases)
    for i in range(500):
        xs, qs = base_regs()
        nzcv = i % 16
        cond = (i // 16) % 16
        sf = rng.choice([0, 1])
        rd = rng.choice([0, 1, 2, 3])
        rn = rng.choice([0, 1, 2, 4])
        rm = rng.choice([0, 1, 2, 5])
        if i % 3 != 0:
            op = rng.choice([0, 1])
            op2 = rng.choice([0, 1])
            insn = enc_csel(sf, op, rm, cond, op2, rn, rd)
            cases.append({
                "cat": "cond_edge_cases",
                "name": f"csel_cond_{cond}_nzcv_{nzcv}_{i}",
                "steps": 1,
                "pc": CODE_BASE,
                "sp": DATA_BASE + 0x400,
                "nzcv": nzcv,
                "x": xs,
                "q": qs,
                "code": u32_bytes([insn]),
                "data_addr": DATA_BASE,
                "data": b"",
            })
        else:
            c = cond if cond < 15 else 14
            landing_fall = enc_mov_wide(1, 2, 0, 0x1111, 10)
            landing_tgt = enc_mov_wide(1, 2, 0, 0x2222, 10)
            insn = (0b01010100 << 24) | (2 << 5) | c
            cases.append({
                "cat": "cond_edge_cases",
                "name": f"bcond_cond_{c}_nzcv_{nzcv}_{i}",
                "steps": 2,
                "pc": CODE_BASE,
                "sp": DATA_BASE + 0x400,
                "nzcv": nzcv,
                "x": xs,
                "q": qs,
                "code": u32_bytes([insn, landing_fall, landing_tgt]),
                "data_addr": DATA_BASE,
                "data": b"",
            })

    # Category 11: wreg_zero_extension_sp_xzr (500 cases)
    for i in range(500):
        xs, qs = base_regs()
        nzcv = rng.randint(0, 15)
        kind = i % 5
        dirty_prefix = 0xDEADBEEF00000000
        rd = rng.choice([0, 1, 2, 3])
        xs[rd] = dirty_prefix | (xs[rd] & 0xFFFFFFFF)
        rn = rng.randint(0, 5)
        rm = rng.randint(0, 5)
        if kind == 0:
            op = rng.choice([0, 1])
            s = rng.choice([0, 1])
            shift = rng.choice([0, 1, 2])
            imm6 = rng.randint(0, 31)
            insn = enc_add_sub_shifted(0, op, s, shift, rm, imm6, rn, rd)
            name = f"wreg_addsub_sh_{i}"
        elif kind == 1:
            opc = rng.choice([0b001000, 0b001001, 0b001010, 0b001011, 0b000010, 0b000011])
            insn = enc_dp2(0, rm, opc, rn, rd)
            name = f"wreg_dp2_opc{opc}_{i}"
        elif kind == 2:
            opc = rng.choice([0, 2])
            immr = rng.randint(0, 31)
            imms = rng.randint(0, 31)
            insn = enc_bitfield(0, opc, 0, immr, imms, rn, rd)
            name = f"wreg_bf_{i}"
        elif kind == 3:
            imm12 = rng.randint(0, 0xFFF)
            subkind = rng.choice([0, 1, 2, 3])
            if subkind == 0:
                insn = enc_add_sub_imm(1, 0, 0, 0, imm12, 31, rd)
                name = f"add_xd_sp_{i}"
            elif subkind == 1:
                insn = enc_add_sub_imm(1, 0, 0, 0, imm12, rn, 31)
                name = f"add_sp_xn_{i}"
            elif subkind == 2:
                insn = enc_add_sub_imm(0, 0, 0, 0, imm12, rn, 31)
                name = f"add_wsp_wn_{i}"
            else:
                insn = enc_add_sub_imm(0, 1, 1, 0, imm12, rn, 31)
                name = f"cmp_wn_imm_{i}"
        else:
            xs[5] = DATA_BASE + 0x200 + (rng.randint(0, 30) * 4)
            insn = enc_ldr_str_uimm(2, 0, 1, 0, 5, rd)
            name = f"ldr_w_zeroext_{i}"

        cases.append({
            "cat": "wreg_zero_extension_sp_xzr",
            "name": name,
            "steps": 1,
            "pc": CODE_BASE,
            "sp": DATA_BASE + 0x400,
            "nzcv": nzcv,
            "x": xs,
            "q": qs,
            "code": u32_bytes([insn]),
            "data_addr": DATA_BASE,
            "data": fixed_pattern if kind == 4 else b"",
        })

    # Category 12: simd_vector_corner_cases (500 cases)
    for i in range(500):
        xs, qs = base_regs()
        nzcv = rng.randint(0, 15)
        kind = i % 4
        rd = rng.randint(0, 5)
        rn = rng.randint(0, 5)
        rm = rng.randint(0, 5)
        q = rng.choice([0, 1])
        if kind == 0:
            esz = rng.choice([0, 1, 2])
            w = 8 << esz
            shift = rng.randint(1, w)
            immhb = (w * 2) - shift
            insn = (q << 30) | (0 << 29) | (0b011110 << 23) | (immhb << 16) | (0b10000 << 11) | (1 << 10) | (rn << 5) | rd
            name = f"shrn_q{q}_esz{esz}_s{shift}_{i}"
        elif kind == 1:
            u = rng.choice([0, 1])
            sz = rng.choice([0, 1, 2])
            insn = (q << 30) | (u << 29) | (0b01110 << 24) | (sz << 22) | (1 << 21) | (rm << 16) | (0b11000 << 11) | (rn << 5) | rd
            name = f"mull_q{q}_u{u}_sz{sz}_{i}"
        elif kind == 2:
            ftype = rng.choice([0, 1])
            cond = rng.randint(0, 15)
            insn = 0x1E200C00 | (ftype << 22) | (rm << 16) | (cond << 12) | (rn << 5) | rd
            name = f"fcsel_t{ftype}_c{cond}_{i}"
        else:
            cmode = rng.choice([0, 2, 4, 6, 8, 10, 14])
            op = 0 if cmode == 14 else rng.choice([0, 1])
            imm8 = rng.randint(0, 255)
            imm8_hi = (imm8 >> 5) & 7
            imm8_lo = imm8 & 31
            insn = 0x0F000400 | (q << 30) | (op << 29) | (imm8_hi << 16) | (cmode << 12) | (imm8_lo << 5) | rd
            name = f"vec_imm_q{q}_cmode{cmode}_op{op}_{i}"

        cases.append({
            "cat": "simd_vector_corner_cases",
            "name": name,
            "steps": 1,
            "pc": CODE_BASE,
            "sp": DATA_BASE + 0x400,
            "nzcv": nzcv,
            "x": xs,
            "q": qs,
            "code": u32_bytes([insn]),
            "data_addr": DATA_BASE,
            "data": b"",
        })

    return cases


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--runner", required=True, help="Path to diff_winios_runner.exe")
    ap.add_argument("--seed", type=int, default=2026191)
    ap.add_argument("--report", default="", help="Optional JSON summary output path")
    args = ap.parse_args()

    cases = generate_cases(args.seed)
    proc = subprocess.Popen(
        [args.runner],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )

    stats = {
        "total": len(cases),
        "compared": 0,
        "matched": 0,
        "mismatched": 0,
        "winios_unsupported": 0,
        "oracle_fault": 0,
        "by_category": {},
    }
    mismatches = []

    for c in cases:
        cat = c["cat"]
        cstats = stats["by_category"].setdefault(
            cat, {"total": 0, "matched": 0, "mismatched": 0, "unsupported": 0, "oracle_fault": 0}
        )
        cstats["total"] += 1

        uc_res = run_unicorn_case(
            c["steps"], c["pc"], c["sp"], c["nzcv"], c["x"], c["q"],
            c["code"], c["data_addr"], c["data"],
        )
        msg = pack_case(
            c["steps"], c["pc"], c["sp"], c["nzcv"], c["x"], c["q"],
            c["code"], c["data_addr"], c["data"],
        )
        proc.stdin.write(msg)
        proc.stdin.flush()
        w_res = parse_winios_response(proc)

        if w_res["status"].startswith("ERR:Unsupported"):
            stats["winios_unsupported"] += 1
            cstats["unsupported"] += 1
            continue

        if uc_res["status"] != "OK":
            stats["oracle_fault"] += 1
            cstats["oracle_fault"] += 1
            continue

        stats["compared"] += 1
        diffs = []
        if w_res["status"] != "OK":
            diffs.append(f"status winios={w_res['status']} esr=0x{w_res['esr']:x} vs uc=OK")
        if w_res["pc"] != uc_res["pc"]:
            diffs.append(f"PC winios=0x{w_res['pc']:x} vs uc=0x{uc_res['pc']:x}")
        if w_res["sp"] != uc_res["sp"]:
            diffs.append(f"SP winios=0x{w_res['sp']:x} vs uc=0x{uc_res['sp']:x}")
        if w_res["nzcv"] != uc_res["nzcv"]:
            diffs.append(f"NZCV winios=0x{w_res['nzcv']:x} vs uc=0x{uc_res['nzcv']:x}")
        for idx in range(31):
            if w_res["x"][idx] != uc_res["x"][idx]:
                diffs.append(f"X{idx} winios=0x{w_res['x'][idx]:x} vs uc=0x{uc_res['x'][idx]:x}")
        for idx in range(32):
            if w_res["q"][idx] != uc_res["q"][idx]:
                diffs.append(f"Q{idx} winios={w_res['q'][idx]} vs uc={uc_res['q'][idx]}")
        if c["data"] and w_res["data"] != uc_res["data"]:
            diffs.append("MEM_DIFF")

        if diffs:
            stats["mismatched"] += 1
            cstats["mismatched"] += 1
            mismatches.append({
                "name": c["name"],
                "cat": cat,
                "code": c["code"].hex(),
                "diffs": diffs[:8],
            })
        else:
            stats["matched"] += 1
            cstats["matched"] += 1

    proc.stdin.write("QUIT\n")
    proc.stdin.flush()
    proc.wait(timeout=5)

    print(json.dumps({"stats": stats, "mismatches": mismatches[:25]}, indent=2))
    if args.report:
        with open(args.report, "w", encoding="utf-8") as rf:
            json.dump({"stats": stats, "mismatches": mismatches}, rf, indent=2)

    return 0 if stats["mismatched"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
