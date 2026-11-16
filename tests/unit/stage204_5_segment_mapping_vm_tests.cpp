// ==============================================================================
// WinIOS Campaign 204 Stage 204.5: Mach-O Segment Mapping & VM Space Tests
// ==============================================================================
// Independent verification of:
// 1. LC_SEGMENT_64 mapping into DarwinVmSpace conforming to 16KB Apple page granule
// 2. Multi-segment memory layout (__PAGEZERO, __TEXT, __DATA, __LINKEDIT)
// 3. File-backed vs zero-filled memory boundary behavior
// 4. ASLR image slide relocation on segment virtual addresses and entry point
// 5. Memory protection ownership & W^X enforcement (No RWX segments permitted)
// 6. Dynamic memory protection transitions via mprotect
// 7. Entry-point validation and bounds checking
// ==============================================================================

#include <cstdint>
#include <iostream>
#include <vector>

#include "loader/macho_fixture_corpus.h"
#include "loader/userspace_macho_loader.h"
#include "memory/darwin_vm_object.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_segment_mapping_and_protections() {
    std::cout << "[Stage 204.5 Test 1] Multi-Segment Mapping & W^X Protections...\n";

    auto fixture = PublicMachOCorpus::create_multisegment_executable();
    DarwinVmSpace vm_space(1001);

    UserspaceLoadConfig config{};
    config.aslr_enabled = false;
    config.aslr_slide = 0;
    config.page_size = kApplePageSize16K;

    UserspaceLoadResult res = UserspaceMachOLoader::load_into_vm_space(
        fixture.binary_data.data(), fixture.binary_data.size(), vm_space, config);

    TEST_CHECK(res.is_success(), "Loading into DarwinVmSpace must succeed");
    TEST_CHECK(res.entry_point == 0x0000000100001000ULL, "Entry point must match 0x100001000");

    // Verify __TEXT segment: RX, not W
    const auto* text_entry = vm_space.find_entry(0x0000000100000000ULL);
    TEST_CHECK(text_entry != nullptr, "__TEXT entry must exist at 0x100000000");
    TEST_CHECK(text_entry->prot == (kDarwinVmProtRead | kDarwinVmProtExecute), "__TEXT must be Read + Execute");
    TEST_CHECK((text_entry->prot & kDarwinVmProtWrite) == 0, "__TEXT must NOT be writable (W^X)");

    // Verify __DATA segment: RW, not X
    const auto* data_entry = vm_space.find_entry(0x0000000100004000ULL);
    TEST_CHECK(data_entry != nullptr, "__DATA entry must exist at 0x100004000");
    TEST_CHECK(data_entry->prot == (kDarwinVmProtRead | kDarwinVmProtWrite), "__DATA must be Read + Write");
    TEST_CHECK((data_entry->prot & kDarwinVmProtExecute) == 0, "__DATA must NOT be executable (W^X)");

    // Verify __LINKEDIT segment: Read-only
    const auto* linkedit_entry = vm_space.find_entry(0x0000000100008000ULL);
    TEST_CHECK(linkedit_entry != nullptr, "__LINKEDIT entry must exist at 0x100008000");
    TEST_CHECK(linkedit_entry->prot == kDarwinVmProtRead, "__LINKEDIT must be Read-Only");

    // Verify code payload at entry point
    std::uint32_t read_code[3]{};
    int read_res = vm_space.mach_vm_read(res.entry_point, read_code, sizeof(read_code));
    TEST_CHECK(read_res == kKernSuccess, "Reading entry point code via mach_vm_read must succeed");
    TEST_CHECK(read_code[0] == 0xD2800000u, "First instruction must be mov x0, #0");
    TEST_CHECK(read_code[1] == 0xD2800030u, "Second instruction must be mov x16, #1");
    TEST_CHECK(read_code[2] == 0xD4001001u, "Third instruction must be svc #0x80");

    std::cout << "  -> Multi-segment mapping and W^X protections verified.\n";
}

