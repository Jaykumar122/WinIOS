# WINIOS CAMPAIGN 192 — STAGE 192.10 REPORT
## Mach IPC Right and Message Validation

**Campaign:** 192  
**Stage:** 192.10 (Mach IPC Right and Message Validation)  
**Date:** 2026-11-13  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Host Architecture:** Windows 11 x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Verdict:** **PASSED (5/5 test suites passed, 100% CTest regression clean, Mach IPC and traps verified)**

---

## 1. Executive Summary

In Stage 192.10, the Mach IPC subsystem (`MachIpcSubsystem`, `MachPortSpace`, and `MachPortObject`) and its integration into the Darwin userspace engine (`DarwinUserspaceEngine::handle_mach_trap`) were audited against Apple XNU kernel references (`osfmk/mach/message.h`, `osfmk/mach/port.h`, and `osfmk/kern/syscall_sw.c`).

Key capabilities validated in Stage 192.10:
1. **Port Rights Lifecycle & User Reference Counting:**
   - Evaluated port allocation, ownership tracking, right types (`kMachPortRightReceive`, `kMachPortRightSend`, `kMachPortRightSendOnce`), and user reference (`urefs`) deallocation mechanics.
2. **Standard Mach Trap Dispatch:**
   - Validated guest traps matching public XNU indices:
     - `task_self_trap` (`-28`): Allocates / returns task self port.
     - `mach_reply_port` (`-26`): Allocates / returns thread reply port.
     - `thread_self_trap` (`-27`): Returns thread self port.
     - `host_self_trap` (`-29`): Returns host self port.
3. **`mach_msg` Message Delivery:**
   - Validated message header parsing (`msgh_bits`, `msgh_size`, `msgh_remote_port`, `msgh_local_port`, `msgh_id`) and FIFO payload queuing into kernel port objects.
4. **Mach Error Return Code Conformance:**
   - `kMachSendInvalidData` (`0x10000002`): Truncated message header (`send_size < sizeof(MachMsgHeaderRaw)`).
   - `kMachSendInvalidDest` (`0x10000003`): Unmapped / invalid destination port.
   - `kMachRcvInvalidName` (`0x10004002`): Unmapped receive port name.
   - `kMachRcvTimedOut` (`0x10004003`): Dequeue from empty port queue.
   - `kMachRcvTooLarge` (`0x10004004`): Receive buffer smaller than queued message payload.
5. **Bidirectional Request-Reply Pattern:**
   - Validated request-reply IPC exchange between client and server tasks.
   - Validated kernel port translation: When client sends a request with its `msgh_local_port` as the reply port, the kernel translates this right into a valid send right in the server's task port space upon receive, allowing the server to transmit replies directly back to the client.

---

## 2. Technical Architecture & Traps

### 2.1 Mach Trap Reference Mapping
```cpp
trap_nr == -26: mach_reply_port_trap
trap_nr == -27: thread_self_trap
trap_nr == -28: task_self_trap
trap_nr == -29: host_self_trap
trap_nr == -31: mach_msg_trap
trap_nr == -32: mach_msg_overwrite_trap
```

### 2.2 mach_msg Port Rights Translation
When a message with `msgh_local_port` is dequeued by the receiver:
```cpp
if (dequeued->reply_port_object) {
    mach_port_t receiver_remote_port = space->allocate_port(dequeued->reply_port_object, kMachPortRightSend);
    hdr.msgh_remote_port = receiver_remote_port;
    hdr.msgh_local_port = kMachPortNull;
}
```

---

## 3. Test Verification Matrix

A dedicated test suite [`tests/unit/stage_192_10_mach_ipc_tests.cpp`](file:///D:/WinISO26/WinIOS/tests/unit/stage_192_10_mach_ipc_tests.cpp) was developed and registered as CMake target `stage_192_10_mach_ipc_tests`:

| Test # | Focus Area | Description | Status |
|---|---|---|---|
| **Test 1** | Port Rights Lifecycle | Allocation, right insertion, reference counting (`urefs`), and deallocation | **PASSED** |
| **Test 2** | Standard Mach Traps | Trap execution for `task_self` (-28), `reply_port` (-26), `thread_self` (-27), `host_self` (-29) | **PASSED** |
| **Test 3** | `mach_msg` Message Delivery | Header verification, payload transport, and message ID matching | **PASSED** |
| **Test 4** | Error Matrix & Bounds | Validation of `kMachSendInvalidData`, `kMachSendInvalidDest`, `kMachRcvInvalidName`, `kMachRcvTimedOut`, `kMachRcvTooLarge` | **PASSED** |
| **Test 5** | Request-Reply Pattern | Bidirectional IPC exchange with port rights translation in receiver task space | **PASSED** |

### Output:
```
=========================================================
STAGE 192.10: Darwin Mach IPC Conformance Tests
=========================================================
[Test 1] Port Rights Allocation, Ownership, and Deallocation...
[PASS] Port Rights Allocation, Ownership, and Deallocation
[Test 2] Standard Mach Trap Dispatch (task_self, thread_self, reply_port)...
[PASS] Standard Mach Trap Dispatch (task_self, thread_self, reply_port)
[Test 3] mach_msg Send, Receive, and Payload Verification...
[PASS] mach_msg Send, Receive, and Payload Verification
[Test 4] mach_msg Error Matrix & Boundary Validation...
[PASS] mach_msg Error Matrix & Boundary Validation
[Test 5] Bidirectional Request-Reply Pattern with Port Translation...
[PASS] Bidirectional Request-Reply Pattern with Port Translation

Results: 5 passed, 0 failed
```

---

## 4. Conformance & Regression Status
- MSVC C++20 build with `/W4 /WX /permissive-`: Clean (0 warnings, 0 errors).
- All prior stages (192.0 through 192.9) continue to pass.
