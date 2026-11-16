# WinIOS Campaign 204 — Stage 204.10 Report
## Runtime Initialization and Termination Order

**Date:** 2026-11-16  
**Status:** PASS  
**Component:** `src/loader/dyld_engine.h`, `src/loader/dyld_engine.cpp`  
**Test Suite:** `tests/unit/stage204_10_image_initialization_order_tests.cpp`

---

## 1. Overview & Architectural Scope

Stage 204.10 completed Phase B (Mach-O & Dynamic Linker Expansion) by implementing deterministic topological dependency initialization (`__mod_init_func`) and reverse topological termination (`__mod_term_func` / atexit) within the WinIOS Darwin dyld runtime.

---

## 2. Initialization & Termination Mechanics

| Subsystem | Behavioral Semantics |
|---|---|
| **Bottom-Up Topological Order** | `compute_topological_initializers` executes dynamic library initializers depth-first, ensuring all dependencies are fully initialized before dependent modules execute |
| **Reverse Topological Order (LIFO)** | `compute_topological_terminators` invokes module termination handlers in exact reverse order (root applications first, leaf libraries last) |
| **Circular Dependency Cycle Breaking** | Graph cycles in dependent libraries are detected during depth-first traversal and broken deterministically without deadlock or stack exhaustion |
| **Run-Once Guarantee** | Boolean flags (`is_initialized`, `is_terminated`) guarantee that each module's routines are invoked at most once per process lifetime |
| **Pointer Executability Validation** | `validate_initializers` enforces that all initializer function pointers lie strictly within an executable segment (`kVmProtExecute`), rejecting null or data pointers |

---

## 3. Test Evidence

The test suite in `tests/unit/stage204_10_image_initialization_order_tests.cpp` verified:
- Correct bottom-up initialization order for a 4-node diamond dependency DAG (`libbase` -> `liba`, `libb` -> `MainApp`).
- Correct LIFO termination order upon exit (`MainApp` -> `libb`, `liba` -> `libbase`).
- Strict run-once execution under duplicate scheduler invocations.
- Deterministic cycle resolution between mutually-dependent modules.
- Rejection of invalid initializer pointers residing outside executable segment ranges.

**Stage 204.10 Status: PASS**
