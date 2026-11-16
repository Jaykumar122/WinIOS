#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "loader/kernel_image_parser.h"
#include "loader/userspace_macho_loader.h"

using namespace winios::loader;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// Helper: Build synthetic Mach-O kernel binary
std::vector<std::uint8_t> create_synthetic_kernel_macho(
    std::uint64_t text_vmaddr,
    std::uint64_t text_vmsize,
    std::uint64_t data_vmaddr,
    std::uint64_t data_vmsize,
    std::uint64_t entry_vaddr,
    bool include_unixthread = true) {

    std::vector<std::uint8_t> buffer;
    buffer.resize(0x8000, 0); // 32KB buffer

    MachOHeader64Raw header{};
    header.magic = kMachO64Magic;
    header.cputype = kMachOCpuTypeARM64;
    header.cpusubtype = kMachOCpuSubtypeArm64All;
    header.filetype = kMachOFileTypeExecute;
    header.ncmds = include_unixthread ? 3 : 2;

    std::size_t offset = sizeof(header);

    // Segment 1: __TEXT
    MachOSegment64Raw text_seg{};
    text_seg.cmd = kCmdSegment64;
    text_seg.cmdsize = sizeof(MachOSegment64Raw);
    std::memcpy(text_seg.segname, "__TEXT", 6);
    text_seg.vmaddr = text_vmaddr;
    text_seg.vmsize = text_vmsize;
    text_seg.fileoff = 0;
    text_seg.filesize = 0x4000;
    text_seg.maxprot = kProtRead | kProtExecute;
    text_seg.initprot = kProtRead | kProtExecute;

    std::memcpy(buffer.data() + offset, &text_seg, sizeof(text_seg));
    offset += sizeof(text_seg);

    // Segment 2: __DATA
    MachOSegment64Raw data_seg{};
    data_seg.cmd = kCmdSegment64;
    data_seg.cmdsize = sizeof(MachOSegment64Raw);
    std::memcpy(data_seg.segname, "__DATA", 6);
    data_seg.vmaddr = data_vmaddr;
    data_seg.vmsize = data_vmsize;
    data_seg.fileoff = 0x4000;
    data_seg.filesize = 0x4000;
    data_seg.maxprot = kProtRead | kProtWrite;
    data_seg.initprot = kProtRead | kProtWrite;

    std::memcpy(buffer.data() + offset, &data_seg, sizeof(data_seg));
    offset += sizeof(data_seg);

    // Command 3: LC_UNIXTHREAD with entry PC
    if (include_unixthread) {
        std::vector<std::uint8_t> thread_cmd(280, 0);
        std::uint32_t cmd = kCmdUnixThread;
        std::uint32_t cmdsize = 280;
        std::uint32_t flavor = 6; // ARM_THREAD_STATE64
        std::uint32_t count = 68;

        std::memcpy(thread_cmd.data(), &cmd, 4);
        std::memcpy(thread_cmd.data() + 4, &cmdsize, 4);
        std::memcpy(thread_cmd.data() + 8, &flavor, 4);
        std::memcpy(thread_cmd.data() + 12, &count, 4);
        std::memcpy(thread_cmd.data() + 272, &entry_vaddr, sizeof(entry_vaddr));

        std::memcpy(buffer.data() + offset, thread_cmd.data(), thread_cmd.size());
        offset += thread_cmd.size();
    }

    header.sizeofcmds = static_cast<std::uint32_t>(offset - sizeof(header));
    std::memcpy(buffer.data(), &header, sizeof(header));

    return buffer;
}

void test_valid_kernel_parsing() {
    std::cout << "[Test 1] Valid ARM64 Mach-O Kernel Parsing...\n";
    auto bin = create_synthetic_kernel_macho(
        0xFFFFFE0007004000ULL, 0x4000,
        0xFFFFFE0007008000ULL, 0x4000,
        0xFFFFFE0007004100ULL, true);

    KernelParsedImage image;
    auto status = KernelImageParser::parse(bin.data(), bin.size(), image);

    TEST_CHECK(status == KernelImageParseStatus::Success, "Kernel parsing must succeed");
    TEST_CHECK(image.format == KernelImageFormat::MachO64, "Format must be MachO64");
    TEST_CHECK(image.cputype == kMachOCpuTypeARM64, "CPU type must be ARM64");
    TEST_CHECK(image.entry_point_vaddr == 0xFFFFFE0007004100ULL, "Entry point must match unixthread PC");
    TEST_CHECK(image.segments.size() == 2, "Must have parsed 2 segments");
    TEST_CHECK(image.find_segment("__TEXT") != nullptr, "Must contain __TEXT segment");
    TEST_CHECK(image.find_segment("__DATA") != nullptr, "Must contain __DATA segment");
    std::cout << "  -> Valid parsing passed.\n";
}

void test_truncated_header_rejection() {
    std::cout << "[Test 2] Truncated Header Rejection...\n";
    std::uint8_t small_buf[16] = {0};
    KernelParsedImage image;
    auto status = KernelImageParser::parse(small_buf, sizeof(small_buf), image);

    TEST_CHECK(status == KernelImageParseStatus::TruncatedHeader, "Must reject truncated buffer");
    std::cout << "  -> Truncated header rejection passed.\n";
}

