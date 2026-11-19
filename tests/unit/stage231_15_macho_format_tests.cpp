#include "loader/app_inspector.h"
#include "loader/embedded_campaign230_workloads.inl"

#include <cassert>
#include <iostream>
#include <vector>
#include <cstring>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #cond << std::endl; \
        std::exit(1); \
    } \
} while(0)

using namespace winios::loader;

// 1. Verify Genuine Zig ARM64 Mach-O Binaries
static void test_genuine_zig_macho_binaries() {
    std::cout << "[Test 1] Genuine Zig ARM64 Mach-O binaries inspection...\n";

    const std::vector<uint8_t>& bin_startup = get_embedded_campaign230_app_startup();
    const std::vector<uint8_t>& bin_settings = get_embedded_campaign230_app_settings();
    const std::vector<uint8_t>& bin_docs = get_embedded_campaign230_app_docs();

    TEST_ASSERT(!bin_startup.empty());
    TEST_ASSERT(!bin_settings.empty());
    TEST_ASSERT(!bin_docs.empty());

    const std::vector<uint8_t>* test_bins[] = { &bin_startup, &bin_settings, &bin_docs };

    for (const auto* bin : test_bins) {
        auto rep = AppInspector::inspect_binary(bin->data(), bin->size());
        TEST_ASSERT(rep.is_valid);
        TEST_ASSERT(rep.magic == AppInspector::kMachO64Magic);
        TEST_ASSERT(rep.cputype == AppInspector::kCpuTypeArm64);
        TEST_ASSERT(rep.filetype == AppInspector::kMhExecute);
        TEST_ASSERT(rep.wx_clean);
        TEST_ASSERT(rep.page_aligned_16k);
        TEST_ASSERT(!rep.segments.empty());
        TEST_ASSERT(!rep.load_commands.empty());

        bool has_text = false;
        for (const auto& seg : rep.segments) {
            if (seg.segname == "__TEXT") {
                has_text = true;
                // __TEXT should be readable and executable, but not writable
                TEST_ASSERT((seg.initprot & 0x1) != 0); // Read
                TEST_ASSERT((seg.initprot & 0x4) != 0); // Exec
                TEST_ASSERT((seg.initprot & 0x2) == 0); // Not Write
            }
        }
        TEST_ASSERT(has_text);
    }
    std::cout << "  -> All embedded ARM64 binaries conform to 16 KB Mach-O standards.\n";
}

// 2. Adversarial Binary Formats & Corruption Detection
static void test_malformed_macho_rejection() {
    std::cout << "[Test 2] Malformed Mach-O binary rejection...\n";

    // 1. Below minimum size
    uint8_t tiny[16]{0};
    auto r_tiny = AppInspector::inspect_binary(tiny, sizeof(tiny));
    TEST_ASSERT(!r_tiny.is_valid);

    // 2. ELF magic instead of Mach-O
    uint8_t elf_header[64]{0x7f, 'E', 'L', 'F', 2, 1, 1, 0};
    auto r_elf = AppInspector::inspect_binary(elf_header, sizeof(elf_header));
    TEST_ASSERT(!r_elf.is_valid);
    TEST_ASSERT(r_elf.rejection_reason.find("magic") != std::string::npos);

    // 3. x86_64 architecture instead of ARM64
    uint8_t x86_bin[64]{0};
    uint32_t magic = AppInspector::kMachO64Magic;
    uint32_t x86_cputype = 0x01000007; // CPU_TYPE_X86_64
    uint32_t filetype = AppInspector::kMhExecute;
    std::memcpy(x86_bin, &magic, 4);
    std::memcpy(x86_bin + 4, &x86_cputype, 4);
    std::memcpy(x86_bin + 12, &filetype, 4);
    auto r_x86 = AppInspector::inspect_binary(x86_bin, sizeof(x86_bin));
    TEST_ASSERT(!r_x86.is_valid);
    TEST_ASSERT(r_x86.rejection_reason.find("ARM64") != std::string::npos);

    // 4. W^X violation mutation
    auto bin_copy = get_embedded_campaign230_app_startup();
    // Locate __TEXT segment initprot and set write bit (0x2)
    // Find offset of __TEXT segment
    for (size_t i = 32; i + 72 <= bin_copy.size(); ++i) {
        if (std::memcmp(bin_copy.data() + i + 8, "__TEXT", 6) == 0) {
            // initprot is at offset 60
            bin_copy[i + 60] |= 0x2; // Add write permission -> W^X violation!
            break;
        }
    }
    auto r_wx = AppInspector::inspect_binary(bin_copy.data(), bin_copy.size());
    TEST_ASSERT(!r_wx.is_valid);
    TEST_ASSERT(r_wx.rejection_reason.find("W^X") != std::string::npos);

    std::cout << "  -> Malformed headers and W^X violations strictly rejected.\n";
}

