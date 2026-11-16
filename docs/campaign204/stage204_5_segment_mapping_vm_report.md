# WinIOS Campaign 204 — Stage 204.5 Report
## Mach-O Segment Mapping & Image Placement into DarwinVmSpace

**Date:** 2026-11-16  
**Status:** PASS  
**Component:** `src/loader/userspace_macho_loader.h`, `src/loader/userspace_macho_loader.cpp`, `src/memory/darwin_vm_object.h`  
**Test Suite:** `tests/unit/stage204_5_segment_mapping_vm_tests.cpp`

---

## 1. Overview & Architectural Integration

Stage 204.5 bridges the Mach-O loader with the synthetic virtual memory manager (`DarwinVmSpace`).
`UserspaceMachOLoader::load_into_vm_space` maps each `LC_SEGMENT_64` into an isolated, per-process virtual memory address space conforming to Apple Silicon 16KB page granule boundaries (`0x4000` bytes).

---

## 2. Key Capabilities & Invariants Enforced

1. **Granule & Segment Alignment:**
   - Every segment virtual address (`vmaddr`), virtual size (`vmsize`), and file offset (`fileoff`) is validated against 16KB alignment.
2. **File-Backed vs. Zero-Filled Regions:**
   - Where `filesize < vmsize`, only `filesize` bytes are populated from the binary payload; the remaining `vmsize - filesize` region is automatically zero-filled by the underlying `DarwinVmObject`.
3. **ASLR Image Slide Relocation:**
   - When ASLR is active, all non-`__PAGEZERO` segment base addresses and the entry point are shifted by `aslr_slide`.
   - `__PAGEZERO` remains anchored at `0x0000000000000000ULL` as an unbacked, inaccessible barrier preventing NULL-pointer dereferencing.
4. **W^X Memory Protection:**
   - `__TEXT` segments map strictly as Read + Execute (`kDarwinVmProtRead | kDarwinVmProtExecute`), non-writable.
   - `__DATA` segments map strictly as Read + Write (`kDarwinVmProtRead | kDarwinVmProtWrite`), non-executable.
   - Any attempt to map or transition to `RWX` is rejected.
5. **Dynamic Protection Changes:**
   - Validated that `mprotect` dynamically modifies permissions on mapped segments and that `mach_vm_write` rejects attempts to write into write-protected segments with `kKernProtectionFailure`.

---

## 3. Test Evidence

The dedicated test suite `stage204_5_segment_mapping_vm_tests` confirmed:
- Multi-segment executable correctly places `__PAGEZERO`, `__TEXT`, `__DATA`, and `__LINKEDIT` into `DarwinVmSpace`.
- ASLR relocation shifts addresses and entry point by 512 MB while keeping unslid memory unmapped.
- Dynamic `mprotect` transitions from RW to Read-Only and bounds violations produce expected failures.

**Stage 204.5 Status: PASS**
