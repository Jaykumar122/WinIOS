# WINIOS CAMPAIGN 192 — STAGE 192.5 REPORT
## Multi-Image Dependency Graph and @rpath Expansion

**Campaign:** 192  
**Stage:** 192.5 (Multi-Image Dependency Graph and @rpath Expansion)  
**Date:** 2026-11-13  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Host Architecture:** Windows 11 x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Verdict:** **PASSED (6/6 test suites passed, 100% CTest regression clean, real three-image Mach-O dependency chain verified)**

---

## 1. Executive Summary

In Stage 192.5, Darwin's multi-image dependency graph resolution, path expansion rules, and circular dependency detection were audited, expanded, and validated across both synthetic DAG topologies and real compiled Mach-O image chains (`REAL-009`: `main_chain` -> `libmiddle.dylib` -> `libleaf.dylib`).

Key capabilities completed in Stage 192.5:
1. **Topological Dependency Ordering:** Depth-First Search (DFS) post-order traversal was implemented to guarantee that leaf dependencies are initialized and bound strictly before intermediate dependencies and root executables across linear and diamond DAG structures.
2. **Cycle Detection & Diagnostics:** DFS three-color cycle detection (unvisited = white, visiting = grey, visited = black) detects self-loops, 2-node cycles, and multi-node cyclic loops without infinite recursion, identifying participating cycle nodes.
3. **`@executable_path` and `@loader_path` Resolution:** Full compliance with Apple Darwin dyld path token semantics: `@executable_path` expands relative to the main binary's directory, and `@loader_path` expands relative to the referring image's directory (falling back to `@executable_path` if the referring image path is empty).
4. **Multi-Candidate `@rpath` Search Fallback Chains:** Implemented ordered expansion of `@rpath` references against active runpaths, expanding nested `@executable_path` and `@loader_path` prefixes within runpaths and appending the Darwin system fallback directory `/usr/lib/`.
5. **Weak vs Required Dependency Distinctions:** Ensured missing weak dylibs (`LC_LOAD_WEAK_DYLIB`) are gracefully handled without halting graph resolution or polluting `load_order`, while missing required dylibs (`LC_LOAD_DYLIB`) cause graph resolution failure.
6. **Real Mach-O Multi-Image Chain Validation:** Validated the dependency resolution against real compiled Mach-O binaries (`tests/artifacts/bin/main_chain`), verifying load order `["@rpath/libleaf.dylib", "@rpath/libmiddle.dylib", "main_chain"]`.

---

## 2. Technical Implementation Details

### 2.1 Extended Path Expansion (`MachODependencyGraph::resolve_rpath_fallbacks`)
Added support in `src/loader/macho_dependency_graph.cpp` and `src/loader/macho_dependency_graph.h` for generating prioritized candidate search paths:
```cpp
std::vector<std::string> MachODependencyGraph::resolve_rpath_fallbacks(
    std::string_view raw_path,
    std::string_view executable_path,
    const std::vector<std::string>& active_rpaths,
    std::string_view loader_path) const;
```
For `@rpath/libexample.dylib`:
- Iterates over all `active_rpaths` in order of appearance (`LC_RPATH`).
- Recursively expands `@executable_path` or `@loader_path` embedded within any runpath entry.
- Appends the standard Darwin fallback `/usr/lib/libexample.dylib`.

### 2.2 Graph Dependency Resolution (`MachODependencyGraph::resolve_dependencies`)
- Traverses registered nodes and dynamic dylib dependencies using DFS with cycle detection.
- Skips missing libraries from being added to the execution/load order, while accurately sorting dependencies so callees appear before callers.
- Categorizes missing libraries into `missing_required` (causing overall failure) versus `missing_weak` (permitting continued loading).

---

## 3. Test Verification Matrix

A dedicated test suite [`tests/unit/stage_192_5_multi_image_graph_tests.cpp`](file:///D:/WinISO26/WinIOS/tests/unit/stage_192_5_multi_image_graph_tests.cpp) was developed and registered as CMake target `stage_192_5_multi_image_graph_tests`:

| Test # | Focus Area | Description | Status |
|---|---|---|---|
| **Test 1** | Topological Dependency Ordering | Linear chain (Leaf -> Mid -> App) & Diamond DAG (Shared -> A, B -> Root) | **PASSED** |
| **Test 2** | Circular Dependency Detection | Self-loop, 2-node cycle (A <-> B), 3-node cycle (A -> B -> C -> A) | **PASSED** |
| **Test 3** | Path Token Expansion | `@executable_path` and `@loader_path` expansion | **PASSED** |
| **Test 4** | `@rpath` Fallback Chains | 3 active rpaths expanded plus `/usr/lib/` system fallback | **PASSED** |
| **Test 5** | Weak vs Required Dependencies | Missing required fails; missing weak succeeds with proper load order | **PASSED** |
| **Test 6** | Real Three-Image Mach-O Chain | Verifies dependency graph of real compiled `main_chain` binary | **PASSED** |

### Output:
```
[Test 1] Topological dependency ordering (linear & diamond DAG)...
[Test 2] Circular dependency detection...
[Test 3] @executable_path and @loader_path expansion...
[Test 4] Multi-candidate @rpath fallback chains...
[Test 5] Weak vs required missing dependencies...
[Test 6] Real multi-image Mach-O dependency chain (REAL-009 chain)...
ALL STAGE 192.5 MULTI-IMAGE GRAPH TESTS PASSED
```

---

## 4. Conformance & Regression Status
- MSVC C++20 build with `/W4 /WX /permissive-`: Clean (0 warnings, 0 errors).
- All previous stages (192.0 through 192.4) remain passing.
