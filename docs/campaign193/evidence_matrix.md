# Campaign 193 — Evidence Matrix

## 1. Evidence Classification Taxonomy
In strict accordance with WinIOS Campaign rules, every technical claim and architectural capability is classified into one of the following mutually exclusive categories:

1. **`HOST_MODEL`**: Host-side emulation, C++ mock, or test fixture.
2. **`GUEST_EL0`**: Authentic ARM64 instructions executed by the guest CPU core at unprivileged EL0.
3. **`GUEST_EL1`**: Authentic ARM64 instructions executed by the guest CPU core at privileged EL1.
4. **`GUEST_EXCEPTION`**: Architectural exception entry triggered directly by a guest instruction or fault.
5. **`GUEST_INTERRUPT`**: Asynchronous interrupt delivered through the guest GICv3 interrupt architecture.
6. **`SYNTHETIC_KERNEL`**: Freestanding ARM64 kernel runtime compiled independently with Zig 0.17.0.
7. **`APPLE_KERNEL`**: Authentic Apple XNU kernel execution. **NOT CLAIMED / NOT IMPLEMENTED**.

---

## 2. Evidence Mapping Table

| Stage | Feature / Capability | Classification | Primary Evidence & Verifier |
| :--- | :--- | :--- | :--- |
| **193.0** | Baseline Test Suite & Architecture Inventory | `HOST_MODEL` | 348 passed / 1 skipped in Debug & Release |
| **193.1** | Execution Level Query & Privilege Enforcement | `GUEST_EL0` / `GUEST_EL1` | `stage193_1_execution_level_tests` (5/5 pass) |
| **193.2** | Centralized Exception Delivery & PSTATE Save | `GUEST_EXCEPTION` | `stage193_2_exception_entry_tests` (5/5 pass) |
| **193.3** | Exception Syndrome Register (ESR_EL1, ISS, ISV) | `GUEST_EXCEPTION` | `stage193_3_syndrome_register_tests` (4/4 pass) |
| **193.4** | Architectural Vector Table & Alignment (VBAR_EL1) | `GUEST_EXCEPTION` | `stage193_4_vector_table_tests` (6/6 pass) |
| **193.5** | ERET Exception Return & Context Restoration | `GUEST_EL1` -> `GUEST_EL0` | `stage193_5_eret_return_tests` (5/5 pass) |
| **193.6** | System Register Privilege Enforcement (EL0 Trap EC 0x00)| `GUEST_EXCEPTION` | `stage193_6_sysreg_privilege_tests` (5/5 pass) |
| **193.7** | 16KB Stage-1 MMU Fault Levels (L2/L3 Translation) | `GUEST_EXCEPTION` | `stage193_7_mmu_translation_fault_tests` (4/4 pass) |
| **193.8** | TLB Caching & Invalidation (TLBI VAE1, ASIDE1) | `GUEST_EL1` / `HOST_MODEL` | `stage193_8_tlb_invalidation_tests` (5/5 pass) |
| **193.9** | ASID Address-Space Isolation (TTBR0/1, TCR.A1) | `GUEST_EL0` | `stage193_9_asid_isolation_tests` (3/3 pass) |
| **193.10**| EL0/EL1 Memory Protection & W^X (WXN, UXN, PXN) | `GUEST_EXCEPTION` | `stage193_10_memory_protection_tests` (6/6 pass) |
| **193.11**| Banked Stack Pointers & SP Alignment Fault (EC 0x26)| `GUEST_EXCEPTION` | `stage193_11_stack_management_tests` (4/4 pass) |
| **193.12**| GICv3 Distributor & CPU Interface (ICC_IAR1/EOIR1) | `GUEST_INTERRUPT` | `stage193_12_gic_delivery_tests` (4/4 pass) |
| **193.13**| IRQ/FIQ Vector Routing & PSTATE DAIF Masking | `GUEST_INTERRUPT` | `stage193_13_irq_routing_tests` (4/4 pass) |
| **193.14**| Virtual Generic Timer Expiration (PPI 27 Delivery) | `GUEST_INTERRUPT` | `stage193_14_virtual_timer_tests` (4/4 pass) |
| **193.15**| Dual-Core Exception Isolation & SGI Delivery | `GUEST_INTERRUPT` / `GUEST_EXCEPTION` | `stage193_15_smp_exception_tests` (4/4 pass) |
| **193.16**| Freestanding ARM64 Kernel Execution | `SYNTHETIC_KERNEL` | `stage193_16_synthetic_kernel_tests` (3/3 pass, 178 retired) |
| **193.17**| Integrated Syscall, Paging & Timer Flow | `SYNTHETIC_KERNEL` / `GUEST_EXCEPTION` | `stage193_17_kernel_integration_tests` (4/4 pass) |
| **193.18**| Architectural Conformance Audit (ARM DDI 0487) | `HOST_MODEL` | `stage193_18_conformance_audit.md` |
| **193.19**| Full Regression Testing (Debug & Release) | `HOST_MODEL` | 365/366 passed in Debug and Release |

---

## 3. Strict Authenticity Verification
- **Zero Proprietary Apple Firmware Extraction:** WinIOS does NOT decrypt, extract, or redistribute genuine Apple Secure Enclave Processor (SEP) firmware, Darwin iboot/xnu proprietary kernels, or Apple development keys.
- **Independent Toolchain Exclusivity:** All compiled guest binaries are assembled and linked via Zig 0.17.0 (`clang`/`lld`).
- **Absence of Host Simulation Masking:** In Stage 193.16 and 193.17, guest execution progresses through instructions decoded and retired by the ARM64 guest engine (`CPU::step`), with privilege demotion via `ERET` and escalation via `SVC` / interrupts directly driven by architectural processor state.
