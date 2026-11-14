# WinIOS Campaign 197: ARMv8-A Barrier Semantics & CRM Encoding Matrix

**Document ID:** `docs/campaign197/barrier_semantics_matrix.md`  
**Classification:** `GUEST_MEMORY_MODEL`  
**Architecture:** ARMv8-A / ARMv8.2-A (AArch64)  
**Standard References:** ARM DDI 0487 Section C6.2.79 (DMB), C6.2.80 (DSB), C6.2.117 (ISB), B2.3 (Barrier operations)

---

## 1. Executive Summary

ARMv8-A distinguishes three fundamentally distinct barrier instructions:
- **DMB (Data Memory Barrier):** Orders memory accesses before and after the barrier in the global memory observation order without requiring their completion before subsequent instructions execute.
- **DSB (Data Synchronization Barrier):** Enforces completion of prior memory accesses before any subsequent instruction can execute, draining store buffers and awaiting memory system acknowledgments.
- **ISB (Instruction Synchronization Barrier):** Synchronizes the instruction stream and execution context (flushes the instruction prefetch buffer and MMU TLB state), but performs **no** data memory ordering.

In WinIOS Campaign 197, these three barriers are decoupled and architecturally separated, eliminating the legacy conflation of barriers with indiscriminate host fences (`seq_cst`).

---

## 2. Complete Barrier Encoding & Option Matrix

ARMv8-A encodes barrier options in the `CRm` field (bits [11:8]) of the `MSR/SYS` instruction space (`op0=01, op1=011, CRn=0111, op2={100, 101, 110}`).

| Opcode | Mnemonic | op2 | CRm (Bits [11:8]) | Domain | Access Type | Architectural Semantics |
|---|---|---|---|---|---|---|
| `0xD50331BF` | `DMB OSHLD` | `5` | `0b0001` (1) | Outer Shareable | Reads | Load-Load, Load-Store ordering across Outer Shareable domain |
| `0xD50332BF` | `DMB OSHST` | `5` | `0b0010` (2) | Outer Shareable | Writes | Store-Store ordering across Outer Shareable domain |
| `0xD50333BF` | `DMB OSH`   | `5` | `0b0011` (3) | Outer Shareable | All | Any-Any ordering across Outer Shareable domain |
| `0xD50330BF` | *Reserved*  | `5` | `0b0100` (4) | Unallocated | N/A | Undefined instruction exception |
| `0xD50335BF` | `DMB NSHLD` | `5` | `0b0101` (5) | Non-Shareable | Reads | Load-Load, Load-Store ordering within Non-Shareable domain |
| `0xD50336BF` | `DMB NSHST` | `5` | `0b0110` (6) | Non-Shareable | Writes | Store-Store ordering within Non-Shareable domain |
| `0xD50337BF` | `DMB NSH`   | `5` | `0b0111` (7) | Non-Shareable | All | Any-Any ordering within Non-Shareable domain |
| `0xD50330BF` | *Reserved*  | `5` | `0b1000` (8) | Unallocated | N/A | Undefined instruction exception |
| `0xD50339BF` | `DMB ISHLD` | `5` | `0b1001` (9) | Inner Shareable | Reads | Load-Load, Load-Store ordering across Inner Shareable domain |
| `0xD5033ABF` | `DMB ISHST` | `5` | `0b1010` (10) | Inner Shareable | Writes | Store-Store ordering across Inner Shareable domain |
| `0xD5033BBF` | `DMB ISH`   | `5` | `0b1011` (11) | Inner Shareable | All | Any-Any ordering across Inner Shareable domain |
| `0xD50330BF` | *Reserved*  | `5` | `0b1100` (12) | Unallocated | N/A | Undefined instruction exception |
| `0xD5033DBF` | `DMB LD`    | `5` | `0b1101` (13) | Full System | Reads | Load-Load, Load-Store ordering across Full System |
| `0xD5033EBF` | `DMB ST`    | `5` | `0b1110` (14) | Full System | Writes | Store-Store ordering across Full System |
| `0xD5033FBF` | `DMB SY`    | `5` | `0b1111` (15) | Full System | All | Any-Any ordering across Full System |
| `0xD503319F` | `DSB OSHLD` | `4` | `0b0001` (1) | Outer Shareable | Reads | Waits for completion of Outer Shareable read accesses |
| `0xD503329F` | `DSB OSHST` | `4` | `0b0010` (2) | Outer Shareable | Writes | Waits for completion of Outer Shareable write accesses (drains store buffer) |
| `0xD503339F` | `DSB OSH`   | `4` | `0b0011` (3) | Outer Shareable | All | Waits for completion of all Outer Shareable accesses |
| `0xD503359F` | `DSB NSHLD` | `4` | `0b0101` (5) | Non-Shareable | Reads | Waits for completion of Non-Shareable read accesses |
| `0xD503369F` | `DSB NSHST` | `4` | `0b0110` (6) | Non-Shareable | Writes | Waits for completion of Non-Shareable write accesses |
| `0xD503379F` | `DSB NSH`   | `4` | `0b0111` (7) | Non-Shareable | All | Waits for completion of all Non-Shareable accesses |
| `0xD503399F` | `DSB ISHLD` | `4` | `0b1001` (9) | Inner Shareable | Reads | Waits for completion of Inner Shareable read accesses |
| `0xD5033A9F` | `DSB ISHST` | `4` | `0b1010` (10) | Inner Shareable | Writes | Waits for completion of Inner Shareable write accesses |
| `0xD5033B9F` | `DSB ISH`   | `4` | `0b1011` (11) | Inner Shareable | All | Waits for completion of all Inner Shareable accesses |
| `0xD5033D9F` | `DSB LD`    | `4` | `0b1101` (13) | Full System | Reads | Waits for completion of Full System read accesses |
| `0xD5033E9F` | `DSB ST`    | `4` | `0b1110` (14) | Full System | Writes | Waits for completion of Full System write accesses |
| `0xD5033F9F` | `DSB SY`    | `4` | `0b1111` (15) | Full System | All | Full completion barrier (drains all pending stores to memory) |
| `0xD5033FDF` | `ISB SY`    | `6` | `0b1111` (15) | N/A | Pipeline/Context | Synchronizes instruction stream, flushes pipeline, invalidates MMU cache |
| Any other | `ISB <other>`| `6` | `< 15` | N/A | N/A | Undefined instruction exception |

---

## 3. Behavioral Comparison Matrix

| Property | DMB | DSB | ISB |
|---|---|---|---|
| **Primary Architectural Function** | Memory Observation Order | Memory Access Completion | Context & Instruction Stream Synchronization |
| **Drains Store Buffer to Memory?** | **No** (maintains relative order only) | **Yes** (forces completion) | **No** |
| **Blocks Instruction Execution?** | No (allows pipelined issue) | Yes (halts execution until completion) | Yes (flushes pipeline/prefetch) |
| **Flushes MMU TLB / Prefetch?** | No | No | **Yes** |
| **Applies to Data Memory?** | Yes | Yes | **No** |
| **Valid CRM Options** | 12 valid options (1,2,3,5,6,7,9,10,11,13,14,15) | 12 valid options (1,2,3,5,6,7,9,10,11,13,14,15) | **1 valid option (15 / SY only)** |
| **Undefined Instruction Behavior** | CRM in {0, 4, 8, 12} triggers Undefined Instruction | CRM in {0, 4, 8, 12} triggers Undefined Instruction | CRM != 15 triggers Undefined Instruction |

---

## 4. Verification Evidence

- Executed by test binary: `stage197_3_barrier_semantics_tests.exe`
- Result: **100% Pass** (Zero warnings, zero failures).
