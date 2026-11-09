#include "loader/shared_library_mapper.h"

#include <algorithm>
#include <cstring>
#include "loader/dyld_engine.h"

namespace winios::loader {

const char* shared_lib_map_status_string(SharedLibMapStatus s) noexcept {
    switch (s) {
        case SharedLibMapStatus::Success:           return "Success";
        case SharedLibMapStatus::AddressCollision:  return "AddressCollision";
        case SharedLibMapStatus::MemoryExhausted:   return "MemoryExhausted";
        case SharedLibMapStatus::InvalidMachO:      return "InvalidMachO";
        case SharedLibMapStatus::MemoryWriteFailed: return "MemoryWriteFailed";
        case SharedLibMapStatus::ImageNotFound:     return "ImageNotFound";
        case SharedLibMapStatus::UnalignedSegment:  return "UnalignedSegment";
        default:                                    return "Unknown";
    }
}

namespace {

inline std::size_t translate_vaddr(std::uint64_t vaddr, std::size_t mem_size, std::uint64_t base) noexcept {
    if (vaddr < mem_size) {
        return static_cast<std::size_t>(vaddr);
    }
    if (base > 0 && vaddr >= base) {
        std::uint64_t rel = vaddr - base;
        if (rel < mem_size) {
            return static_cast<std::size_t>(rel);
        }
    }
    return static_cast<std::size_t>(vaddr % mem_size);
}

} // anonymous namespace

SharedLibraryMapper::SharedLibraryMapper(
    std::uint64_t dylib_arena_base,
    std::uint64_t dylib_arena_size)
    : dylib_arena_base_(dylib_arena_base),
      dylib_arena_size_(dylib_arena_size),
      next_dylib_alloc_vaddr_(dylib_arena_base) {
}

std::uint64_t SharedLibraryMapper::allocate_dylib_vaddr(std::uint64_t required_size) {
    constexpr std::uint64_t kAlign = 0x4000ULL; // 16 KB page boundary
    std::uint64_t alloc_vaddr = (next_dylib_alloc_vaddr_ + (kAlign - 1)) & ~(kAlign - 1);
    std::uint64_t needed = (required_size + (kAlign - 1)) & ~(kAlign - 1);
    if (alloc_vaddr + needed > dylib_arena_base_ + dylib_arena_size_ || alloc_vaddr + needed < alloc_vaddr) {
        return 0; // Arena exhausted
    }
    next_dylib_alloc_vaddr_ = alloc_vaddr + needed;
    return alloc_vaddr;
}

bool SharedLibraryMapper::check_collision(std::uint64_t vaddr, std::uint64_t size) const noexcept {
    if (size == 0) return false;
    const std::uint64_t end = vaddr + size;

    for (const auto& img : mapped_images_) {
        for (const auto& seg : img.segments) {
            if (seg.vmsize == 0 || seg.is_pagezero) continue;
            const std::uint64_t seg_start = seg.mapped_vaddr;
            const std::uint64_t seg_end   = seg.mapped_vaddr + seg.vmsize;

            if (vaddr < seg_end && seg_start < end) {
                return true; // Collision detected
            }
        }
    }
    return false;
}

SharedLibMapStatus SharedLibraryMapper::map_main_executable(
    const void* macho_bytes,
    std::size_t macho_size,
    Memory& memory,
    std::uint64_t aslr_slide,
    std::string& out_error) {

    UserspaceLoadResult meta;
    if (!UserspaceMachOLoader::parse_metadata(macho_bytes, macho_size, meta)) {
        out_error = "Failed to parse main executable Mach-O metadata: " + meta.error_message;
        return SharedLibMapStatus::InvalidMachO;
    }

    MappedImageRecord record;
    record.path = "main_executable";
    record.install_name = "main_executable";
    record.aslr_slide = aslr_slide;
    record.is_main_executable = true;

    const auto* bytes = static_cast<const std::uint8_t*>(macho_bytes);

    std::uint64_t min_vaddr = UINT64_MAX;
    std::uint64_t max_vaddr = 0;

    for (auto seg : meta.segments) {
        if (seg.is_pagezero) {
            seg.mapped_vaddr = 0;
            record.segments.push_back(std::move(seg));
            continue;
        }

        seg.mapped_vaddr = seg.vmaddr + aslr_slide;

        if (check_collision(seg.mapped_vaddr, seg.vmsize)) {
            out_error = "Collision mapping main executable segment " + seg.name;
            return SharedLibMapStatus::AddressCollision;
        }

        if (seg.mapped_vaddr < min_vaddr) min_vaddr = seg.mapped_vaddr;
        if (seg.mapped_vaddr + seg.vmsize > max_vaddr) max_vaddr = seg.mapped_vaddr + seg.vmsize;

        // Copy segment file content to memory
        if (seg.filesize > 0 && seg.fileoff + seg.filesize <= macho_size) {
            std::size_t mem_off = translate_vaddr(seg.mapped_vaddr, memory.size(), seg.mapped_vaddr);
            if (mem_off + seg.filesize > memory.size()) {
                out_error = "Memory bounds exceeded writing segment " + seg.name;
                return SharedLibMapStatus::MemoryWriteFailed;
            }
            for (std::size_t i = 0; i < seg.filesize; ++i) {
                memory.write_u8(mem_off + i, bytes[seg.fileoff + i]);
            }
        }

        record.segments.push_back(std::move(seg));
    }

    record.base_vaddr = (min_vaddr != UINT64_MAX) ? min_vaddr : aslr_slide;
    record.vmsize = (max_vaddr > min_vaddr) ? (max_vaddr - min_vaddr) : 0;

    mapped_images_.push_back(std::move(record));
    return SharedLibMapStatus::Success;
}

SharedLibMapStatus SharedLibraryMapper::map_dylib(
    const void* macho_bytes,
    std::size_t macho_size,
    const std::string& path,
    Memory& memory,
    std::optional<std::uint64_t> requested_base,
    std::uint64_t aslr_slide,
    std::string& out_error) {

    UserspaceLoadResult meta;
    if (!UserspaceMachOLoader::parse_metadata(macho_bytes, macho_size, meta)) {
        out_error = "Failed to parse dylib Mach-O metadata: " + meta.error_message;
        return SharedLibMapStatus::InvalidMachO;
    }

    std::uint64_t total_vmsize = 0;
    std::uint64_t dylib_first_vmaddr = UINT64_MAX;
    for (const auto& s : meta.segments) {
        if (!s.is_pagezero) {
            total_vmsize += s.vmsize;
            if (s.vmaddr < dylib_first_vmaddr) dylib_first_vmaddr = s.vmaddr;
        }
    }
    if (dylib_first_vmaddr == UINT64_MAX) dylib_first_vmaddr = 0;

    std::uint64_t base_vaddr = requested_base.has_value() ? *requested_base : allocate_dylib_vaddr(total_vmsize);
    if (base_vaddr == 0) {
        out_error = "Shared library arena memory exhausted";
        return SharedLibMapStatus::MemoryExhausted;
    }

    MappedImageRecord record;
    record.path = path;
    record.install_name = path;
    record.base_vaddr = base_vaddr + aslr_slide;
    record.aslr_slide = aslr_slide;
    record.vmsize = total_vmsize;
    record.is_main_executable = false;

    const auto* bytes = static_cast<const std::uint8_t*>(macho_bytes);

    for (auto seg : meta.segments) {
        if (seg.is_pagezero) {
            seg.mapped_vaddr = 0;
            record.segments.push_back(std::move(seg));
            continue;
        }

        std::uint64_t rel = seg.vmaddr - dylib_first_vmaddr;
        seg.mapped_vaddr = base_vaddr + rel + aslr_slide;

        if (check_collision(seg.mapped_vaddr, seg.vmsize)) {
            out_error = "Collision mapping dylib segment " + seg.name + " at 0x" + std::to_string(seg.mapped_vaddr);
            return SharedLibMapStatus::AddressCollision;
        }

        if (seg.filesize > 0 && seg.fileoff + seg.filesize <= macho_size) {
            std::size_t mem_off = translate_vaddr(seg.mapped_vaddr, memory.size(), record.base_vaddr);
            if (mem_off + seg.filesize > memory.size()) {
                out_error = "Memory bounds exceeded writing dylib segment " + seg.name;
                return SharedLibMapStatus::MemoryWriteFailed;
            }
            for (std::size_t i = 0; i < seg.filesize; ++i) {
                memory.write_u8(mem_off + i, bytes[seg.fileoff + i]);
            }
        }

        record.segments.push_back(std::move(seg));
    }

    mapped_images_.push_back(std::move(record));
    return SharedLibMapStatus::Success;
}

SharedLibMapStatus SharedLibraryMapper::unmap_image(
    std::string_view path,
    Memory& memory,
    std::string& out_error) {

    auto it = std::find_if(mapped_images_.begin(), mapped_images_.end(), [&](const MappedImageRecord& r) {
        return r.path == path || r.install_name == path;
    });

    if (it == mapped_images_.end()) {
        out_error = "Image not found for unmapping: " + std::string(path);
        return SharedLibMapStatus::ImageNotFound;
    }

    // Zero out memory of mapped segments
    for (const auto& seg : it->segments) {
        if (seg.vmsize > 0 && !seg.is_pagezero && seg.mapped_vaddr > 0) {
            std::size_t mem_off = translate_vaddr(seg.mapped_vaddr, memory.size(), it->base_vaddr);
            std::size_t clr_len = std::min<std::size_t>(static_cast<std::size_t>(seg.vmsize), memory.size() - mem_off);
            for (std::size_t i = 0; i < clr_len; ++i) {
                memory.write_u8(mem_off + i, 0);
            }
        }
    }

    mapped_images_.erase(it);
    return SharedLibMapStatus::Success;
}

const MappedImageRecord* SharedLibraryMapper::find_image(std::string_view path) const noexcept {
    for (const auto& img : mapped_images_) {
        if (img.path == path || img.install_name == path) {
            return &img;
        }
    }
    return nullptr;
}

const MappedImageRecord* SharedLibraryMapper::find_image_at(std::uint64_t vaddr) const noexcept {
    for (const auto& img : mapped_images_) {
        for (const auto& seg : img.segments) {
            if (seg.vmsize > 0 && !seg.is_pagezero) {
                if (vaddr >= seg.mapped_vaddr && vaddr < seg.mapped_vaddr + seg.vmsize) {
                    return &img;
                }
            }
        }
    }
    return nullptr;
}

const UserspaceSegment* SharedLibraryMapper::find_segment_at(std::uint64_t vaddr) const noexcept {
    for (const auto& img : mapped_images_) {
        for (const auto& seg : img.segments) {
            if (seg.vmsize > 0 && !seg.is_pagezero) {
                if (vaddr >= seg.mapped_vaddr && vaddr < seg.mapped_vaddr + seg.vmsize) {
                    return &seg;
                }
            }
        }
    }
    return nullptr;
}

} // namespace winios::loader
