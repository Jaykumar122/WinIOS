#include "loader/initramfs_loader.h"

#include <cstring>
#include <fstream>
#include "loader/linux_image_loader.h"

namespace winios::loader {

InitramfsFormat InitramfsLoader::detect_format(const void* data, std::size_t size) noexcept {
    if (!data || size < 2) return InitramfsFormat::Unknown;

    const auto* p = static_cast<const std::uint8_t*>(data);

    // Gzip magic: 0x1F, 0x8B
    if (p[0] == 0x1F && p[1] == 0x8B) {
        return InitramfsFormat::Gzip;
    }

    // CPIO magic check (first 6 characters)
    if (size >= 6) {
        if (std::memcmp(p, "070701", 6) == 0) {
            return InitramfsFormat::CpioNewc;
        }
        if (std::memcmp(p, "070702", 6) == 0) {
            return InitramfsFormat::CpioCrc;
        }
        if (std::memcmp(p, "070707", 6) == 0) {
            return InitramfsFormat::CpioOld;
        }
    }

    return InitramfsFormat::RawBinary;
}

InitramfsLoadResult InitramfsLoader::load_from_buffer(
    const void* data,
    std::size_t size,
    Memory& guest_mem,
    std::uint64_t target_load_addr,
    std::uint64_t ram_base,
    std::uint64_t ram_size,
    GuestMemoryMap* memory_map) {

    InitramfsLoadResult result{};

    if (!data || size == 0) {
        result.status = InitramfsLoadStatus::EmptyBuffer;
        result.error_message = "Initramfs buffer is empty";
        return result;
    }

    // 1. Alignment check (must be 4KB page aligned)
    if ((target_load_addr % 4096ULL) != 0) {
        result.status = InitramfsLoadStatus::MisalignedPlacement;
        result.error_message = "Initramfs target address must be 4KB page aligned (0x1000)";
        return result;
    }

    // 2. RAM containment check
    std::uint64_t ram_end = ram_base + ram_size;
    std::uint64_t initrd_end = target_load_addr + size;
    if (target_load_addr < ram_base || initrd_end > ram_end) {
        result.status = InitramfsLoadStatus::RamCapacityExceeded;
        result.error_message = "Initramfs extends outside physical RAM bounds";
        return result;
    }

    // 3. Collision check via memory map if provided
    if (memory_map) {
        if (!memory_map->register_initramfs(target_load_addr, size)) {
            result.status = InitramfsLoadStatus::MemoryCollision;
            result.error_message = "Failed to register initramfs in guest memory map";
            return result;
        }
        std::string err;
        if (memory_map->validate(&err) != MemoryMapValidationResult::Valid) {
            result.status = InitramfsLoadStatus::MemoryCollision;
            result.error_message = "Initramfs collision: " + err;
            return result;
        }
    }

    // 4. Memory offset calculation (flat or base-relative)
    std::uint64_t mem_offset = target_load_addr;
    if (guest_mem.size() < target_load_addr + size) {
        if (target_load_addr >= ram_base && (target_load_addr - ram_base + size) <= guest_mem.size()) {
            mem_offset = target_load_addr - ram_base;
        } else {
            result.status = InitramfsLoadStatus::MemoryWriteFailed;
            result.error_message = "Guest memory capacity too small for initramfs write";
            return result;
        }
    }

    // 5. Write initramfs bytes to physical RAM
    const auto* src = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        guest_mem.write_u8(mem_offset + i, src[i]);
    }

    // 6. Populate result
    result.status = InitramfsLoadStatus::Success;
    result.format = detect_format(data, size);
    result.load_address = target_load_addr;
    result.end_address = initrd_end;
    result.size = size;
    result.sha256_hex = LinuxImageLoader::compute_sha256(data, size);

    return result;
}

InitramfsLoadResult InitramfsLoader::load_from_file(
    const std::string& path,
    Memory& guest_mem,
    std::uint64_t target_load_addr,
    std::uint64_t ram_base,
    std::uint64_t ram_size,
    GuestMemoryMap* memory_map) {

    InitramfsLoadResult result{};

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        result.status = InitramfsLoadStatus::FileNotFound;
        result.error_message = "Cannot open initramfs file: " + path;
        return result;
    }

    std::streamsize file_size = file.tellg();
    if (file_size <= 0) {
        result.status = InitramfsLoadStatus::EmptyBuffer;
        result.error_message = "Initramfs file is empty: " + path;
        return result;
    }

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        result.status = InitramfsLoadStatus::FileReadError;
        result.error_message = "Failed reading initramfs file: " + path;
        return result;
    }

    return load_from_buffer(buffer.data(), buffer.size(), guest_mem, target_load_addr, ram_base, ram_size, memory_map);
}

} // namespace winios::loader
