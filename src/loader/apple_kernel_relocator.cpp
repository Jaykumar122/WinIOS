#include "loader/apple_kernel_relocator.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace winios::loader {

const char* AppleKernelRelocator::status_to_string(RelocationStatus status) noexcept {
    switch (status) {
        case RelocationStatus::Success:
            return "Success";
        case RelocationStatus::InvalidSlideAlignment:
            return "InvalidSlideAlignment (Slide must be 16 KB page-aligned)";
        case RelocationStatus::SlideOverflow:
            return "SlideOverflow (Slide exceeds 512 MB or causes 36-bit VA overflow)";
        case RelocationStatus::TruncatedMetadata:
            return "TruncatedMetadata (Fixup or relocation header truncated)";
        case RelocationStatus::UnsupportedFixupFormat:
            return "UnsupportedFixupFormat (Unknown pointer format in fixup chain)";
        case RelocationStatus::PointerOutOfRange:
            return "PointerOutOfRange (Rebased pointer outside kernel virtual space)";
        case RelocationStatus::CollisionDetected:
            return "CollisionDetected (Slid kernel collides with reserved aperture)";
        case RelocationStatus::RelocationBoundsExceeded:
            return "RelocationBoundsExceeded (Slid kernel exceeds physical DRAM capacity)";
    }
    return "Unknown";
}

const char* AppleKernelRelocator::reloc_type_to_string(KernelRelocationType type) noexcept {
    switch (type) {
        case KernelRelocationType::ChainedFixupRebase:   return "ChainedFixupRebase";
        case KernelRelocationType::Arm64RelocUnsigned:   return "ARM64_RELOC_UNSIGNED";
        case KernelRelocationType::Arm64RelocPage21:     return "ARM64_RELOC_PAGE21";
        case KernelRelocationType::Arm64RelocPageOff12:  return "ARM64_RELOC_PAGEOFF12";
        case KernelRelocationType::DyldChainedPtrArm64e: return "DYLD_CHAINED_PTR_ARM64E";
        case KernelRelocationType::Unknown:              return "Unknown";
    }
    return "Unknown";
}

RelocationStatus AppleKernelRelocator::validate_slide(
    std::uint64_t slide,
    std::uint64_t kernel_vmsize,
    std::string& out_error) {
    if (slide == 0) {
        return RelocationStatus::Success;
    }

    // 1. 16 KB Page Granule Alignment
    if (slide % 16384ULL != 0) {
        std::stringstream ss;
        ss << "KASLR slide 0x" << std::hex << slide << " is not aligned to Apple Silicon 16 KB page granule";
        out_error = ss.str();
        return RelocationStatus::InvalidSlideAlignment;
    }

    // 2. Slide Limit (Max 512 MB for 36-bit kernel virtual space)
    if (slide > 0x20000000ULL) {
        std::stringstream ss;
        ss << "KASLR slide 0x" << std::hex << slide << " exceeds 512 MB limit for 36-bit kernel virtual space";
        out_error = ss.str();
        return RelocationStatus::SlideOverflow;
    }

    // 3. Virtual Address Space Overflow Check
    constexpr std::uint64_t kMaxKernelVa = 0xFFFFFFFFFFFFFFFFULL;
    if (slide > (kMaxKernelVa - kernel_vmsize)) {
        out_error = "Slid kernel virtual footprint overflows 64-bit address space";
        return RelocationStatus::SlideOverflow;
    }

    return RelocationStatus::Success;
}

