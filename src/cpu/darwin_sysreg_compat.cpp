#include "cpu/darwin_sysreg_compat.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace winios::darwin {

namespace {

constexpr DarwinSysRegEntry kDarwinEntries[] = {
    // --- 1. Generic Architectural Registers Critical to Darwin/XNU ---
    {
        "TPIDR_EL0",
        0x5E82u, 3, 3, 13, 0, 2, 0, true, true, 0x0ULL,
        Classification::GenericArchitectural,
        Confidence::High,
        CompatibilityStatus::FullyImplemented,
        "Armv8-A DDI 0487 / Apple Darwin Libc pthread_self()",
        "EL0 Read/Write thread-local storage base pointer for userspace Mach threads."
    },
    {
        "TPIDRRO_EL0",
        0x5E83u, 3, 3, 13, 0, 3, 0, true, false, 0x0ULL,
        Classification::PubliclyDocumentedDarwin,
        Confidence::High,
        CompatibilityStatus::FullyImplemented,
        "XNU osfmk/arm64/locore.s / Apple Darwin Commpage",
        "EL0 Read-only pointer configured by XNU kernel to expose CPU data / commpage address."
    },
    {
        "TPIDR_EL1",
        0x4684u, 3, 0, 13, 0, 4, 1, true, true, 0x0ULL,
        Classification::GenericArchitectural,
        Confidence::High,
        CompatibilityStatus::FullyImplemented,
        "XNU osfmk/arm/cpu_data.h",
        "EL1 Kernel thread pointer holding current cpu_data_t / thread_t."
    },
    {
        "CPACR_EL1",
        0x4082u, 3, 0, 1, 0, 2, 1, true, true, 0x0ULL,
        Classification::GenericArchitectural,
        Confidence::High,
        CompatibilityStatus::FullyImplemented,
        "Armv8-A DDI 0487 / XNU osfmk/arm64/locore.s",
        "Coprocessor Access Control Register controlling FPEN (bits [21:20]) for NEON/FP."
    },
    {
        "CTR_EL0",
        0x5801u, 3, 3, 0, 0, 1, 0, true, false, 0x8444C004ULL,
        Classification::GenericArchitectural,
        Confidence::High,
        CompatibilityStatus::PartiallyImplemented,
        "Armv8-A DDI 0487 / Apple sys_icache_invalidate",
        "Cache Type Register defining ICache/DCache minimum line size and IDC/DIC attributes."
    },
    {
        "CLIDR_EL1",
        0x4801u, 3, 1, 0, 0, 1, 1, true, false, 0x09200023ULL,
        Classification::GenericArchitectural,
        Confidence::High,
        CompatibilityStatus::Stubbed,
        "Armv8-A DDI 0487 / XNU cache discovery",
        "Cache Level ID Register describing multi-level cache hierarchy."
    },
    {
        "CCSIDR_EL1",
        0x4800u, 3, 1, 0, 0, 0, 1, true, false, 0x701FE00AULL,
        Classification::GenericArchitectural,
        Confidence::High,
        CompatibilityStatus::Stubbed,
        "Armv8-A DDI 0487 / XNU cache sizing",
        "Current Cache Size ID Register indexed by CSSELR_EL1."
    },
    {
        "CSSELR_EL1",
        0x5000u, 3, 2, 0, 0, 0, 1, true, true, 0x0ULL,
        Classification::GenericArchitectural,
        Confidence::High,
        CompatibilityStatus::Stubbed,
        "Armv8-A DDI 0487 / XNU cache sizing",
        "Cache Size Selection Register selecting cache level and type for CCSIDR."
    },
    {
        "ACTLR_EL1",
        0x4081u, 3, 0, 1, 0, 1, 1, true, true, 0x0ULL,
        Classification::GenericArchitectural,
        Confidence::Medium,
        CompatibilityStatus::PartiallyImplemented,
        "XNU osfmk/arm64/locore.s",
        "Auxiliary Control Register configured during early kernel CPU bring-up."
    },
    {
        "AIDR_EL1",
        0x4807u, 3, 1, 0, 0, 7, 1, true, false, 0x0ULL,
        Classification::GenericArchitectural,
        Confidence::High,
        CompatibilityStatus::Stubbed,
        "Armv8-A DDI 0487",
        "Auxiliary ID Register identifying implementation-specific revision."
    },

    // --- 2. Apple Silicon Specific System Registers (Research Foundation) ---
    {
        "APSTS_EL1",
        0x6784u, 3, 4, 15, 0, 4, 1, true, false, 0x0ULL,
        Classification::ResearchOnly,
        Confidence::Medium,
        CompatibilityStatus::Deferred,
        "m1n1 / Asahi Linux public research",
        "Apple Silicon Power Status Register reflecting cluster deep sleep / power rail."
    },
    {
        "ACC_OVRD_EL1",
        0x6F93u, 3, 5, 15, 2, 3, 1, true, true, 0x0ULL,
        Classification::ResearchOnly,
        Confidence::Low,
        CompatibilityStatus::Deferred,
        "m1n1 / Asahi Linux public research",
        "Apple Core Configuration Override Register for speculative execution controls."
    },
    {
        "SPRR_CONFIG_EL1",
        0x778Du, 3, 6, 15, 1, 5, 1, true, true, 0x0ULL,
        Classification::PubliclyDocumentedDarwin,
        Confidence::Medium,
        CompatibilityStatus::Deferred,
        "Apple XNU pmap_sprr.c / OpenSource xnu-8792",
        "Permission Indirection / Stage 1 Permission Overlay Register."
    },
    {
        "GXF_CONFIG_EL1",
        0x778Au, 3, 6, 15, 1, 2, 1, true, true, 0x0ULL,
        Classification::PubliclyDocumentedDarwin,
        Confidence::Low,
        CompatibilityStatus::Deferred,
        "Apple XNU gxf.h / OpenSource xnu-8792",
        "Guarded Execution Filter Configuration for user-kernel call interception."
    },
    {
        "PMC0_EL0",
        0x5780u, 3, 2, 15, 0, 0, 0, true, true, 0x0ULL,
        Classification::PubliclyDocumentedDarwin,
        Confidence::Medium,
        CompatibilityStatus::Deferred,
        "Apple Instruments / kperf documentation",
        "Apple Custom Performance Monitor Counter 0."
    },
    {
        "PMC1_EL0",
        0x5788u, 3, 2, 15, 1, 0, 0, true, true, 0x0ULL,
        Classification::PubliclyDocumentedDarwin,
        Confidence::Medium,
        CompatibilityStatus::Deferred,
        "Apple Instruments / kperf documentation",
        "Apple Custom Performance Monitor Counter 1."
    },
    {
        "HID0_EL1",
        0x4780u, 3, 0, 15, 0, 0, 1, true, true, 0x0ULL,
        Classification::ResearchOnly,
        Confidence::Low,
        CompatibilityStatus::Deferred,
        "m1n1 public reverse engineering",
        "Hardware Implementation Dependent Register 0 (pipeline branch predictor controls)."
    },
    {
        "HID1_EL1",
        0x4788u, 3, 0, 15, 1, 0, 1, true, true, 0x0ULL,
        Classification::ResearchOnly,
        Confidence::Low,
        CompatibilityStatus::Deferred,
        "m1n1 public reverse engineering",
        "Hardware Implementation Dependent Register 1 (memory disambiguation controls)."
    },
    {
        "HID4_EL1",
        0x47A0u, 3, 0, 15, 4, 0, 1, true, true, 0x0ULL,
        Classification::ResearchOnly,
        Confidence::Low,
        CompatibilityStatus::Deferred,
        "m1n1 public reverse engineering",
        "Hardware Implementation Dependent Register 4 (cache prefetcher tuning)."
    },
    {
        "HID5_EL1",
        0x47A8u, 3, 0, 15, 5, 0, 1, true, true, 0x0ULL,
        Classification::ResearchOnly,
        Confidence::Low,
        CompatibilityStatus::Deferred,
        "m1n1 public reverse engineering",
        "Hardware Implementation Dependent Register 5 (load queue and store buffer sizing)."
    }
};

} // namespace

