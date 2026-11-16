# WinIOS Stage 208.7: Physical Memory Model Audit

## 1. Physical Address Space Layout (A16 Bionic Profile)
On the iPhone 15 / Apple A16 Bionic (`T8120`) profile, physical memory is partitioned into guest DRAM, MMIO apertures, and reserved regions:

| Physical Address Range | Size | Region Name | Access Attributes | Ownership / Usage |
|---|---|---|---|---|
| `0x0000_0000 - 0x0000_FFFF` | 64 KB | Bootstrap & C Runtime Stack | Normal Cacheable RW | `bootstrap_stack` (Top at `0x10000`) |
| `0x0001_0000 - 0x0001_FFFF` | 64 KB | Global State Records | Normal Cacheable RW | `g_arm_init_state` (`0x10000`), `g_pe_state` (`0x10028`) |
| `0x0002_0000 - 0x0002_FFFF` | 64 KB | Kernel Binary `__TEXT` & `__DATA` | Normal Cacheable RX/RW | Mach-O executable image |
| `0x0003_0000 - 0x0003_3FFF` | 16 KB | Bootstrap Page Table Root | Normal Cacheable RW | Stolen at `topOfKernelData` (`0x30000`) |
| `0x0003_4000 - 0x0003_7FFF` | 16 KB | L2 Translation Table | Normal Cacheable RW | Allocated by early VM stealer |
| `0x0003_8000 - 0x0003_BFFF` | 16 KB | L3 Translation Table | Normal Cacheable RW | Allocated by early VM stealer |
| `0x0005_0000 - 0x0005_0007` | 8 B | Kernel Completion Mailbox | Normal Cacheable RW | `0xCAFEBABE207` |
| `0x0009_0000 - 0x0009_0FFF` | 4 KB | Virtual UART Console | Device-nGnRnE RW | S5L / S8000 UART character transmitter |
| `0x000A_0000 - 0x003F_FFFF` | ~3.4 MB | Free Guest Physical RAM | Normal Cacheable RW | Managed by `XnuPhysicalPageAllocator` |
| `0x2_3B10_0000 - 0x2_3B1F_FFFF`| 1 MB | Apple AIC2 Interrupt Controller | Device-nGnRnE RW | AIC_INFO, WHOAMI, CONFIG, IRQ registers |

## 2. Invariant & Boundary Verification
1. **Stack Isolation:** `bootstrap_stack` spans `[0x0, 0x10000)`. Stack pointer decrements from `0x10000`. Frame allocation never touches page tables.
2. **Page Table Non-Overlapping:** The initial translation table root is stolen at `topOfKernelData` (`0x30000`), exactly 16KB-aligned. It never collides with `__TEXT` (`< 0x20000`) or stack (`< 0x10000`).
3. **Physical Memory Bounds:** `memSize` (e.g. 4MB, 8MB, or 4GB) is passed in `boot_args.memSize`. Allocations beyond `physBase + memSize` are strictly trapped.
