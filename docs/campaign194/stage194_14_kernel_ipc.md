# Campaign 194 Stage 194.14: Guest Kernel IPC & Message Queues

## Objective
Implement kernel-mediated inter-process communication:
1. Message queues & Port abstraction:
   - Fixed-size 64-byte message structures: `KernelMessage` (sender TID, receiver TID, message ID, payload size, timestamp, 32-byte payload).
   - Port / Channel abstraction (`IpcPort`):
     - Thread or process-owned IPC endpoint.
     - Capacity limit (default 16 queued messages).
     - Non-blocking send (`try_send`) and non-blocking receive (`try_recv`).
     - Blocking send (`send_blocking`) with FIFO wait queue on full ports.
     - Blocking receive (`recv_blocking`) with FIFO wait queue on empty ports.
2. Integration with scheduler and synchronization:
   - Direct integration with `WaitQueue` and `PriorityScheduler::block_current` / `unblock_thread`.
   - Message delivery wakes blocked threads and transitions them to `ThreadState::Ready`.
   - Guest memory buffer transfer: copy message from sender guest memory to port, and from port to receiver guest memory.

## Implementation Details
- Created `src/kernel/ipc.h` and `src/kernel/ipc.cpp`:
  - `KernelMessage`: 64-byte packed layout.
  - `IpcPort`: Manages queue capacity, queued messages, and dual wait queues (`send_waiters_` and `recv_waiters_`).
  - `IpcManager`: Manages port lifetime (`create_port`, `get_port`, `destroy_port`) and cross-guest memory buffer transfers (`send_from_guest`, `recv_to_guest`).
- Authored test suite `tests/unit/stage194_14_kernel_ipc_tests.cpp`.

## Test Results
- `stage194_14_kernel_ipc_tests`:
  - `test_kernel_message_layout`: PASSED (exact 64-byte alignment).
  - `test_port_creation_and_capacity`: PASSED (capacity limits and full queue rejection).
  - `test_nonblocking_send_and_receive`: PASSED (FIFO order message queueing and draining).
  - `test_blocking_send_unblocked_by_recv`: PASSED (sender blocked when full, unblocked to Ready when drained).
  - `test_blocking_recv_unblocked_by_send`: PASSED (receiver blocked when empty, unblocked to Ready when message arrives).
  - `test_guest_memory_ipc_transfer`: PASSED (end-to-end guest memory transfer at 0x2000 -> port -> 0x3000).
  - Total: 60 passed, 0 failed.

## Evidence Classification
- IPC Queues & Ports: `SYNTHETIC_KERNEL`
- IPC Thread Blocking & Wake: `GUEST_SCHEDULER`
