# WinIOS Campaign 224 — Stage 224.19 & 224.20 Desktop Application Launcher

## 1. Registry-Driven Discovery
`DesktopLauncher` (`src/graphics/desktop_launcher.h`) replaces hardcoded application paths by discovering installed applications directly from `ApplicationRegistry`.

### Features
- Queries `ApplicationRegistry::list_apps(active_only = true)`.
- Inspects running process state via `AppSupervisor::is_running(bundle_id)`.
- If an application is already running, clicking its launcher entry focuses and raises its existing window to the front without spawning duplicate processes.
- If not running, launches the package binary via `LaunchHandler` and registers the newly created process with `AppSupervisor`.
- Provides keyboard and pointer navigation (`select_next`, `select_previous`, `launch_selected`).
- Captures and displays launch failures if binary admission or runtime initialization fails.
