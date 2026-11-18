# WINIOS CAMPAIGN 217 — GUEST RUNTIME ARCHITECTURE

**Date:** November 18, 2026  
**Campaign:** 217  
**System:** WinIOS Freestanding Guest Application Runtime

---

## 1. Architectural Overview

Campaign 217 provides a cohesive guest execution runtime layered atop the Darwin kernel simulation and window management services:

```
+-----------------------------------------------------------------------------------+
|                           Guest Application Layer                                 |
|         Runtime Inspector   |   Resource Viewer   |   Task Notes Application      |
+-----------------------------------------------------------------------------------+
|                           Guest UI & Framework Layer                              |
|     GuestWidget | GuestContainer | GuestLabel | GuestButton | GuestTextInput      |
|              GuestLayoutEngine (Linear Horiz/Vert) | FontEngine (8x16)            |
+-----------------------------------------------------------------------------------+
|                         Runtime & Application Services                            |
|    AppLifecycleManager | AppPreferences | ResourceManager | ResourceBundle        |
|     GuestEventLoop     | GuestStorageApi | GuestAsyncIo    | GuestIpcRouter        |
+-----------------------------------------------------------------------------------+
|                            Core Freestanding Runtime                              |
|       GuestAppContract (ABI v1.2) | GuestAllocator (Heap) | mini_crt              |
+-----------------------------------------------------------------------------------+
|                         Kernel & Host Subsystems (EL1/Host)                       |
|       ARM64 CPU Engine | IsolatedStorage | WindowManager | Compositor             |
+-----------------------------------------------------------------------------------+
```

---

## 2. Core Components

### 2.1 Guest Application Contract (`GuestAppContract`)
Defines the standard entry and exit interface between the WinIOS runtime and guest binaries:
- Register `X0` points to a 96-byte `GuestAppArgBlock`:
  - `abi_version`: `WINIOS_CATEGORY_C_ABI_VERSION_1_2` (`0x00010200`)
  - `process_id`: Unique guest process ID
  - `bundle_id`: Null-terminated string (up to 64 chars)
  - `heap_base` / `heap_size`: Sandboxed memory bounds
  - `storage_path`: Sandboxed path
  - `flags`: Startup flags
- Stack Pointer (`SP`): Strictly 16-byte aligned.
- Exit Handling: Clean exit via stub writing to `GuestExitDescriptor`.

### 2.2 Memory Allocator (`GuestAllocator`) & `mini_crt`
- Bump pointer allocator with per-allocation metadata and quota limits.
- Supports in-place expansion when reallocating the most recent block.
- Double-free protection and leak tracking (`DetectLeaks`).
- `mini_crt` provides freestanding utilities: `w_memcpy`, `w_memset`, `w_strlen`, `w_strcmp`, `w_snprintf`, `w_atoi`.

### 2.3 Event Loop & Dispatch (`GuestEventLoop`)
- Thread-safe FIFO queue for input and system events.
- Dispatches keyboard, mouse, touch, resize, close, and virtual timer events.
- Guarantees event source provenance integrity (`EventSourceProvenance`).

### 2.4 Resource Bundles & Asset Loader (`ResourceManager`)
- Bundles packaged into `.wiosres` archive format with CRC32 checks.
- Built-in decoder for uncompressed 24bpp and 32bpp Windows BMP files.
- In-memory cache for fast asset retrieval.

### 2.5 UI Framework & Layout Engine (`GuestUIFramework`, `GuestLayoutEngine`)
- Tree-based widget hierarchy rooted at `GuestContainer`.
- Linear layout engine automatically arranges children horizontally or vertically with spacing, margins, and padding.
- `FontEngine` renders text using an embedded 8x16 bitmap font into ARGB surfaces.
