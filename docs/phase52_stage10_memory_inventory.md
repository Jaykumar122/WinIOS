# WinIOS Phase 52 — Stage 10: Memory Access Inventory

**Document Version**: 1.0.0  
**Phase**: 52 (Execution & Diagnostic Tracing Pipeline)  
**Stage**: 10 (Memory Read/Write Trace Integration)  
**Date**: November 2026  
**Status**: APPROVED / IMPLEMENTATION INVENTORY COMPLETE  

---

## 1. Overview

This document provides a comprehensive inventory of all supported guest memory access paths in the WinIOS ARM64 emulator. Every memory instruction family, access width, addressing mode, fault behavior, and trace integration point is cataloged below.

---

## 2. Supported Memory Access Path Inventory

| # | Instruction Family / Form | Access Width | Effective Address Source | Translation Path | Permission Checks | Memory Side Effects | Fault Behavior | Access Category | Trace Integration Point |
|---|---|---|---|---|---|---|---|---|---|
| 1 | `LDR` (unsigned immediate, 64-bit) | 8 B | `Rn + (imm12 * 8)` | `mmu.translate(..., Read)` | Stage 1/2 Read AP | None (loads $X_t$) | Alignment / Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::read` (MemoryRead) |
| 2 | `LDR` (unsigned immediate, 32-bit) | 4 B | `Rn + (imm12 * 4)` | `mmu.translate(..., Read)` | Stage 1/2 Read AP | None (zero-extends into $W_t$) | Alignment / Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::read` (MemoryRead) |
| 3 | `STR` (unsigned immediate, 64-bit) | 8 B | `Rn + (imm12 * 8)` | `mmu.translate(..., Write)` | Stage 1/2 Write AP | Writes 8 bytes to RAM/MMIO; clears reservation | Alignment / Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::write` (MemoryWrite) |
| 4 | `STR` (unsigned immediate, 32-bit) | 4 B | `Rn + (imm12 * 4)` | `mmu.translate(..., Write)` | Stage 1/2 Write AP | Writes 4 bytes to RAM/MMIO; clears reservation | Alignment / Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::write` (MemoryWrite) |
| 5 | `LDRB` (unsigned immediate) | 1 B | `Rn + imm12` | `mmu.translate(..., Read)` | Stage 1/2 Read AP | None (zero-extends into $W_t$) | Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::read` (MemoryRead) |
| 6 | `LDRH` (unsigned immediate) | 2 B | `Rn + (imm12 * 2)` | `mmu.translate(..., Read)` | Stage 1/2 Read AP | None (zero-extends into $W_t$) | Alignment / Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::read` (MemoryRead) |
| 7 | `STRB` (unsigned immediate) | 1 B | `Rn + imm12` | `mmu.translate(..., Write)` | Stage 1/2 Write AP | Writes 1 byte to RAM/MMIO; clears reservation | Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::write` (MemoryWrite) |
| 8 | `STRH` (unsigned immediate) | 2 B | `Rn + (imm12 * 2)` | `mmu.translate(..., Write)` | Stage 1/2 Write AP | Writes 2 bytes to RAM/MMIO; clears reservation | Alignment / Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::write` (MemoryWrite) |
| 9 | `LDRSB` (32-bit / 64-bit) | 1 B | `Rn + imm12` | `mmu.translate(..., Read)` | Stage 1/2 Read AP | None (sign-extends into $W_t$/$X_t$) | Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::read` (MemoryRead) |
| 10 | `LDRSH` (32-bit / 64-bit) | 2 B | `Rn + (imm12 * 2)` | `mmu.translate(..., Read)` | Stage 1/2 Read AP | None (sign-extends into $W_t$/$X_t$) | Alignment / Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::read` (MemoryRead) |
| 11 | `LDRSW` (64-bit sign-extended) | 4 B | `Rn + (imm12 * 4)` | `mmu.translate(..., Read)` | Stage 1/2 Read AP | None (sign-extends 32-bit into $X_t$) | Alignment / Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::read` (MemoryRead) |
| 12 | `LDR` / `STR` (pre-indexed, 32/64) | 4 / 8 B | `writeback = Rn + simm9; ea = writeback` | `mmu.translate(...)` | Stage 1/2 AP | Updates $R_n$; reads/writes RAM/MMIO | Alignment / Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::read/write` |
| 13 | `LDR` / `STR` (post-indexed, 32/64) | 4 / 8 B | `ea = Rn; writeback = Rn + simm9` | `mmu.translate(...)` | Stage 1/2 AP | Updates $R_n$; reads/writes RAM/MMIO | Alignment / Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::read/write` |
| 14 | `LDUR` / `STUR` (unscaled 9-bit) | 1, 2, 4, 8 B | `Rn + simm9` | `mmu.translate(...)` | Stage 1/2 AP | Reads/writes RAM/MMIO | Alignment / Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::read/write` |
| 15 | `LDR` / `STR` (register offset) | 1, 2, 4, 8 B | `Rn + Extend(Rm, opt) << shift` | `mmu.translate(...)` | Stage 1/2 AP | Reads/writes RAM/MMIO | Alignment / Data Abort | Normal RAM / MMIO | `CheckedGuestMemory::read/write` |
| 16 | `LDP` (load pair, 32/64-bit) | 2x4 / 2x8 B | `ea1 = ea; ea2 = ea + scale` | `mmu.translate(..., Read)` (both) | Stage 1/2 Read AP | Reads both elements into $X_{t1}$, $X_{t2}$ | Alignment / Data Abort | Normal RAM / MMIO | 2x `CheckedGuestMemory::read` |
| 17 | `STP` (store pair, 32/64-bit) | 2x4 / 2x8 B | `ea1 = ea; ea2 = ea + scale` | `mmu.translate(..., Write)` (both) | Stage 1/2 Write AP | Writes both elements to RAM/MMIO; clears reservation | Alignment / Data Abort | Normal RAM / MMIO | 2x `CheckedGuestMemory::write` |
| 18 | `LDPSW` (load pair signed word) | 2x4 B | `ea1 = ea; ea2 = ea + 4` | `mmu.translate(..., Read)` (both) | Stage 1/2 Read AP | Sign-extends both words into $X_{t1}$, $X_{t2}$ | Alignment / Data Abort | Normal RAM / MMIO | 2x `CheckedGuestMemory::read` |
| 19 | `LDR` literal (PC-relative, 32/64) | 4 / 8 B | `PC + (simm19 * 4)` | `mmu.translate(..., Read)` | Stage 1/2 Read AP | Loads constant from literal pool | Alignment / Data Abort | Normal RAM | `CheckedGuestMemory::read` (MemoryRead) |
| 20 | `LDRSW` literal (PC-relative, 64) | 4 B | `PC + (simm19 * 4)` | `mmu.translate(..., Read)` | Stage 1/2 Read AP | Sign-extends literal word into $X_t$ | Alignment / Data Abort | Normal RAM | `CheckedGuestMemory::read` (MemoryRead) |
| 21 | `LDR` (SIMD/FP scalar B/H/S/D) | 1, 2, 4, 8 B | `Rn + offset` | `mmu.translate(..., Read)` | Stage 1/2 Read AP | Loads into $V_t$ (zeroes high bits) | Alignment / Data Abort | Normal RAM | `CheckedGuestMemory::read` (MemoryRead) |
| 22 | `STR` (SIMD/FP scalar B/H/S/D) | 1, 2, 4, 8 B | `Rn + offset` | `mmu.translate(..., Write)` | Stage 1/2 Write AP | Writes scalar FP value to RAM | Alignment / Data Abort | Normal RAM | `CheckedGuestMemory::write` (MemoryWrite) |
| 23 | `LDR Q` (SIMD/FP 128-bit vector) | 16 B (2x8 B) | `ea1 = ea; ea2 = ea + 8` | `mmu.translate(..., Read)` | Stage 1/2 Read AP | Loads 128-bit vector into $V_t$ | 16-byte Alignment / Data Abort | Normal RAM | 2x `CheckedGuestMemory::read` |
| 24 | `STR Q` (SIMD/FP 128-bit vector) | 16 B (2x8 B) | `ea1 = ea; ea2 = ea + 8` | `mmu.translate(..., Write)` | Stage 1/2 Write AP | Writes 128-bit vector to RAM | 16-byte Alignment / Data Abort | Normal RAM | 2x `CheckedGuestMemory::write` |
| 25 | `LDXR` / `LDAXR` (Load Exclusive) | 1, 2, 4, 8 B | `Rn` | `mmu.translate(..., Read)` | Stage 1/2 Read AP | Sets local/global exclusive reservation | Alignment (WnR=1) / Abort | Exclusive | `CheckedGuestMemory::read` (`IsAtomic`) |
| 26 | `STXR` / `STLXR` (Store Exclusive) | 1, 2, 4, 8 B | `Rn` | `mmu.translate(..., Write)` | Stage 1/2 Write AP | If reservation valid: writes RAM, status=0. If invalid: status=1, no write. | Alignment / Data Abort | Exclusive | `CheckedGuestMemory::write` if success; **0 trace events if failed** |
| 27 | `LDXP` / `LDAXP` (Load Exclusive Pair) | 2x4 / 2x8 B | `ea1 = ea; ea2 = ea + scale` | `mmu.translate(..., Read)` | Stage 1/2 Read AP | Sets pair exclusive reservation | Alignment (WnR=1) / Abort | Exclusive | 2x `CheckedGuestMemory::read` (`IsAtomic`) |
| 28 | `STXP` / `STLXP` (Store Exclusive Pair)| 2x4 / 2x8 B | `ea1 = ea; ea2 = ea + scale` | `mmu.translate(..., Write)` | Stage 1/2 Write AP | If valid: writes both, status=0. If invalid: status=1, no write. | Alignment / Data Abort | Exclusive | 2x `CheckedGuestMemory::write` if success; **0 trace events if failed** |
| 29 | `LDAR` / `STLR` (Acquire/Release) | 1, 2, 4, 8 B | `Rn` | `mmu.translate(...)` | Stage 1/2 AP | Enforces memory ordering fence | Alignment / Data Abort | Ordered | `CheckedGuestMemory::read/write` |
| 30 | `LDADD` / `LDCLR` / `LDEOR` / `LDSET` | 1, 2, 4, 8 B | `Rn` | `mmu.translate(..., Write)` | Stage 1/2 Write AP | Atomic RMW: reads `old_value`, writes `new_value` | Alignment (WnR=1) / Abort | LSE Atomic | 1x Read (`IsAtomic`) + 1x Write (`IsAtomic`) |
| 31 | `LDSMAX` / `LDSMIN` / `LDUMAX` / `LDUMIN` | 1, 2, 4, 8 B | `Rn` | `mmu.translate(..., Write)` | Stage 1/2 Write AP | Atomic RMW: reads `old_value`, writes `new_value` | Alignment (WnR=1) / Abort | LSE Atomic | 1x Read (`IsAtomic`) + 1x Write (`IsAtomic`) |
| 32 | `SWP` (Atomic Swap) | 1, 2, 4, 8 B | `Rn` | `mmu.translate(..., Write)` | Stage 1/2 Write AP | Atomic RMW: reads `old_value`, writes `source_value` | Alignment (WnR=1) / Abort | LSE Atomic | 1x Read (`IsAtomic`) + 1x Write (`IsAtomic`) |
| 33 | `DC ZVA` (Zero 64-byte block) | 64 B (8x8 B) | `va & ~63ULL` | `mmu.translate(..., Write)` | Stage 1/2 Write AP | Zeroes 64 contiguous bytes in RAM | Alignment / Data Abort | Cache Maintenance | 8x `CheckedGuestMemory::write` |

---

## 3. Unsupported or Non-Generating Paths
- `PRFM` (Prefetch Memory): Throws unsupported instruction encoding in current model. Zero memory events.
- `DC CVAC`, `DC CVAU`, `DC CIVAC`, `IC IVAU`, `DC IVAC`: Validate memory range via `memory.check_range()`; do not perform data memory transfers. Zero `MemoryRead` or `MemoryWrite` events.
- Instruction Fetch: Performed via `guest_memory.read_u32()` before instruction decode. Does not emit data memory events.

---

## 4. Verification Checkpoints
- **Read/Write Event Types**: Exact match (`TraceEventType::MemoryRead = 7`, `TraceEventType::MemoryWrite = 8`).
- **Data Capture**: Exact data value in `data` field with `TraceEventFlags::HasData` set.
- **Direction Flag**: `TraceEventFlags::IsWrite` set for stores and atomics writes; cleared for loads.
- **Atomic Flag**: `TraceEventFlags::IsAtomic` set for LDXR, STXR, LDXP, STXP, LDADD, SWP, etc.
- **Failed STXR**: Verified that zero `MemoryRead` and zero `MemoryWrite` events are emitted.
- **Faulting Access**: Verified that zero `MemoryRead` and zero `MemoryWrite` events are emitted.