std::uint64_t DarwinProfileConfig::compute_profile_hash() const noexcept {
    // 64-bit FNV-1a hash of profile configuration
    std::uint64_t h = 0xCBF29CE484222325ULL;
    auto add_byte = [&h](std::uint8_t b) {
        h ^= static_cast<std::uint64_t>(b);
        h *= 0x100000001B3ULL;
    };
    add_byte(static_cast<std::uint8_t>(mode));
    add_byte(allow_actlr_el1_rw ? 1 : 0);
    add_byte(log_unimplemented_darwin_access ? 1 : 0);
    return h;
}

std::span<const DarwinSysRegEntry> DarwinCompatibilityRegistry::get_all_entries() noexcept {
    return kDarwinEntries;
}

const DarwinSysRegEntry* DarwinCompatibilityRegistry::find_by_key(std::uint16_t key) noexcept {
    for (const auto& entry : kDarwinEntries) {
        if (entry.key == key) return &entry;
    }
    return nullptr;
}

const DarwinSysRegEntry* DarwinCompatibilityRegistry::find_by_name(std::string_view name) noexcept {
    for (const auto& entry : kDarwinEntries) {
        if (name == entry.name) return &entry;
    }
    return nullptr;
}

std::size_t DarwinCompatibilityRegistry::get_entry_count() noexcept {
    return std::size(kDarwinEntries);
}

std::size_t DarwinCompatibilityRegistry::count_by_classification(Classification c) noexcept {
    std::size_t count = 0;
    for (const auto& entry : kDarwinEntries) {
        if (entry.classification == c) ++count;
    }
    return count;
}

std::size_t DarwinCompatibilityRegistry::count_by_status(CompatibilityStatus s) noexcept {
    std::size_t count = 0;
    for (const auto& entry : kDarwinEntries) {
        if (entry.status == s) ++count;
    }
    return count;
}

} // namespace winios::darwin
