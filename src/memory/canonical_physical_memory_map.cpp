#include "memory/canonical_physical_memory_map.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace winios::memory {

const char* canonical_memory_kind_to_string(CanonicalMemoryKind kind) noexcept {
    switch (kind) {
    case CanonicalMemoryKind::KernelTextPrimary:   return "KernelTextPrimary";
    case CanonicalMemoryKind::KernelTextSecondary: return "KernelTextSecondary";
    case CanonicalMemoryKind::KernelData:          return "KernelData";
    case CanonicalMemoryKind::LowPaddingScratch:   return "LowPaddingScratch";
    case CanonicalMemoryKind::DeviceTree:          return "DeviceTree";
    case CanonicalMemoryKind::BootArgs:            return "BootArgs";
    case CanonicalMemoryKind::BootstrapStack:      return "BootstrapStack";
    case CanonicalMemoryKind::PageTables:          return "PageTables";
    case CanonicalMemoryKind::CoreGuard:           return "CoreGuard";
    case CanonicalMemoryKind::CoreInterruptGuard:  return "CoreInterruptGuard";
    case CanonicalMemoryKind::CoreThreadGuard:     return "CoreThreadGuard";
    case CanonicalMemoryKind::SmpSharedMemory:     return "SmpSharedMemory";
    case CanonicalMemoryKind::DiagnosticGuard:     return "DiagnosticGuard";
    case CanonicalMemoryKind::CoreInterruptStack:  return "CoreInterruptStack";
    case CanonicalMemoryKind::CoreThreadStack:     return "CoreThreadStack";
    case CanonicalMemoryKind::AllocatorMetadata:   return "AllocatorMetadata";
    case CanonicalMemoryKind::DynamicFreePool:     return "DynamicFreePool";
    case CanonicalMemoryKind::MmioDevice:          return "MmioDevice";
    }
    return "Unknown";
}

CanonicalPhysicalMemoryMap::CanonicalPhysicalMemoryMap(std::uint64_t ram_base, std::uint64_t ram_size)
    : ram_base_(ram_base), ram_size_(ram_size) {}

bool CanonicalPhysicalMemoryMap::add_interval(const CanonicalMemoryInterval& interval, std::string* out_error) {
    // 1. Basic validation
    if (interval.size == 0) {
        if (out_error) *out_error = "Interval size must be greater than zero";
        return false;
    }
    if (interval.start_address % interval.page_granule != 0) {
        if (out_error) *out_error = "Interval start address is not page-granule aligned";
        return false;
    }
    if (interval.size % interval.page_granule != 0) {
        if (out_error) *out_error = "Interval size is not a multiple of page granule";
        return false;
    }
    if (interval.start_address + interval.size < interval.start_address) {
        if (out_error) *out_error = "Integer overflow detected in interval bounds";
        return false;
    }
    if (interval.end_address != interval.start_address + interval.size) {
        if (out_error) *out_error = "Interval end_address does not match start + size";
        return false;
    }

    intervals_.push_back(interval);
    return true;
}

const CanonicalMemoryInterval* CanonicalPhysicalMemoryMap::find_interval_by_name(std::string_view name) const {
    for (const auto& iv : intervals_) {
        if (iv.name == name) return &iv;
    }
    return nullptr;
}

const CanonicalMemoryInterval* CanonicalPhysicalMemoryMap::find_interval_by_address(std::uint64_t addr) const {
    for (const auto& iv : intervals_) {
        if (iv.contains(addr)) return &iv;
    }
    return nullptr;
}

bool CanonicalPhysicalMemoryMap::is_range_reserved(std::uint64_t base, std::uint64_t size) const {
    if (size == 0) return false;
    const std::uint64_t end = base + size;
    for (const auto& iv : intervals_) {
        if (iv.kind != CanonicalMemoryKind::DynamicFreePool) {
            // Check overlap
            if (base < iv.end_address && iv.start_address < end) {
                return true;
            }
        }
    }
    return false;
}

std::vector<CanonicalPhysicalMemoryMap::FreeRange> CanonicalPhysicalMemoryMap::derive_free_ranges() const {
    std::vector<FreeRange> free_ranges;
    for (const auto& iv : intervals_) {
        if (iv.kind == CanonicalMemoryKind::DynamicFreePool) {
            FreeRange r;
            r.start = iv.start_address;
            r.end = iv.end_address;
            r.size = iv.size;
            r.frame_count = static_cast<std::size_t>(iv.size / kPageGranule16K);
            free_ranges.push_back(r);
        }
    }
    return free_ranges;
}

