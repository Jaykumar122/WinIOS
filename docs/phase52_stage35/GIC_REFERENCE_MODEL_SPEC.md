# GICv3 Reference Model Specification
**Deterministic Interrupt Evaluation, PMR Masking & Tie-Breaking Model**

---

## 1. Scope

`ReferenceGicModel` provides an independent, golden model for GICv3 interrupt evaluation across multicore topologies. It implements deterministic priority arbitration and strict tie-breaking without host thread nondeterminism.

---

## 2. Priority and Masking Invariants

In Arm GICv3:
- Priority values are 8-bit integers in $[0, 255]$.
- **Architectural Inversion:** Numerically lower values represent higher priority (0x00 is highest, 0xFF is lowest).
- **Priority Mask Register (`ICC_PMR_EL1`):** An interrupt with priority $P$ signals the virtual CPU if and only if:
  $$P < \text{PMR}$$
  If $P \ge \text{PMR}$, the interrupt is masked.

---

## 3. Deterministic Tie-Breaking Algorithm

When multiple interrupts simultaneously satisfy:
1. Interrupt is enabled in distributor/redistributor.
2. Interrupt is pending.
3. Interrupt is not active.
4. Priority passes PMR ($P < \text{PMR}$).
5. Routing targets the current core.

If multiple candidates have identical priority $P_a == P_b$:
$$\text{Winner} = \min(\text{INTID}_a, \text{INTID}_b)$$
The candidate with the **numerically lower INTID** is selected deterministically.

This rule eliminates race conditions and ensures repeatable replay across all topologies.
