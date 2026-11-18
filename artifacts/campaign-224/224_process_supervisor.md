# WinIOS Campaign 224 — Stage 224.15 Process Supervision & Lifecycle Management

## 1. Supervisor Architecture
`AppSupervisor` (`src/kernel/app_supervisor.h`) operates as the centralized supervisor for guest process lifecycles and resource containment.

### Responsibilities
- **Registration & Binding:** On process launch, maps `pid` to canonical `bundle_id` and binds security tokens in `CapabilityManager`.
- **Termination Management:**
  - `request_termination(pid)`: Graceful teardown signalling.
  - `force_kill(pid, exit_code)`: Instant termination with crash categorization.
- **Automated Resource Cleanup:**
  Upon process exit or crash, `cleanup_process_resources()` automatically executes:
  1. Eviction and unregistration of all IPC ports and message queues owned by `pid`.
  2. State transition in `AppLifecycleManager` to `AppState::Terminated`.
  3. Window destruction and non-client cleanup in `WindowManager`.
  4. Compositor damaged surface invalidation and full damage redraw.
