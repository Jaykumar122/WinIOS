// =============================================================================
// WinIOS — Stage 204.9: Loader Relocation and Protection Finalization Unit Tests
// =============================================================================

#include "loader/userspace_macho_loader.h"
#include "loader/macho_fixture_corpus.h"
#include "loader/dyld_engine.h"
#include "memory/darwin_vm_object.h"
#include <iostream>
#include <vector>
#include <string>

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            g_failed_tests++; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Full Relocation & Protection Finalization Workflow
// -----------------------------------------------------------------------------
void test_relocation_and_protection_finalization() {
    std::cout << "[Stage 204.9 Test 1] Full Relocation & Protection Finalization...\n";

    // 1. Set up DyldEngine with system libraries
    DyldEngine dyld;
    dyld.clear_modules();

    DyldLoadedModule libsys;
    libsys.name = "libSystem.B.dylib";
    libsys.install_name = "/usr/lib/libSystem.B.dylib";
    libsys.base_address = 0x180000000ULL;
    libsys.exports["_printf"] = 0x180001000ULL;
    libsys.exports["_exit"]   = 0x180002000ULL;
    dyld.register_module(std::move(libsys));

    // 2. Create synthetic multi-segment Mach-O binary
    auto fixture = PublicMachOCorpus::create_multisegment_executable();
    const auto& binary = fixture.binary_data;
    TEST_CHECK(!binary.empty(), "Generated MultiSegmentApp binary");

    // 3. Load into DarwinVmSpace with ASLR slide = 0x100000
    DarwinVmSpace vm_space(101);
    UserspaceLoadConfig config;
    config.aslr_enabled = true;
    config.aslr_slide = 0x100000ULL;
    config.page_size = 0x4000ULL;

    UserspaceLoadResult load_res = UserspaceMachOLoader::load_into_vm_space(
        binary.data(), binary.size(), vm_space, config);

    TEST_CHECK(load_res.status == UserspaceMachOLoadStatus::Success, "Binary mapped into DarwinVmSpace");
    TEST_CHECK(load_res.aslr_slide == 0x100000ULL, "ASLR slide recorded");

    // Locate __DATA segment mapped vaddr
    std::uint64_t data_vaddr = 0;
    for (const auto& seg : load_res.segments) {
        if (seg.name == "__DATA") {
            data_vaddr = seg.mapped_vaddr;
            break;
        }
    }
    TEST_CHECK(data_vaddr != 0, "Found __DATA segment");

    // 4. Define Rebase and Binding records
    // Rebase: Internal pointer at data_vaddr + 0x10 holding 0x100000000 -> rebased by 0x100000
    std::uint64_t initial_ptr = 0x100000000ULL;
    (void)vm_space.mach_vm_write(data_vaddr + 0x10, &initial_ptr, sizeof(initial_ptr));

    std::vector<DyldRebaseRecord> rebases = {
        {data_vaddr + 0x10, 1}
    };

    // Binding: Dynamic symbol _printf bound to data_vaddr + 0x20
    std::vector<DyldBindingRecord> bindings = {
        {"_printf", data_vaddr + 0x20, 0, 1, false}
    };

    // 5. Run relocation and protection finalization
    std::string err;
    bool ok = UserspaceMachOLoader::relocate_and_finalize_protections(
        vm_space, load_res, dyld, rebases, bindings, err);

    TEST_CHECK(ok, "relocate_and_finalize_protections succeeded");

    // 6. Verify rebased and bound pointer values in guest memory
    std::uint64_t rebased_val = 0;
    TEST_CHECK(vm_space.mach_vm_read(data_vaddr + 0x10, &rebased_val, sizeof(rebased_val)) == 0, "Read rebase location");
    TEST_CHECK(rebased_val == 0x100000000ULL + 0x100000ULL, "Rebased pointer value matches ASLR slide");

    std::uint64_t bound_val = 0;
    TEST_CHECK(vm_space.mach_vm_read(data_vaddr + 0x20, &bound_val, sizeof(bound_val)) == 0, "Read binding location");
    TEST_CHECK(bound_val == 0x180001000ULL, "Bound pointer value matches _printf");

    std::cout << "  -> Relocation & protection finalization verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Post-Relocation W^X and Write Revocation on __DATA_CONST
// -----------------------------------------------------------------------------
void test_data_const_write_revocation() {
    std::cout << "[Stage 204.9 Test 2] __DATA_CONST Write Revocation...\n";

    DarwinVmSpace vm_space(102);

    // Create synthetic segment layout simulating __TEXT, __DATA_CONST, __DATA
    UserspaceLoadResult load_res;
    load_res.status = UserspaceMachOLoadStatus::Success;
    load_res.aslr_slide = 0;

    // Segment 1: __TEXT at 0x100000000, 16KB, RX
    UserspaceSegment text_seg;
    text_seg.name = "__TEXT";
    text_seg.vmaddr = 0x100000000ULL;
    text_seg.vmsize = 0x4000ULL;
    text_seg.mapped_vaddr = 0x100000000ULL;
    auto text_obj = std::make_shared<DarwinVmObject>(0x4000, VmMappingType::FileBacked, false);
    (void)vm_space.map_object(text_obj, text_seg.mapped_vaddr, 0x4000,
                              kDarwinVmProtRead | kDarwinVmProtExecute, kDarwinMapFixed, 0);
    load_res.segments.push_back(text_seg);

    // Segment 2: __DATA_CONST at 0x100004000, 16KB, RW initially during fixups
    UserspaceSegment const_seg;
    const_seg.name = "__DATA_CONST";
    const_seg.vmaddr = 0x100004000ULL;
    const_seg.vmsize = 0x4000ULL;
    const_seg.mapped_vaddr = 0x100004000ULL;
    auto const_obj = std::make_shared<DarwinVmObject>(0x4000, VmMappingType::Anonymous, false);
    (void)vm_space.map_object(const_obj, const_seg.mapped_vaddr, 0x4000,
                              kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapFixed, 0);
    load_res.segments.push_back(const_seg);

    // Segment 3: __DATA at 0x100008000, 16KB, RW
    UserspaceSegment data_seg;
    data_seg.name = "__DATA";
    data_seg.vmaddr = 0x100008000ULL;
    data_seg.vmsize = 0x4000ULL;
    data_seg.mapped_vaddr = 0x100008000ULL;
    auto data_obj = std::make_shared<DarwinVmObject>(0x4000, VmMappingType::Anonymous, false);
    (void)vm_space.map_object(data_obj, data_seg.mapped_vaddr, 0x4000,
                              kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapFixed, 0);
    load_res.segments.push_back(data_seg);

    DyldEngine dyld;
    dyld.clear_modules();
    DyldLoadedModule lib;
    lib.name = "lib.dylib";
    lib.exports["_stub"] = 0x5555ULL;
    dyld.register_module(std::move(lib));

    // Bind into __DATA_CONST while writable
    std::vector<DyldBindingRecord> bindings = {
        {"_stub", 0x100004010ULL, 0, 1, false}
    };
    std::string err;
    bool ok = UserspaceMachOLoader::relocate_and_finalize_protections(
        vm_space, load_res, dyld, {}, bindings, err);

    TEST_CHECK(ok, "Finalization succeeded");

    // 1. Verify read succeeds and returns bound value
    std::uint64_t val = 0;
    TEST_CHECK(vm_space.mach_vm_read(0x100004010ULL, &val, sizeof(val)) == 0, "Read from __DATA_CONST succeeded");
    TEST_CHECK(val == 0x5555ULL, "Read value matches bound stub");

    // 2. Verify write permission is REVOKED on __DATA_CONST
    std::uint64_t rogue = 0xDEADBEEFULL;
    int write_rc = vm_space.mach_vm_write(0x100004010ULL, &rogue, sizeof(rogue));
    TEST_CHECK(write_rc != 0, "Write to __DATA_CONST after finalization MUST be rejected");

    // 3. Verify __DATA remains writable
    std::uint64_t data_write = 0xCAFEULL;
    TEST_CHECK(vm_space.mach_vm_write(0x100008020ULL, &data_write, sizeof(data_write)) == 0, "Write to __DATA succeeded");

    // 4. Verify __TEXT remains non-writable
    TEST_CHECK(vm_space.mach_vm_write(0x100000000ULL, &rogue, sizeof(rogue)) != 0, "Write to __TEXT rejected");

    std::cout << "  -> __DATA_CONST write revocation verified.\n";
}

int main() {
    std::cout << "=== WinIOS Stage 204.9 Loader Relocation and Protection Finalization Tests ===\n";
    test_relocation_and_protection_finalization();
    test_data_const_write_revocation();

    if (g_failed_tests == 0) {
        std::cout << "[PASS] Stage 204.9 Loader Relocation and Protection Finalization passed all tests.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.9 Loader Relocation and Protection Finalization failed with "
                  << g_failed_tests << " errors.\n";
        return 1;
    }
}
