#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "loader/apple_kernel_loader.h"
#include "loader/macho_loader.h"
#include "memory/apple_physical_memory_map.h"
#include "memory/memory.h"

using namespace winios::loader;
using namespace winios::memory;

#define STAGE100_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[STAGE 100 ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to create a multi-segment Mach-O 64-bit fileset kernelcache
std::vector<std::uint8_t> create_synthetic_kernelcache_with_fileset(
    std::uint64_t text_vmaddr = 0xFFFFFE0007004000ULL,
    std::uint64_t text_vmsize = 0x20000ULL, // 128 KB
    std::uint64_t data_vmsize = 0x10000ULL  // 64 KB
) {
    // Layout:
    // Header (32 bytes)
    // LC_SEGMENT_64 "__TEXT" (72 bytes)
    // LC_SEGMENT_64 "__DATA" (72 bytes)
    // LC_UNIXTHREAD (184 bytes)
    // LC_FILESET_ENTRY "com.apple.kernel" (32 bytes + string)
    // LC_FILESET_ENTRY "com.apple.driver.AppleARMPE" (32 bytes + string)
    // Segment data payload

    std::size_t kext1_str_len = 17; // "com.apple.kernel\0"
    std::size_t kext2_str_len = 28; // "com.apple.driver.AppleARMPE\0"

    std::size_t text_fileoff = 0x4000;
    std::size_t data_fileoff = text_fileoff + 0x4000;
    std::size_t total_buf_size = data_fileoff + 0x4000;

    std::vector<std::uint8_t> buf(total_buf_size, 0);

    auto* hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->magic = kMachOMagic64;
    hdr->cputype = kCpuTypeARM64;
    hdr->cpusubtype = 2u; // ARM64e
    hdr->filetype = kMachOFileTypeFileset;
    hdr->ncmds = 5;
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
    seg_data->filesize = 0x2000; // filesize < vmsize (BSS padding)
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
    state->pc = text_vmaddr + 0x40; // Entry point 0x40 into __TEXT
    cur_off += thread_cmdsize;

    // 4. LC_FILESET_ENTRY kext1
    std::size_t fs1_cmdsize = (sizeof(MachOFilesetEntryCommand) + kext1_str_len + 7) & ~7;
    auto* fs1 = reinterpret_cast<MachOFilesetEntryCommand*>(buf.data() + cur_off);
    fs1->cmd = kLoadCmdFilesetEntry;
    fs1->cmdsize = static_cast<std::uint32_t>(fs1_cmdsize);
    fs1->vmaddr = text_vmaddr;
    fs1->fileoff = text_fileoff;
    fs1->entry_id_offset = sizeof(MachOFilesetEntryCommand);
    std::memcpy(buf.data() + cur_off + sizeof(MachOFilesetEntryCommand), "com.apple.kernel", 16);
    cur_off += fs1_cmdsize;

    // 5. LC_FILESET_ENTRY kext2
    std::size_t fs2_cmdsize = (sizeof(MachOFilesetEntryCommand) + kext2_str_len + 7) & ~7;
    auto* fs2 = reinterpret_cast<MachOFilesetEntryCommand*>(buf.data() + cur_off);
    fs2->cmd = kLoadCmdFilesetEntry;
    fs2->cmdsize = static_cast<std::uint32_t>(fs2_cmdsize);
    fs2->vmaddr = text_vmaddr + 0x8000;
    fs2->fileoff = text_fileoff + 0x8000;
    fs2->entry_id_offset = sizeof(MachOFilesetEntryCommand);
    std::memcpy(buf.data() + cur_off + sizeof(MachOFilesetEntryCommand), "com.apple.driver.AppleARMPE", 27);
    cur_off += fs2_cmdsize;

    hdr->sizeofcmds = static_cast<std::uint32_t>(cur_off - sizeof(MachOHeader64));

    // Fill dummy instructions at entry point: 0xD503201F (NOP) and 0xD65F03C0 (RET)
    std::uint32_t nop = 0xD503201Fu;
    std::uint32_t ret = 0xD65F03C0u;
    std::memcpy(buf.data() + text_fileoff + 0x40, &nop, 4);
    std::memcpy(buf.data() + text_fileoff + 0x44, &ret, 4);

    // Fill data pattern in __DATA
    std::uint32_t data_val = 0xA16B101C;
    std::memcpy(buf.data() + data_fileoff, &data_val, 4);

    return buf;
}

} // namespace

