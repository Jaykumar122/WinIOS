#include "loader/app_inspector.h"
#include "loader/embedded_campaign229_workloads.inl"

#include <cassert>
#include <iostream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;

static void test_rejects_malformed_binaries() {
    auto rep1 = loader::AppInspector::inspect_binary(nullptr, 0);
    TEST_ASSERT(!rep1.is_valid);
    TEST_ASSERT(!rep1.rejection_reason.empty());

    uint8_t short_buf[16] = {0};
    auto rep2 = loader::AppInspector::inspect_binary(short_buf, sizeof(short_buf));
    TEST_ASSERT(!rep2.is_valid);

    uint8_t elf_buf[64] = {0x7f, 'E', 'L', 'F', 0x02, 0x01, 0x01, 0x00};
    auto rep3 = loader::AppInspector::inspect_binary(elf_buf, sizeof(elf_buf));
    TEST_ASSERT(!rep3.is_valid);
    TEST_ASSERT(rep3.rejection_reason.find("Invalid Mach-O 64-bit magic") != std::string::npos);

    uint32_t header[8] = {
        0xfeedfacf, // magic
        0x01000007, // cputype x86_64
        0x80000003, // cpusubtype
        0x00000002, // filetype MH_EXECUTE
        0, 0, 0, 0
    };
    auto rep4 = loader::AppInspector::inspect_binary(reinterpret_cast<const uint8_t*>(header), sizeof(header));
    TEST_ASSERT(!rep4.is_valid);
    TEST_ASSERT(rep4.rejection_reason.find("Unsupported CPU architecture") != std::string::npos);

    std::cout << "[PASS] test_rejects_malformed_binaries\n";
}

static void test_inspects_valid_arm64_workloads() {
    const auto& diag_bin = loader::get_embedded_campaign229_dev_diag();
    TEST_ASSERT(!diag_bin.empty());

    auto rep = loader::AppInspector::inspect_binary(diag_bin.data(), diag_bin.size());
    TEST_ASSERT(rep.is_valid);
    TEST_ASSERT(rep.magic == loader::AppInspector::kMachO64Magic);
    TEST_ASSERT(rep.cputype == loader::AppInspector::kCpuTypeArm64);
    TEST_ASSERT(rep.filetype == loader::AppInspector::kMhExecute);
    TEST_ASSERT(rep.ncmds > 0);
    TEST_ASSERT(rep.segments.size() > 0);
    TEST_ASSERT(rep.wx_clean);

    bool found_text = false;
    for (const auto& s : rep.segments) {
        if (s.segname == "__TEXT") {
            found_text = true;
            TEST_ASSERT(s.vmsize > 0);
            TEST_ASSERT((s.initprot & 4) != 0); // VM_PROT_EXECUTE
            TEST_ASSERT((s.initprot & 2) == 0); // Not writable
        }
    }
    TEST_ASSERT(found_text);
    std::cout << "[PASS] test_inspects_valid_arm64_workloads\n";
}

static void test_inspects_lifecycle_demo_workload() {
    const auto& demo_bin = loader::get_embedded_campaign229_lifecycle_demo();
    TEST_ASSERT(!demo_bin.empty());

    auto rep = loader::AppInspector::inspect_binary(demo_bin.data(), demo_bin.size());
    TEST_ASSERT(rep.is_valid);
    TEST_ASSERT(rep.magic == loader::AppInspector::kMachO64Magic);
    TEST_ASSERT(rep.cputype == loader::AppInspector::kCpuTypeArm64);
    TEST_ASSERT(rep.wx_clean);
    TEST_ASSERT(rep.entry_point_offset > 0);
    std::cout << "[PASS] test_inspects_lifecycle_demo_workload\n";
}

int main() {
    std::cout << "=== Running Stage 229 App Inspector Tests ===\n";
    test_rejects_malformed_binaries();
    test_inspects_valid_arm64_workloads();
    test_inspects_lifecycle_demo_workload();
    std::cout << "All Stage 229 App Inspector Tests Passed!\n";
    return 0;
}
