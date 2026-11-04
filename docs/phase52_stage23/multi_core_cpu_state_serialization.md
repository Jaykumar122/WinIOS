# Multi-Core CPU State Serialization

## 1. Scope and Coverage

The `CORE_ARCHITECTURAL_STATE` (Section Type `0x12`) serializes the complete supported architectural state of an individual virtual core as encapsulated by `SmpCoreState`.

Each registered core is serialized into an independent section descriptor with `section_id = core_id`.

---

## 2. Binary Layout Specification (1056 Bytes Fixed)

| Byte Range | Size | Field Group | Contents & Field Types |
|---|---|---|---|
| 0–39 | 40 B | Identification & Execution | `core_id` (u32), `lifecycle` (u32), `retire_count` (u64), `budget` (u64), `exec_in_quantum` (u64), `irq_pending` (u32), `reserved` (u32) |
| 40–287 | 248 B | General Purpose Registers | `X0`..`X30` (31 x u64 little-endian) |
| 288–327 | 40 B | Program Counter & Stack Pointers | `PC` (u64), `SP_EL0` (u64), `SP_EL1` (u64), `SP_EL2` (u64), `SP_EL3` (u64) |
| 328–335 | 8 B | Program Status State (PSTATE) | `nzcv` (u8), `daif` (u8), `sp_sel` (u8), `il` (u8), `ss` (u8), `current_el` (u8), `pad` (u16) |
| 336–855 | 520 B | Advanced SIMD & Floating Point | `V0`..`V31` (32 x 128-bit: low u64, high u64), `FPCR` (u32), `FPSR` (u32) |
| 856–887 | 32 B | EL1 Exception State | `ELR_EL1` (u64), `SPSR_EL1` (u32), `ESR_EL1` (u32), `FAR_EL1` (u64), `VBAR_EL1` (u64) |
| 888–927 | 40 B | EL2 Exception State | `ELR_EL2` (u64), `SPSR_EL2` (u32), `ESR_EL2` (u32), `FAR_EL2` (u64), `VBAR_EL2` (u64), `HCR_EL2` (u64) |
| 928–967 | 40 B | EL3 Exception State | `ELR_EL3` (u64), `SPSR_EL3` (u32), `ESR_EL3` (u32), `FAR_EL3` (u64), `VBAR_EL3` (u64), `SCR_EL3` (u64) |
| 968–1023 | 56 B | System Control Registers | `SCTLR_EL1` (u64), `TCR_EL1` (u64), `TTBR0_EL1` (u64), `TTBR1_EL1` (u64), `MAIR_EL1` (u64), `CPACR_EL1` (u64), `CNTVOFF_EL2` (u64) |
| 1024–1055 | 32 B | Per-Core Virtual Timers | `CNTP_CVAL_EL0` (u64), `CNTP_CTL_EL0` (u32), `pad0` (u32), `CNTV_CVAL_EL0` (u64), `CNTV_CTL_EL0` (u32), `pad1` (u32) |

---

## 3. Explicit Exclusions

The following elements are explicitly excluded from serialization:
- Host pointer to virtual CPU (`CPU*`, `std::shared_ptr<CPU>`).
- Host pointer to shared memory (`SmpSharedMemory*`).
- Debug architecture telemetry and sink interfaces.
- Host thread IDs and OS synchronization primitives.
- Dynamic heap allocation tracking.

---

## 4. Verification and Validation Rules

On deserialization:
1. Payload size must be at least 1056 bytes.
2. `core_id` must match `section_id` in descriptor.
3. `lifecycle` must be a valid `CoreLifecycleState` enum value (0..4).
4. `current_el` must be $\le 3$.
5. Floating-point control and status registers are validated against architectural bitmasks.
