# WinIOS Campaign 204 — Stage 204.9 Report
## Loader Relocation and Protection Finalization

**Date:** 2026-11-16  
**Status:** PASS  
**Component:** `src/loader/userspace_macho_loader.h`, `src/loader/userspace_macho_loader.cpp`  
**Test Suite:** `tests/unit/stage204_9_loader_relocation_protection_tests.cpp`

---

## 1. Overview & Architectural Scope

Stage 204.9 integrated the full userspace loader relocation and protection finalization lifecycle.
In modern Darwin runtimes (dyld-852+), dynamic linking requires writable access to `__DATA_CONST` (GOT, nl_symbol_ptr) during pointer rebasing and binding resolution. Once fixups are committed to guest virtual memory, write access must be permanently revoked via `mprotect` (`R--` / `kDarwinVmProtRead`), preserving strict Apple Silicon $W \oplus X$ guarantees.

---

## 2. Pipeline Execution Sequence

```
1. Map Segments into DarwinVmSpace
   ├── __PAGEZERO: kDarwinVmProtNone (0x0)
   ├── __TEXT:      kDarwinVmProtRead | kDarwinVmProtExecute (R-X)
   ├── __DATA_CONST:kDarwinVmProtRead | kDarwinVmProtWrite (RW-) [Temporary fixup window]
   └── __DATA:      kDarwinVmProtRead | kDarwinVmProtWrite (RW-)
2. Apply Rebases (ASLR Slide)
   └── Read guest pointer -> Add aslr_slide -> Write updated pointer via mach_vm_write
3. Apply Bindings (Two-Level & Flat Resolution)
   └── Resolve symbol via DyldEngine -> Write target virtual address into GOT
4. Finalize Protections
   └── mprotect(__DATA_CONST, kDarwinVmProtRead) [Revoke Write Access]
5. Architectural Protection Verification
   ├── Verify W^X across all mapped segments
   ├── Attempt rogue write to __DATA_CONST -> MUST FAIL
   ├── Attempt rogue write to __TEXT -> MUST FAIL
   └── Write to __DATA -> MUST SUCCEED
```

---

## 3. Test Evidence

The test suite in `tests/unit/stage204_9_loader_relocation_protection_tests.cpp` verified:
- Correct application of ASLR slide to internal pointers.
- Accurate dynamic symbol binding into memory via `mach_vm_write`.
- Successful revocation of write permissions on `__DATA_CONST` resulting in subsequent writes failing with memory protection errors.
- Strict $W \oplus X$ compliance across `__PAGEZERO`, `__TEXT`, `__DATA_CONST`, and `__DATA`.
- 100% pass rate in both MSVC Release and Debug builds.

**Stage 204.9 Status: PASS**