void test_invalid_magic_rejection() {
    std::cout << "[Test 3] Invalid Magic Rejection...\n";
    auto bin = create_synthetic_kernel_macho(0x10000, 0x4000, 0x14000, 0x4000, 0x10000);
    bin[0] = 0x7F; bin[1] = 'E'; bin[2] = 'L'; bin[3] = 'F'; // Fake ELF magic

    KernelParsedImage image;
    auto status = KernelImageParser::parse(bin.data(), bin.size(), image);

    TEST_CHECK(status == KernelImageParseStatus::InvalidMagic, "Must reject invalid magic");
    std::cout << "  -> Invalid magic rejection passed.\n";
}

void test_unsupported_cpu_type() {
    std::cout << "[Test 4] Unsupported CPU Type Rejection...\n";
    auto bin = create_synthetic_kernel_macho(0x10000, 0x4000, 0x14000, 0x4000, 0x10000);
    std::uint32_t x86_64 = 0x01000007u;
    std::memcpy(bin.data() + 4, &x86_64, 4);

    KernelParsedImage image;
    auto status = KernelImageParser::parse(bin.data(), bin.size(), image);

    TEST_CHECK(status == KernelImageParseStatus::UnsupportedCpuType, "Must reject non-ARM64");
    std::cout << "  -> Unsupported CPU type rejection passed.\n";
}

void test_unsupported_file_type() {
    std::cout << "[Test 5] Unsupported File Type Rejection...\n";
    auto bin = create_synthetic_kernel_macho(0x10000, 0x4000, 0x14000, 0x4000, 0x10000);
    std::uint32_t dylib_type = kMachOFileTypeDylib;
    std::memcpy(bin.data() + 12, &dylib_type, 4);

    KernelParsedImage image;
    auto status = KernelImageParser::parse(bin.data(), bin.size(), image);

    TEST_CHECK(status == KernelImageParseStatus::UnsupportedFileType, "Must reject dylib as kernel");
    std::cout << "  -> Unsupported file type rejection passed.\n";
}

void test_corrupted_load_commands() {
    std::cout << "[Test 6] Corrupted Load Commands Rejection...\n";
    auto bin = create_synthetic_kernel_macho(0x10000, 0x4000, 0x14000, 0x4000, 0x10000);
    // Set cmdsize of first command to 0 (infinite loop attack)
    std::uint32_t zero_cmdsize = 0;
    std::memcpy(bin.data() + sizeof(MachOHeader64Raw) + 4, &zero_cmdsize, 4);

    KernelParsedImage image;
    auto status = KernelImageParser::parse(bin.data(), bin.size(), image);

    TEST_CHECK(status == KernelImageParseStatus::CorruptedLoadCommands, "Must reject 0 cmdsize");
    std::cout << "  -> Corrupted load commands rejection passed.\n";
}

void test_misaligned_segment() {
    std::cout << "[Test 7] Misaligned Segment Rejection...\n";
    // 0x10001 is not aligned to 16KB (0x4000)
    auto bin = create_synthetic_kernel_macho(0x10001, 0x4000, 0x18000, 0x4000, 0x10001);

    KernelParsedImage image;
    auto status = KernelImageParser::parse(bin.data(), bin.size(), image);

    TEST_CHECK(status == KernelImageParseStatus::MisalignedSegment, "Must reject unaligned segment");
    std::cout << "  -> Misaligned segment rejection passed.\n";
}

void test_overlapping_segments() {
    std::cout << "[Test 8] Overlapping Segments Rejection...\n";
    // __TEXT at 0x10000..0x18000, __DATA at 0x14000..0x1C000 (overlapping by 16KB)
    auto bin = create_synthetic_kernel_macho(0x10000, 0x8000, 0x14000, 0x8000, 0x10000);

    KernelParsedImage image;
    auto status = KernelImageParser::parse(bin.data(), bin.size(), image);

    TEST_CHECK(status == KernelImageParseStatus::OverlappingSegments, "Must reject overlapping segments");
    std::cout << "  -> Overlapping segments rejection passed.\n";
}

void test_segment_out_of_bounds() {
    std::cout << "[Test 9] Segment Out-of-Bounds Rejection...\n";
    auto bin = create_synthetic_kernel_macho(0x10000, 0x4000, 0x14000, 0x4000, 0x10000);
    // Corrupt __DATA fileoff to 0x100000, exceeding buffer size
    std::uint64_t huge_off = 0x100000ULL;
    std::size_t data_cmd_offset = sizeof(MachOHeader64Raw) + sizeof(MachOSegment64Raw);
    std::memcpy(bin.data() + data_cmd_offset + 40, &huge_off, sizeof(huge_off));

    KernelParsedImage image;
    auto status = KernelImageParser::parse(bin.data(), bin.size(), image);

    TEST_CHECK(status == KernelImageParseStatus::SegmentOutOfBounds, "Must reject OOB fileoff");
    std::cout << "  -> Segment out of bounds rejection passed.\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Running Stage 205.4 Kernel Image Parser Tests ===\n";
    test_valid_kernel_parsing();
    test_truncated_header_rejection();
    test_invalid_magic_rejection();
    test_unsupported_cpu_type();
    test_unsupported_file_type();
    test_corrupted_load_commands();
    test_misaligned_segment();
    test_overlapping_segments();
    test_segment_out_of_bounds();

    if (g_failures == 0) {
        std::cout << "ALL STAGE 205.4 KERNEL IMAGE PARSER TESTS PASSED!\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED!\n";
        return 1;
    }
}
