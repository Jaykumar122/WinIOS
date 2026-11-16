# WinIOS Campaign 203 Stage 203.18: Mach Message Descriptors & Complex Messages Report

**Date:** 2026-11-16  
**Status:** COMPLETE (PASS)  
**Target Device Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Environment:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`)  
**Test Suite:** `stage203_18_mach_message_descriptors_tests` (Release & Debug)

---

## 1. Executive Summary

Stage 203.18 implements and independently validates Mach IPC complex messages (`MACH_MSGH_BITS_COMPLEX = 0x80000000`), typed descriptors (`MachMsgBodyRaw`), port right transfers, and Out-Of-Line (OOL) memory transfers across task boundaries in `MachIpcSubsystem`.

Key verified architectural features:
1. **Port Descriptors (`MACH_MSG_PORT_DESCRIPTOR`):**
   - `MACH_MSG_TYPE_MAKE_SEND`: Safely creates and exports a new send right to the receiver without consuming or modifying the sender's underlying receive right.
   - `MACH_MSG_TYPE_MOVE_RECEIVE`: Atomically transfers the receive right from sender to receiver, reassigning `receiver_task_` on the kernel object and invalidating the sender's right.
   - Port name translation: In-flight descriptors resolve objects in the sender's space and allocate appropriate new right handles in the receiver's space.
2. **Out-Of-Line Memory Descriptors (`MACH_MSG_OOL_DESCRIPTOR`):**
   - Captures large memory buffers out-of-line without packing overhead into the message header.
   - Respects `deallocate = 1`, zeroing/unmapping the sender's source memory region upon message transmission.
   - Automatically maps/copies the OOL payload into the receiver's memory space, rewriting `address` in the received descriptor to point to the receiver's memory buffer.
3. **Mixed Descriptor Pipelines:** Correctly handles arbitrary sequences of Port and OOL descriptors alongside trailing inline payloads, preserving bit-exact data fidelity and structural integrity.

---

## 2. Test Execution Matrix

| Test Case | Description | Result (Release) | Result (Debug) |
|---|---|---|---|
| `test_port_descriptor_make_send` | Transferring send rights via MAKE_SEND, sender retention | PASS | PASS |
| `test_port_descriptor_move_receive` | Transferring receive rights via MOVE_RECEIVE, sender revocation, receiver ownership | PASS | PASS |
| `test_ool_memory_descriptor` | OOL memory capture, deallocate=1 sender clearing, receiver placement & fidelity | PASS | PASS |
| `test_mixed_complex_message` | Multi-descriptor complex message (Port + OOL + Trailer magic) transfer | PASS | PASS |

---

## 3. Verification Details

All tests compiled under `/W4 /WX /permissive-` with 0 warnings and executed with 0 failures in both Release and Debug configurations.
