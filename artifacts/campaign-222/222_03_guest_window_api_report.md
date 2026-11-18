# Campaign 222 — Stage 222.3 Guest Window API v1 & Window Lifecycle Subsystem Report

## Objectives & Achievements
1. **Extended Guest Window API Header**: Updated `guest-sdk/include/winios/ui/window.h` with:
   - `winios_window_set_visibility`
   - `winios_window_set_geometry`
   - `winios_window_request_focus`
   - `winios_window_get_state`
   - Enum definitions for `winios_window_state_t` (`WINIOS_WINDOW_STATE_HIDDEN`, `WINIOS_WINDOW_STATE_VISIBLE`, `WINIOS_WINDOW_STATE_MINIMIZED`, `WINIOS_WINDOW_STATE_MAXIMIZED`, `WINIOS_WINDOW_STATE_CLOSED`).
2. **C Runtime Implementation**: Added corresponding syscall dispatch wrappers in `guest-sdk/lib/winios_runtime.c` binding to `WINIOS_SYS_WINDOW_SET_VISIBILITY` (533), `WINIOS_SYS_WINDOW_SET_GEOMETRY` (509), and `WINIOS_SYS_WINDOW_SET_FOCUS` (510).
3. **Kernel Dispatcher Support**: Handled `kSysWiniosWindowSetVisibility` in `src/kernel/user_syscall_dispatcher.cpp` with strict caller PID access validation (`can_access`).
4. **Collision Prevention**: Protected window type declarations with `WINIOS_WINDOW_TYPES_DEFINED` across both SDK and host runtime contract headers.
5. **Unit Verification**: Built and executed `tests/unit/stage222_3_guest_window_api_tests.cpp` with 100% pass rate.
