#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "loader/guest_memory_map.h"
#include "loader/macho_loader.h"
#include "memory/memory.h"

#define MACHO_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::loader;

namespace {

// Helper to construct a synthetic 64-bit ARM64 Mach-O buffer
std::vector<std::uint8_t> create_synthetic_macho_arm64(
    std::uint64_t text_vmaddr = 0xFFFFFFF007004000ULL,
    std::uint64_t text_vmsize = 0x8000,
    std::uint64_t text_filesize = 0x4000,
    std::uint64_t entry_pc = 0xFFFFFFF007004100ULL) {

    std::vector<std::uint8_t> buf;
    buf.resize(0x10000, 0);

    auto* hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->magic = kMachOMagic64;
    hdr->cputype = kCpuTypeARM64;
    hdr->cpusubtype = kCpuSubtypeARM64All;
    hdr->filetype = kMachOFileTypeExecute;
    hdr->ncmds = 2; // LC_SEGMENT_64 + LC_UNIXTHREAD
    hdr->sizeofcmds = sizeof(MachOSegmentCommand64) + sizeof(MachOLoadCommand) + 8 + sizeof(MachOArmThreadState64);
    hdr->flags = 0;
    hdr->reserved = 0;

    std::size_t offset = sizeof(MachOHeader64);

    // 1. LC_SEGMENT_64 (__TEXT)
    auto* seg = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + offset);
    seg->cmd = kLoadCmdSegment64;
    seg->cmdsize = sizeof(MachOSegmentCommand64);
    std::memcpy(seg->segname, "__TEXT", 6);
    seg->vmaddr = text_vmaddr;
    seg->vmsize = text_vmsize;
    seg->fileoff = 0x1000;
    seg->filesize = text_filesize;
    seg->maxprot = kVmProtRead | kVmProtExecute;
    seg->initprot = kVmProtRead | kVmProtExecute;
    seg->nsects = 0;
    seg->flags = 0;

    offset += sizeof(MachOSegmentCommand64);

    // Write some code payload into __TEXT at fileoff 0x1000
    // 0xD503201F = NOP
    for (std::size_t i = 0; i < text_filesize; i += 4) {
        *reinterpret_cast<std::uint32_t*>(buf.data() + 0x1000 + i) = 0xD503201Fu;
    }
    // Write distinct opcode at entry point offset (0x100 within segment)
    *reinterpret_cast<std::uint32_t*>(buf.data() + 0x1000 + 0x100) = 0x14000010u; // B +64

    // 2. LC_UNIXTHREAD
    auto* th_cmd = reinterpret_cast<MachOLoadCommand*>(buf.data() + offset);
    th_cmd->cmd = kLoadCmdUnixThread;
    th_cmd->cmdsize = sizeof(MachOLoadCommand) + 8 + sizeof(MachOArmThreadState64);

    offset += sizeof(MachOLoadCommand);
    *reinterpret_cast<std::uint32_t*>(buf.data() + offset) = kArmThreadState64;
    *reinterpret_cast<std::uint32_t*>(buf.data() + offset + 4) = sizeof(MachOArmThreadState64) / 4;
    offset += 8;

    auto* ts = reinterpret_cast<MachOArmThreadState64*>(buf.data() + offset);
    ts->pc = entry_pc;
    ts->sp = 0xFFFFFFF00700B000ULL;
    ts->lr = 0;
    ts->cpsr = 0x60000004u; // EL1t, NZCV flags

    return buf;
}

