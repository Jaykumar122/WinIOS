# WinIOS Campaign 204 — Stage 204.8 Report
## Runtime Search Paths and Dependency Identity

**Date:** 2026-11-16  
**Status:** PASS  
**Component:** `src/loader/dyld_engine.h`, `src/loader/dyld_engine.cpp`  
**Test Suite:** `tests/unit/stage204_8_rpath_dependency_identity_tests.cpp`

---

## 1. Overview & Architectural Scope

Stage 204.8 implemented complete Apple dyld search path resolution and sandbox boundary isolation within the WinIOS runtime environment.
This includes lexical virtual path normalization, expansion of `@executable_path` and `@loader_path` variables, ordered evaluation of `LC_RPATH` directives, standard system fallback directories, and strict sandboxing to prevent guest filesystem traversal.

---

## 2. Path Resolution Engine & Security Semantics

| Feature | Implementation | Behavioral Description |
|---|---|---|
| **Lexical Path Normalization** | `normalize_virtual_path` | Unifies path delimiters to `/`, collapses redundant `//`, eliminates `.`, and evaluates `..` while preventing upward traversal past virtual root `/` |
| **Token Expansion** | `expand_path_tokens` | Replaces `@executable_path` with primary application directory, and `@loader_path` with the directory containing the referring binary/dylib |
| **Sandbox Isolation** | `is_path_sandboxed` | Enforces that all evaluated candidate paths stay strictly inside `sandbox_root`, rejecting traversal attempts (e.g. `../../etc/passwd`) |
| **Priority-Ordered RPATHs** | `resolve_dependency_path` | Iterates `LC_RPATH` entries in declaration order, evaluating expanded paths before proceeding to fallback locations |
| **Standard Fallbacks** | `fallback_paths` | Default search in `/usr/lib`, `/usr/local/lib`, and `/System/Library/Frameworks` when symbols are not located in binary-specific RPATHs |
| **Virtual File Predicate** | `file_exists_predicate` | Enables dependency resolution across simulated in-memory VFS and clean-room binary fixtures |

---

## 3. Test Evidence

The test suite in `tests/unit/stage204_8_rpath_dependency_identity_tests.cpp` verified:
- Accurate normalization of complex paths containing relative dot-dot segments and mixed slashes.
- Token expansion of `@executable_path` and `@loader_path`.
- Enforced sandbox boundaries and rejection of out-of-sandbox traversals.
- Priority selection among competing `@rpath` locations and fallback directories.
- Full verification under both MSVC Release and Debug builds.

**Stage 204.8 Status: PASS**