// 3. Code Signature Command (LC_CODE_SIGNATURE) Handling
static void test_code_signature_command() {
    std::cout << "[Test 3] LC_CODE_SIGNATURE load command handling...\n";

    // Build synthetic minimal Mach-O with LC_SEGMENT_64, LC_MAIN, and LC_CODE_SIGNATURE
    std::vector<uint8_t> syn(256, 0);

    uint32_t magic = AppInspector::kMachO64Magic;
    uint32_t cputype = AppInspector::kCpuTypeArm64;
    uint32_t filetype = AppInspector::kMhExecute;
    uint32_t ncmds = 3;
    uint32_t sizeofcmds = 72 + 24 + 16; // SEGMENT_64 + MAIN + CODE_SIGNATURE

    std::memcpy(syn.data(), &magic, 4);
    std::memcpy(syn.data() + 4, &cputype, 4);
    std::memcpy(syn.data() + 12, &filetype, 4);
    std::memcpy(syn.data() + 16, &ncmds, 4);
    std::memcpy(syn.data() + 20, &sizeofcmds, 4);

    size_t off = 32;

    // Command 1: LC_SEGMENT_64 (72 bytes)
    uint32_t cmd_seg = 0x19;
    uint32_t sz_seg = 72;
    std::memcpy(syn.data() + off, &cmd_seg, 4);
    std::memcpy(syn.data() + off + 4, &sz_seg, 4);
    std::memcpy(syn.data() + off + 8, "__TEXT\0\0\0\0\0\0\0\0\0\0", 16);
    uint32_t maxprot = 0x5;
    uint32_t initprot = 0x5; // Read + Exec (clean)
    std::memcpy(syn.data() + off + 56, &maxprot, 4);
    std::memcpy(syn.data() + off + 60, &initprot, 4);
    off += sz_seg;

    // Command 2: LC_MAIN (24 bytes)
    uint32_t cmd_main = 0x80000028;
    uint32_t sz_main = 24;
    uint64_t entry_off = 0x1000;
    std::memcpy(syn.data() + off, &cmd_main, 4);
    std::memcpy(syn.data() + off + 4, &sz_main, 4);
    std::memcpy(syn.data() + off + 8, &entry_off, 8);
    off += sz_main;

    // Command 3: LC_CODE_SIGNATURE (16 bytes)
    uint32_t cmd_sig = 0x1d;
    uint32_t sz_sig = 16;
    uint32_t sig_data_off = 0x80;
    uint32_t sig_data_sz = 64;
    std::memcpy(syn.data() + off, &cmd_sig, 4);
    std::memcpy(syn.data() + off + 4, &sz_sig, 4);
    std::memcpy(syn.data() + off + 8, &sig_data_off, 4);
    std::memcpy(syn.data() + off + 12, &sig_data_sz, 4);

    auto rep = AppInspector::inspect_binary(syn.data(), syn.size());
    TEST_ASSERT(rep.is_valid);
    TEST_ASSERT(rep.has_code_signature);
    TEST_ASSERT(rep.code_signature_offset == sig_data_off);
    TEST_ASSERT(rep.code_signature_size == sig_data_sz);
    TEST_ASSERT(rep.entry_point_offset == entry_off);

    std::cout << "  -> LC_CODE_SIGNATURE correctly parsed and recorded.\n";
}

int main() {
    std::cout << "=== Stage 231.15: Mach-O Toolchain and Binary Format Truth Tests ===\n";
    test_genuine_zig_macho_binaries();
    test_malformed_macho_rejection();
    test_code_signature_command();
    std::cout << "=== All Stage 231.15 Tests PASSED ===\n";
    return 0;
}
