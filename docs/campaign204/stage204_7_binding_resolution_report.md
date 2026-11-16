# WinIOS Campaign 204 — Stage 204.7 Report
## Dynamic Binding and Import Resolution

**Date:** 2026-11-16  
**Status:** PASS  
**Component:** `src/loader/dyld_engine.h`, `src/loader/dyld_engine.cpp`  
**Test Suite:** `tests/unit/stage204_7_binding_resolution_tests.cpp`

---

## 1. Overview & Architectural Scope

Stage 204.7 extended the WinIOS dyld engine (`DyldEngine`) to provide complete Darwin dynamic binding and import resolution semantics, supporting modern Mach-O two-level namespaces, special ordinals, weak imports, symbol definition coalescing, and recursive re-export resolution.

---

## 2. Namespace & Resolution Rules Implemented

| Resolution Mechanism | Ordinal / Mode | Resolution Strategy |
|---|---|---|
| **Two-Level Namespace** | $k > 0$ | Direct lookup strictly in designated module $(k-1)$ and its transitive re-exports |
| **Main Executable** | `BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE` (`-1`) | Bound to the primary loaded executable image (module 0) |
| **Flat Lookup** | `BIND_SPECIAL_DYLIB_FLAT_LOOKUP` (`-2`) | Search in image load order across all loaded modules with weak definition preemption |
| **Self Lookup** | `BIND_SPECIAL_DYLIB_SELF` (`0`) | Bound to the importing module itself (`caller_module_name`) before fallback |
| **Weak Import** | `is_weak_import = true` | If symbol is missing, safely binds to `0` (`nullptr`) with `DyldLinkStatus::Success` |
| **Strong Import** | `is_weak_import = false` | If symbol is missing, fails with `DyldLinkStatus::UndefinedSymbol` and descriptive diagnostic |
| **Weak Def Coalescing** | `resolve_symbol_coalesced` | Strong symbol definitions strictly preempt weak symbol definitions across modules |
| **Re-Export Resolution** | `mod.reexports` | Transitive DFS resolution with cycle prevention via active module set tracking |

---

## 3. Verification & Test Evidence

The test suite in `tests/unit/stage204_7_binding_resolution_tests.cpp` verified:
1. **Two-Level Namespace Isolation:** Symbols with the same name in different libraries (`_version`) are isolated according to ordinal, preventing cross-library pollution.
2. **Special Ordinals:** Correctly resolves main executable (`-1`), self (`0`), and flat (`-2`) references.
3. **Weak Import Semantics:** Unresolved weak imports write NULL (`0`) to guest memory, while unresolved strong imports halt linking and report the missing symbol name.
4. **Weak Coalescing:** Strong definitions in late-loaded libraries override early weak definitions.
5. **Re-Export & Cycle Safety:** Multi-hop re-exports resolve symbols transparently; circular re-export topologies terminate cleanly without stack overflow.
6. **Memory Binding Integration:** Verified pointer writes into virtual memory via `apply_bindings`.

**Stage 204.7 Status: PASS**
