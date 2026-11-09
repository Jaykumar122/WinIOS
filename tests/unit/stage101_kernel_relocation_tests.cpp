#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "loader/apple_kernel_loader.h"
#include "loader/apple_kernel_relocator.h"
#include "memory/apple_physical_memory_map.h"
#include "memory/memory.h"

using namespace winios::loader;
using namespace winios::memory;

#define STAGE101_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[STAGE 101 ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to create a synthetic kernelcache with pointers in __DATA
std::vector<std::uint8_t> create_synthetic_kernel_with_data_ptrs(
    std::uint64_t text_vmaddr = 0xFFFFFE0007004000ULL,
    std::uint64_t text_vmsize = 0x20000ULL,
    std::uint64_t data_vmsize = 0x10000ULL) {
    std::size_t text_fileoff = 0x4000;
    std::size_t data_fileoff = text_fileoff + 0x4000;
    std::size_t total_buf_size = data_fileoff + 0x4000;

    std::vector<std::uint8_t> buf(total_buf_size, 0);

    auto* hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->magic = kMachOMagic64;
    hdr->cputype = kCpuTypeARM64;
    hdr->cpusubtype = 2u; // ARM64e
    hdr->filetype = kMachOFileTypeFileset;
    hdr->ncmds = 3;
    hdr->flags = 0x00200000; // MH_PIE

    std::size_t cur_off = sizeof(MachOHeader64);

    // 1. __TEXT segment
    auto* seg_text = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + cur_off);
    seg_text->cmd = kLoadCmdSegment64;
    seg_text->cmdsize = sizeof(MachOSegmentCommand64);
    std::memcpy(seg_text->segname, "__TEXT", 6);
    seg_text->vmaddr = text_vmaddr;
    seg_text->vmsize = text_vmsize;
    seg_text->fileoff = text_fileoff;
    seg_text->filesize = 0x4000;
    seg_text->maxprot = kVmProtRead | kVmProtExecute;
    seg_text->initprot = kVmProtRead | kVmProtExecute;
    cur_off += sizeof(MachOSegmentCommand64);

    // 2. __DATA segment
    auto* seg_data = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + cur_off);
    seg_data->cmd = kLoadCmdSegment64;
    seg_data->cmdsize = sizeof(MachOSegmentCommand64);
    std::memcpy(seg_data->segname, "__DATA", 6);
    seg_data->vmaddr = text_vmaddr + text_vmsize;
    seg_data->vmsize = data_vmsize;
    seg_data->fileoff = data_fileoff;
    seg_data->filesize = 0x2000;
    seg_data->maxprot = kVmProtRead | kVmProtWrite;
    seg_data->initprot = kVmProtRead | kVmProtWrite;
    cur_off += sizeof(MachOSegmentCommand64);

    // 3. LC_UNIXTHREAD
    std::size_t thread_cmdsize = sizeof(MachOLoadCommand) + 8 + sizeof(MachOArmThreadState64);
    auto* th_cmd = reinterpret_cast<MachOLoadCommand*>(buf.data() + cur_off);
    th_cmd->cmd = kLoadCmdUnixThread;
    th_cmd->cmdsize = static_cast<std::uint32_t>(thread_cmdsize);
    *reinterpret_cast<std::uint32_t*>(buf.data() + cur_off + sizeof(MachOLoadCommand)) = kArmThreadState64;
    *reinterpret_cast<std::uint32_t*>(buf.data() + cur_off + sizeof(MachOLoadCommand) + 4) = kArmThreadState64Count;
    auto* state = reinterpret_cast<MachOArmThreadState64*>(buf.data() + cur_off + sizeof(MachOLoadCommand) + 8);
    state->pc = text_vmaddr + 0x40;
    cur_off += thread_cmdsize;

    hdr->sizeofcmds = static_cast<std::uint32_t>(cur_off - sizeof(MachOHeader64));

    // Fill pointers in __DATA segment
    // Pointer 1: points to text_vmaddr + 0x100
    std::uint64_t ptr1 = text_vmaddr + 0x100;
    std::memcpy(buf.data() + data_fileoff, &ptr1, 8);

    // Pointer 2: points to text_vmaddr + 0x200
    std::uint64_t ptr2 = text_vmaddr + 0x200;
    std::memcpy(buf.data() + data_fileoff + 8, &ptr2, 8);

    // Non-pointer data value
    std::uint64_t non_ptr = 0x1122334455667788ULL;
    std::memcpy(buf.data() + data_fileoff + 16, &non_ptr, 8);

    return buf;
}

} // namespace

