# WinIOS Campaign 203 Stage 203.9: Image Initializer Metadata Report

## 1. Overview
Stage 203.9 implements discovery, validation, and deterministic ordering of image initializers (`__mod_init_func`) in `MachOLoader` and `DyldEngine` (`src/loader/macho_loader.h`, `src/loader/macho_loader.cpp`, `src/loader/dyld_engine.h`, `src/loader/dyld_engine.cpp`).

## 2. Key Capabilities Implemented
1. **`__mod_init_func` Discovery:**
   - `MachOLoader::parse_metadata` inspects all sections within segment load commands (`LC_SEGMENT_64`).
   - Sections with name `"__mod_init_func"` or section type `0x09` (`S_MOD_INIT_FUNC_POINTERS`) are parsed as arrays of 64-bit function pointers.
   - Pointers are safely extracted and recorded in `MachOLoadResult::mod_init_funcs`.
2. **Initializer Target Boundary Validation (`validate_initializers`):**
   - Ensures initializer function pointers are non-null (`fn != 0`).
   - Verifies that every initializer target resides within an executable segment (`(initprot & kVmProtExecute) != 0`).
   - Rejects targets pointing into non-executable segments (e.g. `__DATA`) or out-of-bounds addresses with clear diagnostics.
3. **Deterministic Multi-Image Dependency Sequencing (`get_initialization_order`):**
   - Consumes the topologically sorted load order from the dependency graph (Stage 203.5).
   - Sequences initializer execution so that dependencies run before dependees (leaves -> intermediate dylibs -> main application).
   - Applies the appropriate ASLR slide offset to each initializer pointer (`fn + mod.slide`).
4. **Duplicate Execution Prevention:**
   - Tracks image initialization status via `DyldLoadedModule::is_initialized`.
   - Repeated calls to `get_initialization_order` skip already-initialized images, preventing duplicate constructor invocations.

## 3. Verification Results
The test suite `tests/unit/stage203_9_image_initializer_tests.cpp` was compiled and executed under MSVC C++20 (`/W4 /WX /permissive-`):
- Release: 3/3 tests PASSED.
- Debug: 3/3 tests PASSED.
- Cumulative Stage 203.9 tests: 3 registered, 3 passed, 0 failed.
