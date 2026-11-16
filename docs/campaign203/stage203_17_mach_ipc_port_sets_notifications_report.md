# WinIOS Campaign 203 Stage 203.17: Mach IPC Port Sets & Notifications Report

**Date:** 2026-11-16  
**Status:** COMPLETE (PASS)  
**Target Device Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Environment:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`)  
**Test Suite:** `stage203_17_mach_ipc_port_sets_notifications_tests` (Release & Debug)

---

## 1. Executive Summary

Stage 203.17 extends and independently validates Mach IPC port sets (`kMachPortRightPortSet`), port set membership management, message multiplexing, and dead-name notifications (`MACH_NOTIFY_DEAD_NAME = 72`) within `MachPortSpace` and `MachIpcSubsystem`.

Key verified architectural features:
1. **Port Set Allocation & Membership:** Allocating `kMachPortRightPortSet` yields a port set handle. Ports holding receive rights can be dynamically inserted (`mach_port_insert_member`) or extracted (`mach_port_extract_member`).
2. **Message Multiplexing over Port Sets:** Receiving on a port set via `mach_msg(..., kMachRcvMsg, ..., rcv_name = pset, ...)` transparently queries member ports and dequeues messages as they arrive across member queues.
3. **Local Port Header Attribution:** Upon receiving a message through a port set, `msgh_local_port` in the message header is dynamically rewritten to the exact member port handle on which the message was queued, adhering to Darwin XNU semantics.
4. **Dead-Name Notifications:** Registering a notification via `mach_port_request_notification` triggers an asynchronous notification message with ID 72 (`MACH_NOTIFY_DEAD_NAME`) delivered to the requested notification port whenever the target port object is marked dead or its receive right is deallocated. The client right cleanly transitions to `kMachPortRightDeadName`.
5. **Automatic Membership Cleanup:** Deallocating a member port cleanly removes it from its enclosing set; deallocating a port set extracts all member ports while preserving their individual receive rights intact.

---

## 2. Test Execution Matrix

| Test Case | Description | Result (Release) | Result (Debug) |
|---|---|---|---|
| `test_port_set_allocation_and_membership` | Port set allocation, member insertion, membership queries, extraction | PASS | PASS |
| `test_port_set_message_receipt_multiplexing` | Message queue multiplexing over set, msgh_local_port member attribution, timeout | PASS | PASS |
| `test_dead_name_notification` | MACH_NOTIFY_DEAD_NAME registration, trigger on port deallocation, payload check | PASS | PASS |
| `test_port_set_cleanup_on_deallocate` | Automatic member cleanup upon set destruction, receive right retention | PASS | PASS |

---

## 3. Verification Details

All tests compiled under `/W4 /WX /permissive-` with 0 warnings and executed with 0 failures in both Release and Debug configurations.
