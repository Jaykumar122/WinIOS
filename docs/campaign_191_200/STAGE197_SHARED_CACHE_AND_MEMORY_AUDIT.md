# WINIOS STAGE 197: DYLD SHARED CACHE AND MEMORY AUDIT
## Capability Level Deconstruction, Granule Alignment, W^X Protection, and Arena Boundary Auditing

**Target:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120` / ARM64e)  
**Host Toolchain:** Windows 11 Pro x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Apple Reference Source:** `https://github.com/apple-oss-distributions/dyld/blob/main/common/MachOFile.cpp`  
**Deliverable Document:** `docs/campaign_191_200/STAGE197_SHARED_CACHE_AND_MEMORY_AUDIT.md`

---

### 1. Seven-Level Capability Assessment of Dyld Shared Cache

To prevent architectural overstatement, WinIOS deconstructs its dyld shared cache capabilities into seven explicit levels:

| Level | Capability Description | WinIOS Status | Detailed Verification Evidence |
| :---: | :--- | :---: | :--- |
| **Level 1** | Header Parsing | **YES** | Validates `dyld_v1  arm64e` and `dyld_v1   arm64` magic, base address, slide info offset, local symbols offset |
| **Level 2** | Mapping Metadata Parsing | **YES** | Validates mapping table offsets and sizes, extracts `address`, `size`, `file_offset`, `init_prot`, `max_prot` |
| **Level 3** | Image Catalog Parsing | **YES** | Traverses `DyldCacheImageInfoRaw`, extracts string paths from file offsets, detects corrupted table offsets |
| **Level 4** | Mapping Regions into Guest Memory | **PARTIAL** | Discrete dylibs are mapped into the 2GB shared library arena; monolithic 4GB+ cache file mapping is not yet wired |
| **Level 5** | Resolving Cache-Relative Pointers | **PARTIAL** | Pointer unchaining supports `DYLD_CHAINED_PTR_64_OFFSET`, but assumes pre-segmented image bases |
| **Level 6** | Resolving Shared-Cache Monolithic Exports | **PARTIAL** | Export Trie parser supports per-dylib export tries; monolithic sub-cache tries require multi-trie lookup |
| **Level 7** | Executing an Image directly from Shared Cache | **NO** | Not implemented; images are executed via `SharedLibraryMapper` as standalone dylibs |

> [!IMPORTANT]
> **Definitive Conformance Statement:**
> WinIOS currently implements **Level 1 through Level 3** (structural parsing and validation) with **Partial Level 4–6** support. It does **not** claim full monolithic shared cache execution (Level 7).

---

### 2. Virtual Memory Architecture & Alignment Audit

#### A. 16 KB Apple Silicon Page Granularity
- In Apple Silicon (A16 Bionic), userspace MMU page granule is strictly 16 KB (`0x4000` bytes).
- In `UserspaceMachOLoader::validate_segments`:
  ```cpp
  if ((s.vmaddr % page_size) != 0) {
      out_error = "Segment " + s.name + " vmaddr is not page aligned";
      return UserspaceMachOLoadStatus::MisalignedSegment;
  }
  ```
  Any Mach-O binary whose segments do not align to 16 KB is rejected.

#### B. Strict Apple Silicon W^X Memory Protection
- Apple Silicon enforces strict W^X (Write XOR Execute) policy in userspace hardware.
- Audited across all admitted binaries:
  ```cpp
  bool is_w = (seg.initprot & kProtWrite) != 0;
  bool is_x = (seg.initprot & kProtExecute) != 0;
  assert(!(is_w && is_x)); // No segment may be RWX simultaneously
  ```
  - `__TEXT`: `r-x` (Read + Execute)
  - `__DATA_CONST`: `r--` (Read-only)
  - `__DATA`: `rw-` (Read + Write)
  - `__LINKEDIT`: `r--` (Read-only)

#### C. Dedicated Shared Library Arena (2 GB)
- Managed by `SharedLibraryMapper`:
  - Base Virtual Address: `0x0000000180000000ULL` (6 GB mark)
  - Arena Capacity: `0x0000000080000000ULL` (2 GB)
  - End Address: `0x0000000200000000ULL` (8 GB mark)
- Images are placed sequentially with 16 KB alignment padding.
- Bounds exhaustion check:
  If `allocated_base + vmsize > arena_base + arena_size`, returns `SharedLibMapStatus::MemoryExhausted`.

#### D. Collision Detection and Mapping Rollback
- `SharedLibraryMapper::check_collision` verifies that newly mapped segments do not overlap with existing images or the primary executable.
- `SharedLibraryMapper::unmap_image` zeroes the backing guest memory range and removes the image record, restoring arena space without fragmentation.

---

### 3. Conclusion

The memory and shared-cache audit verifies that WinIOS rigorously adheres to 16 KB alignment, strict W^X policies, and isolated 2 GB shared library arena partitioning, while transparently scoping shared-cache capabilities to structural metadata parsing (Levels 1–3).
