#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "firmware/apple_boot_args.h"
#include "firmware/kernel_boot_args_builder.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::firmware;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_default_construction() {
    std::cout << "[Test 1] Default construction and sizing..." << std::endl;

    KernelBootArgsBuilder builder;
    auto args = builder.build();
    TEST_CHECK(args.Revision == kAppleBootArgsRevision2, "Revision == 2");
    TEST_CHECK(args.Version == kAppleBootArgsVersion2, "Version == 2");

    auto bytes = builder.serialize();
    TEST_CHECK(bytes.size() == 736, "Serialized size is exactly 736 bytes");

    std::cout << "  -> Default construction verified." << std::endl;
}

void test_builder_fluent_api_and_offsets() {
    std::cout << "[Test 2] Fluent builder population and binary offsets..." << std::endl;

    KernelBootArgsBuilder builder;
    builder.set_virt_base(0xFFFFFE0007004000ULL)
           .set_phys_base(0x800000000ULL)
           .set_mem_size(0x180000000ULL) // 6GB
           .set_top_of_kernel_data(0x810000000ULL)
           .set_device_tree(0x805000000ULL, 0x12340)
           .set_command_line("debug=0x144 -v keepsyms=1 serial=3")
           .set_boot_flags(0x800ULL)
           .set_machine_type(0x37)
           .set_video(0x880000000ULL, 1, 1179 * 4, 1179, 2556, 32);

    auto args = builder.build();
    TEST_CHECK(args.virtBase == 0xFFFFFE0007004000ULL, "virtBase");
    TEST_CHECK(args.physBase == 0x800000000ULL, "physBase");
    TEST_CHECK(args.memSize == 0x180000000ULL, "memSize");
    TEST_CHECK(args.memSizeActual == 0x180000000ULL, "memSizeActual");
    TEST_CHECK(args.topOfKernelData == 0x810000000ULL, "topOfKernelData");
    TEST_CHECK(args.deviceTreeP == 0x805000000ULL, "deviceTreeP");
    TEST_CHECK(args.deviceTreeLength == 0x12340, "deviceTreeLength");
    TEST_CHECK(std::string(args.CommandLine) == "debug=0x144 -v keepsyms=1 serial=3", "CommandLine");
    TEST_CHECK(args.bootFlags == 0x800ULL, "bootFlags");
    TEST_CHECK(args.machineType == 0x37, "machineType");
    TEST_CHECK(args.Video.v_baseAddr == 0x880000000ULL, "Video baseAddr");
    TEST_CHECK(args.Video.v_width == 1179, "Video width");
    TEST_CHECK(args.Video.v_height == 2556, "Video height");

    // Check byte offsets in serialized blob
    auto bytes = builder.serialize();
    TEST_CHECK(bytes.size() == 736, "Serialized size 736 bytes");

    std::uint16_t rev = 0;
    std::memcpy(&rev, &bytes[0], sizeof(rev));
    TEST_CHECK(rev == 2, "Offset 0 is Revision");

    std::uint16_t ver = 0;
    std::memcpy(&ver, &bytes[2], sizeof(ver));
    TEST_CHECK(ver == 2, "Offset 2 is Version");

    std::uint64_t vb = 0;
    std::memcpy(&vb, &bytes[8], sizeof(vb));
    TEST_CHECK(vb == 0xFFFFFE0007004000ULL, "Offset 8 is virtBase");

    std::uint64_t pb = 0;
    std::memcpy(&pb, &bytes[16], sizeof(pb));
    TEST_CHECK(pb == 0x800000000ULL, "Offset 16 is physBase");

    std::uint64_t ms = 0;
    std::memcpy(&ms, &bytes[24], sizeof(ms));
    TEST_CHECK(ms == 0x180000000ULL, "Offset 24 is memSize");

    std::uint64_t top = 0;
    std::memcpy(&top, &bytes[32], sizeof(top));
    TEST_CHECK(top == 0x810000000ULL, "Offset 32 is topOfKernelData");

    std::uint64_t dtp = 0;
    std::memcpy(&dtp, &bytes[96], sizeof(dtp));
    TEST_CHECK(dtp == 0x805000000ULL, "Offset 96 is deviceTreeP");

    std::uint32_t dtl = 0;
    std::memcpy(&dtl, &bytes[104], sizeof(dtl));
    TEST_CHECK(dtl == 0x12340, "Offset 104 is deviceTreeLength");

    const char* cmd = reinterpret_cast<const char*>(&bytes[108]);
    TEST_CHECK(std::string(cmd) == "debug=0x144 -v keepsyms=1 serial=3", "Offset 108 is CommandLine");

    std::uint64_t bf = 0;
    std::memcpy(&bf, &bytes[720], sizeof(bf));
    TEST_CHECK(bf == 0x800ULL, "Offset 720 is bootFlags");

    std::cout << "  -> Fluent builder and binary offsets verified." << std::endl;
}

