# WINIOS CAMPAIGN 218 — STAGE 218.13 GUEST SYSCALL ABI MATRIX

**Date:** November 18, 2026  
**Campaign:** 218 (Stage 218.13)  
**Host:** Windows 11 x64  
**Trap Mechanism:** `SVC #0x80` or `SVC #0` transitioning from EL0 to EL1 with ESR_EL1 EC=0x15

---

## 1. System Call ABI Specification (Syscalls 500 – 521)

All syscalls in the Category C extension range (500–521) follow the standard 64-bit ARM64 Darwin calling convention:
- **Syscall Number:** Register `X16` (or `X0` in fast-call mode)
- **Arguments:** Registers `X0` through `X5`
- **Return Value:** Register `X0` (Success: non-negative result; Error: `-1` or errno)
- **Carry Flag (CPSR.C):** Set on error, cleared on success

---

## 2. Category C ABI Table

| Syscall # | Constant Name | Arguments | Return Value | Memory & Security Checks |
|---|---|---|---|---|
| 500 | `kSysWiniosWindowCreate` | `X0`: title_va, `X1`: x, `X2`: y, `X3`: w, `X4`: h | Window ID in `X0` (or error) | Validates user string, bounds width/height |
| 501 | `kSysWiniosWindowDestroy` | `X0`: window_id | 0 on success | Checks window process ownership |
| 502 | `kSysWiniosSurfaceUpdate` | `X0`: window_id, `X1`: pixel_buf_va, `X2`: byte_len | 0 on success | Bounds copy-in to surface dimensions |
| 503 | `kSysWiniosSurfacePresent` | `X0`: window_id | 0 on success | Flushes surface to compositor |
| 504 | `kSysWiniosPollEvent` | `X0`: window_id, `X1`: out_event_va | Event count in `X0` | Copies 32-byte `winios_event_t` to user VA |
| 505 | `kSysWiniosSurfaceFill` | `X0`: window_id, `X1`: argb_color | 0 on success | Validates window access |
| 506 | `kSysWiniosSetWindowTitle` | `X0`: window_id, `X1`: title_va | 0 on success | Reads user string with 1024-byte bound |
| 507 | `kSysWiniosGetAbiVersion` | — | ABI Version `0x00010100` / `0x00010200` | Immediate return |
| 508 | `kSysWiniosWindowGetGeometry` | `X0`: window_id, `X1`: out_geom_va | 0 on success | Writes 32-byte `winios_window_geometry_t` |
| 509 | `kSysWiniosWindowSetGeometry` | `X0`: window_id, `X1`: in_geom_va | 0 on success | Reads 32-byte `winios_window_geometry_t` |
| 510 | `kSysWiniosWindowSetFocus` | `X0`: window_id | 0 on success | Updates active desktop focus |
| 511 | `kSysWiniosWindowList` | `X0`: out_list_va | 0 on success | Writes 136-byte `winios_window_list_t` |
| 512 | `kSysWiniosCreateTimer` | `X0`: interval_ms, `X1`: is_periodic | Timer ID in `X0` | Non-zero interval validation |
| 513 | `kSysWiniosAllocHeap` | `X0`: size, `X1`: alignment | Guest heap address in `X0` | Checks heap quota & bounds |
| 514 | `kSysWiniosFreeHeap` | `X0`: ptr | 0 on success, or `EINVAL` | Validates pointer & double-free |
| 515 | `kSysWiniosGetRuntimeInfo` | `X0`: out_info_va | 0 on success | Writes 24-byte `winios_runtime_info_t` |
| 516 | `kSysWiniosStorageRead` | `X0`: in_req_va, `X1`: guest_buf, `X2`: max_len | Bytes read in `X0` | Traversal check (`..`), isolated storage |
| 517 | `kSysWiniosStorageWrite` | `X0`: in_req_va, `X1`: guest_buf, `X2`: len | Bytes written in `X0` | Traversal check (`..`), isolated storage |
| 518 | `kSysWiniosIpcCreatePort` | `X0`: name_va | Port ID in `X0` | Reads port name, bounds port count |
| 519 | `kSysWiniosIpcSend` | `X0`: in_desc_va | 0 on success, or `EINVAL` | Reads 24-byte desc, checks queue limits |
| 520 | `kSysWiniosIpcRecv` | `X0`: port_id, `X1`: out_desc_va, `X2`: buf, `X3`: max_len | 0 on success, or `ENOENT` | Writes 24-byte desc + payload |
| 521 | `kSysWiniosPrefGetSet` | `X0`: op (0=get, 1=set), `X1`: entry_va | 0 on success | Reads/writes 32-byte `winios_pref_entry_t` |