std::optional<CanonicalPhysicalMemoryMap::CoreStackGeometry>
CanonicalPhysicalMemoryMap::get_core_stack_geometry(std::size_t core_id) const {
    if (core_id >= kNumCores) return std::nullopt;

    CoreStackGeometry geom;
    geom.core_id = core_id;

    std::string int_guard_name = "Core" + std::to_string(core_id) + "_InterruptGuard";
    std::string thr_guard_name = "Core" + std::to_string(core_id) + "_ThreadGuard";
    std::string leg_guard_name = "Core" + std::to_string(core_id) + "_Guard";
    std::string int_name       = "Core" + std::to_string(core_id) + "_InterruptStack";
    std::string thr_name       = "Core" + std::to_string(core_id) + "_ThreadStack";

    const auto* int_guard = find_interval_by_name(int_guard_name);
    const auto* thr_guard = find_interval_by_name(thr_guard_name);
    const auto* leg_guard = find_interval_by_name(leg_guard_name);
    const auto* int_stk   = find_interval_by_name(int_name);
    const auto* thr_stk   = find_interval_by_name(thr_name);

    if (!int_stk || !thr_stk) return std::nullopt;

    if (int_guard && thr_guard) {
        // Dual-guard layout (Campaign 262)
        geom.int_guard_base = int_guard->start_address;
        geom.int_guard_size = int_guard->size;
        geom.thread_guard_base = thr_guard->start_address;
        geom.thread_guard_size = thr_guard->size;
        // Primary guard accessor points to interrupt stack guard
        geom.guard_base = int_guard->start_address;
        geom.guard_size = int_guard->size;
    } else if (leg_guard) {
        // Single-guard legacy layout (Campaign 261)
        geom.guard_base = leg_guard->start_address;
        geom.guard_size = leg_guard->size;
        geom.int_guard_base = leg_guard->start_address;
        geom.int_guard_size = leg_guard->size;
        geom.thread_guard_base = leg_guard->start_address;
        geom.thread_guard_size = leg_guard->size;
    } else {
        return std::nullopt;
    }

    geom.int_stack_base = int_stk->start_address;
    geom.int_stack_size = int_stk->size;
    geom.int_stack_initial_sp = int_stk->end_address; // Downward-growing stack: initial SP is top
    geom.thread_stack_base = thr_stk->start_address;
    geom.thread_stack_size = thr_stk->size;
    geom.thread_stack_initial_sp = thr_stk->end_address;

    return geom;
}

