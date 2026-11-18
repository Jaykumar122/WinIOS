# WinIOS Campaign 219 — Stage 219.18: Minimal Launch-Service Architecture Report

## 1. Architecture & Design

`ServiceManager` (`src/services/service_manager.h`) introduces a centralized daemon and launch-service framework modeled after Darwin's `launchd`:

- **Service Registry**: Manages registered services keyed by reverse-DNS names (e.g. `com.apple.windowserver`, `com.winios.workerd`).
- **Supervision & PID Linkage**: When a service starts, it is spawned under supervisor PID 1 (`launchd`).
- **Port Rendezvous**: Services register named IPC capabilities. Client processes query `resolve_service_port(name)` to obtain the destination port integer for message trap IPC without hardcoding endpoints.
- **Fault Tolerance & Auto-Restart**: Services configured with `auto_restart = true` automatically relaunch upon abnormal exit (non-zero status code) up to a bounded threshold (`max_restarts = 2..3`), preventing infinite crash loops.

## 2. Test Verification

Verified by `stage219_18_service_manager_tests.exe`:
- Registration and sorted enumeration of services.
- Rendezvous port resolution returning `1001` and `1002`.
- Graceful stopping and unregistration.
- Auto-restart recovery after crash, with bounded threshold enforcement (transitions to `Crashed` upon exceeding max restarts).
