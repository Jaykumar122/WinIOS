# WinIOS Campaign 219 — Stage 219.5: Multi-Thread Context Model

## 1. Architectural Model

In WinIOS Campaign 219, each `BsdProcess` can own multiple concurrent execution threads (`KernelThread`). All threads belonging to the same process share that process's `UserAddressSpace` (and its associated `TTBR0_EL1`), but possess independent architectural CPU state:

- Dedicated User Stack Pointer (`SP_EL0`) and stack memory range (16 KB granule aligned).
- Dedicated Architectural Program Counter (`PC`).
- Dedicated General-Purpose Register File (`X0` through `X30`).
- Dedicated Thread-Local Storage Pointer (`TPIDR_EL0`).
- Dedicated Processor State (`SPSR_EL1`).

## 2. Guest TCB Binary Layout Conformance

The `GuestTcb` structure is exactly 512 bytes, packed without compiler padding (`#pragma pack(push, 1)`), allowing direct zero-copy mapping into guest memory:

| Offset Range | Field Name | Type | Description |
|---|---|---|---|
| `0x000 - 0x007` | `magic` | `uint64_t` | Identification constant `0x5443425F54485244ULL` ("TCB_THRD") |
| `0x008 - 0x00F` | `tid` | `uint64_t` | Unique 64-bit Thread Identifier |
| `0x010 - 0x013` | `state` | `uint32_t` | `ThreadState` enum (`Ready=0`, `Running=1`, `Blocked=2`, `Terminated=3`) |
| `0x014 - 0x017` | `priority` | `uint32_t` | Scheduling priority (0..31, default 16) |
| `0x018 - 0x01B` | `time_slice` | `uint32_t` | Remaining quantum ticks |
| `0x01C - 0x01F` | `wait_reason` | `uint32_t` | `WaitReason` enum |
| `0x020 - 0x027` | `sp_el0` | `uint64_t` | Architectural user stack pointer |
| `0x028 - 0x02F` | `sp_el1` | `uint64_t` | Architectural kernel stack pointer |
| `0x030 - 0x037` | `pc` | `uint64_t` | Architectural program counter |
| `0x038 - 0x03F` | `spsr_el1` | `uint64_t` | Saved process status register |
| `0x040 - 0x137` | `gpr[31]` | `uint64_t[31]` | 31 general-purpose registers (X0..X30) |
| `0x138 - 0x13F` | `cpu_affinity` | `uint64_t` | CPU core affinity bitmask |
| `0x140 - 0x147` | `entry_point` | `uint64_t` | Initial thread entry function |
| `0x148 - 0x14F` | `stack_base` | `uint64_t` | Virtual base address of stack allocation |
| `0x150 - 0x157` | `stack_size` | `uint64_t` | Stack size in bytes |
| `0x158 - 0x15F` | `total_runtime_ticks` | `uint64_t` | Cumulative CPU cycles/ticks |
| `0x160 - 0x163` | `exit_code` | `int32_t` | Return code on termination |
| `0x164 - 0x183` | `name` | `char[32]` | Human-readable thread name |
| `0x184 - 0x18B` | `tpidr_el0` | `uint64_t` | Thread-Local Storage base register |
| `0x18C - 0x193` | `instructions_retired` | `uint64_t` | Verified retired instructions counter |
| `0x194 - 0x1FF` | `reserved` | `uint8_t[108]` | Padding to exactly 512 bytes |

## 3. Verification

Verified by `stage219_5_thread_context_tests.exe`:
- Static assert: `sizeof(GuestTcb) == 512`.
- Exact member offset checks matching ARM64 ABI requirements.
- CPU context synchronization and register preservation.
- Round-trip guest memory serialization.
