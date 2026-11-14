# Stage 196.15 — Kernel IPC & Object Lifetime Races (Acceptance Test M)

## 1. Architectural Implementation
- **HandleTable Concurrency & Rights Management:**
  - Thread-safe object handle table protected by `std::recursive_mutex mutex_`.
  - Attenuated handle duplication: `duplicate_handle` enforces `new_rights_mask & parent_rights`, preventing privilege elevation.
  - Stale handle invalidation: closing a handle immediately zeroes its rights, marks it invalid, releases the object reference, and recycles the descriptor slot.
  - Rejection of stale or closed handles on `get_object` (returns `nullptr`) and `close_handle` (returns `false`).
- **IpcManager and IpcPort Synchronization:**
  - Thread-safe IPC port message queues protected by `std::recursive_mutex mutex_`.
  - Non-blocking and blocking send/receive with FIFO wait-queue signaling (`WaitQueue send_waiters_`, `recv_waiters_`).
  - Safe port destruction via `destroy_port`: drains blocked waiters and purges port mappings without use-after-free or memory leaks.

## 2. Test Evidence (Acceptance Test M)
Verified via `stage196_15_ipc_lifetime_tests`:
- Test M1: 8 concurrent threads executing 800 handle allocations, duplications with rights attenuation, and closures. Stale handle rejection verified. Zero active handles remain upon completion.
- Test M2: 4 producer threads sending 400 messages and 4 consumer threads receiving 400 messages concurrently across a shared IPC port. Safe port destruction verified.

## 3. Evidence Classification
`GUEST_SMP`, `SYNTHETIC_KERNEL`. Tested in Debug and Release.