CanonicalPhysicalMemoryMap CanonicalPhysicalMemoryMap::create_canonical_iphone15_layout() {
    CanonicalPhysicalMemoryMap map(kDefaultRamBase, kDefaultRamSize);

    // -------------------------------------------------------------------------
    // 1. Low Bootstrap Enclave (Frames 0..19: 0x00000000 .. 0x00050000)
    // -------------------------------------------------------------------------
    // Frame 0: Primary Mach-O __TEXT segment (16 KiB, RX, VBAR at 0x800)
    map.add_interval({
        "KernelTextPrimary", CanonicalMemoryKind::KernelTextPrimary,
        0x00000000ULL, 0x00004000ULL, 0x00004000ULL,
        "Mach-O Kernelcache", "Kernel execution entry & exception vectors (VBAR at 0x800)",
        "RAM", 0x5u /* RX */, true, kPageGranule16K,
        "Authoritative kernelcode", -1, "Category B", "Mach-O entry point"
    });

    // Frames 1..3: Low bootstrap scratch / padding (48 KiB, RW-NX)
    map.add_interval({
        "LowPaddingScratch", CanonicalMemoryKind::LowPaddingScratch,
        0x00004000ULL, 0x00010000ULL, 0x0000C000ULL,
        "Bootstrap Loader", "Early scratch workspace and alignment padding",
        "RAM", 0x3u /* RW */, true, kPageGranule16K,
        "Reserved bootstrap memory", -1, "Category C", "Bootstrap padding"
    });

    // Frames 4..11: Mach-O __DATA segment (128 KiB, RW-NX)
    map.add_interval({
        "KernelData", CanonicalMemoryKind::KernelData,
        0x00010000ULL, 0x00030000ULL, 0x00020000ULL,
        "Mach-O Kernelcache", "Static kernel variables, dispatch tables, sysregs",
        "RAM", 0x3u /* RW */, true, kPageGranule16K,
        "Kernel static state", -1, "Category B", "Mach-O data segment"
    });

    // Frame 12: Apple DeviceTree Blob (16 KiB, RW-NX)
    map.add_interval({
        "DeviceTree", CanonicalMemoryKind::DeviceTree,
        0x00030000ULL, 0x00034000ULL, 0x00004000ULL,
        "Apple I/O Kit", "Apple flattened device tree (ADTS)",
        "RAM", 0x3u /* RW */, true, kPageGranule16K,
        "Hardware topology descriptor", -1, "Category C", "Bootloader DT staging"
    });

    // Frame 13: BootArgs Structure (16 KiB, RW-NX)
    map.add_interval({
        "BootArgs", CanonicalMemoryKind::BootArgs,
        0x00034000ULL, 0x00038000ULL, 0x00004000ULL,
        "Apple Bootloader", "Kernel boot arguments (boot_args)",
        "RAM", 0x3u /* RW */, true, kPageGranule16K,
        "Boot parameter block", -1, "Category C", "Bootloader parameter block"
    });

    // Frame 14: Cold-Boot Bootstrap Stack (16 KiB, top 0x3C000, RW-NX)
    map.add_interval({
        "BootstrapStack", CanonicalMemoryKind::BootstrapStack,
        0x00038000ULL, 0x0003C000ULL, 0x00004000ULL,
        "Core 0 Boot", "Early initialization stack before SMP start",
        "RAM", 0x3u /* RW */, true, kPageGranule16K,
        "Cold boot execution stack", 0, "Category C", "Early bootstrap stack"
    });

    // Frame 15: Core 0 Stack Guard (16 KiB, unmapped/trapping guard)
    map.add_interval({
        "Core0_Guard", CanonicalMemoryKind::CoreGuard,
        0x0003C000ULL, 0x00040000ULL, 0x00004000ULL,
        "Core 0 Exception Guard", "Underflow protection guard for Core 0 exception stack",
        "Guard", 0x0u /* None */, false /* Unmapped */, kPageGranule16K,
        "Stack guard page", 0, "Category C", "Core 0 exception guard"
    });

    // Frame 16: Core 0 Interrupt Stack (16 KiB, initial SP_EL1 = 0x44000, RW-NX)
    map.add_interval({
        "Core0_InterruptStack", CanonicalMemoryKind::CoreInterruptStack,
        0x00040000ULL, 0x00044000ULL, 0x00004000ULL,
        "Core 0 Exception Subsystem", "Dedicated interrupt stack (SP_EL1 = 0x44000)",
        "RAM", 0x3u /* RW */, true, kPageGranule16K,
        "Core 0 IRQ stack", 0, "Category B", "Authoritative Frame 16 stack"
    });

    // Frame 17: Core 0 Thread Stack / CPU Data (16 KiB, initial SP_EL0 = 0x48000, RW-NX)
    map.add_interval({
        "Core0_ThreadStack", CanonicalMemoryKind::CoreThreadStack,
        0x00044000ULL, 0x00048000ULL, 0x00004000ULL,
        "Core 0 Thread Dispatcher", "Thread stack and per-cpu data area (SP_EL0 = 0x48000)",
        "RAM", 0x3u /* RW */, true, kPageGranule16K,
        "Core 0 thread context", 0, "Category B", "Authoritative Frame 17 thread stack"
    });

    // Frames 18..19: Stage 1 Translation Tables (32 KiB, RW-NX)
    map.add_interval({
        "Stage1PageTables", CanonicalMemoryKind::PageTables,
        0x00048000ULL, 0x00050000ULL, 0x00008000ULL,
        "Mach VM Pmap", "Stage 1 L1/L2 and L3 page tables",
        "RAM", 0x3u /* RW */, true, kPageGranule16K,
        "Memory management translation tables", -1, "Category C", "Pmap bootstrap tables"
    });

    // -------------------------------------------------------------------------
    // 2. Multi-Core Stacks & Guards Enclave (Frames 20..35: 0x00050000 .. 0x00090000)
    // -------------------------------------------------------------------------
    // Core 1 (Frames 20..22)
    map.add_interval({
        "Core1_Guard", CanonicalMemoryKind::CoreGuard,
        0x00050000ULL, 0x00054000ULL, 0x00004000ULL,
        "Core 1 Exception Guard", "Underflow protection guard for Core 1",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 1, "Category C", "Core 1 guard"
    });
    map.add_interval({
        "Core1_InterruptStack", CanonicalMemoryKind::CoreInterruptStack,
        0x00054000ULL, 0x00058000ULL, 0x00004000ULL,
        "Core 1 Exception Subsystem", "Dedicated interrupt stack (SP_EL1 = 0x58000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 1 IRQ stack", 1, "Category B", "Core 1 stack"
    });
    map.add_interval({
        "Core1_ThreadStack", CanonicalMemoryKind::CoreThreadStack,
        0x00058000ULL, 0x0005C000ULL, 0x00004000ULL,
        "Core 1 Thread Dispatcher", "Thread stack and CPU data (SP_EL0 = 0x5C000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 1 thread context", 1, "Category B", "Core 1 thread stack"
    });

    // Core 2 (Frames 23..25)
    map.add_interval({
        "Core2_Guard", CanonicalMemoryKind::CoreGuard,
        0x0005C000ULL, 0x00060000ULL, 0x00004000ULL,
        "Core 2 Exception Guard", "Underflow protection guard for Core 2",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 2, "Category C", "Core 2 guard"
    });
    map.add_interval({
        "Core2_InterruptStack", CanonicalMemoryKind::CoreInterruptStack,
        0x00060000ULL, 0x00064000ULL, 0x00004000ULL,
        "Core 2 Exception Subsystem", "Dedicated interrupt stack (SP_EL1 = 0x64000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 2 IRQ stack", 2, "Category B", "Core 2 stack"
    });
    map.add_interval({
        "Core2_ThreadStack", CanonicalMemoryKind::CoreThreadStack,
        0x00064000ULL, 0x00068000ULL, 0x00004000ULL,
        "Core 2 Thread Dispatcher", "Thread stack and CPU data (SP_EL0 = 0x68000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 2 thread context", 2, "Category B", "Core 2 thread stack"
    });

    // Core 3 (Frames 26..28)
    map.add_interval({
        "Core3_Guard", CanonicalMemoryKind::CoreGuard,
        0x00068000ULL, 0x0006C000ULL, 0x00004000ULL,
        "Core 3 Exception Guard", "Underflow protection guard for Core 3",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 3, "Category C", "Core 3 guard"
    });
    map.add_interval({
        "Core3_InterruptStack", CanonicalMemoryKind::CoreInterruptStack,
        0x0006C000ULL, 0x00070000ULL, 0x00004000ULL,
        "Core 3 Exception Subsystem", "Dedicated interrupt stack (SP_EL1 = 0x70000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 3 IRQ stack", 3, "Category B", "Core 3 stack"
    });
    map.add_interval({
        "Core3_ThreadStack", CanonicalMemoryKind::CoreThreadStack,
        0x00070000ULL, 0x00074000ULL, 0x00004000ULL,
        "Core 3 Thread Dispatcher", "Thread stack and CPU data (SP_EL0 = 0x74000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 3 thread context", 3, "Category B", "Core 3 thread stack"
    });

    // Core 4 (Frames 29..31)
    map.add_interval({
        "Core4_Guard", CanonicalMemoryKind::CoreGuard,
        0x00074000ULL, 0x00078000ULL, 0x00004000ULL,
        "Core 4 Exception Guard", "Underflow protection guard for Core 4",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 4, "Category C", "Core 4 guard"
    });
    map.add_interval({
        "Core4_InterruptStack", CanonicalMemoryKind::CoreInterruptStack,
        0x00078000ULL, 0x0007C000ULL, 0x00004000ULL,
        "Core 4 Exception Subsystem", "Dedicated interrupt stack (SP_EL1 = 0x7C000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 4 IRQ stack", 4, "Category B", "Core 4 stack"
    });
    map.add_interval({
        "Core4_ThreadStack", CanonicalMemoryKind::CoreThreadStack,
        0x0007C000ULL, 0x00080000ULL, 0x00004000ULL,
        "Core 4 Thread Dispatcher", "Thread stack and CPU data (SP_EL0 = 0x80000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 4 thread context", 4, "Category B", "Core 4 thread stack"
    });

    // -------------------------------------------------------------------------
    // 3. Secondary Kernel Text / Historical Diagnostic Area (Frame 32: 0x80000 .. 0x84000)
    // -------------------------------------------------------------------------
    map.add_interval({
        "KernelTextSecondary", CanonicalMemoryKind::KernelTextSecondary,
        0x00080000ULL, 0x00084000ULL, 0x00004000ULL,
        "Staged Diagnostic Code", "Historical 0x80000 / Category D diagnostic vector staging",
        "RAM", 0x5u /* RX */, true, kPageGranule16K,
        "Secondary executable image segment", -1, "Category D", "Historical vector base"
    });

    // Core 5 (Frames 33..35)
    map.add_interval({
        "Core5_Guard", CanonicalMemoryKind::CoreGuard,
        0x00084000ULL, 0x00088000ULL, 0x00004000ULL,
        "Core 5 Exception Guard", "Underflow protection guard for Core 5",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 5, "Category C", "Core 5 guard"
    });
    map.add_interval({
        "Core5_InterruptStack", CanonicalMemoryKind::CoreInterruptStack,
        0x00088000ULL, 0x0008C000ULL, 0x00004000ULL,
        "Core 5 Exception Subsystem", "Dedicated interrupt stack (SP_EL1 = 0x8C000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 5 IRQ stack", 5, "Category B", "Core 5 stack"
    });
    map.add_interval({
        "Core5_ThreadStack", CanonicalMemoryKind::CoreThreadStack,
        0x0008C000ULL, 0x00090000ULL, 0x00004000ULL,
        "Core 5 Thread Dispatcher", "Thread stack and CPU data (SP_EL0 = 0x90000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 5 thread context", 5, "Category B", "Core 5 thread stack"
    });

    // Frame 36: Allocator Metadata & Watermark Table (16 KiB, RW-NX)
    map.add_interval({
        "AllocatorMetadata", CanonicalMemoryKind::AllocatorMetadata,
        0x00090000ULL, 0x00094000ULL, 0x00004000ULL,
        "Mach VM Zone Allocator", "Allocator management structures and bitmaps",
        "RAM", 0x3u, true, kPageGranule16K,
        "Allocator metadata block", -1, "Category C", "Dynamic allocator header"
    });

    // -------------------------------------------------------------------------
    // 4. Dynamic Free Pool (Frames 37..511: 0x00094000 .. 0x00800000, 475 frames)
    // -------------------------------------------------------------------------
    map.add_interval({
        "DynamicFreePool", CanonicalMemoryKind::DynamicFreePool,
        0x00094000ULL, 0x00800000ULL, 0x0076C000ULL, // 7,782,400 bytes = 475 frames
        "Mach VM Dynamic Heap", "Available allocatable physical memory pool",
        "RAM", 0x3u, true, kPageGranule16K,
        "Unreserved dynamic frame pool", -1, "Category C", "Derived unreserved memory"
    });

    // -------------------------------------------------------------------------
    // 5. External MMIO Devices (Sub-1GB Apertures outside RAM)
    // -------------------------------------------------------------------------
    map.add_interval({
        "MmioAIC2", CanonicalMemoryKind::MmioDevice,
        0x08000000ULL, 0x08010000ULL, 0x00010000ULL, // 64 KiB
        "Apple Interrupt Controller 2", "AIC2 hardware MMIO registers",
        "MMIO", 0x3u, true, kPageGranule16K,
        "AIC2 hardware register bank", -1, "Category C", "AIC2 MMIO base"
    });
    map.add_interval({
        "MmioSerialUART", CanonicalMemoryKind::MmioDevice,
        0x09000000ULL, 0x09001000ULL, 0x00001000ULL, // 4 KiB
        "Apple Serial UART", "UART console I/O registers",
        "MMIO", 0x3u, true, kPageGranule16K,
        "Serial console hardware", -1, "Category C", "UART MMIO base"
    });
    map.add_interval({
        "MmioPMGR", CanonicalMemoryKind::MmioDevice,
        0x10000000ULL, 0x10004000ULL, 0x00004000ULL, // 16 KiB
        "Apple Power Manager", "Power management registers",
        "MMIO", 0x3u, true, kPageGranule16K,
        "PMGR hardware", -1, "Category C", "PMGR MMIO base"
    });
    map.add_interval({
        "MmioDCP", CanonicalMemoryKind::MmioDevice,
        0x20000000ULL, 0x20010000ULL, 0x00010000ULL, // 64 KiB
        "Apple Display Coprocessor", "DCP / Display controller registers",
        "MMIO", 0x3u, true, kPageGranule16K,
        "DCP display hardware", -1, "Category C", "DCP MMIO base"
    });

    return map;
}

