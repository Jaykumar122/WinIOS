# WinIOS Campaign 224 — Stage 224.13 & 224.14 IPC Reliability v2 & Lifecycle

## 1. Generation-Tagged Handle Architecture
To resolve the ABA problem and prevent stale handle reuse when ports are closed and IDs recycled, `GuestIpcRouter` introduces 64-bit generation-tagged handles:

$$\text{PortHandle} = (\text{generation} \ll 32) \mid (\text{port\_id} \ \& \ 0\text{xFFFFFFFF})$$

- When a port is allocated, its initial generation is 1.
- When destroyed, any subsequent IPC operations referencing that generation will be rejected immediately with `false`.
- Port ID recycling with a new generation will not match the stale handle.

---

## 2. Directory Discovery & Authentication
- **Named Port Directory:** Services can bind public names (`"com.winios.messenger.port"`) via `RegisterPortName`.
- **Discovery:** Other processes query `LookupPortName` to obtain the live, generation-verified port handle without out-of-band communication.
- **Queue Limits:** Strict queue depth bounds ($32$ messages max) and message size ceiling ($4096$ bytes max).
- **Process Crash Eviction:** When a process crashes or is terminated by `AppLifecycleManager`, `CleanupProcess(pid)` automatically unregisters all associated names and wipes pending queues, preventing memory leaks and orphaned receivers.
