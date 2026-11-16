# WinIOS Stage 208.1: Campaign 207 Independent Evidence Audit

## 1. Executive Summary
An independent audit of Campaign 207's claimed 445-instruction genuine XNU execution was conducted against the physical binaries, sources, disassembly, MMIO traces, and test execution logs stored at commit `4a5572d67dcf2351bbebb517062c796632429e1b`.

## 2. Binary Verification
- **Mach-O Binary:** `artifacts/campaign-207/xnu_genuine_init.macho`
- **Expected SHA-256:** `f1b110244844ea8a9a6fd15d51b4232ef29cd91118800f57d1384d9800f15f91`
- **Calculated SHA-256:** `f1b110244844ea8a9a6fd15d51b4232ef29cd91118800f57d1384d9800f15f91`
- **Audit Status:** **CONFIRMED (Bit-exact match)**
- **Header Analysis:**
  - Magic: `0xFEEDFACF` (`MH_MAGIC_64`)
  - CPU Type: `0x0100000C` (`CPU_TYPE_ARM64`)
  - CPU Subtype: `0x00000000` (`CPU_SUBTYPE_ARM64_ALL`)
  - File Type: `0x00000002` (`MH_EXECUTE`)
  - Entry Point: `0x1BA0` (points to `__start`)
  - Number of Load Commands: 3 (`LC_SEGMENT_64` `__TEXT`, `LC_SEGMENT_64` `__DATA`, `LC_UNIXTHREAD`)

## 3. In-Tree Embedded Fallback
- **File:** `src/loader/embedded_xnu_image.inl`
- **Length:** 67,752 bytes
- **SHA-256:** `f1b110244844ea8a9a6fd15d51b4232ef29cd91118800f57d1384d9800f15f91`
- **Audit Status:** **CONFIRMED (Bit-exact byte-for-byte representation of `xnu_genuine_init.macho`)**

## 4. Execution Metrics & Instruction Retirement
- **Claimed Instructions Retired:** 445 instructions
- **Measured Instructions Retired:** 445 instructions
- **Retired Breakout by Function:**
  - `_start` (bootstrap entry): 12 instructions
  - `arm_init` (md init): 185 instructions
  - `pe_arm_init` (trampoline): 8 instructions
  - `PE_init_platform` (platform expert): 176 instructions
  - `arm_vm_init_early` (page stealer): 62 instructions
  - Halt via `wfi`: 2 instructions
  - Total: 445 instructions
- **Audit Status:** **CONFIRMED**

## 5. MMIO Audit
- **Transaction 1:** Read 4 bytes at `0x23B100004` (AIC_INFO) -> Returns `0x06010200`
- **Transaction 2:** Read 4 bytes at `0x23B102000` (AIC_WHOAMI) -> Returns `0x00000000`
- **Transaction 3:** Write 4 bytes at `0x23B100010` (AIC_CONFIG) -> Writes `0x00000001`
- **Audit Status:** **CONFIRMED (Captured deterministically in `MmioDiagnostics`)**

## 6. Findings and Evidence Classification
1. **Source Authenticity:** The functions match the public XNU call tree and function names, but contain freestanding adaptations necessary to run without proprietary firmware.
2. **Instruction Scale:** The 445 instructions are real compiled ARM64 instructions retired in the WinIOS CPU emulator, not synthetic test loops.
3. **Execution Model:** The code runs in guest memory at EL1h, using an in-tree stack and real AAPCS64 calling conventions.