// -----------------------------------------------------------------------------
// PhysicalMemoryOverlapAuditor Implementation
// -----------------------------------------------------------------------------
PhysicalMemoryOverlapAuditor::PhysicalMemoryOverlapAuditor(const CanonicalPhysicalMemoryMap& map)
    : map_(map) {}

bool PhysicalMemoryOverlapAuditor::audit_overlaps(std::vector<ConflictRecord>& out_conflicts) const {
    const auto& ivs = map_.intervals();
    bool passed = true;

    for (std::size_t i = 0; i < ivs.size(); ++i) {
        for (std::size_t j = i + 1; j < ivs.size(); ++j) {
            if (ivs[i].overlaps_with(ivs[j])) {
                passed = false;
                ConflictRecord rec;
                rec.owner_a = ivs[i].name + " (" + ivs[i].owner + ")";
                rec.owner_b = ivs[j].name + " (" + ivs[j].owner + ")";
                rec.overlap_start = std::max(ivs[i].start_address, ivs[j].start_address);
                rec.overlap_end   = std::min(ivs[i].end_address, ivs[j].end_address);
                rec.overlap_size  = rec.overlap_end - rec.overlap_start;

                std::ostringstream ss;
                ss << "Overlap detected between [" << rec.owner_a << "] and ["
                   << rec.owner_b << "] in interval [0x" << std::hex << rec.overlap_start
                   << " - 0x" << rec.overlap_end << "), size 0x" << rec.overlap_size << " bytes";
                rec.description = ss.str();

                out_conflicts.push_back(rec);
            }
        }
    }

    return passed;
}

