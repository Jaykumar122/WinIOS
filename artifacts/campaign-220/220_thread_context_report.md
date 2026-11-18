# WinIOS Campaign 220 — Stage 220.5 ARM64 Thread Context Completeness Report

## 1. Executive Summary

Stage 220.5 audited and enhanced the `GuestTcb` and `KernelThread` context management engine to guarantee architectural completeness across ARM64 execution states.
Key accomplishments:
1. **Packed 512-Byte TCB Invariant:** Verified that `sizeof(GuestTcb) == 512` holds strictly with static layout assertions and field offset verification matching the documented specification.
2. **Comprehensive Register File Preservation:**
   - 31 General-Purpose Registers: X0 through X30.
   - Program Counter: `pc`.
   - Stack Pointers: `sp_el0` (user stack) and `sp_el1` (kernel stack).
   - Saved Process Status Register: `spsr_el1`.
   - Thread-Local Storage Pointer: `tpidr_el0`.
   - Instruction Retirement State: `instructions_retired`.
3. **Advanced SIMD & Floating-Point (FP/SIMD) Architecture:**
   - Evaluated the virtual CPU model which supports 32 128-bit vector registers (V0–V31).
   - Integrated per-thread vector register backup in `KernelThread::fp_regs_`.
   - Extended `save_context_from_cpu()` and `load_context_to_cpu()` to preserve all 32 vector registers on context switch.
4. **Context Switch Engine Harmonization:**
   - Updated `MultiProcessScheduler::perform_context_switch()` to invoke `save_context_from_cpu()` and `load_context_to_cpu()`, eliminating manual field-by-field copying and ensuring full architectural state preservation.

---

## 2. Structural Layout & Offset Verification Matrix

| Byte Offset Range | Field Name | Data Type | Purpose |
| :---: | :--- | :--- | :--- |
| `0x000 - 0x008` | `magic` | `uint64_t` | `kTcbMagic` (`0x5443425F54485244`) |
| `0x008 - 0x010` | `tid` | `uint64_t` | Thread Unique Identifier |
| `0x010 - 0x014` | `state` | `uint32_t` | Ready / Running / Blocked / Terminated |
| `0x014 - 0x018` | `priority` | `uint32_t` | Priority tier (0..31) |
| `0x018 - 0x01C` | `time_slice` | `uint32_t` | Remaining quantum ticks |
| `0x01C - 0x020` | `wait_reason` | `uint32_t` | Mutex / Pipe / Sleep / None |
| `0x020 - 0x028` | `sp_el0` | `uint64_t` | User Stack Pointer |
| `0x028 - 0x030` | `sp_el1` | `uint64_t` | Kernel Stack Pointer |
| `0x030 - 0x038` | `pc` | `uint64_t` | Program Counter |
| `0x038 - 0x040` | `spsr_el1` | `uint64_t` | Saved PSTATE register |
| `0x040 - 0x138` | `gpr[31]` | `uint64_t[31]` | General-purpose registers X0–X30 (248 B) |
| `0x138 - 0x140` | `cpu_affinity` | `uint64_t` | CPU mask |
| `0x140 - 0x148` | `entry_point` | `uint64_t` | Initial function address |
| `0x148 - 0x150` | `stack_base` | `uint64_t` | Stack bottom virtual address |
| `0x150 - 0x158` | `stack_size` | `uint64_t` | Stack allocation size |
| `0x158 - 0x160` | `total_runtime_ticks` | `uint64_t` | Cumulative scheduler execution |
| `0x160 - 0x164` | `exit_code` | `int32_t` | Thread exit code |
| `0x164 - 0x184` | `name[32]` | `char[32]` | Human-readable thread label |
| `0x184 - 0x18C` | `tpidr_el0` | `uint64_t` | User Thread-Local Storage pointer |
| `0x18C - 0x194` | `instructions_retired` | `uint64_t` | Verified instruction counter |
| `0x194 - 0x200` | `reserved[108]` | `uint8_t[108]` | Reserved padding to 512 bytes |

Total size: **512 bytes**.