// -----------------------------------------------------------------------------
// Suite 1: Invalid and Truncated Magic Handling
// -----------------------------------------------------------------------------
bool test_invalid_and_truncated_magic() {
    std::cout << "[1] test_invalid_and_truncated_magic...\n";

    Memory mem(0x100000);
    MachOLoadResult res;

    // Empty buffer
    res = MachOLoader::load_from_buffer(nullptr, 0, mem, 0, 0x100000);
    MACHO_ASSERT(!res.is_success(), "Empty buffer fails");
    MACHO_ASSERT(res.status == MachOLoadStatus::InvalidMagic, "Status is InvalidMagic");

    // Random non-Mach-O bytes
    std::uint8_t garbage[16] = {0x7F, 'E', 'L', 'F', 0, 0, 0, 0};
    res = MachOLoader::load_from_buffer(garbage, sizeof(garbage), mem, 0, 0x100000);
    MACHO_ASSERT(!res.is_success(), "ELF header rejected as Mach-O");

    // 32-bit Mach-O rejected
    std::uint32_t magic32 = kMachOMagic32;
    res = MachOLoader::load_from_buffer(&magic32, sizeof(magic32), mem, 0, 0x100000);
    MACHO_ASSERT(!res.is_success(), "32-bit Mach-O rejected");

    std::cout << "[PASS] Invalid and Truncated Magic Handling\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Unsupported CPU Type Rejection
// -----------------------------------------------------------------------------
bool test_unsupported_cpu_type() {
    std::cout << "[2] test_unsupported_cpu_type...\n";

    auto buf = create_synthetic_macho_arm64();
    auto* hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->cputype = 0x01000007u; // CPU_TYPE_X86_64

    Memory mem(0x100000);
    auto res = MachOLoader::load_from_buffer(buf.data(), buf.size(), mem, 0, 0x100000);
    MACHO_ASSERT(!res.is_success(), "x86_64 Mach-O rejected");
    MACHO_ASSERT(res.status == MachOLoadStatus::UnsupportedCpuType, "Status is UnsupportedCpuType");

    std::cout << "[PASS] Unsupported CPU Type Rejection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Thin ARM64 Header and Segment Metadata Parsing
// -----------------------------------------------------------------------------
bool test_thin_arm64_header_parsing() {
    std::cout << "[3] test_thin_arm64_header_parsing...\n";

    auto buf = create_synthetic_macho_arm64();
    MachOLoadResult res;

    bool ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    MACHO_ASSERT(ok, "Metadata parsing succeeded");
    MACHO_ASSERT(res.header.magic == kMachOMagic64, "Magic is MH_MAGIC_64");
    MACHO_ASSERT(res.header.cputype == kCpuTypeARM64, "CPU is ARM64");
    MACHO_ASSERT(res.segments.size() == 1, "Parsed 1 segment");
    MACHO_ASSERT(res.segments[0].name == "__TEXT", "Segment name is __TEXT");
    MACHO_ASSERT(res.segments[0].vmaddr == 0xFFFFFFF007004000ULL, "VM addr matches");
    MACHO_ASSERT(res.segments[0].vmsize == 0x8000, "VM size matches");

    std::cout << "[PASS] Thin ARM64 Header and Segment Metadata Parsing\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Universal Fat Binary Slice Extraction
// -----------------------------------------------------------------------------
bool test_universal_fat_binary_slice_extraction() {
    std::cout << "[4] test_universal_fat_binary_slice_extraction...\n";

    auto arm64_slice = create_synthetic_macho_arm64();

    // Construct fat binary with 2 slices: x86_64 at 0x1000, ARM64 at 0x20000
    std::vector<std::uint8_t> fat_buf;
    fat_buf.resize(0x40000, 0);

    auto* fhdr = reinterpret_cast<MachOFatHeader*>(fat_buf.data());
    // Fat headers are big-endian in standard Apple specs
    fhdr->magic = 0xCAFEBABEu;
    fhdr->nfat_arch = 0x02000000u; // swap32(2)

    auto* archs = reinterpret_cast<MachOFatArch*>(fat_buf.data() + sizeof(MachOFatHeader));
    // Slice 0: x86_64
    archs[0].cputype = 0x07000001u;    // swap32(0x01000007)
    archs[0].cpusubtype = 0x03000000u; // swap32(3)
    archs[0].offset = 0x00100000u;     // swap32(0x1000)
    archs[0].size = 0x00800000u;       // swap32(0x8000)
    archs[0].align = 0x0C000000u;      // swap32(12)

    // Slice 1: ARM64
    archs[1].cputype = 0x0C000001u;    // swap32(0x0100000C)
    archs[1].cpusubtype = 0x00000000u; // swap32(0)
    archs[1].offset = 0x00000200u;     // swap32(0x20000)
    archs[1].size = 0x00000100u;       // swap32(0x10000)
    archs[1].align = 0x0C000000u;

    // Copy ARM64 slice into fat buffer at offset 0x20000
    std::memcpy(fat_buf.data() + 0x20000, arm64_slice.data(), arm64_slice.size());

    std::size_t slice_off = 0;
    std::size_t slice_sz = 0;
    bool located = MachOLoader::locate_arm64_slice(fat_buf.data(), fat_buf.size(), slice_off, slice_sz);
    MACHO_ASSERT(located, "ARM64 slice located in fat binary");
    MACHO_ASSERT(slice_off == 0x20000, "Slice offset is 0x20000");
    MACHO_ASSERT(slice_sz == 0x10000, "Slice size is 0x10000");

    MachOLoadResult res;
    bool ok = MachOLoader::parse_metadata(fat_buf.data(), fat_buf.size(), res);
    MACHO_ASSERT(ok, "Parse metadata directly from fat binary succeeds");
    MACHO_ASSERT(res.header.cputype == kCpuTypeARM64, "Extracted slice CPU is ARM64");

    std::cout << "[PASS] Universal Fat Binary Slice Extraction\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: LC_UNIXTHREAD Entrypoint and Thread State Discovery
// -----------------------------------------------------------------------------
bool test_unixthread_entry_point_discovery() {
    std::cout << "[5] test_unixthread_entry_point_discovery...\n";

    const std::uint64_t target_pc = 0xFFFFFFF007004180ULL;
    auto buf = create_synthetic_macho_arm64(0xFFFFFFF007004000ULL, 0x8000, 0x4000, target_pc);

    MachOLoadResult res;
    bool ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    MACHO_ASSERT(ok, "Metadata parsed");
    MACHO_ASSERT(res.entry_point == target_pc, "Entry point PC matches LC_UNIXTHREAD");
    MACHO_ASSERT(res.initial_sp == 0xFFFFFFF00700B000ULL, "Initial SP matches thread state");
    MACHO_ASSERT(res.initial_cpsr == 0x60000004u, "Initial CPSR matches thread state");

    std::cout << "[PASS] LC_UNIXTHREAD Entrypoint and Thread State Discovery\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: In-Memory Segment Loading and BSS Zero-Fill
// -----------------------------------------------------------------------------
bool test_in_memory_segment_loading_and_bss_clearing() {
    std::cout << "[6] test_in_memory_segment_loading_and_bss_clearing...\n";

    // Text segment: vmsize 0x8000 (32 KB), filesize 0x4000 (16 KB) -> 16 KB BSS
    auto buf = create_synthetic_macho_arm64(0xFFFFFFF007004000ULL, 0x8000, 0x4000, 0xFFFFFFF007004100ULL);

    Memory mem(0x100000); // 1 MB RAM
    // Map with physical base = 0x10000
    auto res = MachOLoader::load_from_buffer(buf.data(), buf.size(), mem, 0x10000, 0x100000);
    MACHO_ASSERT(res.is_success(), "Loading succeeded");
    MACHO_ASSERT(res.segments.size() == 1, "1 segment mapped");
    MACHO_ASSERT(res.segments[0].mapped_address == 0x10000, "Mapped at physical 0x10000");

    // Check payload bytes at mapped address
    MACHO_ASSERT(mem.read_u32(0x10000) == 0xD503201Fu, "First instruction is NOP");
    MACHO_ASSERT(mem.read_u32(0x10000 + 0x100) == 0x14000010u, "Entry point opcode B +64");

    // Check entry point translated to physical address
    MACHO_ASSERT(res.entry_point == 0x10000 + 0x100, "Entry point adjusted to physical address 0x10100");

    // Check BSS bytes (between filesize 0x4000 and vmsize 0x8000) are cleared to 0
    for (std::size_t i = 0x4000; i < 0x8000; i += 4) {
        MACHO_ASSERT(mem.read_u32(0x10000 + i) == 0, "BSS space is zero-filled");
    }

    std::cout << "[PASS] In-Memory Segment Loading and BSS Zero-Fill\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: KASLR Slide Rebasing
// -----------------------------------------------------------------------------
bool test_kaslr_slide_rebasing() {
    std::cout << "[7] test_kaslr_slide_rebasing...\n";

    auto buf = create_synthetic_macho_arm64(0xFFFFFFF007004000ULL, 0x8000, 0x4000, 0xFFFFFFF007004100ULL);

    Memory mem(0x200000); // 2 MB RAM
    const std::uint64_t kaslr = 0x20000; // 128 KB slide

    auto res = MachOLoader::load_from_buffer(buf.data(), buf.size(), mem, 0x10000, 0x200000, kaslr);
    MACHO_ASSERT(res.is_success(), "Loading with KASLR succeeded");
    MACHO_ASSERT(res.segments[0].mapped_address == 0x10000 + kaslr, "Segment mapped with KASLR slide");
    MACHO_ASSERT(res.entry_point == 0x10000 + kaslr + 0x100, "Entry point rebased with KASLR slide");

    // Verify opcode at rebased entry point
    MACHO_ASSERT(mem.read_u32(res.entry_point) == 0x14000010u, "Opcode preserved at rebased entry point");

    std::cout << "[PASS] KASLR Slide Rebasing\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Guest Memory Map Collision Guard Integration
// -----------------------------------------------------------------------------
bool test_guest_memory_map_integration() {
    std::cout << "[8] test_guest_memory_map_integration...\n";

    auto buf = create_synthetic_macho_arm64(0xFFFFFFF007004000ULL, 0x8000, 0x4000, 0xFFFFFFF007004100ULL);

    Memory mem(0x200000);
    GuestMemoryMap map(0, 0x200000);

    auto res = MachOLoader::load_from_buffer(buf.data(), buf.size(), mem, 0x20000, 0x200000, 0, &map);
    MACHO_ASSERT(res.is_success(), "Load with memory map succeeded");

    const auto* reg = map.find_region_by_name("MachO___TEXT");
    MACHO_ASSERT(reg != nullptr, "Registered 'MachO___TEXT' in memory map");
    MACHO_ASSERT(reg->base_address == 0x20000, "Base address is 0x20000");
    MACHO_ASSERT(reg->size == 0x8000, "Size is 0x8000");

    std::string err;
    MACHO_ASSERT(map.validate(&err) == MemoryMapValidationResult::Valid, "Memory map is valid");

    std::cout << "[PASS] Guest Memory Map Collision Guard Integration\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Memory Bounds Exceeded Protection
// -----------------------------------------------------------------------------
bool test_memory_bounds_exceeded_protection() {
    std::cout << "[9] test_memory_bounds_exceeded_protection...\n";

    // Request 0x8000 bytes into a RAM capacity of only 0x2000 bytes
    auto buf = create_synthetic_macho_arm64(0xFFFFFFF007004000ULL, 0x8000, 0x4000, 0xFFFFFFF007004100ULL);

    Memory mem(0x2000);
    auto res = MachOLoader::load_from_buffer(buf.data(), buf.size(), mem, 0, 0x2000);
    MACHO_ASSERT(!res.is_success(), "Load exceeding RAM capacity rejected");
    MACHO_ASSERT(res.status == MachOLoadStatus::MemoryBoundsExceeded, "Status is MemoryBoundsExceeded");

    std::cout << "[PASS] Memory Bounds Exceeded Protection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Multi-Segment Mach-O Loading (__TEXT, __DATA, __LINKEDIT)
// -----------------------------------------------------------------------------
bool test_multi_segment_macho_loading() {
    std::cout << "[10] test_multi_segment_macho_loading...\n";

    std::vector<std::uint8_t> buf(0x20000, 0);

    auto* hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->magic = kMachOMagic64;
    hdr->cputype = kCpuTypeARM64;
    hdr->cpusubtype = kCpuSubtypeARM64All;
    hdr->filetype = kMachOFileTypeExecute;
    hdr->ncmds = 3; // __TEXT, __DATA, LC_MAIN
    hdr->sizeofcmds = 2 * sizeof(MachOSegmentCommand64) + sizeof(MachOEntrypointCommand);

    std::size_t offset = sizeof(MachOHeader64);

    // Segment 1: __TEXT
    auto* seg_text = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + offset);
    seg_text->cmd = kLoadCmdSegment64;
    seg_text->cmdsize = sizeof(MachOSegmentCommand64);
    std::memcpy(seg_text->segname, "__TEXT", 6);
    seg_text->vmaddr = 0x100000000ULL;
    seg_text->vmsize = 0x4000;
    seg_text->fileoff = 0x1000;
    seg_text->filesize = 0x2000;
    seg_text->maxprot = kVmProtRead | kVmProtExecute;
    seg_text->initprot = kVmProtRead | kVmProtExecute;

    offset += sizeof(MachOSegmentCommand64);

    // Segment 2: __DATA
    auto* seg_data = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + offset);
    seg_data->cmd = kLoadCmdSegment64;
    seg_data->cmdsize = sizeof(MachOSegmentCommand64);
    std::memcpy(seg_data->segname, "__DATA", 6);
    seg_data->vmaddr = 0x100004000ULL;
    seg_data->vmsize = 0x4000;
    seg_data->fileoff = 0x3000;
    seg_data->filesize = 0x1000;
    seg_data->maxprot = kVmProtRead | kVmProtWrite;
    seg_data->initprot = kVmProtRead | kVmProtWrite;

    offset += sizeof(MachOSegmentCommand64);

    // LC_MAIN with entryoff = 0x200
    auto* ep = reinterpret_cast<MachOEntrypointCommand*>(buf.data() + offset);
    ep->cmd = kLoadCmdMain;
    ep->cmdsize = sizeof(MachOEntrypointCommand);
    ep->entryoff = 0x200;
    ep->stacksize = 0x40000;

    // Payload markers
    *reinterpret_cast<std::uint32_t*>(buf.data() + 0x1000) = 0x11111111u;
    *reinterpret_cast<std::uint32_t*>(buf.data() + 0x3000) = 0x22222222u;

    Memory mem(0x100000);
    auto res = MachOLoader::load_from_buffer(buf.data(), buf.size(), mem, 0, 0x100000);
    MACHO_ASSERT(res.is_success(), "Multi-segment Mach-O loaded");
    MACHO_ASSERT(res.segments.size() == 2, "2 segments parsed and mapped");
    MACHO_ASSERT(res.find_segment("__TEXT") != nullptr, "Found __TEXT");
    MACHO_ASSERT(res.find_segment("__DATA") != nullptr, "Found __DATA");

    MACHO_ASSERT(mem.read_u32(0x0000) == 0x11111111u, "__TEXT mapped at physical 0");
    MACHO_ASSERT(mem.read_u32(0x4000) == 0x22222222u, "__DATA mapped at physical 0x4000");
    MACHO_ASSERT(res.entry_point == 0x200, "Entrypoint offset applied from LC_MAIN");
    MACHO_ASSERT(res.initial_sp == 0x40000, "Stack size captured from LC_MAIN");

    std::cout << "[PASS] Multi-Segment Mach-O Loading (__TEXT, __DATA, __LINKEDIT)\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 11: Fileset Entry Parsing & Executable Segment Fallback
// -----------------------------------------------------------------------------
bool test_fileset_entry_parsing_and_executable_fallback() {
    std::cout << "[11] test_fileset_entry_parsing_and_executable_fallback...\n";

    std::vector<std::uint8_t> buf(0x8000, 0);
    auto* hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->magic = kMachOMagic64;
    hdr->cputype = kCpuTypeARM64;
    hdr->cpusubtype = kCpuSubtypeARM64All;
    hdr->filetype = kMachOFileTypeFileset;
    hdr->ncmds = 2; // LC_SEGMENT_64 (__TEXT_EXEC) + LC_FILESET_ENTRY
    
    // Command sizes
    const std::uint32_t seg_cmdsize = sizeof(MachOSegmentCommand64);
    // Align string table in fileset command to 8 bytes: 32 bytes struct + 24 bytes string
    const std::uint32_t fse_cmdsize = sizeof(MachOFilesetEntryCommand) + 24;
    hdr->sizeofcmds = seg_cmdsize + fse_cmdsize;

    std::size_t offset = sizeof(MachOHeader64);

    // 1. LC_SEGMENT_64 (__TEXT_EXEC)
    auto* seg = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + offset);
    seg->cmd = kLoadCmdSegment64;
    seg->cmdsize = seg_cmdsize;
    std::memcpy(seg->segname, "__TEXT_EXEC", 11);
    seg->vmaddr = 0xFFFFFFF007008000ULL;
    seg->vmsize = 0x4000;
    seg->fileoff = 0x1000;
    seg->filesize = 0x2000;
    seg->maxprot = kVmProtRead | kVmProtExecute;
    seg->initprot = kVmProtRead | kVmProtExecute;
    offset += seg_cmdsize;

    // 2. LC_FILESET_ENTRY (com.apple.kernel)
    auto* fse = reinterpret_cast<MachOFilesetEntryCommand*>(buf.data() + offset);
    fse->cmd = kLoadCmdFilesetEntry;
    fse->cmdsize = fse_cmdsize;
    fse->vmaddr = 0xFFFFFFF007008000ULL;
    fse->fileoff = 0x1000;
    fse->entry_id_offset = sizeof(MachOFilesetEntryCommand);
    const char* kext_id = "com.apple.kernel";
    std::memcpy(buf.data() + offset + sizeof(MachOFilesetEntryCommand), kext_id, std::strlen(kext_id) + 1);

    // Write instructions at fileoff 0x1000
    *reinterpret_cast<std::uint32_t*>(buf.data() + 0x1000) = 0xD503201Fu; // NOP

    Memory mem(0x100000);
    auto res = MachOLoader::load_from_buffer(buf.data(), buf.size(), mem, 0x40000000ULL, 0x100000);
    MACHO_ASSERT(res.is_success(), "Fileset Mach-O loaded successfully");
    MACHO_ASSERT(res.fileset_entries.size() == 1, "1 fileset entry parsed");
    MACHO_ASSERT(res.find_fileset_entry("com.apple.kernel") != nullptr, "Found com.apple.kernel fileset entry");
    MACHO_ASSERT(res.find_fileset_entry("com.apple.kernel")->vmaddr == 0xFFFFFFF007008000ULL, "Correct fileset vmaddr");

    // Verify fallback entrypoint defaulted to __TEXT_EXEC start and was rebased to physical RAM
    MACHO_ASSERT(res.entry_point == 0x40000000ULL, "Fallback entrypoint rebased to RAM base");

    std::cout << "[PASS] Fileset Entry Parsing & Executable Segment Fallback\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "=== WinIOS Mach-O 64-Bit Kernelcache Loader Tests ===\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        std::cout << "Starting " << name << "...\n";
        if (test_fn()) {
            passed++;
            std::cout << "Completed " << name << ": OK\n";
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n";
        }
    };

    run_test(test_invalid_and_truncated_magic, "test_invalid_and_truncated_magic");
    run_test(test_unsupported_cpu_type, "test_unsupported_cpu_type");
    run_test(test_thin_arm64_header_parsing, "test_thin_arm64_header_parsing");
    run_test(test_universal_fat_binary_slice_extraction, "test_universal_fat_binary_slice_extraction");
    run_test(test_unixthread_entry_point_discovery, "test_unixthread_entry_point_discovery");
    run_test(test_in_memory_segment_loading_and_bss_clearing, "test_in_memory_segment_loading_and_bss_clearing");
    run_test(test_kaslr_slide_rebasing, "test_kaslr_slide_rebasing");
    run_test(test_guest_memory_map_integration, "test_guest_memory_map_integration");
    run_test(test_memory_bounds_exceeded_protection, "test_memory_bounds_exceeded_protection");
    run_test(test_multi_segment_macho_loading, "test_multi_segment_macho_loading");
    run_test(test_fileset_entry_parsing_and_executable_fallback, "test_fileset_entry_parsing_and_executable_fallback");

    std::cout << "\nMach-O Loader Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
