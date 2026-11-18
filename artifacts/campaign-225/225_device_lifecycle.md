# WinIOS Campaign 225 — Stage 225.4 Virtual Device Lifecycle State Machine

## 1. Lifecycle State Machine
Virtual devices follow a deterministic finite state machine ([src/devices/device_types.h](file:///D:/WinISO26/WinIOS/src/devices/device_types.h), [src/devices/virtual_device_interface.h](file:///D:/WinISO26/WinIOS/src/devices/virtual_device_interface.h)) ensuring orderly power-on, initialization, operational execution, suspension, teardown, and error isolation.

```
       [Uninitialized]
              │
         initialize()
              ▼
           [Ready] ◄──────┐
          ╱       ╲       │
      start()     reset() │
        ▼           │     │
    [Running] ──────┘     │
     │     ▲              │
suspend() resume()        │
     ▼     │              │
   [Suspended]            │
        │                 │
      stop()              │
        ▼                 │
     [Stopped] ───────────┘
        │
   (Device Error) ──► [Error]
        │
   (Unregistered) ──► [Removed]
```

---

## 2. State Transition Table

| Current State | Transition Trigger | Next State | Allowed Operations |
| :--- | :--- | :--- | :--- |
| `Uninitialized` | `initialize()` | `Ready` | Allocates internal buffers, binds hardware ports |
| `Ready` | `start()` | `Running` | Begins event consumption, timer triggers, DMA buffers |
| `Running` | `suspend()` | `Suspended` | Pauses audio streams, drops sensor polling rate |
| `Suspended` | `resume()` | `Running` | Restores normal polling, reconnects frame buffers |
| `Running` / `Suspended` | `stop()` | `Stopped` | Flushes queues, silences audio channels |
| `Stopped` | `reset()` | `Ready` | Clears operational state to clean defaults |
| Any State | Internal failure | `Error` | Halts I/O, records fault telemetry |
| Any State | Unregister / Removal | `Removed` | Invalidates handle, marks slot available for reuse |

---

## 3. Subsystem Lifecycle Coordination
When `DeviceRegistry::initialize_all()` or `start_all()` is executed:
1. Devices initialize in topological order (Display $\rightarrow$ Input $\rightarrow$ Audio $\rightarrow$ Sensors $\rightarrow$ Network $\rightarrow$ Services).
2. Any failure in an essential device transitions it to `Error` without crashing neighboring devices.
3. Observers subscribed via `DeviceRegistry::add_observer` receive synchronous state transition callbacks to gracefully adjust system topology.

---

## 4. Verification Evidence
- [tests/unit/stage225_3_device_registry_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_3_device_registry_tests.cpp): Validates all valid state transitions and asserts that illegal transitions (e.g. `start()` on `Uninitialized`) return false without state mutation.