AppleKernelRelocationResult AppleKernelRelocator::relocate_kernel(
    Memory& guest_mem,
    AppleKernelLoadResult& kernel_load,
    std::uint64_t kaslr_slide,
    const memory::ApplePhysicalMemoryMap& memory_map) {
    AppleKernelRelocationResult result;
    const bool already_slid = (kaslr_slide > 0 && kernel_load.virt_kernel_base > kernel_load.macho_result.lowest_vmaddr);
    result.applied_slide = kaslr_slide;
    result.original_virt_base = already_slid ? (kernel_load.virt_kernel_base - kaslr_slide) : kernel_load.virt_kernel_base;
    result.original_phys_entry = already_slid ? (kernel_load.phys_entry_point - kaslr_slide) : kernel_load.phys_entry_point;
    result.original_virt_entry = already_slid ? (kernel_load.virt_entry_point - kaslr_slide) : kernel_load.virt_entry_point;

    // 1. Slide Validation
    RelocationStatus val_status = validate_slide(kaslr_slide, kernel_load.effective_kernel_size, result.error_message);
    if (val_status != RelocationStatus::Success) {
        result.status = val_status;
        return result;
    }

    // 2. Physical Memory Bounds Check
    std::uint64_t slid_phys_top = kernel_load.phys_load_base + kaslr_slide + kernel_load.effective_kernel_size;
    if (slid_phys_top > memory_map.ram_end()) {
        result.status = RelocationStatus::RelocationBoundsExceeded;
        std::stringstream ss;
        ss << "Slid kernel physical top (0x" << std::hex << slid_phys_top
           << ") exceeds DRAM capacity (0x" << memory_map.ram_end() << ")";
        result.error_message = ss.str();
        return result;
    }

    // 3. Collision Detection with Reserved Memory Regions
    for (const auto& r : memory_map.all_regions()) {
        if (r.kind != memory::AppleMemoryRegionKind::DramKernel &&
            r.kind != memory::AppleMemoryRegionKind::DramAvailable) {
            std::uint64_t kernel_start = kernel_load.phys_load_base + kaslr_slide;
            std::uint64_t kernel_end = kernel_start + kernel_load.effective_kernel_size;
            if (kernel_start < r.end_address() && r.phys_base < kernel_end) {
                result.status = RelocationStatus::CollisionDetected;
                result.error_message = "Slid kernel collides with existing region '" + r.name + "'";
                return result;
            }
        }
    }

    // 4. If slide is 0, nothing to rebase
    if (kaslr_slide == 0) {
        result.slid_virt_base = result.original_virt_base;
        result.slid_phys_entry = result.original_phys_entry;
        result.slid_virt_entry = result.original_virt_entry;
        result.status = RelocationStatus::Success;
        return result;
    }

    // 5. Apply Rebasing to Data Segments
    std::uint64_t virt_range_start = result.original_virt_base;
    std::uint64_t virt_range_end   = result.original_virt_base + kernel_load.effective_kernel_size;

    for (const auto& seg : kernel_load.macho_result.segments) {
        // Only inspect data segments (__DATA, __DATA_CONST, or segments with write/read permission)
        if (seg.name.find("__DATA") == std::string::npos &&
            seg.name.find("DATA") == std::string::npos &&
            seg.name.find("__kld") == std::string::npos) {
            continue;
        }

        std::size_t mem_offset = 0;
        if (!memory_map.translate_ram_address(seg.mapped_address, seg.filesize, mem_offset)) {
            continue;
        }

        // Scan 8-byte aligned pointer slots in the segment
        for (std::size_t off = 0; off + 8 <= seg.filesize; off += 8) {
            std::uint64_t ptr_val = guest_mem.read_u64(mem_offset + off);
            if (ptr_val >= virt_range_start && ptr_val < virt_range_end) {
                std::uint64_t slid_val = ptr_val + kaslr_slide;
                guest_mem.write_u64(mem_offset + off, slid_val);

                AppleRelocationRecord rec;
                rec.target_vmaddr = seg.vmaddr + off;
                rec.target_phys_addr = seg.mapped_address + off;
                rec.original_value = ptr_val;
                rec.slid_value = slid_val;
                rec.type = KernelRelocationType::Arm64RelocUnsigned;
                rec.segment_name = seg.name;
                result.relocation_records.push_back(rec);
                result.relocations_applied++;
            }
        }
    }

    // 6. Update Kernel Load Result
    if (!already_slid) {
        kernel_load.virt_kernel_base += kaslr_slide;
        kernel_load.virt_entry_point += kaslr_slide;
        kernel_load.phys_entry_point += kaslr_slide;
        kernel_load.top_of_kernel_data = (kernel_load.top_of_kernel_data + kaslr_slide + 16383ULL) & ~16383ULL;
    }

    result.slid_virt_base = kernel_load.virt_kernel_base;
    result.slid_phys_entry = kernel_load.phys_entry_point;
    result.slid_virt_entry = kernel_load.virt_entry_point;
    result.status = RelocationStatus::Success;
    return result;
}

