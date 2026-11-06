# MMU Differential Validation Results
**Stage 1 Translation Cross-Check: Production MMU vs ReferencePageTableWalker**

---

## 1. Test Methodology

Differential validation executed identical translation workloads against both:
1. `winios::MMU` / `AArch64Mmu` (production engine with TLB caching and hardware walking).
2. `winios::ReferencePageTableWalker` (isolated, pure reference model).

Memory fixtures were dual-written to guarantee identical physical table structures.

---

## 2. Multi-Level Page Walk Conformance Matrix

| Test Case | Virtual Address | Expected Level | Output Physical Address | Production Status | Reference Status | Bit-for-Bit Match |
|---|---|---|---|---|---|---|
| Standard 4KB Page | `0x0000000000002050` | Level 3 | `0x0000000000040050` | SUCCESS | SUCCESS | YES |
| 2MB Block (Level 2) | `0x0000000000201000` | Level 2 | `0x0000000002001000` | SUCCESS | SUCCESS | YES |
| 1GB Block (Level 1) | `0x0000004000005000` | Level 1 | `0x0000004000005000` | SUCCESS | SUCCESS | YES |
| Unmapped VA (L0 Fault) | `0x0000700000000000` | Level 0 | N/A (Translation Fault) | FAULT (FSC 0x04) | FAULT (FSC 0x04) | YES |
| Unmapped VA (L2 Fault) | `0x0000000000800000` | Level 2 | N/A (Translation Fault) | FAULT (FSC 0x06) | FAULT (FSC 0x06) | YES |
| Access Flag Fault | `0x0000000000003000` | Level 3 | N/A (AF == 0) | FAULT (FSC 0x0B) | FAULT (FSC 0x0B) | YES |
| EL0 Write to RO Page | `0x0000000000004000` | Level 3 | N/A (Permission Fault) | FAULT (FSC 0x0F) | FAULT (FSC 0x0F) | YES |
| EL0 Exec of UXN Page | `0x0000000000005000` | Level 3 | N/A (Permission Fault) | FAULT (FSC 0x0F) | FAULT (FSC 0x0F) | YES |

---

## 3. Guest Memory Protection on Fault Verification

A critical architectural invariant is that a translation or permission fault **must never corrupt guest physical memory**.
In `mmu_conformance_tests` Test 3:
- A write was attempted through a read-only virtual mapping (`0x4000`) with payload `0xDEADBEEFCAFEBABELL`.
- The translation raised `PermissionFault` (FSC `0x0F`).
- Physical memory inspection of the underlying page confirmed initial value `0x0000000000000000LL` was preserved untouched.
- Status: **VERIFIED PASS**.
