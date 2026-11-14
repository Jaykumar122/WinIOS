#include "loader/synthetic_kernel_loader.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

#include "loader/real_artifact_harness.h"

namespace winios::loader {

const char* synthetic_kernel_load_status_string(SyntheticKernelLoadStatus s) noexcept {
    switch (s) {
        case SyntheticKernelLoadStatus::Success:
            return "Success";
        case SyntheticKernelLoadStatus::FileNotFound:
            return "FileNotFound";
        case SyntheticKernelLoadStatus::FileReadError:
            return "FileReadError";
        case SyntheticKernelLoadStatus::InvalidMachO:
            return "InvalidMachO";
        case SyntheticKernelLoadStatus::InvalidMagic:
            return "InvalidMagic";
        case SyntheticKernelLoadStatus::UnsupportedArchitecture:
            return "UnsupportedArchitecture";
        case SyntheticKernelLoadStatus::MemoryCapacityExceeded:
            return "MemoryCapacityExceeded";
        case SyntheticKernelLoadStatus::InvalidBootInfo:
            return "InvalidBootInfo";
        case SyntheticKernelLoadStatus::NullMemory:
            return "NullMemory";
    }
    return "Unknown";
}

static std::string resolve_candidate_path(const std::string& path) {
    const std::vector<std::string> candidates = {
        path,
        "WinIOS/" + path,
        "../" + path,
        "../../" + path,
        "D:/WinISO26/WinIOS/" + path,
        "D:/WinISO26/" + path
    };
    for (const auto& c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f.is_open()) {
            return c;
        }
    }
    return path;
}

SyntheticKernelLoadResult SyntheticKernelLoader::load_from_file(
    const std::string& path,
    Memory& guest_mem,
    const SyntheticKernelLoadConfig& config) {
    SyntheticKernelLoadResult res;
    const std::string resolved = resolve_candidate_path(path);
    std::ifstream file(resolved, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        res.status = SyntheticKernelLoadStatus::FileNotFound;
        res.error_message = "Synthetic kernel artifact not found: " + path;
        return res;
    }

    const auto file_size = file.tellg();
    if (file_size <= 0) {
        res.status = SyntheticKernelLoadStatus::FileReadError;
        res.error_message = "Synthetic kernel artifact is empty: " + path;
        return res;
    }

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(file_size));
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);
    if (!file) {
        res.status = SyntheticKernelLoadStatus::FileReadError;
        res.error_message = "Failed reading synthetic kernel file: " + path;
        return res;
    }

    return load_from_buffer(buffer.data(), buffer.size(), guest_mem, config);
}

