# WinIOS Phase 52, Stage 17: Virtual Time and Peripheral Device Snapshot Specification

## 1. Scope and Rationale

To ensure deterministic execution replay and seamless snapshot resumption, the virtual timebase and peripheral interrupt controllers must be frozen and restored in exact synchrony with the CPU and memory state.

Any drift between the CPU's instruction count and the virtual timer counter (`cntpct_el0`), or loss of pending interrupt states in the Generic Interrupt Controller (GICv3), causes guest OS timer tick starvation or missed device notifications upon restore.

---

## 2. Virtual Time and Timer State (`Section 3`)

WinIOS decouples emulated execution time from host wall-clock time using the `VirtualTime` subsystem and AArch64 architectural timer registers.

### 2.1 Timer Payload Schema (Fixed 88 Bytes)

The `SnapshotSectionType::TimerState` payload layout:

| Offset | Field Name | Type | Size | Description |
|---|---|---|---|---|
| `+0x00` | `physical_counter` | `uint64_t` | 8 bytes | Base physical counter value `cntpct_el0`. |
| `+0x08` | `counter_frequency` | `uint64_t` | 8 bytes | Counter frequency in Hz (`cntfrq_el0`, typically 62.5 MHz). |
| `+0x10` | `virtual_offset` | `uint64_t` | 8 bytes | Virtual counter offset `cntvoff_el2`. |
| `+0x18` | `cntp_cval_el0` | `uint64_t` | 8 bytes | EL1 Physical Timer CompareValue register. |
| `+0x20` | `cntp_ctl_el0` | `uint32_t` | 4 bytes | EL1 Physical Timer Control register (ENABLE, IMASK, ISTATUS). |
| `+0x24` | `cntv_ctl_el0` | `uint32_t` | 4 bytes | EL1 Virtual Timer Control register. |
| `+0x28` | `cntv_cval_el0` | `uint64_t` | 8 bytes | EL1 Virtual Timer CompareValue register. |
| `+0x30` | `cnthp_cval_el2` | `uint64_t` | 8 bytes | EL2 Hypervisor Physical Timer CompareValue register. |
| `+0x38` | `cnthp_ctl_el2` | `uint32_t` | 4 bytes | EL2 Hypervisor Physical Timer Control register. |
| `+0x3C` | `reserved_timer` | `uint32_t` | 4 bytes | Alignment padding (zero-filled). |
| `+0x40` | `reserved_exp` | `uint8_t[24]`| 24 bytes | Reserved expansion area for future timer channels (zero-filled). |
| **TOTAL** | — | — | **88 B** | — |

### 2.2 Restoration Guarantees
- `VirtualTime::set_current_tick()` restores the exact tick count of the hardware model.
- Setting `cntfrq_el0` restores the hardware clock scale.
- Re-writing `cntp_ctl_el0`, `cntp_cval_el0`, `cntv_ctl_el0`, and `cntv_cval_el0` automatically recalibrates internal comparator triggers and timer interrupt asserts.

---

## 3. GICv3 Interrupt Controller State (`Section 4`)

The ARM Generic Interrupt Controller Architecture version 3 (GICv3) state is captured in `SnapshotSectionType::GicState`.

### 3.1 GICv3 Architecture Partitioning
WinIOS supports up to 1024 interrupts across three physical domains:
1. **GICD (Distributor)**: Global interrupt routing, group enables, priority masks, pending and active state bitmasks.
2. **GICR (Redistributor)**: Per-core SGIs (Interrupts 0–15) and PPIs (Interrupts 16–31), wake-up controls, and sleep status.
3. **ICC (CPU Interface)**: System register interface (`ICC_PMR_EL1`, `ICC_BPR1_EL1`, `ICC_CTLR_EL1`, `ICC_IGRPEN1_EL1`).

### 3.2 GIC Payload Schema (Fixed 1212 Bytes)

The distributor state payload has a fixed binary layout of **1212 bytes**:

```
+-------------------------------------------------------------------------+
| Offset     | Field Name       | Size     | Description                  |
+------------+------------------+----------+------------------------------+
| 0000..0003 | gicd_ctlr        | 4 bytes  | Distributor Control Register |
| 0004..0007 | gicd_status      | 4 bytes  | Status Register              |
| 0008..0011 | num_irqs         | 4 bytes  | Supported IRQ Count (1024)   |
| 0012..0015 | reserved_align   | 4 bytes  | Alignment padding            |
| 0016..0143 | igroupr          | 128 bytes| Group Registers (32 * 4B)    |
| 0144..0271 | isenabler        | 128 bytes| Enable-Set Registers (32 * 4)|
| 0272..0399 | ispendr          | 128 bytes| Pending-Set Registers (32 *4)|
| 0400..0527 | isactiver        | 128 bytes| Active-Set Registers (32 * 4)|
| 0528..0655 | ipriorityr_pfx   | 128 bytes| Priority Registers (Prefix)  |
| 0656..0783 | itargetsr_pfx    | 128 bytes| Routing Registers (Prefix)   |
| 0784..1199 | reserved_irq_map | 416 bytes| Reserved SPI routing map     |
| 1200..1211 | reserved_gic_exp | 12 bytes | Alignment padding            |
+-------------------------------------------------------------------------+
| TOTAL: 1212 BYTES                                                       |
+-------------------------------------------------------------------------+
```

### 3.3 GIC State Capture Implementation

The `GICv3` model provides thread-safe snapshot accessors:

```cpp
GICv3::DistributorState gic_state;
gic.get_distributor_state(gic_state);
// Serialized to canonical little-endian byte array...

// Restore:
gic.set_distributor_state(gic_state);
```

When `set_distributor_state()` executes, all distributor bitmaps are atomically updated under the GIC state mutex, ensuring no synthetic interrupt injection occurs during the transition.

---

## 4. Peripheral State Restoration Order

To prevent spurious interrupts from firing before CPU exception vectors or memory are prepared, restoration enforces this strict order:
1. **Memory**: Guest RAM must be 100% restored so interrupt handler code exists at target addresses.
2. **GICv3**: Distributor and redistributor priority masks and group enables are configured.
3. **Virtual Timers**: Timers and comparator thresholds are restored.
4. **CPU Cores**: Processor registers and PSTATE (DAIF interrupt mask bits) are restored.
