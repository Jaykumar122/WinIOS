# WinIOS Campaign 223 — Guest Application Framework Architecture (Stage 223.2)

## 1. Executive Summary

Campaign 223 establishes **Guest Application Framework v2** (`winios_app_t`), elevating WinIOS from raw Mach-O syscall programming to a structured, declarative, and robust guest application framework. Freestanding ARM64 Mach-O guest applications now execute within a structured lifecycle contract with inversion-of-control, zero-heap layout nodes, and deterministic event dispatching.

---

## 2. Architectural Design: `winios_app_t` Contract

The framework encapsulates window management, main loop orchestration, event dispatching, frame timing, and clean shutdown into a cohesive C API:

```c
typedef struct winios_app_s winios_app_t;

typedef struct {
    const char*  title;
    uint32_t     window_x;
    uint32_t     window_y;
    uint32_t     width;
    uint32_t     height;
    void*        user_data;
    
    // Lifecycle & event callbacks
    void (*on_init)(winios_app_t* app, void* user_data);
    int  (*on_event)(winios_app_t* app, const winios_event_v2_t* event, void* user_data);
    void (*on_update)(winios_app_t* app, uint32_t delta_us, void* user_data);
    void (*on_render)(winios_app_t* app, uint32_t* fb, uint32_t stride, void* user_data);
    void (*on_destroy)(winios_app_t* app, void* user_data);
} winios_app_config_t;

struct winios_app_s {
    winios_app_config_t config;
    winios_wid_t        main_window_id;
    uint32_t            width;
    uint32_t            height;
    int                 is_running;
    int                 is_foreground;
    int                 needs_redraw;
    int                 is_dispatching; // Reentrancy guard
};
```

### Key Architectural Invariants
1. **Reentrancy Protection**: `is_dispatching` guards against nested event loops or recursive event processing calls that could corrupt application stack or memory state.
2. **Deterministic Startup**: Window creation occurs automatically before `on_init`, guaranteeing valid window and surface IDs during initialization.
3. **Cooperative Yielding**: If no events are queued during a frame, the runtime yields execution slices via `winios_sched_yield()` to prevent 100% host CPU spinning.
4. **Invalidation Driven Rendering**: Rendering runs only when `needs_redraw == 1` or explicitly triggered by `winios_app_invalidate(app)`, avoiding unnecessary redraw overhead.
5. **Clean Destruction**: `on_destroy` is guaranteed to be invoked after the event loop breaks, followed by orderly `winios_window_destroy(app->main_window_id)`.

---

## 3. Verification & Evidence

- Source: `guest-sdk/include/winios/app/app.h`
- Implementation: `guest-sdk/lib/winios_runtime.c`
- Test Suite: `tests/unit/stage223_2_app_framework_tests.cpp` (100% PASS)