// -----------------------------------------------------------------------------
// Test 1: KASLR Slide 16 KB Alignment Validation
// -----------------------------------------------------------------------------
static bool test_kaslr_slide_alignment_validation() {
    std::cout << "\n[1] test_kaslr_slide_alignment_validation...\n";

    std::string err;
    // 1. Aligned slide (0x4000 = 16 KB)
    auto res_aligned = AppleKernelRelocator::validate_slide(0x4000ULL, 0x100000ULL, err);
    STAGE101_ASSERT(res_aligned == RelocationStatus::Success, "16 KB aligned slide must be accepted");

    // 2. Aligned slide (0x400000 = 4 MB)
    auto res_4mb = AppleKernelRelocator::validate_slide(0x400000ULL, 0x100000ULL, err);
    STAGE101_ASSERT(res_4mb == RelocationStatus::Success, "4 MB aligned slide must be accepted");

    // 3. Misaligned slide (0x1000 = 4 KB, violates 16 KB Apple Silicon granule)
    auto res_misaligned = AppleKernelRelocator::validate_slide(0x1000ULL, 0x100000ULL, err);
    STAGE101_ASSERT(res_misaligned == RelocationStatus::InvalidSlideAlignment, "4 KB slide must be rejected");
    STAGE101_ASSERT(err.find("16 KB page granule") != std::string::npos, "Error must mention 16 KB granule");

    std::cout << "[PASS] KASLR Slide 16 KB Alignment Validation Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: KASLR Slide Overflow Protection
// -----------------------------------------------------------------------------
static bool test_kaslr_slide_overflow_protection() {
    std::cout << "\n[2] test_kaslr_slide_overflow_protection...\n";

    std::string err;
    // 1. Exactly 512 MB limit (0x20000000)
    auto res_512mb = AppleKernelRelocator::validate_slide(0x20000000ULL, 0x100000ULL, err);
    STAGE101_ASSERT(res_512mb == RelocationStatus::Success, "512 MB slide is supported limit");

    // 2. Exceeding 512 MB limit (0x20004000)
    auto res_overflow = AppleKernelRelocator::validate_slide(0x20004000ULL, 0x100000ULL, err);
    STAGE101_ASSERT(res_overflow == RelocationStatus::SlideOverflow, "Slide > 512 MB must be rejected");
    STAGE101_ASSERT(err.find("512 MB limit") != std::string::npos, "Error must mention 512 MB limit");

    // 3. Integer overflow
    auto res_huge = AppleKernelRelocator::validate_slide(0xFFFFFFFFFFFF0000ULL, 0x200000ULL, err);
    STAGE101_ASSERT(res_huge == RelocationStatus::SlideOverflow, "Huge slide must overflow");

    std::cout << "[PASS] KASLR Slide Overflow Protection Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Unslid Kernel Relocation (Slide = 0)
// -----------------------------------------------------------------------------
static bool test_unslid_kernel_rebasing() {
    std::cout << "\n[3] test_unslid_kernel_rebasing...\n";

    Memory guest_mem(256 * 1024 * 1024);
    ApplePhysicalMemoryMap pmap(0x40000000ULL, 256 * 1024 * 1024);

    auto kc_data = create_synthetic_kernel_with_data_ptrs();
    auto load_res = AppleKernelLoader::load_kernelcache(kc_data.data(), kc_data.size(), guest_mem, pmap);
    STAGE101_ASSERT(load_res.success, "Load kernelcache must succeed");

    auto reloc_res = AppleKernelRelocator::relocate_kernel(guest_mem, load_res, 0ULL, pmap);
    STAGE101_ASSERT(reloc_res.is_success(), "Unslid relocation must succeed");
    STAGE101_ASSERT(reloc_res.applied_slide == 0, "Applied slide must be 0");
    STAGE101_ASSERT(reloc_res.slid_virt_base == reloc_res.original_virt_base, "Base unchanged");
    STAGE101_ASSERT(reloc_res.slid_phys_entry == reloc_res.original_phys_entry, "Phys entry unchanged");
    STAGE101_ASSERT(reloc_res.relocations_applied == 0, "0 relocations applied for slide 0");

    std::cout << "[PASS] Unslid Kernel Relocation Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Dynamic Pointer Rebasing in __DATA Segments
// -----------------------------------------------------------------------------
static bool test_pointer_rebasing_in_data_segments() {
    std::cout << "\n[4] test_pointer_rebasing_in_data_segments...\n";

    Memory guest_mem(256 * 1024 * 1024);
    ApplePhysicalMemoryMap pmap(0x40000000ULL, 256 * 1024 * 1024);

    std::uint64_t text_base = 0xFFFFFE0007004000ULL;
    auto kc_data = create_synthetic_kernel_with_data_ptrs(text_base);
    auto load_res = AppleKernelLoader::load_kernelcache(kc_data.data(), kc_data.size(), guest_mem, pmap);
    STAGE101_ASSERT(load_res.success, "Load kernelcache");

    // Apply slide = 0x00400000 (4 MB)
    std::uint64_t slide = 0x00400000ULL;
    auto reloc_res = AppleKernelRelocator::relocate_kernel(guest_mem, load_res, slide, pmap);
    STAGE101_ASSERT(reloc_res.is_success(), "Relocation must succeed");
    STAGE101_ASSERT(reloc_res.relocations_applied >= 2, "Must rebase at least 2 pointers");

    // Verify pointer 1: original was text_base + 0x100, slid must be text_base + 0x100 + slide
    const auto* data_seg = load_res.macho_result.find_segment("__DATA");
    STAGE101_ASSERT(data_seg != nullptr, "__DATA must exist");

    std::size_t mem_offset = 0;
    STAGE101_ASSERT(pmap.translate_ram_address(data_seg->mapped_address, 8, mem_offset), "Translate __DATA");

    std::uint64_t p1 = guest_mem.read_u64(mem_offset);
    STAGE101_ASSERT(p1 == (text_base + 0x100 + slide), "Pointer 1 rebased to slid virtual address");

    std::uint64_t p2 = guest_mem.read_u64(mem_offset + 8);
    STAGE101_ASSERT(p2 == (text_base + 0x200 + slide), "Pointer 2 rebased to slid virtual address");

    // Verify non-pointer value unchanged
    std::uint64_t non_ptr = guest_mem.read_u64(mem_offset + 16);
    STAGE101_ASSERT(non_ptr == 0x1122334455667788ULL, "Non-pointer value must remain untouched");

    std::cout << "[PASS] Dynamic Pointer Rebasing in __DATA Verified (2 pointers rebased)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Physical DRAM Bounds Enforcement
// -----------------------------------------------------------------------------
static bool test_physical_dram_bounds_enforcement() {
    std::cout << "\n[5] test_physical_dram_bounds_enforcement...\n";

    // Small RAM of 256 MB (0x10000000 bytes) starting at 0x40000000 (RAM end = 0x50000000)
    Memory guest_mem(256 * 1024 * 1024);
    ApplePhysicalMemoryMap pmap(0x40000000ULL, 256 * 1024 * 1024);

    auto kc_data = create_synthetic_kernel_with_data_ptrs();
    auto load_res = AppleKernelLoader::load_kernelcache(kc_data.data(), kc_data.size(), guest_mem, pmap);
    STAGE101_ASSERT(load_res.success, "Load");

    // Slide of 256 MB (0x10000000) pushes kernel past RAM end 0x50000000
    std::uint64_t slide = 0x10000000ULL;
    auto reloc_res = AppleKernelRelocator::relocate_kernel(guest_mem, load_res, slide, pmap);
    STAGE101_ASSERT(!reloc_res.is_success(), "Out-of-bounds slide must be rejected");
    STAGE101_ASSERT(reloc_res.status == RelocationStatus::RelocationBoundsExceeded,
                   "Status must be RelocationBoundsExceeded");

    std::cout << "[PASS] Physical DRAM Bounds Enforcement Verified: " << reloc_res.error_message << "\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Collision Detection with Reserved Memory Regions
// -----------------------------------------------------------------------------
static bool test_collision_detection_with_reserved_memory() {
    std::cout << "\n[6] test_collision_detection_with_reserved_memory...\n";

    Memory guest_mem(6ULL * 1024 * 1024 * 1024); // 6 GB
    ApplePhysicalMemoryMap pmap(0x40000000ULL, 6ULL * 1024 * 1024 * 1024);

    auto kc_data = create_synthetic_kernel_with_data_ptrs();
    auto load_res = AppleKernelLoader::load_kernelcache(kc_data.data(), kc_data.size(), guest_mem, pmap);
    STAGE101_ASSERT(load_res.success, "Load");

    // In default layout, DeviceTree is at 0x48000000 (base + 0x08000000).
    // If we apply slide = 0x08000000, kernel will collide with DeviceTree staging area!
    std::uint64_t dt_collision_slide = 0x08000000ULL;
    auto reloc_res = AppleKernelRelocator::relocate_kernel(guest_mem, load_res, dt_collision_slide, pmap);
    STAGE101_ASSERT(!reloc_res.is_success(), "Collision with DeviceTree must be detected");
    STAGE101_ASSERT(reloc_res.status == RelocationStatus::CollisionDetected, "Status must be CollisionDetected");
    STAGE101_ASSERT(reloc_res.error_message.find("DeviceTree") != std::string::npos,
                   "Error must name DeviceTree collision");

    std::cout << "[PASS] Collision Detection with Reserved Memory Verified: " << reloc_res.error_message << "\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Chained Fixup Chain Processing
// -----------------------------------------------------------------------------
static bool test_chained_fixup_parsing() {
    std::cout << "\n[7] test_chained_fixup_parsing...\n";

    Memory guest_mem(256 * 1024 * 1024);
    ApplePhysicalMemoryMap pmap(0x40000000ULL, 256 * 1024 * 1024);

    auto kc_data = create_synthetic_kernel_with_data_ptrs();
    auto load_res = AppleKernelLoader::load_kernelcache(kc_data.data(), kc_data.size(), guest_mem, pmap);
    STAGE101_ASSERT(load_res.success, "Load");

    // Synthesize a valid DyldChainedFixups buffer
    std::vector<std::uint8_t> fixup_buf(512, 0);
    auto* fix_hdr = reinterpret_cast<DyldChainedFixupsHeader*>(fixup_buf.data());
    fix_hdr->fixups_version = 0;
    fix_hdr->starts_offset = sizeof(DyldChainedFixupsHeader);

    auto* starts_img = reinterpret_cast<DyldChainedStartsInImage*>(fixup_buf.data() + fix_hdr->starts_offset);
    starts_img->seg_count = 1;
    starts_img->seg_info_offset[0] = sizeof(DyldChainedStartsInImage);

    auto* seg_starts = reinterpret_cast<DyldChainedStartsInSegment*>(
        fixup_buf.data() + fix_hdr->starts_offset + sizeof(DyldChainedStartsInImage));
    seg_starts->size = sizeof(DyldChainedStartsInSegment);
    seg_starts->page_size = 0x4000; // 16 KB pages
    seg_starts->pointer_format = 1; // ARM64E
    seg_starts->segment_offset = 0x4000;
    seg_starts->page_count = 1;
    seg_starts->page_start[0] = 0x20; // Chain starts at offset 0x20 on page 0

    // Set up chain pointer in guest memory at segment_offset + page_start[0]
    std::size_t mem_off = 0x4020;
    // Chain entry: next=0 (end of chain), target = 0xFFFFFE0007005000
    std::uint64_t chain_val = 0xFFFFFE0007005000ULL & 0x0007FFFFFFFFFFFULL;
    guest_mem.write_u64(mem_off, chain_val);

    AppleKernelRelocationResult out_res;
    bool ok = AppleKernelRelocator::apply_chained_fixups(
        guest_mem,
        fixup_buf.data(),
        fixup_buf.size(),
        0x40000ULL, // 256 KB slide
        load_res,
        out_res);

    STAGE101_ASSERT(ok, "Chained fixup application must succeed");
    STAGE101_ASSERT(out_res.relocations_applied == 1, "Exactly 1 chain entry rebased");

    std::uint64_t updated = guest_mem.read_u64(mem_off) & 0x0007FFFFFFFFFFFULL;
    std::uint64_t expected = (0xFFFFFE0007005000ULL + 0x40000ULL) & 0x0007FFFFFFFFFFFULL;
    STAGE101_ASSERT(updated == expected, "Chain pointer slid accurately");

    std::cout << "[PASS] Chained Fixup Chain Processing Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Relocation Markdown Report Generation
// -----------------------------------------------------------------------------
static bool test_relocation_report_generation() {
    std::cout << "\n[8] test_relocation_report_generation...\n";

    Memory guest_mem(256 * 1024 * 1024);
    ApplePhysicalMemoryMap pmap(0x40000000ULL, 256 * 1024 * 1024);

    auto kc_data = create_synthetic_kernel_with_data_ptrs();
    auto load_res = AppleKernelLoader::load_kernelcache(kc_data.data(), kc_data.size(), guest_mem, pmap);
    auto reloc_res = AppleKernelRelocator::relocate_kernel(guest_mem, load_res, 0x00400000ULL, pmap);

    std::string report = AppleKernelRelocator::generate_relocation_report(reloc_res);
    STAGE101_ASSERT(!report.empty(), "Report must not be empty");
    STAGE101_ASSERT(report.find("# iPhone 15 Kernel Relocation and KASLR Compatibility Report") != std::string::npos,
                   "Header must be present");
    STAGE101_ASSERT(report.find("0x400000") != std::string::npos, "Slide must be reported");
    STAGE101_ASSERT(report.find("ARM64_RELOC_UNSIGNED") != std::string::npos, "Relocation type must be reported");

    std::cout << "[PASS] Relocation Markdown Report Generation\n";
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "Running Stage 101: Kernel Relocation & KASLR Tests\n";
    std::cout << "========================================================\n";

    if (!test_kaslr_slide_alignment_validation()) return 1;
    if (!test_kaslr_slide_overflow_protection()) return 1;
    if (!test_unslid_kernel_rebasing()) return 1;
    if (!test_pointer_rebasing_in_data_segments()) return 1;
    if (!test_physical_dram_bounds_enforcement()) return 1;
    if (!test_collision_detection_with_reserved_memory()) return 1;
    if (!test_chained_fixup_parsing()) return 1;
    if (!test_relocation_report_generation()) return 1;

    std::cout << "\nALL 8 STAGE 101 KERNEL RELOCATION SUITES PASSED!\n";
    return 0;
}