bool AppleKernelRelocator::apply_chained_fixups(
    Memory& guest_mem,
    const std::uint8_t* fixup_data,
    std::size_t fixup_size,
    std::uint64_t kaslr_slide,
    AppleKernelLoadResult& kernel_load,
    AppleKernelRelocationResult& out_result) {
    if (!fixup_data || fixup_size < sizeof(DyldChainedFixupsHeader)) {
        out_result.status = RelocationStatus::TruncatedMetadata;
        out_result.error_message = "Chained fixups header smaller than minimum DyldChainedFixupsHeader";
        return false;
    }

    const auto* hdr = reinterpret_cast<const DyldChainedFixupsHeader*>(fixup_data);
    if (hdr->starts_offset >= fixup_size || hdr->starts_offset + sizeof(DyldChainedStartsInImage) > fixup_size) {
        out_result.status = RelocationStatus::TruncatedMetadata;
        out_result.error_message = "Chained starts offset out of bounds";
        return false;
    }

    const auto* starts_in_image = reinterpret_cast<const DyldChainedStartsInImage*>(fixup_data + hdr->starts_offset);
    for (std::uint32_t seg_idx = 0; seg_idx < starts_in_image->seg_count; ++seg_idx) {
        std::uint32_t seg_info_off = starts_in_image->seg_info_offset[seg_idx];
        if (seg_info_off == 0) continue; // No fixups for this segment
        if (hdr->starts_offset + seg_info_off + sizeof(DyldChainedStartsInSegment) > fixup_size) continue;

        const auto* seg_starts = reinterpret_cast<const DyldChainedStartsInSegment*>(
            fixup_data + hdr->starts_offset + seg_info_off);

        if (seg_starts->page_size != 0x4000) {
            // Apple Silicon requires 16 KB pages
            out_result.status = RelocationStatus::UnsupportedFixupFormat;
            out_result.error_message = "Unsupported chained fixup page size (expected 16 KB / 0x4000)";
            return false;
        }

        // Process each 16 KB page
        for (std::uint16_t page_idx = 0; page_idx < seg_starts->page_count; ++page_idx) {
            std::uint16_t start_off = seg_starts->page_start[page_idx];
            if (start_off == kDyldChainedPtrStartNone) continue;

            // Follow chain on this page
            std::uint64_t page_offset = static_cast<std::uint64_t>(page_idx) * 0x4000ULL;
            std::uint64_t chain_offset = page_offset + start_off;

            while (chain_offset < (page_offset + 0x4000ULL)) {
                std::uint64_t seg_phys = kernel_load.phys_load_base + seg_starts->segment_offset + chain_offset;
                if (seg_phys >= kernel_load.phys_load_base && (seg_phys - kernel_load.phys_load_base) + 8 <= guest_mem.size()) {
                    std::size_t mem_off = static_cast<std::size_t>(seg_phys - kernel_load.phys_load_base);
                    std::uint64_t raw_val = guest_mem.read_u64(mem_off);

                    // Rebase target address
                    std::uint64_t next_stride = (raw_val >> 51) & 0xFFFu;
                    std::uint64_t target_addr = raw_val & 0x0007FFFFFFFFFFFULL;
                    std::uint64_t slid_addr = target_addr + kaslr_slide;

                    std::uint64_t updated_raw = (raw_val & ~0x0007FFFFFFFFFFFULL) | (slid_addr & 0x0007FFFFFFFFFFFULL);
                    guest_mem.write_u64(mem_off, updated_raw);

                    AppleRelocationRecord rec;
                    rec.target_vmaddr = kernel_load.virt_kernel_base + seg_starts->segment_offset + chain_offset;
                    rec.target_phys_addr = seg_phys;
                    rec.original_value = target_addr;
                    rec.slid_value = slid_addr;
                    rec.type = KernelRelocationType::ChainedFixupRebase;
                    rec.segment_name = "Segment" + std::to_string(seg_idx);
                    out_result.relocation_records.push_back(rec);
                    out_result.relocations_applied++;

                    if (next_stride == 0) break;
                    chain_offset += next_stride * 4;
                } else {
                    break;
                }
            }
        }
    }

    out_result.status = RelocationStatus::Success;
    return true;
}

std::string AppleKernelRelocator::generate_relocation_report(
    const AppleKernelRelocationResult& result) {
    std::stringstream ss;
    ss << "# iPhone 15 Kernel Relocation and KASLR Compatibility Report\n\n";

    ss << "| Metric / Parameter | Value |\n";
    ss << "| :--- | :--- |\n";
    ss << "| **Relocation Status** | `" << status_to_string(result.status) << "` |\n";
    ss << "| **Applied KASLR Slide** | `0x" << std::hex << result.applied_slide << std::dec << "` ("
       << (result.applied_slide / (1024 * 1024)) << " MB) |\n";
    ss << "| **Original Virtual Base** | `0x" << std::hex << result.original_virt_base << std::dec << "` |\n";
    ss << "| **Slid Virtual Base** | `0x" << std::hex << result.slid_virt_base << std::dec << "` |\n";
    ss << "| **Original Physical Entry** | `0x" << std::hex << result.original_phys_entry << std::dec << "` |\n";
    ss << "| **Slid Physical Entry** | `0x" << std::hex << result.slid_phys_entry << std::dec << "` |\n";
    ss << "| **Original Virtual Entry** | `0x" << std::hex << result.original_virt_entry << std::dec << "` |\n";
    ss << "| **Slid Virtual Entry** | `0x" << std::hex << result.slid_virt_entry << std::dec << "` |\n";
    ss << "| **Relocations Applied** | " << result.relocations_applied << " pointer(s) |\n\n";

    if (!result.relocation_records.empty()) {
        ss << "### Sample Rebased Pointer Records\n\n";
        ss << "| Target VM Address | Physical Address | Original Pointer | Slid Pointer | Relocation Type | Segment |\n";
        ss << "| :--- | :--- | :--- | :--- | :--- | :--- |\n";
        std::size_t limit = std::min<std::size_t>(result.relocation_records.size(), 10);
        for (std::size_t i = 0; i < limit; ++i) {
            const auto& r = result.relocation_records[i];
            ss << "| `0x" << std::hex << r.target_vmaddr << "` | `0x" << r.target_phys_addr
               << "` | `0x" << r.original_value << "` | `0x" << r.slid_value << std::dec
               << "` | `" << reloc_type_to_string(r.type) << "` | `" << r.segment_name << "` |\n";
        }
        if (result.relocation_records.size() > limit) {
            ss << "\n*(Showing " << limit << " of " << result.relocation_records.size() << " rebased pointers)*\n";
        }
    }

    return ss.str();
}

} // namespace winios::loader
