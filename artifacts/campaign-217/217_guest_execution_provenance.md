# WINIOS CAMPAIGN 217 — GUEST EXECUTION PROVENANCE & FORENSIC AUDIT

**Date:** November 18, 2026  
**Campaign:** 217  
**Host Architecture:** Windows 11 x64  
**Guest Architecture:** ARM64 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Cross Compiler:** Zig 0.17.0 (`-target aarch64-macos`)  
**Baseline Commit:** `db6f6827a0d8532a20b5c6c241a1280d7ef6248d`

---

## 1. Provenance Architecture

WinIOS maintains strict forensic separation between host simulation and genuine guest CPU execution. 

```
+--------------------------------------------------------------------------------+
|                        Physical Host Input (Win32 API)                         |
|                    WM_KEYDOWN, WM_MOUSEMOVE, WM_LBUTTONDOWN                    |
+--------------------------------------------------------------------------------+
                                       |
                                       v
+--------------------------------------------------------------------------------+
|                      WinIOS Host Event Ingestion Engine                        |
|              Attaches Provenance Tag: EventSourceProvenance::kPhysicalWindows  |
+--------------------------------------------------------------------------------+
                                       |
                                       v
+--------------------------------------------------------------------------------+
|                             GuestEventLoop                                     |
|               Preserves Provenance Tag & Delivers to Event Queue               |
+--------------------------------------------------------------------------------+
                                       |
                                       v
+--------------------------------------------------------------------------------+
|                        Guest Application (ARM64 EL0)                           |
|        Executes genuine ARM64 Mach-O instructions retired via CPU engine       |
+--------------------------------------------------------------------------------+
```

---

## 2. Instruction Retirement Verification

During the verified 20-step end-to-end developer workflow (`stage217_23_developer_e2e_tests`):
- `runtime->Step(2048)` was executed.
- Verified `runtime->GetRetiredInstructions() == 2048`.
- Guest PC progressed monotonically across genuine ARM64 instructions without unhandled instruction traps or emulation skips.
- The input event was validated with `EventSourceProvenance::kPhysicalWindows`.

---

## 3. Toolchain & Binary Provenance

- **Compiler:** Zig 0.17.0 on Windows host.
- **Compilation Command:**
  ```cmd
  zig cc -target aarch64-macos -nostdlib -fno-sanitize=undefined -O2 -Wl,-e,__start <source.c> -o <binary.macho>
  ```
- **Mach-O Header Verification:**
  - Magic: `0xFEEDFACF` (`MH_MAGIC_64`)
  - CPU Type: `0x0100000C` (`CPU_TYPE_ARM64`)
  - CPU Subtype: `0x00000000` (`CPU_SUBTYPE_ARM64_ALL`)
  - File Type: `0x00000002` (`MH_EXECUTE`)
  - Load Commands: `LC_SEGMENT_64` (`__PAGEZERO`, `__TEXT`, `__LINKEDIT`), `LC_MAIN`.
- **Packaging:** Packages are assembled by `tools/wpack.py` into valid ZIP `.wios` archives with SHA256 integrity checks.
