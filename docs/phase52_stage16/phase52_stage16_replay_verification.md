# WinIOS Phase 52, Stage 16: Replay State Verification and Divergence Diagnostics

**Document Identifier:** `WINIOS-SPEC-52-16-005`  
**Status:** Approved Technical Specification  
**Date:** 2026-11-04  
**Author:** WinIOS System Architecture Team  
**Classification:** Core System Architecture Specification  

---

## 1. Objectives of Replay Verification

In a deterministic record-and-replay system, **replay validity** requires that every state transition in the replayed execution is identical to the original recorded execution:
$$\forall N \in [0, N_{\text{max}}], \quad \text{State}_{\text{Replay}}(N) \equiv \text{State}_{\text{Record}}(N)$$

A **divergence** occurs when the replayed CPU or memory state deviates by even a single bit from the recorded execution. If unchecked, a single divergent bit in a general-purpose register will quickly propagate into divergent memory writes, corrupted branch conditions, and completely erratic control flow.

The WinIOS **VerificationEngine** provides:
1. **Periodic Keyframe Hashing:** Computes compact, high-speed 64-bit state hashes during execution and embeds them into the `.sndl` log.
2. **Online Divergence Detection:** Compares the replayed state against the embedded keyframe hashes in real time.
3. **Automated Divergence Triage:** Instantly freezes execution at the point of divergence, isolates the exact register or memory address that differed, and generates an actionable forensic report.

---

## 2. Keyframe State Hashing Specification

To verify execution without recording or transferring hundreds of megabytes of raw register files every few seconds, the engine generates periodic **Keyframe Hashes** every $K$ instructions (default: $K = 100,000$ retired instructions).

### 2.1 State Components Included in Keyframe Hash
The keyframe state hash is computed over a contiguous, packed memory buffer containing:
1. **CPU Core State (Packed Canonical Representation):**
   - Core ID (4 bytes)
   - Instruction Retirement Counter (8 bytes)
   - Program Counter `PC` (8 bytes)
   - General-Purpose Registers `X0`–`X30` (248 bytes)
   - Stack Pointers `SP_EL0`, `SP_EL1`, `SP_EL2`, `SP_EL3` (32 bytes)
   - Processor State `PSTATE` (NZCV, DAIF, CurrentEL, PAN, UAO) (16 bytes)
   - SIMD Registers `V0`–`V31` (512 bytes)
   - FPCR, FPSR (8 bytes)
   - EL1 System Registers (`SCTLR`, `TCR`, `TTBR0`, `TTBR1`, `MAIR`, `VBAR`, `ELR`, `SPSR`, `ESR`, `FAR`) (80 bytes)
   - Local Timer State (`phys_timer.cval`, `virt_timer.cval`, status flags) (32 bytes)
   - Total packed register buffer size: **948 bytes**.
2. **Guest Memory State:**
   - Full memory hashing across several gigabytes on every keyframe would impose unacceptable latency (~50–100ms per keyframe).
   - **Incremental Rolling Memory Hash:** The memory subsystem tracks a dirty page bitmask. At keyframe time, only pages modified during the last quantum are hashed into the running memory accumulator.

### 2.2 Hash Algorithm Trade-Off Analysis

| Algorithm | Digest Size | Throughput (Host x86_64) | Collision Resistance | Architectural Suitability |
|---|:---:|:---:|:---:|---|
| **CRC32C** | 32 bits | ~15 GB/s (Hardware SSE4.2) | Low ($2^{-32}$, accidental matches possible) | Insufficient digest width for large test runs |
| **xxHash64** | 64 bits | ~12 GB/s (Optimized scalar/AVX2) | High ($2^{-64}$, zero accidental collisions) | **Optimal for online keyframe hashing** |
| **Blake3** | 256 bits | ~3.5 GB/s (SIMD tree hash) | Cryptographic ($2^{-256}$) | Reserved for full snapshot archive integrity |
| **SHA-256** | 256 bits | ~0.8 GB/s (Hardware SHA-NI) | Cryptographic ($2^{-256}$) | Snapshot manifest file verification |