SyntheticKernelLoadResult SyntheticKernelLoader::load_from_buffer(
    const void* data,
    std::size_t size,
    Memory& guest_mem,
    const SyntheticKernelLoadConfig& config) {
    SyntheticKernelLoadResult res;
    if (data == nullptr || size == 0) {
        res.status = SyntheticKernelLoadStatus::InvalidMachO;
        res.error_message = "Kernel image buffer is null or empty";
        return res;
    }

    if (guest_mem.size() == 0) {
        res.status = SyntheticKernelLoadStatus::NullMemory;
        res.error_message = "Guest RAM capacity is zero";
        return res;
    }

    const auto* byte_data = reinterpret_cast<const std::uint8_t*>(data);
    res.sha256_hex = RealArtifactHarness::compute_sha256(byte_data, size);

    // Adapt configuration if default values exceed small test memory size
    std::uint64_t eff_dram_base = config.dram_base;
    std::uint64_t eff_dram_size = config.dram_size;
    std::uint64_t eff_boot_info_addr = config.boot_info_addr;
    std::uint64_t eff_sp_el1 = config.initial_sp_el1;
    std::uint64_t eff_sp_el0 = config.initial_sp_el0;

    if (config.dram_base >= guest_mem.size() && config.dram_base == 0x40000000ULL) {
        eff_dram_base = 0;
        eff_dram_size = guest_mem.size();
        eff_boot_info_addr = 0x8000;
        eff_sp_el1 = (guest_mem.size() >= 0x80000) ? 0x70000 : (guest_mem.size() - 0x10000);
        eff_sp_el0 = (guest_mem.size() >= 0x80000) ? 0x80000 : (guest_mem.size() - 0x8000);
    } else if (eff_dram_base + eff_dram_size > guest_mem.size()) {
        if (guest_mem.size() > eff_dram_base) {
            eff_dram_size = guest_mem.size() - eff_dram_base;
        } else {
            res.status = SyntheticKernelLoadStatus::MemoryCapacityExceeded;
            res.error_message = "DRAM base exceeds guest physical memory size";
            return res;
        }
    }

    // Load Mach-O segments into guest memory
    res.macho_result = MachOLoader::load_from_buffer(
        data, size, guest_mem, eff_dram_base, eff_dram_size);
    if (!res.macho_result.is_success()) {
        res.status = SyntheticKernelLoadStatus::InvalidMachO;
        res.error_message = "MachOLoader failed: " + res.macho_result.error_message;
        return res;
    }

    // Determine segment bounds
    std::uint64_t text_base = 0;
    std::uint64_t text_size = 0;
    std::uint64_t data_base = 0;
    std::uint64_t data_size = 0;

    for (const auto& seg : res.macho_result.segments) {
        if (seg.name == "__TEXT") {
            text_base = seg.vmaddr;
            text_size = seg.vmsize;
        } else if (seg.name == "__DATA" || seg.name == "__DATA_CONST") {
            data_base = seg.vmaddr;
            data_size = seg.vmsize;
        }
    }

    if (text_base == 0 && !res.macho_result.segments.empty()) {
        text_base = res.macho_result.segments.front().vmaddr;
        text_size = res.macho_result.segments.front().vmsize;
    }

    const std::uint64_t granule = (config.page_granule != 0) ? config.page_granule : 16384ULL;
    const std::uint64_t highest_kernel_addr = res.macho_result.highest_vmaddr;
    const std::uint64_t aligned_kernel_end = (highest_kernel_addr + (granule - 1)) & ~(granule - 1);

    // Command line placement right after boot info
    const std::uint64_t cmdline_addr = eff_boot_info_addr + sizeof(SyntheticKernelBootInfo);
    const std::uint64_t cmdline_len = config.cmdline.size();

    // Calculate free memory bounds
    std::uint64_t free_base = 0;
    std::uint64_t free_size = 0;
    if (config.auto_calculate_free_memory) {
        const std::uint64_t min_free = std::max(aligned_kernel_end, cmdline_addr + cmdline_len + 64);
        free_base = (min_free + (granule - 1)) & ~(granule - 1);
        if (eff_dram_base + eff_dram_size > free_base) {
            free_size = (eff_dram_base + eff_dram_size) - free_base;
        } else {
            free_size = 0;
        }
    } else {
        free_base = config.explicit_free_base;
        free_size = config.explicit_free_size;
    }

    // Populate SyntheticKernelBootInfo struct
    SyntheticKernelBootInfo& boot = res.boot_info;
    boot.magic = kSyntheticKernelMagic;
    boot.version = kSyntheticKernelVersion;
    boot.flags = kBootFlagConsolePresent;
    if (config.dtb_base != 0 && config.dtb_size != 0) {
        boot.flags |= kBootFlagDtbPresent;
    }
    if (config.cpu_count > 1) {
        boot.flags |= kBootFlagSmpEnabled;
    }
    boot.dram_base = eff_dram_base;
    boot.dram_size = eff_dram_size;
    boot.kernel_text_base = text_base;
    boot.kernel_text_size = text_size;
    boot.kernel_data_base = data_base;
    boot.kernel_data_size = data_size;
    boot.entry_point = res.macho_result.entry_point;
    boot.initial_sp_el1 = eff_sp_el1;
    boot.initial_sp_el0 = eff_sp_el0;
    boot.mmio_console_base = config.mmio_console_base;
    boot.mmio_console_size = config.mmio_console_size;
    boot.free_memory_base = free_base;
    boot.free_memory_size = free_size;
    boot.cmdline_addr = cmdline_addr;
    boot.cmdline_size = cmdline_len;
    boot.dtb_base = config.dtb_base;
    boot.dtb_size = config.dtb_size;
    boot.boot_timestamp_ticks = 0;
    boot.cpu_count = config.cpu_count;
    boot.page_granule_bytes = granule;

    // Write command line to memory
    if (cmdline_addr + cmdline_len < guest_mem.size()) {
        for (std::size_t i = 0; i < cmdline_len; ++i) {
            guest_mem.write_u8(static_cast<std::size_t>(cmdline_addr + i),
                               static_cast<std::uint8_t>(config.cmdline[i]));
        }
        guest_mem.write_u8(static_cast<std::size_t>(cmdline_addr + cmdline_len), 0); // null terminator
    }

    // Write boot contract struct into guest memory
    if (!write_boot_info(guest_mem, eff_boot_info_addr, boot)) {
        res.status = SyntheticKernelLoadStatus::MemoryCapacityExceeded;
        res.error_message = "Failed to write boot info contract into guest memory";
        return res;
    }

    // Validate the populated boot contract
    std::string val_err;
    if (!validate_boot_info(boot, &val_err)) {
        res.status = SyntheticKernelLoadStatus::InvalidBootInfo;
        res.error_message = "Boot contract validation failed: " + val_err;
        return res;
    }

    res.status = SyntheticKernelLoadStatus::Success;
    res.boot_info_guest_addr = eff_boot_info_addr;
    res.entry_point = res.macho_result.entry_point;
    res.initial_sp_el1 = eff_sp_el1;
    res.initial_sp_el0 = eff_sp_el0;
    return res;
}

