# Stage 240.18: Desktop and Window Integration

## 1. Window Management & Input Routing
1. **Window Creation**:
   - Guest requests window creation via `WindowManager::create_window`.
   - Validates ownership, assigning unique window ID to the calling task.
2. **Input Routing**:
   - `InputRouter::route_event` routes keyboard and pointer events to the active foreground process.
   - Confirms process-level input isolation: background processes receive no keyboard telemetry.
3. **Lifecycle Cleanup**:
   - Window resources destroyed automatically upon task termination. Verified in `stage240_19_multi_app_runtime_tests.cpp`.
