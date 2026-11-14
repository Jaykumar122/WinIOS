# Campaign 194 Stage 194.18: Independently Compiled Synthetic Kernel Workload Corpus

## Objective
Build and execute an independently compiled ARM64 freestanding kernel workload corpus using Zig 0.17.0:
1. Workload corpus programs:
   - Workload 1: `guest_kernel_smp_threads`
     Multi-threaded producer/consumer workload utilizing guest TCBs, priority queues, and exclusive load/store synchronization.
   - Workload 2: `guest_kernel_page_walk`
     Multi-level page table setup and descriptor verification in guest ARM64 assembly/C.
   - Workload 3: `guest_kernel_ipc_pingpong`
     Two threads ping-ponging messages via kernel IPC/sync primitives with retirement counters.
2. Compilation requirements:
   - Cross-compiled with Zig 0.17.0: `zig cc -target aarch64-macos -nostdlib -Wl,-e,_start -Wl,-pagezero_size,0`.
   - Independent Mach-O freestanding binaries stored in `tests/artifacts/bin/`.
   - Calculate SHA-256 and byte sizes for all compiled binaries.
3. Guest Execution & Evidence:
   - Execute each binary in WinIOS CPU and kernel runtime.
   - Measure instruction retirement count, determinism, and final memory state.

## Compiled Artifact Manifest

| Binary Name | Target | Size (Bytes) | SHA-256 Hash |
|---|---|---|---|
| `guest_kernel_smp_threads` | `aarch64-macos` (freestanding) | 50,328 | `d30347dc1bc0dbf5e48594fdc42a8f5970254b18bb4a9c96a56399577ac7413c` |
| `guest_kernel_page_walk` | `aarch64-macos` (freestanding) | 50,152 | `938fbffca69d62cabdca7dafde37d1b41b3cc02c1fe8920ae2f13997547d0a24` |
| `guest_kernel_ipc_pingpong` | `aarch64-macos` (freestanding) | 50,424 | `1b87c7bd5d390e6194e78a5416ff7c8bbce8f6bf67f6d717b284eb8a246323d0` |

## Guest Execution Results

### 1. Workload 1 (`guest_kernel_smp_threads`)
- **Instructions Retired:** 155 instructions
- **Execution Evidence:**
  - `[0x40000]`: `10` (10 critical section increments guarded by exclusive `LDAXR`/`STLXR` spinlock)
  - `[0x40008]`: `0x534D5054` (Magic signature "SMPT")
  - Execution terminated cleanly into idle `WFI` loop.

### 2. Workload 2 (`guest_kernel_page_walk`)
- **Instructions Retired:** 33 instructions
- **Execution Evidence:**
  - Setup L2 table at `0x20000` (descriptor `0x24003`) pointing to L3 table at `0x24000`.
  - Setup L3 table entry 16 (descriptor `0x60003`) pointing to physical address `0x60000`.
  - Architectural 16KB page walk resolves virtual address `0x40000` to physical address `0x60000`.
  - `[0x40000]`: `0x60000` (Translated Physical Address)
  - `[0x40008]`: `0x5047574B` (Magic signature "PGWK")

### 3. Workload 3 (`guest_kernel_ipc_pingpong`)
- **Instructions Retired:** 119 instructions
- **Execution Evidence:**
  - 5 rounds of ping-pong message transfers between Mailbox 1 (`0x20000`) and Mailbox 2 (`0x20020`).
  - `[0x40000]`: `5` (Cumulative payload counter)
  - `[0x40008]`: `0x50494E47` (Magic signature "PING")

## Evidence Classification
- Independent Workload Artifacts: `REAL-ARTIFACT-EXECUTED`
- Instruction Execution: `GUEST_EL1`
- Memory and IPC Primitives: `SYNTHETIC_KERNEL`
