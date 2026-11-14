# WINIOS CAMPAIGN 194: STAGE 194.7 REPORT
## Kernel Thread Structures & Lifecycle

### 1. Executive Summary
Stage 194.7 defines and validates the core multitasking data structures and thread lifecycle mechanisms for the WinIOS synthetic ARM64 kernel runtime.
It establishes:
- `GuestTcb`: A packed 512-byte Thread Control Block matching hardware alignment requirements for direct guest memory serialization and hardware context-switching routines.
- Four fundamental thread execution states: `Ready`, `Running`, `Blocked`, and `Terminated`.
- Wait reasons for fine-grained blocking: `Sleep`, `WaitQueue`, `IpcMessage`, `IoDevice`, and `Mutex`.
- Complete architectural register state capture: X0..X30 (general-purpose registers), PC, SP_EL0, SP_EL1, and SPSR_EL1.
- `KernelThread` and `KernelThreadManager` classes providing thread creation, priority scheduling parameters, quantum tracking, CPU affinity, and automatic reaping of terminated threads.

---

### 2. Architecture & Design

#### 2.1 Thread Control Block Layout
The `GuestTcb` struct is statically asserted to 512 bytes with explicit architectural offsets:
- `0x000`: `magic` (`0x5443425F54485244ULL` - "TCB_THRD")
- `0x008`: `tid` (64-bit Thread Identifier)
- `0x010`: `state` (`ThreadState`: Ready=0, Running=1, Blocked=2, Terminated=3)
- `0x014`: `priority` (0..31, default 16)
- `0x018`: `time_slice` (quantum ticks remaining)
- `0x01C`: `wait_reason` (`WaitReason`: None, Sleep, WaitQueue, IpcMessage, IoDevice, Mutex)
- `0x020`: `sp_el0` (EL0 user stack pointer)
- `0x028`: `sp_el1` (EL1 kernel stack pointer)
- `0x030`: `pc` (Instruction pointer)
- `0x038`: `spsr_el1` (Saved program status register, EL0t = 0x0, EL1h = 0x5)
- `0x040`: `gpr[31]` (X0 through X30, 248 bytes)
- `0x138`: `cpu_affinity` (Core mask)
- `0x140`: `entry_point`
- `0x148`: `stack_base`
- `0x150`: `stack_size`
- `0x158`: `total_runtime_ticks`
- `0x160`: `exit_code`
- `0x164`: `name` (32 bytes ASCII)
- `0x184`: Reserved padding to 512 bytes

#### 2.2 CPU Context Save & Restore
`KernelThread::save_context_from_cpu` and `KernelThread::load_context_to_cpu` transfer architectural registers directly between the virtual ARM64 CPU and the TCB. SPSR_EL1 mode encoding automatically configures CPU exception level (`EL0` vs `EL1`) and stack pointer selection (`SP_EL0` vs `SP_EL1`).

---

### 3. Verification & Results

Unit test suite `tests/unit/stage194_7_thread_lifecycle_tests.cpp` executes 6 comprehensive test cases:
1. `test_tcb_layout_and_magic`: Confirms 512-byte size and offsets of all architectural and metadata members.
2. `test_thread_creation_and_initial_context`: Confirms stack alignment, argument passing in X0, LR=0, and correct SPSR mode for kernel and user threads.
3. `test_thread_state_transitions_and_lifecycle`: Tests transition path (Ready -> Running -> Blocked -> Ready -> Terminated), quantum ticks, and exit code.
4. `test_cpu_context_save_and_restore`: Verifies lossless register synchronization with the virtual ARM64 CPU.
5. `test_guest_memory_serialization`: Serializes TCB to guest RAM, modifies fields in-memory, deserializes, and verifies validation of magic.
6. `test_thread_manager_lifecycle_and_reaping`: Validates thread creation, pointer stability using `std::unique_ptr`, status query, and dead thread reclamation.

Both Release and Debug configurations build cleanly under `/W4 /WX /permissive-` and pass all 6 tests with zero errors.

---

### 4. Classification
- Source implementations: `GUEST_SCHEDULER` / `SYNTHETIC_KERNEL`
- Simulation / host harness: `HOST_MODEL`
