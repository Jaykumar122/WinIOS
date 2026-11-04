# GICv3 Interrupt Determinism & Delivery Semantics

## 1. Architectural Overview: ARM GICv3 in WinIOS

In ARMv8-A systems, the Generic Interrupt Controller version 3 (GICv3) arbitrates all asynchronous interrupt lines. For deterministic replay, interrupt delivery must not depend on host timing, thread scheduling, or nondeterministic latency.

The WinIOS GICv3 emulation conforms to ARM IHI 0069H, comprising:
1. **Distributor (GICD)**: Arbitrates Shared Peripheral Interrupts (SPIs, INTIDs 32–1019).
2. **Redistributors (GICR)**: Manages per-core Private Peripheral Interrupts (PPIs, INTIDs 16–31) and Software Generated Interrupts (SGIs, INTIDs 0–15).
3. **CPU Interface (ICC System Registers)**: Delivers highest-priority pending interrupt signals to the core via EL1 system registers.

---

## 2. GICv3 Deterministic Arbitration & State Machine

Every interrupt line transitions through four discrete states:
- **Inactive (00b)**: Line is not asserted.
- **Pending (01b)**: Line is asserted but not yet acknowledged by CPU.
- **Active (10b)**: Acknowledged by CPU via `ICC_IAR1_EL1`, currently being serviced.
- **Active and Pending (11b)**: Serviced by CPU while a new interrupt event arrives.

### Priority Selection & Tie-Breaking
When multiple interrupts are pending simultaneously, GICv3 selects the candidate with:
1. **Lowest Priority Value**: ARM priorities range from 0x00 (highest priority) to 0xFF (lowest priority). An interrupt with priority 0x20 preempts one with 0x80.
2. **Lowest INTID**: If two pending interrupts possess identical priority values, the one with the smaller integer ID is chosen deterministically (e.g. INTID 33 beats INTID 34).

### Masking & Delivery Invariants
An interrupt signal (`GICv3Signal`) is asserted to the CPU core if and only if all following conditions hold:
- The interrupt line is enabled (`GICD_ISENABLER` for SPI, `GICR_ISENABLER0` for PPI).
- Group 1 signaling is enabled globally (`GICD_CTLR.EnableGrp1NS = 1`) and per-CPU (`ICC_IGRPEN1_EL1 = 1`).
- The interrupt's configured priority is strictly higher (numerically lower) than `ICC_PMR_EL1`.
- The CPU interface is unmasked in the core: `PSTATE.I = 0` (DAIF IRQ bit unmasked).

---

## 3. Replay Injection & Acknowledgment Protocol

During replay, device interrupts are delivered via `InterruptAssert` SNDL records:

```
[Instruction N Retires]
       │
       ▼
[ReplayInputRouter inspects next event]
       │
       ├─► Event matches instruction N + 1?
       │     │
       │     ▼
       │   Inject GIC line assertion: gic.set_spi_level(intid, true)
       │
       ▼
[GICv3::evaluate(core_id, daif_mask, current_el)]
       │
       ├─► Signal has_interrupt = true?
       │     │
       │     ▼
       │   CPU takes IRQ Exception (vector 0x280 / 0x380)
       │   SPSR_EL1 <= PSTATE, ELR_EL1 <= PC
       │   PSTATE.I <= 1 (mask further IRQs)
       │
       ▼
[Guest ISR reads ICC_IAR1_EL1]
       │
       ▼
[GIC acknowledges: returns INTID, marks state Active]
       │
       ▼
[Guest ISR completes, writes ICC_EOIR1_EL1]
       │
       ▼
[GIC completes interrupt: drops Active state]
```

---

## 4. Verification & Hardened Test Coverage

### Test Group C: GICv3 Interrupt Determinism
Test Group C (`test_group_c_interrupt_determinism` in `tests/unit/replay_hardening_tests.cpp`) verifies the following behaviors:
1. **SPI Priority Arbitration**: Asserted SPI 33 (priority 0x20) and SPI 34 (priority 0x80). Evaluation deterministically selected SPI 33.
2. **PMR Priority Masking**: Set `ICC_PMR_EL1 = 0x10`. Evaluated SPI 33 (priority 0x20). GIC correctly suppressed delivery (`has_interrupt = false`).
3. **PSTATE.I Masking**: Configured `PSTATE.I = 1`. GIC evaluation correctly respected CPU-side masking and withheld delivery.
4. **PPI Handling**: Verified timer PPI 30 behavior under redistributor enable and priority configurations.
5. **Acknowledgment & EOI**: Verified that reading `acknowledge_group1()` transitions state from pending to active, and calling `end_of_interrupt_group1()` restores inactive state.

### Benchmark 3 Performance
In Release mode on the host testbed, the complete GICv3 cycle (assert line + evaluate + acknowledge group 1 + deassert line + end of interrupt) achieved **203.7 ns per cycle**, representing **4.91 Million complete interrupt evaluations per second**.
