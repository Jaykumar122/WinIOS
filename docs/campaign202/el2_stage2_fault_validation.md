# WinIOS Campaign 202 Stage 19: EL2 Stage-2 Fault Architecture & Guest Recovery Report

## 1. Executive Summary

Phase D (Stages 202.16–202.19) of Campaign 202 implemented and validated the ARM64 EL2 Stage-2 fault injection and guest recovery architecture (`winios::mmu::Stage2FaultInjector` and `winios::mmu::Stage2TranslationEngine`).

The implementation establishes full fidelity with ARM Architecture Reference Manual (DDI 0487) EL2 exception syndrome reporting and Stage-2 translation fault handling:
1. **Configurable Stage-2 Fault Injector:** Support for single-shot and multi-shot fault injection across translation, permission, and access-flag faults at levels 0, 1, 2, and 3.
2. **Architectural EL2 Syndrome Synthesis:** Precise calculation of `ESR_EL2` (Exception Class `0x24` for Data Abort from lower EL, `0x20` for Instruction Abort from lower EL), `IL=1`, `WnR` write indicator, and Fault Status Codes (FSC).
3. **Register Reporting Fidelity:** Accurate population of `FAR_EL2` (guest virtual address) and `HPFAR_EL2` (intermediate physical address shifted by 8: `HPFAR_EL2[39:4] = IPA[47:12]`).
4. **Complete Fault-Trap-Resolve-Replay Loop:** Verified the full execution sequence:
   - Guest EL1 access faults at Stage-2.
   - Execution traps to EL2 hypervisor with valid syndromes.
   - Hypervisor establishes missing physical page mapping via `map_page_16k`.
   - Simulated `ERET` returns execution to guest EL1.
   - Guest re-executes instruction successfully.

---

## 2. Syndrome and Register Encoding Matrix

| Fault Type | Target Level | FSC (DFSC/IFSC) | ESR_EL2 (Read) | ESR_EL2 (Write) | ESR_EL2 (Exec) | HPFAR_EL2 Formula |
|---|---|---|---|---|---|---|
| Translation | Level 0 | `0x04` | `0x92000004` | `0x92000044` | `0x86000004` | `(IPA >> 8) & 0xFFFFFFFFF000` |
| Translation | Level 1 | `0x05` | `0x92000005` | `0x92000045` | `0x86000005` | `(IPA >> 8) & 0xFFFFFFFFF000` |
| Translation | Level 2 | `0x06` | `0x92000006` | `0x92000046` | `0x86000006` | `(IPA >> 8) & 0xFFFFFFFFF000` |
| Translation | Level 3 | `0x07` | `0x92000007` | `0x92000047` | `0x86000007` | `(IPA >> 8) & 0xFFFFFFFFF000` |
| Access Flag | Level 1 | `0x09` | `0x92000009` | `0x92000049` | `0x86000009` | `(IPA >> 8) & 0xFFFFFFFFF000` |
| Access Flag | Level 2 | `0x0A` | `0x9200000A` | `0x9200004A` | `0x8600000A` | `(IPA >> 8) & 0xFFFFFFFFF000` |
| Access Flag | Level 3 | `0x0B` | `0x9200000B` | `0x9200004B` | `0x8600000B` | `(IPA >> 8) & 0xFFFFFFFFF000` |
| Permission | Level 1 | `0x0D` | `0x9200000D` | `0x9200004D` | `0x8600000D` | `(IPA >> 8) & 0xFFFFFFFFF000` |
| Permission | Level 2 | `0x0E` | `0x9200000E` | `0x9200004E` | `0x8600000E` | `(IPA >> 8) & 0xFFFFFFFFF000` |
| Permission | Level 3 | `0x0F` | `0x9200000F` | `0x9200004F` | `0x8600000F` | `(IPA >> 8) & 0xFFFFFFFFF000` |
| Address Size| Level 0 | `0x00` | `0x92000000` | `0x92000040` | `0x86000000` | `(IPA >> 8) & 0xFFFFFFFFF000` |

---

## 3. Verification Test Evidence (`stage202_19_el2_stage2_fault_tests`)

```
=== Running Stage 202.19 EL2 Stage-2 Fault Architecture Tests ===
[Test 1] Stage-2 Translation Fault Injection & Syndrome Generation...
  -> Translation fault injection & syndromes passed.
[Test 2] Stage-2 Permission Fault Injection & FSC Codes...
  -> Permission fault injection passed.
[Test 3] Stage-2 Access Flag Fault Injection & FSC Codes...
  -> Access flag fault injection passed.
[Test 4] End-to-End EL1 Fault -> EL2 Trap -> Fix -> ERET -> Replay...
  -> End-to-end EL1 fault -> EL2 trap -> fix -> ERET -> replay passed.
[Test 5] Stage-2 Address Size Faults & Boundary Enforcement...
  -> Address size faults passed.
All Stage 202.19 tests passed successfully!
```

Zero warnings were reported under `/W4 /WX /permissive-`.