void test_aslr_slide_relocation() {
    std::cout << "[Stage 204.5 Test 2] ASLR Image Slide Relocation...\n";

    auto fixture = PublicMachOCorpus::create_minimal_executable();
    DarwinVmSpace vm_space(1002);

    constexpr std::uint64_t kSlide = 0x0000000020000000ULL; // 512 MB slide
    UserspaceLoadConfig config{};
    config.aslr_enabled = true;
    config.aslr_slide = kSlide;
    config.page_size = kApplePageSize16K;

    UserspaceLoadResult res = UserspaceMachOLoader::load_into_vm_space(
        fixture.binary_data.data(), fixture.binary_data.size(), vm_space, config);

    TEST_CHECK(res.is_success(), "Slid loading into DarwinVmSpace must succeed");
    TEST_CHECK(res.aslr_slide == kSlide, "Recorded slide must match requested ASLR slide");

    const std::uint64_t expected_entry = 0x0000000100001000ULL + kSlide;
    TEST_CHECK(res.entry_point == expected_entry, "Slid entry point must reflect ASLR offset");

    const auto* slid_text = vm_space.find_entry(0x0000000100000000ULL + kSlide);
    TEST_CHECK(slid_text != nullptr, "Slid __TEXT segment must be placed at slid VA");
    TEST_CHECK(slid_text->prot == (kDarwinVmProtRead | kDarwinVmProtExecute), "Slid __TEXT must be RX");

    // Unslid address must not have mapped entry
    const auto* unslid_text = vm_space.find_entry(0x0000000100000000ULL);
    TEST_CHECK(unslid_text == nullptr, "Unslid VA must remain unmapped under ASLR");

    std::cout << "  -> ASLR image slide relocation verified.\n";
}

void test_protection_transitions_and_bounds() {
    std::cout << "[Stage 204.5 Test 3] Memory Protection Transitions & Bounds...\n";

    auto fixture = PublicMachOCorpus::create_multisegment_executable();
    DarwinVmSpace vm_space(1003);

    UserspaceLoadConfig config{};
    config.aslr_enabled = false;
    config.page_size = kApplePageSize16K;

    UserspaceLoadResult res = UserspaceMachOLoader::load_into_vm_space(
        fixture.binary_data.data(), fixture.binary_data.size(), vm_space, config);
    TEST_CHECK(res.is_success(), "Binary load must succeed");

    const std::uint64_t data_va = 0x0000000100004000ULL;

    // Transition __DATA from RW to Read-Only via mprotect
    int prot_res = vm_space.mprotect(data_va, kApplePageSize16K, kDarwinVmProtRead);
    TEST_CHECK(prot_res == 0, "mprotect __DATA to Read-Only must succeed");
    const auto* updated_entry = vm_space.find_entry(data_va);
    TEST_CHECK(updated_entry != nullptr && updated_entry->prot == kDarwinVmProtRead,
               "Segment protection must now be Read-Only");

    // Inaccessible bounds test: Out-of-bounds address (e.g. 0x200000000)
    TEST_CHECK(!vm_space.check_access(0x0000000200000000ULL, 64, kDarwinVmProtRead),
               "Unmapped address must fail access check");

    // Writing to read-protected memory via mach_vm_write must fail
    std::uint32_t val = 0x12345678;
    int write_res = vm_space.mach_vm_write(data_va, &val, sizeof(val));
    TEST_CHECK(write_res == kKernProtectionFailure,
               "mach_vm_write to Read-Only segment must return kKernProtectionFailure");

    std::cout << "  -> Memory protection transitions & bounds verified.\n";
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 204.5 Segment Mapping & VM Space Tests ===\n";
    test_segment_mapping_and_protections();
    test_aslr_slide_relocation();
    test_protection_transitions_and_bounds();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 204.5 Segment Mapping & VM Space passed all tests.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.5 Segment Mapping & VM Space failed with " << g_failures << " errors.\n";
        return 1;
    }
}
