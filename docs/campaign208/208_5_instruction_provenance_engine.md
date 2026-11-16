# WinIOS Stage 208.5: Guest Instruction Provenance Engine

## 1. Engine Purpose & Architecture
The Guest Instruction Provenance Engine provides cycle-accurate, instruction-level accountability for every retired instruction in the WinIOS guest CPU emulator. Rather than relying solely on aggregate retirement counters, the provenance engine annotates each execution event with:
- **Sequence Number (`seq_num`):** Monotonically increasing retirement counter.
- **Guest PC (`pc`):** Virtual Program Counter at instruction execution.
- **Opcode Bytes (`opcode`):** 32-bit little-endian ARM64 instruction encoding.
- **Disassembly Mnemonic (`mnemonic`):** Architectural assembly representation.
- **Binary Hash (`binary_sha256`):** SHA-256 of the loaded guest Mach-O executable.
- **Mach-O Segment (`segment`):** Memory segment (`__TEXT` or `__DATA`).
- **Symbol / Function (`symbol`):** Containing C or assembly symbol.
- **Source File (`source_file`):** In-tree source and public XNU counterpart.
- **Provenance Category (`category`):** Category A (Genuine), Category B (Adapted), Category C (Compatibility), or Category D (Synthetic).

## 2. Provenance Category Classifications for XNU Bootstrap
- `0x1BA0 - 0x1BD0` (`__start` / `_start`): **Category B (Adapted Assembly)** - derived from `osfmk/arm64/start.s`.
- `0x14DC - 0x1620` (`arm_init`): **Category B (Adapted C Runtime)** - derived from `osfmk/arm64/arm_init.c`.
- `0x1B8C - 0x1B9C` (`pe_arm_init`): **Category A/B (Direct Source-Derived)** - derived from `pexpert/arm/pe_bootargs.c`.
- `0x19B4 - 0x1A80` (`PE_init_platform`): **Category B (Adapted C Runtime)** - derived from `pexpert/arm/pe_init.c`.
- `0x1480 - 0x14DC` (`arm_vm_init_early`): **Category B/C (Adapted Page Stealer)** - derived from `osfmk/arm64/arm_vm_init.c`.
- `0x1620 - 0x1750` (`pmap_bootstrap`): **Category B (Adapted Physical Memory Allocator)** - derived from `osfmk/arm64/pmap/pmap.c`.

## 3. Data Flow & Replay Integration
The provenance engine writes machine-readable traces to `docs/campaign208/208_instruction_provenance_trace.json`, enabling external deterministic auditing without reliance on host runtime assumptions.