**Selection:** WinIOS selects **xxHash64** for runtime keyframe hashes in the `.sndl` stream due to its exceptional throughput (>10 GB/s) and collision resistance.

```cpp
uint64_t ComputeCpuStateHash(const CPU& cpu) {
    alignas(16) uint8_t buffer[1024];
    size_t packed_bytes = PackCanonicalCpuState(cpu, buffer, sizeof(buffer));
    return XXH64(buffer, packed_bytes, 0x57494E494F53ULL /* "WINIOS" seed */);
}
```

---

## 3. Divergence Detection and Forensic Reporting

When the replayed CPU reaches instruction retirement count $N$, the `VerificationEngine` computes the live state hash $H_{\text{live}}$ and compares it with the recorded keyframe hash $H_{\text{log}}$ from the `.sndl` record.

```
       Replay Engine Steps to Instruction Count N
                         |
                         v
          Live CPU Computes xxHash64 State Hash
                         |
                         v
       Compare Live Hash vs Logged Keyframe Hash
                     /       \
          Hashes Match       Hashes Differ!
               |                   |
               v                   v
        Continue Replay    [DIVERGENCE DETECTED]
                                   |
                                   v
                    1. Freeze Simulation Clock
                    2. Capture Forensic Snapshot
                    3. Generate Divergence Report
                    4. Terminate Replay with Code -1
```

### 3.1 Divergence Diagnostic Bundle
Upon detecting a divergence, WinIOS generates a forensic triage folder `divergence_<timestamp>/` containing:
1. `divergence_report.json`: Machine-readable breakdown of the divergence point.
2. `core_<id>_state_actual.txt`: Human-readable register dump of the live divergent core.
3. `instruction_history.log`: Ring buffer trace of the last 128 instructions retired immediately prior to the divergence.
4. `memory_diff.bin`: Delta of modified memory pages relative to the base snapshot.

### 3.2 Machine-Readable Divergence Report Schema (`divergence_report.json`)

```json
{
  "report_version": 1,
  "system": "WinIOS",
  "status": "DIVERGENCE_DETECTED",
  "timestamp_ns": 1730718000000000000,
  "base_snapshot_path": "checkpoint_0000.wsnap",
  "sndl_log_path": "session.sndl",
  "divergence_details": {
    "core_id": 0,
    "instruction_retirement_count": 52400000,
    "expected_state_hash": "0xA8F231D9B04E5510",
    "actual_state_hash": "0x33C1488AEF09712B",
    "program_counter": "0x0000000040105820",
    "disassembly_at_pc": "ldr x0, [x1, #8]",
    "isolated_differences": [
      {
        "component": "GPR",
        "register": "X0",
        "expected_value": "0x0000000000000000",
        "actual_value": "0x0000000000000001"
      },
      {
        "component": "PSTATE",
        "flag": "Z",
        "expected_value": 1,
        "actual_value": 0
      }
    ],
    "last_retired_instructions": [
      "0x40105810: mov x1, #0x2000",
      "0x40105814: ldr x2, [x1]",
      "0x40105818: cmp x2, #0",
      "0x4010581C: b.ne 0x40105830",
      "0x40105820: ldr x0, [x1, #8]"
    ]
  }
}
```

---

## 4. Failure Handling and Recovery Policies

### 4.1 Divergence Classification
1. **Fatal Divergence (Default):**
   - Any mismatch in PC, general-purpose registers, or memory contents is fatal.
   - Simulation stops immediately. Replay cannot guarantee architectural correctness beyond this point.
2. **Benign / Non-Architectural Drift (Configurable for Debugging):**
   - Certain registers (such as PMU cycle counters or debug cycle timers) can be configured as "lax" in debug mode. If only cycle counters differ while all architectural registers (X0–X30, PC, Memory) match, the engine can log a warning and continue execution.
3. **Replay Invalidation on Missing Log Events:**
   - If the replay engine encounters an unlogged hardware access (e.g. guest code writes to a new peripheral address for which no MMIO read handler or log record exists), replay halts immediately with an `UNLOGGED_PERIPHERAL_ACCESS` error.
