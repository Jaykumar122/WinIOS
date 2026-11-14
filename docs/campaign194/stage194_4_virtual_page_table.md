# Stage 194.4 — Guest Virtual Memory Manager (Page Table Walker & Descriptor Writer)

## 1. Architectural Scope & Evidence Classification
Stage 194.4 establishes the virtual page table management infrastructure for the synthetic ARM64 EL1 kernel runtime:
- **Evidence Classification:** `GUEST_MEMORY_MANAGER`
- **Granule Size:** 16 KB (`kPhysicalPageSize16KB = 16384`), adhering to the Apple A16 Bionic Stage-1 translation specification.
- **Hierarchical Structure:**
  - **Level 2 Table:** 16 KB root table consisting of 2,048 entries (8 bytes each). Index computed via bits `[35:25]`. Entries can be 32 MB block descriptors (`type == 0b01`) or pointers to Level 3 tables (`type == 0b11`).
  - **Level 3 Table:** 16 KB leaf table consisting of 2,048 entries (8 bytes each). Index computed via bits `[24:14]`. Entries map 16 KB physical pages (`type == 0b11`).
- **Dynamic Allocation Integration:** The virtual page table manager couples with `PhysicalPageManager` (Stage 194.3) to dynamically allocate Level 3 table pages on demand as new virtual addresses are mapped.
- **Guest-Side EL1 Assembly Routines:** ARM64 assembly routine `emit_guest_map_routine` enables the guest kernel to write descriptors directly into Level 3 tables at EL1 using `STR`.

---

## 2. Descriptor Formats & Permission Policies

### 2.1 Descriptor Bitfields (16 KB Granule)
- **Table Descriptor (Level 2):**
  - Bits `[1:0]`: `0b11` (Table)
  - Bits `[47:14]`: Output physical address of Level 3 table (16 KB aligned)
- **Page Descriptor (Level 3):**
  - Bits `[1:0]`: `0b11` (Page)
  - Bits `[4:2]`: `AttrIndx` (0 = Normal Write-Back, 1 = Device nGnRE)
  - Bits `[7:6]`: `AP[2:1]` Access Permissions:
    - `0b00`: Kernel Read-Write, EL0 No Access
    - `0b10`: Kernel Read-Only, EL0 No Access
    - `0b01`: User Read-Write, Kernel Read-Write
    - `0b11`: User Read-Only, Kernel Read-Only
  - Bits `[9:8]`: `SH[1:0]` Shareability (`0b11` = Inner Shareable)
  - Bit `10`: `AF` Access Flag (`1` = Validated)
  - Bits `[47:14]`: Physical page frame base address
  - Bit `53`: `PXN` (Privileged eXecute-Never)
  - Bit `54`: `UXN` (Unprivileged eXecute-Never)

---

## 3. Implementation and Verification Matrix

### 3.1 Files Added
- `src/memory/virtual_page_table.h`
- `src/memory/virtual_page_table.cpp`
- Registered in `CMakeLists.txt` under `winios_core`.
- Test target: `tests/unit/stage194_4_virtual_page_table_tests.cpp`.

### 3.2 Test Verification Results

| Test Case | Conditions Tested | Retired Insns | Result |
| :--- | :--- | :--- | :--- |
| `test_descriptor_construction_and_parsing` | Validates bitfields for Table, Kernel RW, User RW, and Kernel Execute descriptors. | N/A (Unit) | **PASS** |
| `test_page_table_hierarchy_and_translation` | Constructs L2/L3 hierarchy with dynamic PPM allocator and verifies exact VA->PA resolution. | N/A (Unit) | **PASS** |
| `test_range_mapping_and_unmapping` | Maps 4-page contiguous span, unmaps middle page, verifies unmapped address returns 0 while neighbors remain intact. | N/A (Unit) | **PASS** |
| `test_guest_arm64_map_routine` | Emits guest ARM64 EL1 routine into RAM; CPU steps and writes L3 descriptor via guest instructions. | 6 guest insns | **PASS** |
| `test_stage1_mmu_hardware_translation_integration` | Enables Stage-1 MMU with 16KB granule in CPU; CPU steps `LDR X0, [X1]` on virtual address, walking guest page table and reading payload `0xcafebabe12345678`. | 2 guest insns | **PASS** |

Tested in both **Debug** and **Release** builds under MSVC C++20 `/W4 /WX /permissive-`.