bool PhysicalMemoryOverlapAuditor::audit_wx_compliance(std::vector<std::string>& out_violations) const {
    bool passed = true;
    for (const auto& iv : map_.intervals()) {
        if (iv.is_mapped && iv.is_wx_violation()) {
            passed = false;
            std::ostringstream ss;
            ss << "W^X violation in interval [" << iv.name << "]: permissions=0x"
               << std::hex << iv.permissions << " (simultaneous Write and Execute)";
            out_violations.push_back(ss.str());
        }
    }
    return passed;
}

bool PhysicalMemoryOverlapAuditor::audit_granule_alignment(std::vector<std::string>& out_errors) const {
    bool passed = true;
    for (const auto& iv : map_.intervals()) {
        if (iv.start_address % iv.page_granule != 0) {
            passed = false;
            out_errors.push_back("Interval [" + iv.name + "] start_address 0x" +
                                 std::to_string(iv.start_address) + " is not 16 KiB aligned");
        }
        if (iv.size % iv.page_granule != 0) {
            passed = false;
            out_errors.push_back("Interval [" + iv.name + "] size 0x" +
                                 std::to_string(iv.size) + " is not a 16 KiB multiple");
        }
    }
    return passed;
}

bool PhysicalMemoryOverlapAuditor::audit_six_core_stack_isolation(std::vector<std::string>& out_errors) const {
    bool passed = true;
    for (std::size_t c = 0; c < CanonicalPhysicalMemoryMap::kNumCores; ++c) {
        auto geom = map_.get_core_stack_geometry(c);
        if (!geom) {
            passed = false;
            out_errors.push_back("Core " + std::to_string(c) + " stack geometry missing");
            continue;
        }

        // Verify guard is below interrupt stack
        if (geom->guard_base + geom->guard_size != geom->int_stack_base) {
            passed = false;
            out_errors.push_back("Core " + std::to_string(c) + " guard is not contiguous with interrupt stack base");
        }

        // Verify independent thread guard if present (Campaign 262 dual-guard)
        if (geom->thread_guard_base != 0 && geom->thread_guard_base != geom->guard_base) {
            if (geom->thread_guard_base + geom->thread_guard_size != geom->thread_stack_base) {
                passed = false;
                out_errors.push_back("Core " + std::to_string(c) + " thread guard is not contiguous with thread stack base");
            }
        }

        // Verify stack sizes are exactly 16 KiB
        if (geom->int_stack_size != CanonicalPhysicalMemoryMap::kPageGranule16K) {
            passed = false;
            out_errors.push_back("Core " + std::to_string(c) + " interrupt stack is not exactly 16 KiB");
        }
        if (geom->thread_stack_size != CanonicalPhysicalMemoryMap::kPageGranule16K) {
            passed = false;
            out_errors.push_back("Core " + std::to_string(c) + " thread stack is not exactly 16 KiB");
        }
    }
    return passed;
}

bool PhysicalMemoryOverlapAuditor::audit_allocator_exclusion(std::vector<std::string>& out_errors) const {
    const auto* pool = map_.find_interval_by_name("DynamicFreePool");
    if (!pool) {
        out_errors.push_back("DynamicFreePool interval not found");
        return false;
    }

    bool passed = true;
    // Verify that NO reserved interval intersects the DynamicFreePool
    for (const auto& iv : map_.intervals()) {
        if (iv.kind != CanonicalMemoryKind::DynamicFreePool) {
            if (iv.overlaps_with(*pool)) {
                passed = false;
                out_errors.push_back("CRITICAL CONFLICT: Reserved interval [" + iv.name +
                                     "] overlaps DynamicFreePool! Address [0x" +
                                     std::to_string(iv.start_address) + " - 0x" +
                                     std::to_string(iv.end_address) + ")");
            }
        }
    }

    // Explicitly verify KernelTextSecondary (0x80000) does not intersect pool
    const auto* text2 = map_.find_interval_by_name("KernelTextSecondary");
    if (text2 && text2->overlaps_with(*pool)) {
        passed = false;
        out_errors.push_back("CRITICAL CONFLICT: Kernel text at 0x80000 overlaps DynamicFreePool!");
    }

    return passed;
}

