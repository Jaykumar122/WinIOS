# Stage 194.5 — Demand Paging & Page-Fault Resolution

## 1. Architectural Scope & Evidence Classification
Stage 194.5 implements the demand paging and synchronous page-fault resolution subsystem for the synthetic ARM64 kernel runtime:
- **Evidence Classification:** `GUEST_MEMORY_MANAGER`
- **Granule Size:** 16 KB (`kPhysicalPageSize16KB = 16384`), conforming to Apple A16 Bionic Stage-1 translation.
- **Architectural Exception Vectoring:**
  - Synchronous Data Abort exceptions from Current EL with SPx vector to `VBAR_EL1 + 0x200`.
  - Synchronous Data Abort exceptions from Lower EL (EL0) vector to `VBAR_EL1 + 0x400`.
- **Transparent Fault Resolution:**
  - The guest exception handler preserves all caller scratch registers (`X0..X10`) onto `SP_EL1`.
  - Reads `FAR_EL1` (faulting virtual address) and `ESR_EL1` (exception syndrome).
  - Inspects Exception Class (`EC`): if `0x24` (Lower EL Abort), sets `AP = 0b01` (User Read-Write); otherwise `AP = 0b00` (Kernel Read-Write).
  - Aligns fault address to 16 KB and extracts Level 3 index (`(VA >> 14) & 0x7FF`).
  - Reads next free physical page from backing pool at `[0x40088]`.
  - Constructs Level 3 Page Descriptor (`Normal WB`, `AF=1`, `SH=3`, `Page=3`).
  - Writes descriptor directly into the Level 3 translation table in guest RAM.
  - Advances physical backing pool pointer by 16 KB.
  - Logs fault address to `0x40010` and increments fault counter at `0x40018`.
  - Executes broadcast TLB invalidation (`TLBI VMALLE1IS`), followed by memory barrier `DSB ISH` and instruction synchronization `ISB`.
  - Pops caller scratch registers (`X0..X10`) from `SP_EL1`.
  - Executes `ERET` to resume the faulting instruction.
  - The interrupted instruction re-executes seamlessly, translating through the newly installed Stage-1 descriptor and completing without host intervention.

---

## 2. Implementation Summary

### 2.1 File Changes
- **Header:** `src/memory/demand_paging_manager.h`
- **Source:** `src/memory/demand_paging_manager.cpp`
- **Build Registration:** Added `demand_paging_manager.cpp` to `winios_core` and `stage194_5_demand_paging_tests` in `CMakeLists.txt`.

### 2.2 Vector Table Layout (2 KB Base Alignment)

| Vector Offset | Exception Source | Handler Description |
| :--- | :--- | :--- |
| `0x000` - `0x180` | Current EL with SP0 | Unhandled (infinite loop `b .`) |
| `0x200` | Current EL with SPx Sync | Guest EL1 Demand Page Fault Handler |
| `0x280` - `0x380` | Current EL with SPx IRQ/FIQ/SError | Interrupt vectors / unhandled |
| `0x400` | Lower EL (AArch64) Sync | Guest EL0 User Mode Demand Page Fault Handler |
| `0x480` - `0x780` | Lower EL IRQ / FIQ / AArch32 | Interrupt vectors / unhandled |

---

## 3. Verification Matrix

The test suite `tests/unit/stage194_5_demand_paging_tests.cpp` verifies 5 comprehensive scenarios:

| Test Case | Conditions Tested | Retired Insns | Result |
| :--- | :--- | :--- | :--- |
| `test_vector_table_and_control_initialization` | Verifies 2KB alignment of VBAR, control slots in guest RAM, register push sequences at `+0x200` and `+0x400`. | N/A (Unit) | **PASS** |
| `test_single_demand_page_fault_resolution` | Accesses unmapped address `0x10000000`; triggers Data Abort, guest handler maps backing page, re-executes `LDR X0, [X1]`, reads payload `0x55aa112233445566`. | ~40 guest insns | **PASS** |
| `test_multi_page_sequential_fault_resolution` | Successive accesses to 3 unmapped pages (`0x10000000`, `0x10004000`, `0x10008000`); resolves each in turn, allocates 3 backing pages. | 123 guest insns | **PASS** |
| `test_write_triggered_demand_page_fault` | Write (`STR X2, [X1]`) to unmapped virtual address `0x1000C000`; triggers Data Abort, handler maps page with RW permissions, store completes. | 43 guest insns | **PASS** |
| `test_lower_el_demand_page_fault` | CPU in EL0 user mode reads unmapped virtual address; vectors to Lower EL Sync, handler resolves at EL1, `ERET` demotes to EL0, user reads data. | ~45 guest insns | **PASS** |

Tested in both **Debug** and **Release** builds under MSVC C++20 `/W4 /WX /permissive-`.
