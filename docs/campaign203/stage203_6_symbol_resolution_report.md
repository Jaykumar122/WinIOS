# WinIOS Campaign 203 Stage 203.6: Symbol Resolution Expansion Report

## 1. Overview
Stage 203.6 expands and hardens the symbol resolution and binding subsystem within `DyldEngine` (`src/loader/dyld_engine.h`, `src/loader/dyld_engine.cpp`). It implements robust two-level namespace semantics, weak import binding rules, transitive re-export symbol resolution, cycle-safe recursion, and strict symbol/string table bounds verification.

## 2. Key Capabilities Implemented
1. **Two-Level Namespace Resolution (`resolve_symbol_two_level`):**
   - **Ordinal-Specific Lookup (`library_ordinal > 0`):** Resolves symbols strictly from the designated dynamic library dependency (1-based ordinal).
   - **Special Ordinals:** Supports `BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE` (-1), `BIND_SPECIAL_DYLIB_FLAT_LOOKUP` (-2), and `BIND_SPECIAL_DYLIB_SELF` (0).
   - **Slide Application:** Accurately applies the ASLR slide offset associated with the target module when resolving addresses.
2. **Weak Import Semantics:**
   - When a weak import (`is_weak_import == true`) cannot be located in the designated module or flat scope, dyld binds the symbol address to `0` (NULL) and returns `DyldLinkStatus::Success`.
   - When a non-weak import (`is_weak_import == false`) cannot be located, dyld returns `DyldLinkStatus::UndefinedSymbol` with an error message detailing the missing symbol and library ordinal.
3. **Transitive Re-exported Symbols (`mod.reexports`):**
   - When resolving a symbol in a module, if the symbol is not in `mod.exports`, the engine recursively searches each re-exported dylib (`LC_REEXPORT_DYLIB`).
   - A visited set (`std::set<std::string> visited`) prevents infinite loops or stack overflow in the event of circular re-export declarations (e.g. A re-exports B, B re-exports A).
4. **Symbol Table (`LC_SYMTAB`) and String Table Bounds Validation:**
   - Integer overflow checks prevent 32-bit wrapping when multiplying `nsyms * sizeof(NList64Raw)` and adding offsets.
   - String index checks enforce `n_strx < strsize` for every symbol entry.
   - Null-termination scanning guarantees that symbol name strings are safely terminated before the end of the string table buffer, preventing memory over-reads.

## 3. Verification Results
The test suite `tests/unit/stage203_6_symbol_resolution_tests.cpp` was compiled and executed under MSVC C++20 (`/W4 /WX /permissive-`):
- Release: 3/3 tests PASSED.
- Debug: 3/3 tests PASSED.
- Cumulative Stage 203.6 tests: 3 registered, 3 passed, 0 failed.
