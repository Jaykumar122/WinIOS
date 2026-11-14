# Stage 194.1 — Synthetic Kernel Image Loading and Entry Contract

## 1. Objective and Architectural Scope
Stage 194.1 establishes the official boot protocol and loading infrastructure for freestanding synthetic ARM64 kernels in WinIOS. It formalizes:
1. **The `SyntheticKernelBootInfo` Contract:** A packed, 256-byte architectural data structure placed in guest memory by the host loader.
2. **The CPU Boot Entry Register Convention:**
   - `X0`: Physical address of `SyntheticKernelBootInfo` in guest RAM.
   - `X1`: Verification magic `0x53594E54484B524E` (`kSyntheticKernelMagic`, ASCII `"SYNTHKRN"`).
   - `PC`: Executable entry point in kernel `__TEXT` segment.
   - `CurrentEL`: EL1 (Kernel Privilege).
   - `SPSel`: 1 (using `SP_EL1`).
   - `DAIF`: `0` (unmasked interrupts ready for kernel configuration).
3. **The `SyntheticKernelLoader` Engine:**
   - Inspects and parses ARM64 Mach-O binaries (`MH_EXECUTE`).
   - Loads code and data segments into guest memory.
   - Computes free memory regions, aligning on 16 KB boundaries (`page_granule_bytes = 16384`).
   - Writes the command line string and packed boot contract struct.
   - Validates boot contract integrity before returning.

---

## 2. Implementation Details

### 2.1 File Additions and Modifications
- **Header:** `src/loader/synthetic_kernel_loader.h`
- **Implementation:** `src/loader/synthetic_kernel_loader.cpp`
- **Build Registration:** Added `synthetic_kernel_loader.cpp` to `winios_core` target in `CMakeLists.txt`.
- **Test Target:** Added `stage194_1_kernel_boot_contract_tests` in `CMakeLists.txt`.

### 2.2 Boot Contract Layout (`SyntheticKernelBootInfo`)
Total size: 256 bytes (enforced by `static_assert`).

| Offset (Bytes) | Field Name | Type | Description |
| :--- | :--- | :--- | :--- |
| `0` | `magic` | `uint64_t` | `0x53594E54484B524E` ("SYNTHKRN") |
| `8` | `version` | `uint32_t` | Version number (`1`) |
| `12` | `flags` | `uint32_t` | Boot flags (MMU, DTB, SMP, Timer, Console) |
| `16` | `dram_base` | `uint64_t` | Guest physical RAM base address |
| `24` | `dram_size` | `uint64_t` | Guest physical RAM total capacity |
| `32` | `kernel_text_base` | `uint64_t` | Executable `__TEXT` segment base |
| `40` | `kernel_text_size` | `uint64_t` | Executable `__TEXT` segment size |
| `48` | `kernel_data_base` | `uint64_t` | Data `__DATA` segment base |
| `56` | `kernel_data_size` | `uint64_t` | Data `__DATA` segment size |
| `64` | `entry_point` | `uint64_t` | Kernel entry PC |
| `72` | `initial_sp_el1` | `uint64_t` | Initial kernel stack pointer (`SP_EL1`) |
| `80` | `initial_sp_el0` | `uint64_t` | Initial user stack pointer (`SP_EL0`) |
| `88` | `mmio_console_base` | `uint64_t` | Virtual console MMIO address |
| `96` | `mmio_console_size` | `uint64_t` | Virtual console MMIO window size |
| `104` | `free_memory_base` | `uint64_t` | Base of unallocated DRAM (16KB aligned) |
| `112` | `free_memory_size` | `uint64_t` | Size of unallocated DRAM |
| `120` | `cmdline_addr` | `uint64_t` | Address of null-terminated command line string |
| `128` | `cmdline_size` | `uint64_t` | Length of command line string |
| `136` | `dtb_base` | `uint64_t` | Base address of DTB |
| `144` | `dtb_size` | `uint64_t` | Size of DTB |
| `152` | `boot_timestamp_ticks` | `uint64_t` | Hardware ticks at boot |
| `160` | `cpu_count` | `uint64_t` | Active / available CPU cores |
| `168` | `page_granule_bytes` | `uint64_t` | ARM64 granule size (16384 for iOS / Apple A-series) |
| `176..255` | `reserved[10]` | `uint64_t[10]` | Reserved for expansion (80 bytes) |

---

## 3. Verification and Acceptance Matrix

All tests in `tests/unit/stage194_1_kernel_boot_contract_tests.cpp` pass cleanly under MSVC C++20 `/W4 /WX /permissive-`:

| Test Name | Conditions Verified | Status |
| :--- | :--- | :--- |
| `test_boot_info_struct_layout_and_offsets` | Verifies `sizeof == 256` and exact offsets of all 24 struct members. | **PASS** |
| `test_boot_info_serialization_round_trip` | Verifies write to guest memory and exact deserialization match. | **PASS** |
| `test_boot_contract_validation_matrix` | Validates magic, version, dram size, entry point, and 16KB page granule. | **PASS** |
| `test_real_synthetic_kernel_loading` | Loads `synthetic_el1_kernel` Mach-O artifact, calculates free memory, writes cmdline and boot info. | **PASS** |
| `test_kernel_cpu_boot_and_execution` | Configures CPU via `res.configure_cpu(cpu)`, executes kernel EL1 initialization, drops to EL0, runs 178 guest instructions. | **PASS** |

Both **Release** and **Debug** builds succeed with zero warnings and 100% test pass rate.
