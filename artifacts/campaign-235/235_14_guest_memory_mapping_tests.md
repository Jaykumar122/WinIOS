# Stage 235.14 — Guest Memory Mapping Tests

## 1. Executive Summary
Stage 235.14 verified guest memory management and virtual address space isolation for the iPhone 15 (`iPhone15,4`, Apple A16 Bionic `T8120`) research profile. The test suite validated 16 KB page granule conformance, PAGEZERO guard band enforcement, multi-segment W^X memory protection, integer overflow prevention, and cross-boundary access semantics under `DarwinVmSpace`.

## 2. Tested Memory Protections & Granules

| Feature | Architectural Specification | Tested Implementation | Verification Status |
|:---|:---|:---|:---:|
| **Page Granule** | Apple 16 KB (`0x4000` bytes) | `kApplePageSize16K == 0x4000` | **PASSED** |
| **PAGEZERO Guard** | Unmapped 4 GB range $[0x0, 0x100000000)$ | Read, write, execute dereferences rejected | **PASSED** |
| **Strict W^X Policy** | No segment with $(\text{PROT\_WRITE} \land \text{PROT\_EXEC})$ | Multi-segment Mach-O inspection & runtime mapping | **PASSED** |
| **Cross-Page Boundary** | Access spanning page 0 to page 1 | Continuous boundary access verified | **PASSED** |
| **Unmapped Tail Fault** | Access extending past mapped range into unmapped page | Check access returns false | **PASSED** |
| **Integer Overflow** | High VA + size wrapping 64-bit integer | Overflow check rejects access | **PASSED** |

## 3. Execution & Compiler Conformance
- Target: `stage235_14_guest_memory_mapping_tests`
- File: `tests/unit/stage235_14_guest_memory_mapping_tests.cpp`
- RelWithDebInfo: 0.05s (PASSED 100%)
- Debug: 0.06s (PASSED 100%)
- Compiler Warnings: 0 under `/W4 /WX /permissive-`.