bool PhysicalMemoryOverlapAuditor::run_full_audit(std::vector<std::string>& out_report) const {
    std::vector<ConflictRecord> conflicts;
    std::vector<std::string> wx_violations;
    std::vector<std::string> align_errors;
    std::vector<std::string> stack_errors;
    std::vector<std::string> alloc_errors;

    bool c_ok = audit_overlaps(conflicts);
    bool w_ok = audit_wx_compliance(wx_violations);
    bool a_ok = audit_granule_alignment(align_errors);
    bool s_ok = audit_six_core_stack_isolation(stack_errors);
    bool e_ok = audit_allocator_exclusion(alloc_errors);

    out_report.push_back("=== Canonical Physical Memory Map Comprehensive Audit ===");
    out_report.push_back("1. Overlap Audit: " + std::string(c_ok ? "PASSED (Zero collisions)" : "FAILED"));
    for (const auto& c : conflicts) out_report.push_back("   - " + c.description);

    out_report.push_back("2. W^X Permission Audit: " + std::string(w_ok ? "PASSED (Zero violations)" : "FAILED"));
    for (const auto& v : wx_violations) out_report.push_back("   - " + v);

    out_report.push_back("3. 16 KiB Page Alignment: " + std::string(a_ok ? "PASSED (100% compliant)" : "FAILED"));
    for (const auto& err : align_errors) out_report.push_back("   - " + err);

    out_report.push_back("4. Six-Core Stack Isolation: " + std::string(s_ok ? "PASSED (6 disjoint stacks & guards)" : "FAILED"));
    for (const auto& err : stack_errors) out_report.push_back("   - " + err);

    out_report.push_back("5. Allocator Exclusion Audit: " + std::string(e_ok ? "PASSED (Zero reserved overlaps)" : "FAILED"));
    for (const auto& err : alloc_errors) out_report.push_back("   - " + err);

    return c_ok && w_ok && a_ok && s_ok && e_ok;
}

