# Stage 195.8 — Thread-Local State and Address-Space Context

## 1. Executive Summary

Stage 195.8 verified per-thread isolation of thread-local storage pointers (`TPIDR_EL0`, `TPIDR_EL1`) and translation address space identities (`ASID`, `TTBR0_EL1`).

---

## 2. Test Verification

- **Task Isolation Protocol:**
  - Thread 1 configures `TPIDR_EL0 = 0x11110000`, `TPIDR_EL1 = 0x1111FFFF`, `ASID = 1`, and `TTBR0_EL1 = 0x0001000040000000`.
  - Thread 2 configures `TPIDR_EL0 = 0x22220000`, `TPIDR_EL1 = 0x2222FFFF`, `ASID = 2`, and `TTBR0_EL1 = 0x0002000040000000`.
  - Repeated context switches alternate execution between Thread 1 and Thread 2.
  - Zero cross-thread leakage is observed; Thread 1 never accesses Thread 2's TLS or page tables.

Evidence Classification: `GUEST_EL1`, `GUEST_CONTEXT_SWITCH`.