// -----------------------------------------------------------------------------
// Test 1: Valid 64-bit Kernelcache Staging
// -----------------------------------------------------------------------------
static bool test_valid_kernelcache_loading() {
    std::cout << "\n[1] test_valid_kernelcache_loading...\n";

    Memory guest_mem(256 * 1024 * 1024); // 256 MB RAM
    ApplePhysicalMemoryMap pmap(0x40000000ULL, 256 * 1024 * 1024);

    auto kc_data = create_synthetic_kernelcache_with_fileset();
    auto res = AppleKernelLoader::load_kernelcache(kc_data.data(), kc_data.size(), guest_mem, pmap);

    STAGE100_ASSERT(res.success, ("Kernelcache load must succeed: " + res.error_message).c_str());
    STAGE100_ASSERT(res.segment_count >= 2, "Must load at least 2 segments");
    STAGE100_ASSERT(res.phys_load_base == 0x40000000ULL, "Physical base must be 0x40000000");

    // Verify code was written to guest RAM
    std::size_t code_offset = 0;
    STAGE100_ASSERT(pmap.translate_ram_address(res.phys_entry_point, 4, code_offset), "Translate entry point");
    std::uint32_t first_instr = guest_mem.read_u32(code_offset);
    STAGE100_ASSERT(first_instr == 0xD503201Fu, "Instruction at entry point must be NOP (0xD503201F)");

    std::cout << "[PASS] Valid 64-bit Kernelcache Staged into Guest RAM\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Physical and Virtual Entry Point Calculation
// -----------------------------------------------------------------------------
static bool test_entry_point_calculation() {
    std::cout << "\n[2] test_entry_point_calculation...\n";

    Memory guest_mem(256 * 1024 * 1024);
    ApplePhysicalMemoryMap pmap(0x40000000ULL, 256 * 1024 * 1024);

    auto kc_data = create_synthetic_kernelcache_with_fileset(0xFFFFFE0007004000ULL);
    auto res = AppleKernelLoader::load_kernelcache(kc_data.data(), kc_data.size(), guest_mem, pmap);

    STAGE100_ASSERT(res.virt_entry_point == 0xFFFFFE0007004040ULL, "Virtual entry point must be 0xFFFFFE0007004040");
    STAGE100_ASSERT(res.phys_entry_point == 0x40000040ULL, "Physical entry point must be 0x40000040");
    STAGE100_ASSERT(res.virt_kernel_base == 0xFFFFFE0007004000ULL, "Virtual base must match lowest vmaddr");

    std::cout << "[PASS] Physical (0x" << std::hex << res.phys_entry_point
              << ") and Virtual (0x" << res.virt_entry_point << std::dec << ") Entry Points Calculated\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Top of Kernel Data Calculation (16 KB Page Aligned)
// -----------------------------------------------------------------------------
static bool test_top_of_kernel_data_calculation() {
    std::cout << "\n[3] test_top_of_kernel_data_calculation...\n";

    Memory guest_mem(256 * 1024 * 1024);
    ApplePhysicalMemoryMap pmap(0x40000000ULL, 256 * 1024 * 1024);

    auto kc_data = create_synthetic_kernelcache_with_fileset();
    auto res = AppleKernelLoader::load_kernelcache(kc_data.data(), kc_data.size(), guest_mem, pmap);

    STAGE100_ASSERT(res.top_of_kernel_data > res.phys_load_base, "Top of kernel data must exceed base");
    STAGE100_ASSERT(res.top_of_kernel_data % 16384 == 0, "Top of kernel data must be 16 KB page-aligned");
    STAGE100_ASSERT(res.top_of_kernel_data >= res.phys_load_base + res.effective_kernel_size,
                    "Top of kernel data must encompass entire kernel footprint");

    std::cout << "[PASS] Top of Kernel Data (0x" << std::hex << res.top_of_kernel_data
              << std::dec << ") 16 KB Aligned\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Fileset Entry (Embedded KEXT) Parsing
// -----------------------------------------------------------------------------
static bool test_fileset_entry_parsing() {
    std::cout << "\n[4] test_fileset_entry_parsing...\n";

    Memory guest_mem(256 * 1024 * 1024);
    ApplePhysicalMemoryMap pmap(0x40000000ULL, 256 * 1024 * 1024);

    auto kc_data = create_synthetic_kernelcache_with_fileset();
    auto res = AppleKernelLoader::load_kernelcache(kc_data.data(), kc_data.size(), guest_mem, pmap);

    STAGE100_ASSERT(res.kext_count == 2, "Must discover exactly 2 fileset kext entries");
    STAGE100_ASSERT(res.macho_result.find_fileset_entry("com.apple.kernel") != nullptr,
                    "Must locate com.apple.kernel entry");
    STAGE100_ASSERT(res.macho_result.find_fileset_entry("com.apple.driver.AppleARMPE") != nullptr,
                    "Must locate com.apple.driver.AppleARMPE entry");

    std::cout << "[PASS] Fileset Entry (Embedded KEXT) Parsing Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Zero-Fill BSS Expansion
// -----------------------------------------------------------------------------
static bool test_zero_fill_bss_expansion() {
    std::cout << "\n[5] test_zero_fill_bss_expansion...\n";

    Memory guest_mem(256 * 1024 * 1024);
    // Fill memory with dummy non-zero pattern 0xFF
    for (std::size_t i = 0; i < 256 * 1024; ++i) {
        guest_mem.write_u8(i, 0xFF);
    }

    ApplePhysicalMemoryMap pmap(0x40000000ULL, 256 * 1024 * 1024);
    auto kc_data = create_synthetic_kernelcache_with_fileset();
    auto res = AppleKernelLoader::load_kernelcache(kc_data.data(), kc_data.size(), guest_mem, pmap);

    STAGE100_ASSERT(res.success, "Load must succeed");

    // In __DATA segment: filesize was 0x2000, vmsize was 0x10000.
    // The BSS region from offset 0x2000 to 0x10000 must be zeroed!
    const auto* data_seg = res.macho_result.find_segment("__DATA");
    STAGE100_ASSERT(data_seg != nullptr, "__DATA segment must be found");

    std::size_t bss_phys = data_seg->mapped_address + data_seg->filesize + 0x100; // Inside BSS
    std::size_t offset = 0;
    STAGE100_ASSERT(pmap.translate_ram_address(bss_phys, 4, offset), "Translate BSS phys");
    std::uint32_t val = guest_mem.read_u32(offset);
    STAGE100_ASSERT(val == 0, "BSS region must be zero-filled");

    std::cout << "[PASS] Zero-Fill BSS Expansion Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Admission Rejection Before Staging
// -----------------------------------------------------------------------------
static bool test_admission_rejection_before_load() {
    std::cout << "\n[6] test_admission_rejection_before_load...\n";

    Memory guest_mem(256 * 1024 * 1024);
    ApplePhysicalMemoryMap pmap(0x40000000ULL, 256 * 1024 * 1024);

    // Pass encrypted IM4P container
    std::vector<std::uint8_t> enc_buf(256, 0);
    enc_buf[0] = 'I'; enc_buf[1] = 'M'; enc_buf[2] = '4'; enc_buf[3] = 'P';
    enc_buf[32] = 'K'; enc_buf[33] = 'B'; enc_buf[34] = 'A'; enc_buf[35] = 'G';

    auto res = AppleKernelLoader::load_kernelcache(enc_buf.data(), enc_buf.size(), guest_mem, pmap);
    STAGE100_ASSERT(!res.success, "Encrypted Im4p must be rejected before memory staging");
    STAGE100_ASSERT(res.error_message.find("rejected") != std::string::npos ||
                   res.error_message.find("Admission") != std::string::npos,
                   "Error must detail admission rejection");

    std::cout << "[PASS] Admission Rejection Before Staging Intercepted: " << res.error_message << "\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: KASLR Slide Support
// -----------------------------------------------------------------------------
static bool test_kaslr_slide_support() {
    std::cout << "\n[7] test_kaslr_slide_support...\n";

    Memory guest_mem(256 * 1024 * 1024);
    ApplePhysicalMemoryMap pmap(0x40000000ULL, 256 * 1024 * 1024);

    std::uint64_t slide = 0x00400000ULL; // 4 MB KASLR slide
    auto kc_data = create_synthetic_kernelcache_with_fileset(0xFFFFFE0007004000ULL);
    auto res = AppleKernelLoader::load_kernelcache(kc_data.data(), kc_data.size(), guest_mem, pmap, slide);

    STAGE100_ASSERT(res.success, "Load with KASLR slide must succeed");
    STAGE100_ASSERT(res.virt_kernel_base == 0xFFFFFE0007004000ULL + slide, "Slid virtual base");
    STAGE100_ASSERT(res.virt_entry_point == 0xFFFFFE0007004040ULL + slide, "Slid virtual entry point");
    STAGE100_ASSERT(res.phys_entry_point == 0x40000040ULL + slide, "Physical entry point shifted by KASLR slide");

    std::cout << "[PASS] KASLR Slide (0x" << std::hex << slide << std::dec << ") Applied Correctly\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Kernel Staging Markdown Report Generation
// -----------------------------------------------------------------------------
static bool test_kernel_report_generation() {
    std::cout << "\n[8] test_kernel_report_generation...\n";

    Memory guest_mem(256 * 1024 * 1024);
    ApplePhysicalMemoryMap pmap(0x40000000ULL, 256 * 1024 * 1024);

    auto kc_data = create_synthetic_kernelcache_with_fileset();
    auto res = AppleKernelLoader::load_kernelcache(kc_data.data(), kc_data.size(), guest_mem, pmap);
    std::string report = AppleKernelLoader::generate_kernel_report(res);

    STAGE100_ASSERT(!report.empty(), "Report must not be empty");
    STAGE100_ASSERT(report.find("# iPhone 15 Native iOS Kernelcache Staging Report") != std::string::npos,
                   "Header must be present");
    STAGE100_ASSERT(report.find("__TEXT") != std::string::npos, "Segment table must contain __TEXT");
    STAGE100_ASSERT(report.find("com.apple.kernel") != std::string::npos, "Fileset table must contain com.apple.kernel");
    STAGE100_ASSERT(report.find("0x40000040") != std::string::npos, "Physical entry point must be in report");

    std::cout << "[PASS] Kernel Staging Markdown Report Generation\n";
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "Running Stage 100: Kernel Image Loader Tests\n";
    std::cout << "========================================================\n";

    if (!test_valid_kernelcache_loading()) return 1;
    if (!test_entry_point_calculation()) return 1;
    if (!test_top_of_kernel_data_calculation()) return 1;
    if (!test_fileset_entry_parsing()) return 1;
    if (!test_zero_fill_bss_expansion()) return 1;
    if (!test_admission_rejection_before_load()) return 1;
    if (!test_kaslr_slide_support()) return 1;
    if (!test_kernel_report_generation()) return 1;

    std::cout << "\nALL 8 STAGE 100 KERNEL IMAGE LOADER SUITES PASSED!\n";
    return 0;
}
