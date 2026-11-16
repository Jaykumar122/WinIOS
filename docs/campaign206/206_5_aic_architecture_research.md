# Stage 206.5: Apple Interrupt Controller (AIC) Architecture Research Report

## 1. Executive Summary
- **Stage:** 206.5 — AIC Architecture Research
- **Status:** PASS
- **Date:** 2026-11-16
- **Target Subsystem:** Apple Interrupt Controller v2 (AICv2 / AIC 2) on Apple A16 Bionic (`T8120` / `iPhone15,4`)
- **Primary Public References:**
  - Linux kernel upstream: `drivers/irqchip/irq-apple-aic.c` (Asahi Linux project)
  - Apple Open Source XNU: `osfmk/arm64/aic.c`, `osfmk/arm/arm_init.c`
  - m1n1 bootloader hardware register documentation

---

## 2. Publicly Documented AICv2 Architectural Register Map

AICv2 occupies a 1 MB MMIO aperture on Apple Silicon SoCs (base address `0x23B100000` on T8103/T8110/T8120):

```
Offset Range        Mnemonic            Description
0x0000 - 0x0003     AIC_VERSION         Silicon revision / identification
0x0004 - 0x0007     AIC_INFO            Hardware capability: [31:16] version, [15:0] nr_irqs (512)
0x0010 - 0x0013     AIC_CONFIG          Global enable / configuration register (bit 0 = enable)
0x0040 - 0x0043     AIC_IPI_SEND        Global IPI send register: bitmask of target CPU cores
0x2000 - 0x207F     AIC_CORE_BASE(0)    Per-core banked registers for Core 0 (stride 0x80)
  +0x0000           AIC_WHOAMI          Reads logical CPU ID (0..num_cores - 1)
  +0x0004           AIC_EVENT           Pop highest pending event (Read) / Acknowledge EOI (Write)
  +0x0008           AIC_IPI_ACK         Read pending IPI bits / Write to clear
  +0x000C           AIC_IPI_MASK        Per-core IPI mask
0x2080 - 0x20FF     AIC_CORE_BASE(1)    Banked registers for Core 1
0x4000 - 0x47FF     AIC_TARGET_BASE     Target CPU routing table (32-bit core affinity mask per IRQ)
0x8000 - 0x803F     AIC_MASK_SET_BASE   Hardware IRQ mask set registers (16 words = 512 IRQs)
0x8080 - 0x80BF     AIC_MASK_CLR_BASE   Hardware IRQ mask clear registers (16 words)
0x8100 - 0x813F     AIC_HW_PENDING      Hardware IRQ raw pending status
0x8180 - 0x81BF     AIC_HW_STATE        Hardware IRQ active/in-service status
0x8200 - 0x823F     AIC_SW_SET          Software interrupt assert
0x8280 - 0x82BF     AIC_SW_CLR          Software interrupt clear
```

---

## 3. AIC Event Decoding Semantics
Reading `AIC_EVENT` returns a 32-bit value:
- **Bits [31:16] — Event Type:**
  - `0x0000`: Hardware IRQ (`kEventTypeHw`)
  - `0x0001`: Inter-Processor Interrupt (`kEventTypeIpi`)
  - `0x0002`: Architectural Timer Interrupt (`kEventTypeTimer`)
  - `0xFFFF`: No event pending (`kEventNone` / `0xFFFFFFFF`)
- **Bits [15:0] — Event Payload:**
  - For HW IRQ: Hardware IRQ index (`0..511`)
  - For IPI: IPI reason / vector
  - For Timer: Timer source index

---

## 4. Authoritative Public Knowledge vs. Proprietary Boundaries

### 4.1 Authoritatively Verified Public Facts:
1. **Replacement of ARM GIC:** Apple Silicon completely replaces standard ARM GIC architecture. No GICD or GICR exists in physical address space.
2. **Interrupt Delivery Mode:** AIC routes interrupts directly to the CPU core via the physical `FIQ` or `IRQ` signal lines.
3. **Timer Preemption:** Architectural timer events take priority over software IPIs and standard peripheral hardware IRQs.
4. **DeviceTree Integration:** Declared under `/arm-io/aic` with `compatible = "apple,aic2", "apple,aic"`.

### 4.2 Proprietary & Unverified Silicon Details:
1. **SEP & TrustZone Co-processor Mailbox Lines:** Interrupt lines routed to the Secure Enclave are managed by proprietary firmware and are not visible to non-secure EL1.
2. **PMGR Power Handshake:** Clock-gating and power-island transition registers co-located near AIC MMIO apertures remain undocumented in public Apple documentation.
3. **Fast System-Register Access Mode (AICv3):** Later A-series / M-series SoCs utilize implementation-defined system registers (`MSR`/`MRS`) for low-latency IPIs; exact bitfields are undocumented in ARM Architecture Reference Manuals.
