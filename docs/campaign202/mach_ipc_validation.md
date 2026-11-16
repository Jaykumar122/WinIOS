# WinIOS Campaign 202 Stage 15: Synthetic Mach IPC Subsystem Validation Report

## 1. Executive Summary

As part of Campaign 202 (Darwin Runtime Modernization, Mach IPC, EL2 Stage-2 Fault Architecture & DMA Performance Engineering), Phase C (Stages 202.10–202.15) modernized the synthetic Mach IPC subsystem (`winios::runtime::MachIpcSubsystem`, `MachPortSpace`, and `MachPortObject`).

The subsystem now faithfully implements:
1. **Generation-Safe Handle Allocation:** Port handles encode `(generation << 16) | slot_index`, guaranteeing that stale handles cannot target newly allocated ports (ABA prevention).
2. **Reference Counting & Dead Name Transitions:** User reference counts (`urefs`) are tracked per right (`mach_port_mod_refs`, `mach_port_get_refs`). Upon destruction of a port's receive right, active send and send-once rights across all task spaces automatically transition into dead name rights (`kMachPortRightDeadName`).
3. **Send-Once Rights Semantics:** `kMachPortRightSendOnce` rights permit exactly one message transmission, after which the right is atomically consumed and unmapped.
4. **Bounded Queue Capacity:** Port message queues enforce an architectural ceiling of 64 messages (`kMaxQueueLimit`). Excess sends immediately return `kMachSendTimedOut`.
5. **Mach Message Overwrite Trap:** Full emulation of XNU trap `-32` (`mach_msg_overwrite_trap`), supporting distinct send and receive virtual address buffers and payload overflow detection (`kMachRcvTooLarge`).
6. **Cross-Task Rendezvous:** Dynamic service registration and resolution via bootstrap catalog (`launchd`) with automatic reply port right mapping.

---

## 2. Architectural Design & Implementation Details

### 2.1 Generation-Safe Port Table

```
31                              16 15                           0
+--------------------------------+-------------------------------+
|      Generation Counter        |          Slot Index           |
|            (16 bits)           |           (16 bits)           |
+--------------------------------+-------------------------------+
```

- **Slot Index:** Direct array index into `std::vector<MachPortTableEntry> slots_`.
- **Generation Counter:** Incremented on every reuse cycle when a free slot is popped from `free_list_`. Rollover wraps cleanly to 1 (reserving 0 for unallocated).
- **Lookup Invariance:** Any handle whose generation does not match `slots_[index].generation` is rejected with `nullptr`, immediately thwarting stale-handle reuse attacks.

### 2.2 Port Right State Transitions

```
[ Unallocated ]
       |
       | allocate_port(kMachPortRightReceive)
       v
[ Receive Right (urefs >= 1) ] ------(Receiver deallocates)-----> [ Port Marked Dead ]
       |                                                                  |
       | allocate / lookup / reply                                        v
       v                                                        [ Send Right in Peers ]
[ Send Right (urefs >= 1) ]                                               |
       |                                                                  v
       +--------------------------------------------------------> [ Dead Name (urefs) ]
                                                                          |
                                                                          | deallocate (urefs == 0)
                                                                          v
                                                                   [ Unallocated ]
```

### 2.3 `mach_msg_overwrite_trap` Traps & Dispatch

| Trap Number | Symbolic Name | Arguments | Behavior |
|---|---|---|---|
| `-26` | `mach_reply_port` | `()` | Allocates ephemeral receive right for current task |
| `-27` | `thread_self_trap` | `()` | Returns thread port |
| `-28` | `mach_task_self` | `()` | Returns task self port |
| `-29` | `mach_host_self` | `()` | Returns host self port |
| `-31` | `mach_msg_trap` | `(msg, opt, send_sz, rcv_sz, rcv_name, timeout)` | Unified send/receive using shared buffer |
| `-32` | `mach_msg_overwrite_trap` | `(msg, opt, send_sz, rcv_sz, rcv_name, timeout, notify, rcv_msg, rcv_msg_sz)` | Send from `msg`, receive into `rcv_msg` |
| `-61` | `mach_port_allocate` | `(task, right)` | Allocates new port with specified right |
| `-62` | `mach_port_deallocate` | `(task, port)` | Decrements urefs, destroying right if 0 |
| `-63` | `mach_port_mod_refs` | `(task, port, right, delta)` | Adjusts urefs by signed delta |
| `-64` | `mach_port_get_refs` | `(task, port, right)` | Queries current urefs count |

---

## 3. Verification Evidence (`stage202_15_mach_ipc_tests`)

The modernization was verified through the standalone test suite `tests/unit/stage202_15_mach_ipc_tests.cpp`.

```
=== Running Stage 202.15 Synthetic Mach IPC Subsystem Tests ===
[Test 1] Generation-Safe Handle Allocator & Stale Handle Rejection...
  -> Generation-safe handle allocator passed.
[Test 2] Reference Counting & Dead Name Transition...
  -> Reference counting & dead name transition passed.
[Test 3] Send-Once Right Semantics...
  -> Send-once right semantics passed.
[Test 4] Bounded Queue Capacity (64 Messages Limit)...
  -> Bounded queue capacity passed.
[Test 5] mach_msg_overwrite with Separate Buffers...
  -> mach_msg_overwrite separate buffers passed.
[Test 6] Cross-Task Rendezvous & Reply Port Rights Translation...
  -> Cross-task rendezvous & reply port translation passed.
All Stage 202.15 tests passed successfully!
```

All 6 test cases passed with zero warnings under MSVC `/W4 /WX /permissive-`. Regression testing with existing `stage_192_10_mach_ipc_tests` confirmed 100% backwards compatibility.