bool SyntheticKernelLoader::write_boot_info(
    Memory& guest_mem,
    std::uint64_t guest_addr,
    const SyntheticKernelBootInfo& info) {
    if (guest_addr + sizeof(SyntheticKernelBootInfo) > guest_mem.size()) {
        return false;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&info);
    for (std::size_t i = 0; i < sizeof(SyntheticKernelBootInfo); ++i) {
        guest_mem.write_u8(static_cast<std::size_t>(guest_addr + i), bytes[i]);
    }
    return true;
}

bool SyntheticKernelLoader::read_boot_info(
    const Memory& guest_mem,
    std::uint64_t guest_addr,
    SyntheticKernelBootInfo& out_info) {
    if (guest_addr + sizeof(SyntheticKernelBootInfo) > guest_mem.size()) {
        return false;
    }

    auto* dest = reinterpret_cast<std::uint8_t*>(&out_info);
    for (std::size_t i = 0; i < sizeof(SyntheticKernelBootInfo); ++i) {
        dest[i] = guest_mem.read_u8(static_cast<std::size_t>(guest_addr + i));
    }
    return true;
}

bool SyntheticKernelLoader::validate_boot_info(
    const SyntheticKernelBootInfo& info,
    std::string* out_error) {
    if (info.magic != kSyntheticKernelMagic) {
        if (out_error != nullptr) *out_error = "Invalid boot contract magic number";
        return false;
    }
    if (info.version != kSyntheticKernelVersion) {
        if (out_error != nullptr) *out_error = "Unsupported boot contract version";
        return false;
    }
    if (info.dram_size == 0) {
        if (out_error != nullptr) *out_error = "DRAM size is zero";
        return false;
    }
    if (info.entry_point == 0) {
        if (out_error != nullptr) *out_error = "Kernel entry point is zero";
        return false;
    }
    if (info.page_granule_bytes != 4096 &&
        info.page_granule_bytes != 16384 &&
        info.page_granule_bytes != 65536) {
        if (out_error != nullptr) *out_error = "Invalid ARM64 translation page granule";
        return false;
    }
    return true;
}

} // namespace winios::loader