CanonicalPhysicalMemoryMap CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout() {
    CanonicalPhysicalMemoryMap map(kDefaultRamBase, kDefaultRamSize);

    // Frames 0..14: Early Boot Enclave (0x00000000 .. 0x0003C000)
    map.add_interval({
        "KernelTextPrimary", CanonicalMemoryKind::KernelTextPrimary,
        0x00000000ULL, 0x00004000ULL, 0x00004000ULL,
        "Mach-O Kernelcache", "Kernel execution entry & exception vectors (VBAR at 0x800)",
        "RAM", 0x5u /* RX */, true, kPageGranule16K,
        "Authoritative kernelcode", -1, "Category B", "Mach-O entry point"
    });
    map.add_interval({
        "LowPaddingScratch", CanonicalMemoryKind::LowPaddingScratch,
        0x00004000ULL, 0x00010000ULL, 0x0000C000ULL,
        "Bootstrap Loader", "Early scratch workspace and alignment padding",
        "RAM", 0x3u /* RW */, true, kPageGranule16K,
        "Reserved bootstrap memory", -1, "Category C", "Bootstrap padding"
    });
    map.add_interval({
        "KernelData", CanonicalMemoryKind::KernelData,
        0x00010000ULL, 0x00030000ULL, 0x00020000ULL,
        "Mach-O Kernelcache", "Static kernel variables, dispatch tables, sysregs",
        "RAM", 0x3u /* RW */, true, kPageGranule16K,
        "Kernel static state", -1, "Category B", "Mach-O data segment"
    });
    map.add_interval({
        "DeviceTree", CanonicalMemoryKind::DeviceTree,
        0x00030000ULL, 0x00034000ULL, 0x00004000ULL,
        "Apple I/O Kit", "Apple flattened device tree (ADTS)",
        "RAM", 0x3u /* RW */, true, kPageGranule16K,
        "Hardware topology descriptor", -1, "Category C", "Bootloader DT staging"
    });
    map.add_interval({
        "BootArgs", CanonicalMemoryKind::BootArgs,
        0x00034000ULL, 0x00038000ULL, 0x00004000ULL,
        "Apple Bootloader", "Kernel boot arguments (boot_args)",
        "RAM", 0x3u /* RW */, true, kPageGranule16K,
        "Boot parameter block", -1, "Category C", "Bootloader parameter block"
    });
    map.add_interval({
        "BootstrapStack", CanonicalMemoryKind::BootstrapStack,
        0x00038000ULL, 0x0003C000ULL, 0x00004000ULL,
        "Core 0 Boot", "Early initialization stack before SMP start",
        "RAM", 0x3u /* RW */, true, kPageGranule16K,
        "Cold boot execution stack", 0, "Category C", "Early bootstrap stack"
    });

    // Core 0 (Frames 15..18: 0x0003C000 .. 0x0004C000)
    map.add_interval({
        "Core0_InterruptGuard", CanonicalMemoryKind::CoreInterruptGuard,
        0x0003C000ULL, 0x00040000ULL, 0x00004000ULL,
        "Core 0 IRQ Guard", "Independent underflow guard for Core 0 interrupt stack",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 0, "Category C", "Core 0 IRQ guard"
    });
    map.add_interval({
        "Core0_InterruptStack", CanonicalMemoryKind::CoreInterruptStack,
        0x00040000ULL, 0x00044000ULL, 0x00004000ULL,
        "Core 0 Exception Subsystem", "Dedicated interrupt stack (SP_EL1 = 0x44000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 0 IRQ stack", 0, "Category B", "Authoritative Frame 16 stack"
    });
    map.add_interval({
        "Core0_ThreadGuard", CanonicalMemoryKind::CoreThreadGuard,
        0x00044000ULL, 0x00048000ULL, 0x00004000ULL,
        "Core 0 Thread Guard", "Independent underflow guard for Core 0 thread stack",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 0, "Category C", "Core 0 thread guard"
    });
    map.add_interval({
        "Core0_ThreadStack", CanonicalMemoryKind::CoreThreadStack,
        0x00048000ULL, 0x0004C000ULL, 0x00004000ULL,
        "Core 0 Thread Dispatcher", "Thread stack and per-cpu data area (SP_EL0 = 0x4C000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 0 thread context", 0, "Category B", "Core 0 thread stack"
    });

    // Frames 19..20: Stage 1 Translation Tables (32 KiB: 0x0004C000 .. 0x00054000)
    map.add_interval({
        "Stage1PageTables", CanonicalMemoryKind::PageTables,
        0x0004C000ULL, 0x00054000ULL, 0x00008000ULL,
        "Mach VM Pmap", "Stage 1 L1/L2 and L3 page tables",
        "RAM", 0x3u, true, kPageGranule16K,
        "Memory management translation tables", -1, "Category C", "Pmap bootstrap tables"
    });

    // Core 1 (Frames 21..24: 0x00054000 .. 0x00064000)
    map.add_interval({
        "Core1_InterruptGuard", CanonicalMemoryKind::CoreInterruptGuard,
        0x00054000ULL, 0x00058000ULL, 0x00004000ULL,
        "Core 1 IRQ Guard", "Independent underflow guard for Core 1 interrupt stack",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 1, "Category C", "Core 1 IRQ guard"
    });
    map.add_interval({
        "Core1_InterruptStack", CanonicalMemoryKind::CoreInterruptStack,
        0x00058000ULL, 0x0005C000ULL, 0x00004000ULL,
        "Core 1 Exception Subsystem", "Dedicated interrupt stack (SP_EL1 = 0x5C000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 1 IRQ stack", 1, "Category B", "Core 1 stack"
    });
    map.add_interval({
        "Core1_ThreadGuard", CanonicalMemoryKind::CoreThreadGuard,
        0x0005C000ULL, 0x00060000ULL, 0x00004000ULL,
        "Core 1 Thread Guard", "Independent underflow guard for Core 1 thread stack",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 1, "Category C", "Core 1 thread guard"
    });
    map.add_interval({
        "Core1_ThreadStack", CanonicalMemoryKind::CoreThreadStack,
        0x00060000ULL, 0x00064000ULL, 0x00004000ULL,
        "Core 1 Thread Dispatcher", "Thread stack and CPU data (SP_EL0 = 0x64000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 1 thread context", 1, "Category B", "Core 1 thread stack"
    });

    // Core 2 (Frames 25..28: 0x00064000 .. 0x00074000)
    map.add_interval({
        "Core2_InterruptGuard", CanonicalMemoryKind::CoreInterruptGuard,
        0x00064000ULL, 0x00068000ULL, 0x00004000ULL,
        "Core 2 IRQ Guard", "Independent underflow guard for Core 2 interrupt stack",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 2, "Category C", "Core 2 IRQ guard"
    });
    map.add_interval({
        "Core2_InterruptStack", CanonicalMemoryKind::CoreInterruptStack,
        0x00068000ULL, 0x0006C000ULL, 0x00004000ULL,
        "Core 2 Exception Subsystem", "Dedicated interrupt stack (SP_EL1 = 0x6C000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 2 IRQ stack", 2, "Category B", "Core 2 stack"
    });
    map.add_interval({
        "Core2_ThreadGuard", CanonicalMemoryKind::CoreThreadGuard,
        0x0006C000ULL, 0x00070000ULL, 0x00004000ULL,
        "Core 2 Thread Guard", "Independent underflow guard for Core 2 thread stack",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 2, "Category C", "Core 2 thread guard"
    });
    map.add_interval({
        "Core2_ThreadStack", CanonicalMemoryKind::CoreThreadStack,
        0x00070000ULL, 0x00074000ULL, 0x00004000ULL,
        "Core 2 Thread Dispatcher", "Thread stack and CPU data (SP_EL0 = 0x74000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 2 thread context", 2, "Category B", "Core 2 thread stack"
    });

    // Frames 29..30: SMP Shared Memory & Mailbox (32 KiB: 0x00074000 .. 0x0007C000)
    map.add_interval({
        "SmpMailboxShared", CanonicalMemoryKind::SmpSharedMemory,
        0x00074000ULL, 0x0007C000ULL, 0x00008000ULL,
        "SMP Coordination", "Inter-core shared mailbox and message passing buffers",
        "RAM", 0x3u, true, kPageGranule16K, "SMP messaging", -1, "Category C", "SMP mailbox"
    });

    // Frame 31: Diagnostic Vector Guard (16 KiB: 0x0007C000 .. 0x00080000)
    map.add_interval({
        "DiagnosticVectorGuard", CanonicalMemoryKind::DiagnosticGuard,
        0x0007C000ULL, 0x00080000ULL, 0x00004000ULL,
        "Execution Boundary", "Unmapped barrier guard protecting secondary vector page",
        "Guard", 0x0u, false, kPageGranule16K, "Vector boundary guard", -1, "Category C", "Diagnostic guard"
    });

    // Frame 32: Secondary Kernel Text / Diagnostic Area (16 KiB: 0x00080000 .. 0x00084000)
    map.add_interval({
        "KernelTextSecondary", CanonicalMemoryKind::KernelTextSecondary,
        0x00080000ULL, 0x00084000ULL, 0x00004000ULL,
        "Staged Diagnostic Code", "Historical 0x80000 / Category D diagnostic vector staging",
        "RAM", 0x5u /* RX */, true, kPageGranule16K,
        "Secondary executable image segment", -1, "Category D", "Historical vector base"
    });

    // Core 3 (Frames 33..36: 0x00084000 .. 0x00094000)
    map.add_interval({
        "Core3_InterruptGuard", CanonicalMemoryKind::CoreInterruptGuard,
        0x00084000ULL, 0x00088000ULL, 0x00004000ULL,
        "Core 3 IRQ Guard", "Independent underflow guard for Core 3 interrupt stack",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 3, "Category C", "Core 3 IRQ guard"
    });
    map.add_interval({
        "Core3_InterruptStack", CanonicalMemoryKind::CoreInterruptStack,
        0x00088000ULL, 0x0008C000ULL, 0x00004000ULL,
        "Core 3 Exception Subsystem", "Dedicated interrupt stack (SP_EL1 = 0x8C000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 3 IRQ stack", 3, "Category B", "Core 3 stack"
    });
    map.add_interval({
        "Core3_ThreadGuard", CanonicalMemoryKind::CoreThreadGuard,
        0x0008C000ULL, 0x00090000ULL, 0x00004000ULL,
        "Core 3 Thread Guard", "Independent underflow guard for Core 3 thread stack",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 3, "Category C", "Core 3 thread guard"
    });
    map.add_interval({
        "Core3_ThreadStack", CanonicalMemoryKind::CoreThreadStack,
        0x00090000ULL, 0x00094000ULL, 0x00004000ULL,
        "Core 3 Thread Dispatcher", "Thread stack and CPU data (SP_EL0 = 0x94000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 3 thread context", 3, "Category B", "Core 3 thread stack"
    });

    // Core 4 (Frames 37..40: 0x00094000 .. 0x000A4000)
    map.add_interval({
        "Core4_InterruptGuard", CanonicalMemoryKind::CoreInterruptGuard,
        0x00094000ULL, 0x00098000ULL, 0x00004000ULL,
        "Core 4 IRQ Guard", "Independent underflow guard for Core 4 interrupt stack",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 4, "Category C", "Core 4 IRQ guard"
    });
    map.add_interval({
        "Core4_InterruptStack", CanonicalMemoryKind::CoreInterruptStack,
        0x00098000ULL, 0x0009C000ULL, 0x00004000ULL,
        "Core 4 Exception Subsystem", "Dedicated interrupt stack (SP_EL1 = 0x9C000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 4 IRQ stack", 4, "Category B", "Core 4 stack"
    });
    map.add_interval({
        "Core4_ThreadGuard", CanonicalMemoryKind::CoreThreadGuard,
        0x0009C000ULL, 0x000A0000ULL, 0x00004000ULL,
        "Core 4 Thread Guard", "Independent underflow guard for Core 4 thread stack",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 4, "Category C", "Core 4 thread guard"
    });
    map.add_interval({
        "Core4_ThreadStack", CanonicalMemoryKind::CoreThreadStack,
        0x000A0000ULL, 0x000A4000ULL, 0x00004000ULL,
        "Core 4 Thread Dispatcher", "Thread stack and CPU data (SP_EL0 = 0xA4000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 4 thread context", 4, "Category B", "Core 4 thread stack"
    });

    // Core 5 (Frames 41..44: 0x000A4000 .. 0x000B4000)
    map.add_interval({
        "Core5_InterruptGuard", CanonicalMemoryKind::CoreInterruptGuard,
        0x000A4000ULL, 0x000A8000ULL, 0x00004000ULL,
        "Core 5 IRQ Guard", "Independent underflow guard for Core 5 interrupt stack",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 5, "Category C", "Core 5 IRQ guard"
    });
    map.add_interval({
        "Core5_InterruptStack", CanonicalMemoryKind::CoreInterruptStack,
        0x000A8000ULL, 0x000AC000ULL, 0x00004000ULL,
        "Core 5 Exception Subsystem", "Dedicated interrupt stack (SP_EL1 = 0xAC000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 5 IRQ stack", 5, "Category B", "Core 5 stack"
    });
    map.add_interval({
        "Core5_ThreadGuard", CanonicalMemoryKind::CoreThreadGuard,
        0x000AC000ULL, 0x000B0000ULL, 0x00004000ULL,
        "Core 5 Thread Guard", "Independent underflow guard for Core 5 thread stack",
        "Guard", 0x0u, false, kPageGranule16K, "Stack guard page", 5, "Category C", "Core 5 thread guard"
    });
    map.add_interval({
        "Core5_ThreadStack", CanonicalMemoryKind::CoreThreadStack,
        0x000B0000ULL, 0x000B4000ULL, 0x00004000ULL,
        "Core 5 Thread Dispatcher", "Thread stack and CPU data (SP_EL0 = 0xB4000)",
        "RAM", 0x3u, true, kPageGranule16K, "Core 5 thread context", 5, "Category B", "Core 5 thread stack"
    });

    // Frame 45: Allocator Metadata (16 KiB: 0x000B4000 .. 0x000B8000)
    map.add_interval({
        "AllocatorMetadata", CanonicalMemoryKind::AllocatorMetadata,
        0x000B4000ULL, 0x000B8000ULL, 0x00004000ULL,
        "Mach VM Zone Allocator", "Allocator management structures and bitmaps",
        "RAM", 0x3u, true, kPageGranule16K,
        "Allocator metadata block", -1, "Category C", "Dynamic allocator header"
    });

    // Frames 46..511: Dynamic Allocator Free Pool (466 frames: 0x000B8000 .. 0x00800000)
    map.add_interval({
        "DynamicFreePool", CanonicalMemoryKind::DynamicFreePool,
        0x000B8000ULL, 0x00800000ULL, 0x00748000ULL,
        "Mach VM Subsystem", "Unreserved physical memory pool for zone and frame allocators",
        "RAM", 0x3u, true, kPageGranule16K,
        "Available physical memory", -1, "Category C", "Zone free pool"
    });

    return map;
}

} // namespace winios::memory