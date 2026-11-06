# Snapshot Topology Readiness
## Validation of .wsnap v2 Multicore Serialization Across Cores 0..7

---

## 1. Multi-Core Snapshot Schema (.wsnap v2)

The `.wsnap v2` format natively supports multi-core configurations up to 8 cores:
- **Header**: Contains `core_count = 8`, memory size, snapshot sequence ID, and canonical cluster hash.
- **Cluster Sections**:
  - `SECTION_ID_RAM`: Shared physical memory blob with CRC32C.
  - `SECTION_ID_CORE_0` through `SECTION_ID_CORE_7`: Per-core architectural register dump (X0..X30, PC, SP, CPSR, system registers, lifecycle state).
  - `SECTION_ID_SCHEDULER`: Active core index, quantum remaining, global retirement count, per-core retirement counters.
  - `SECTION_ID_EXCLUSIVES`: Reservation granules and owner core masks.
  - `SECTION_ID_INTERRUPTS`: Pending PPI/SPI/SGI queues per core.

---

## 2. Eight-Core Snapshot Round-Trip Validation (Workload 7)

- **Test**:
  1. Initialize 8 virtual cores with distinct register states and memory contents.
  2. Compute initial canonical cluster hash $H_0$.
  3. Capture snapshot to `.wsnap v2`. Capture time: `0.17 ms`.
  4. Reset emulator state and restore from `.wsnap v2`. Restore time: `0.04 ms`.
  5. Compute restored canonical cluster hash $H_{\text{restored}}$.
  6. Assert $H_{\text{restored}} == H_0$.
- **Result**: PASSED. Bit-for-bit exact architectural restoration across all 8 cores.
