#include "loader/ramdisk_loader.h"

#include <cstring>
#include <fstream>
#include "loader/linux_image_loader.h"

namespace winios::loader {

RamdiskFormat RamdiskLoader::detect_format(const void* data, std::size_t size) noexcept {
    if (!data || size < 4) return RamdiskFormat::Unknown;

    const auto* p = static_cast<const std::uint8_t*>(data);

    // 1. APFS Container Superblock ('NXSB' / 0x4253584E)
    // APFS magic is at byte offset 32 within the container superblock (or at offset 0/32)
    if (size >= 36) {
        if (p[32] == 'N' && p[33] == 'X' && p[34] == 'S' && p[35] == 'B') {
            return RamdiskFormat::ApfsContainer;
        }
    }
    if (size >= 4) {
        if (p[0] == 'N' && p[1] == 'X' && p[2] == 'S' && p[3] == 'B') {
            return RamdiskFormat::ApfsContainer;
        }
    }

    // 2. HFS+ Volume Header ('H+' / 0x482B or 'HX' / 0x4858 at byte offset 1024)
    if (size >= 1026) {
        if (p[1024] == 'H' && (p[1025] == '+' || p[1025] == 'X')) {
            return RamdiskFormat::HfsPlus;
        }
    }

    // 3. FAT32 Boot Sector (0x55, 0xAA at offset 510)
    if (size >= 512) {
        if (p[510] == 0x55 && p[511] == 0xAA) {
            return RamdiskFormat::Fat32;
        }
    }

    return RamdiskFormat::RawBinary;
}

RamdiskLoadResult RamdiskLoader::load_from_buffer(
    const void* data,
    std::size_t size,
    Memory& guest_mem,
    std::uint64_t target_load_addr,
    std::uint64_t ram_base,
    std::uint64_t ram_size,
    GuestMemoryMap* memory_map) {

    RamdiskLoadResult result{};

    if (!data || size == 0) {
        result.status = RamdiskLoadStatus::EmptyBuffer;
        result.error_message = "Ramdisk buffer is empty";
        return result;
    }

    // 1. Apple Silicon 16 KB Granule Alignment Check
    if ((target_load_addr % 0x4000ULL) != 0) {
        result.status = RamdiskLoadStatus::MisalignedPlacement;
        result.error_message = "Ramdisk target address must be 16 KB aligned (0x4000) for Apple Silicon";
        return result;
    }

    // 2. RAM Containment Check
    const std::uint64_t ram_end = ram_base + ram_size;
    const std::uint64_t target_end = target_load_addr + size;
    if (target_load_addr < ram_base || target_end > ram_end) {
        result.status = RamdiskLoadStatus::RamCapacityExceeded;
        result.error_message = "Ramdisk extends outside guest RAM boundaries";
        return result;
    }

    // 3. Collision Check via GuestMemoryMap
    if (memory_map) {
        if (!memory_map->register_reserved("DarwinRamdisk", target_load_addr, size)) {
            result.status = RamdiskLoadStatus::MemoryCollision;
            result.error_message = "Failed to reserve ramdisk region in guest memory map";
            return result;
        }
        std::string err;
        if (memory_map->validate(&err) != MemoryMapValidationResult::Valid) {
            result.status = RamdiskLoadStatus::MemoryCollision;
            result.error_message = "Ramdisk collision detected: " + err;
            return result;
        }
    }

    // 4. Memory Offset Calculation
    std::uint64_t mem_offset = target_load_addr;
    if (guest_mem.size() < target_load_addr + size) {
        if (target_load_addr >= ram_base && (target_load_addr - ram_base + size) <= guest_mem.size()) {
            mem_offset = target_load_addr - ram_base;
        } else {
            result.status = RamdiskLoadStatus::MemoryWriteFailed;
            result.error_message = "Guest memory capacity insufficient for ramdisk allocation";
            return result;
        }
    }

    // 5. Copy bytes to guest RAM
    const auto* src = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        guest_mem.write_u8(mem_offset + i, src[i]);
    }

    // 6. Populate Load Result
    result.status = RamdiskLoadStatus::Success;
    result.format = detect_format(data, size);
    result.physical_address = target_load_addr;
    result.size = size;
    result.sha256_hex = LinuxImageLoader::compute_sha256(data, size);

    return result;
}

RamdiskLoadResult RamdiskLoader::load_from_file(
    const std::string& path,
    Memory& guest_mem,
    std::uint64_t target_load_addr,
    std::uint64_t ram_base,
    std::uint64_t ram_size,
    GuestMemoryMap* memory_map) {

    RamdiskLoadResult result{};

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        result.status = RamdiskLoadStatus::FileNotFound;
        result.error_message = "Cannot open ramdisk image file: " + path;
        return result;
    }

    const std::streamsize file_size = file.tellg();
    if (file_size <= 0) {
        result.status = RamdiskLoadStatus::EmptyBuffer;
        result.error_message = "Ramdisk file is empty: " + path;
        return result;
    }

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        result.status = RamdiskLoadStatus::FileReadError;
        result.error_message = "Failed reading ramdisk image file: " + path;
        return result;
    }

    return load_from_buffer(
        buffer.data(), buffer.size(), guest_mem,
        target_load_addr, ram_base, ram_size, memory_map);
}

} // namespace winios::loader
