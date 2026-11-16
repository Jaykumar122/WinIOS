# WinIOS Campaign 209: Physical Page Metadata Architecture

**Stage:** 209.6 — Physical Page Metadata Model  
**Campaign:** 209 — Genuine XNU VM Initialization, Physical Page Management & Kernel Memory Services  
**Date:** 2026-11-16  
**Auditor:** Principal Physical Memory Specialist & Kernel Systems Architect  

---

## 1. Executive Summary

This specification establishes the guest-visible physical-page metadata model for WinIOS Campaign 209, derived directly from Apple Darwin XNU (`xnu-10002.1.13` `osfmk/vm/vm_page.h` and `osfmk/arm/pmap.h`).

The model provides structured, guest-addressable tracking of:
- Physical page frame numbers (PFN = `paddr >> 14` for 16KB granule).
- Physical page attributes (`pmap_phys_attributes`: modified, referenced, managed).
- Allocation state (`FREE`, `ALLOCATED`, `RESERVED`, `WIRED`).
- Wire counts and synchronization flags (`busy`, `zeroed`).

---

## 2. Data Structure Definitions (C & C++ ABI Conformance)

### 2.1 Physical Page Attribute Bits (`pmap.h`)
```c
#define XNU_PHYS_MODIFIED    0x01u
#define XNU_PHYS_REFERENCED  0x02u
#define XNU_PHYS_MANAGED     0x04u
#define XNU_PHYS_NOENCRYPT   0x08u
```

### 2.2 Per-Frame Page Descriptor (`vm_page.h`)
```c
#pragma pack(push, 4)
typedef struct xnu_vm_page {
    uint32_t phys_page;   /* Physical page frame number (PA >> 14) */
    uint16_t wire_count;  /* Number of kernel wire references */
    uint8_t  state;       /* 0 = FREE, 1 = ALLOCATED, 2 = RESERVED, 3 = WIRED */
    uint8_t  flags;       /* bit 0 = busy, bit 1 = encrypted, bit 2 = zeroed */
} xnu_vm_page_t;
#pragma pack(pop)
```

Each page descriptor occupies exactly 8 bytes.

### 2.3 Physical Memory Allocation Header
```c
typedef struct xnu_page_metadata_header {
    uint32_t magic;           /* 0x584E5550 = "XNUP" */
    uint32_t total_pages;     /* 512 total pages in 8 MiB DRAM */
    uint32_t managed_pages;   /* 496 allocatable pages */
    uint32_t free_pages;      /* Count of currently free pages */
    uint64_t avail_start;     /* 0x00040000 */
    uint64_t avail_end;       /* 0x00800000 */
} xnu_page_metadata_header_t;
```

---

## 3. Placement in Stolen Physical Memory

The physical-page metadata structures reside in the reserved stolen metadata zone `[0x28000, 0x2A000)`:

| Address | Size | Data Structure | Contents |
| :--- | :--- | :--- | :--- |
| `0x00028000` | 32 bytes | `xnu_page_metadata_header_t` | Header magic, page counts, bounds |
| `0x00028020` | 512 bytes | `uint8_t pmap_phys_attributes[512]` | Attribute byte per 16KB physical frame |
| `0x00028220` | 3,968 bytes | `xnu_vm_page_t vm_pages[496]` | 496 descriptors for frames 16 to 511 |
| `0x000291A0` | Padding | Alignment to 16KB | Reserved for future free queues |

This layout ensures that physical memory metadata is 100% guest-visible, deterministic, and verifiable through memory reads after guest execution.