void test_validation_logic() {
    std::cout << "[Test 3] Validation contracts..." << std::endl;

    std::string err;
    KernelBootArgsBuilder b_valid;
    b_valid.set_phys_base(0x800000000ULL)
           .set_mem_size(0x80000000ULL)
           .set_top_of_kernel_data(0x810000000ULL)
           .set_device_tree(0x801000000ULL, 0x1000);
    TEST_CHECK(b_valid.validate(&err), "Valid config passes");

    // Unaligned physBase fails
    KernelBootArgsBuilder b_no_phys;
    b_no_phys.set_phys_base(0x800000001ULL)
             .set_mem_size(0x80000000ULL);
    err.clear();
    TEST_CHECK(!b_no_phys.validate(&err), "Unaligned physBase fails");
    TEST_CHECK(!err.empty(), "Error reported");

    // Zero memSize fails
    KernelBootArgsBuilder b_no_mem;
    b_no_mem.set_phys_base(0x800000000ULL);
    err.clear();
    TEST_CHECK(!b_no_mem.validate(&err), "Zero memSize fails");

    // Out-of-bounds topOfKernelData fails
    KernelBootArgsBuilder b_oob_top;
    b_oob_top.set_phys_base(0x800000000ULL)
             .set_mem_size(0x10000000ULL) // 256MB -> range [0x800000000, 0x810000000]
             .set_top_of_kernel_data(0x820000000ULL); // 512MB offset (out of bounds)
    err.clear();
    TEST_CHECK(!b_oob_top.validate(&err), "OOB topOfKernelData fails");

    // Inconsistent device tree fails
    KernelBootArgsBuilder b_bad_dt;
    b_bad_dt.set_phys_base(0x800000000ULL)
            .set_mem_size(0x10000000ULL)
            .set_device_tree(0x805000000ULL, 0); // length is 0
    err.clear();
    TEST_CHECK(!b_bad_dt.validate(&err), "deviceTree with 0 length fails");

    std::cout << "  -> Validation contracts verified." << std::endl;
}

void test_memory_write() {
    std::cout << "[Test 4] Writing boot args to guest RAM..." << std::endl;

    Memory mem(0x100000); // 1 MB guest memory
    KernelBootArgsBuilder builder;
    builder.set_phys_base(0x10000ULL)
           .set_mem_size(0x80000ULL)
           .set_top_of_kernel_data(0x20000ULL)
           .set_command_line("-v debug=0x1");

    std::string err;
    TEST_CHECK(builder.write_to_memory(mem, 0x2000, &err), "Write at 0x2000 succeeds");

    // Read back and compare
    auto expected = builder.serialize();
    for (std::size_t i = 0; i < expected.size(); ++i) {
        TEST_CHECK(mem.read_u8(0x2000 + i) == expected[i], "Byte match at offset " + std::to_string(i));
    }

    // Unaligned write fails
    err.clear();
    TEST_CHECK(!builder.write_to_memory(mem, 0x2001, &err), "Unaligned write fails");
    TEST_CHECK(err.find("8-byte aligned") != std::string::npos, "Alignment error message");

    // OOB write fails
    err.clear();
    TEST_CHECK(!builder.write_to_memory(mem, 0x100000 - 128, &err), "OOB write fails");
    TEST_CHECK(err.find("exceeds guest memory") != std::string::npos, "OOB error message");

    std::cout << "  -> Memory writing verified." << std::endl;
}

} // namespace

int main() {
    std::cout << "=== Stage 205.12 Kernel Boot Arguments Tests ===" << std::endl;

    test_default_construction();
    test_builder_fluent_api_and_offsets();
    test_validation_logic();
    test_memory_write();

    if (g_failures > 0) {
        std::cerr << "FAILED: " << g_failures << " tests failed.\n";
        return 1;
    }

    std::cout << "All Stage 205.12 tests PASSED." << std::endl;
    return 0;
}
