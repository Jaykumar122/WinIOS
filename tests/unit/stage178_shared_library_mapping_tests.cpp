// =============================================================================
// stage178_shared_library_mapping_tests.cpp
//
// Stage 178: Shared Library Mapping and Runtime
// Verifies multi-image mapping in a single guest address space, disjoint
// dylib arena allocation, ASLR slide application, segment collision detection,
// virtual address to image/segment lookup, and image unmapping lifecycle.
// =============================================================================

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "loader/macho_corpus_builder.h"
#include "loader/shared_library_mapper.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (false)

using namespace winios::loader;

// -----------------------------------------------------------------------------
// Test 1: Main Executable Mapping
// -----------------------------------------------------------------------------
bool test_main_executable_mapping() {
    std::cout << "[Test 1] Main Executable Mapping with ASLR Slide...\n";

    auto hello = MachOCorpusRegistry::create_hello_world();
    TEST_ASSERT(!hello.binary_data.empty(), "corpus_hello_world artifact must exist");

    Memory mem(0x100000);
    SharedLibraryMapper mapper;

    std::uint64_t slide = 0x10000ULL;
    std::string err;
    auto status = mapper.map_main_executable(hello.binary_data.data(), hello.binary_data.size(), mem, slide, err);

    TEST_ASSERT(status == SharedLibMapStatus::Success, err.c_str());
    TEST_ASSERT(mapper.mapped_count() == 1, "Mapped count must be 1");

    const auto* main_img = mapper.find_image("main_executable");
    TEST_ASSERT(main_img != nullptr, "Must find main_executable record");
    TEST_ASSERT(main_img->is_main_executable, "Must be flagged as main executable");
    TEST_ASSERT(main_img->aslr_slide == slide, "ASLR slide must match");

    std::cout << "  Passed (Mapped main executable with ASLR slide)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Dylib Automatic Address Allocation
// -----------------------------------------------------------------------------
bool test_dylib_automatic_address_allocation() {
    std::cout << "[Test 2] Dylib Automatic Address Allocation in Arena...\n";

    auto math = MachOCorpusRegistry::create_math_compute();
    TEST_ASSERT(!math.binary_data.empty(), "corpus_math_compute artifact must exist");

    Memory mem(0x200000);
    SharedLibraryMapper mapper(0x180000000ULL, 0x40000000ULL);

    std::string err;
    auto status = mapper.map_dylib(
        math.binary_data.data(), math.binary_data.size(),
        "/usr/lib/libMath.dylib", mem, std::nullopt, 0, err);

    TEST_ASSERT(status == SharedLibMapStatus::Success, err.c_str());
    TEST_ASSERT(mapper.mapped_count() == 1, "Mapped count must be 1");

    const auto* img = mapper.find_image("/usr/lib/libMath.dylib");
    TEST_ASSERT(img != nullptr, "Must find libMath.dylib");
    TEST_ASSERT(!img->is_main_executable, "Dylib must not be flagged as main executable");
    TEST_ASSERT(img->base_vaddr >= 0x180000000ULL, "Base vaddr must be within dylib arena");

    std::cout << "  Passed (Allocated dylib address in arena >= 0x180000000)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Dylib Explicit Base Mapping
// -----------------------------------------------------------------------------
bool test_dylib_explicit_base_mapping() {
    std::cout << "[Test 3] Dylib Explicit Base Mapping...\n";

    auto file_io = MachOCorpusRegistry::create_file_io();
    TEST_ASSERT(!file_io.binary_data.empty(), "corpus_file_io artifact must exist");

    Memory mem(0x200000);
    SharedLibraryMapper mapper;

    std::uint64_t req_base = 0x1A0000000ULL;
    std::uint64_t slide = 0x4000ULL;
    std::string err;
    auto status = mapper.map_dylib(
        file_io.binary_data.data(), file_io.binary_data.size(),
        "/usr/lib/libFileIO.dylib", mem, req_base, slide, err);

    TEST_ASSERT(status == SharedLibMapStatus::Success, err.c_str());
    const auto* img = mapper.find_image("/usr/lib/libFileIO.dylib");
    TEST_ASSERT(img != nullptr, "Must find libFileIO.dylib");
    TEST_ASSERT(img->base_vaddr == req_base + slide, "Base vaddr must equal requested + slide");

    std::cout << "  Passed (Mapped dylib at explicit requested base + slide)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Segment Collision Detection
// -----------------------------------------------------------------------------
bool test_segment_collision_detection() {
    std::cout << "[Test 4] Segment Collision Detection...\n";

    auto math = MachOCorpusRegistry::create_math_compute();
    Memory mem(0x200000);
    SharedLibraryMapper mapper;

    std::string err;
    // Map first copy at 0x180000000
    auto status = mapper.map_dylib(
        math.binary_data.data(), math.binary_data.size(),
        "lib1.dylib", mem, 0x180000000ULL, 0, err);
    TEST_ASSERT(status == SharedLibMapStatus::Success, err.c_str());

    // Attempt to map second copy at the EXACT same virtual address range -> collision
    status = mapper.map_dylib(
        math.binary_data.data(), math.binary_data.size(),
        "lib2.dylib", mem, 0x180000000ULL, 0, err);
    TEST_ASSERT(status == SharedLibMapStatus::AddressCollision, "Must detect address collision");

    std::cout << "  Passed (Detected and prevented segment collision)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Find Image and Segment by Virtual Address
// -----------------------------------------------------------------------------
bool test_find_image_and_segment_by_vaddr() {
    std::cout << "[Test 5] Query Image and Segment by Guest Virtual Address...\n";

    auto hello = MachOCorpusRegistry::create_hello_world();
    Memory mem(0x200000);
    SharedLibraryMapper mapper;

    std::string err;
    auto status = mapper.map_main_executable(hello.binary_data.data(), hello.binary_data.size(), mem, 0x8000ULL, err);
    TEST_ASSERT(status == SharedLibMapStatus::Success, err.c_str());

    const auto* main_img = mapper.find_image("main_executable");
    TEST_ASSERT(main_img != nullptr, "Main image must exist");

    // Pick an address inside a mapped non-pagezero segment
    std::uint64_t target_vaddr = 0;
    for (const auto& s : main_img->segments) {
        if (!s.is_pagezero && s.vmsize > 0) {
            target_vaddr = s.mapped_vaddr + 16;
            break;
        }
    }
    TEST_ASSERT(target_vaddr > 0, "Must have mapped segment");

    const auto* found_img = mapper.find_image_at(target_vaddr);
    TEST_ASSERT(found_img != nullptr && found_img->path == "main_executable", "Image lookup failed");

    const auto* found_seg = mapper.find_segment_at(target_vaddr);
    TEST_ASSERT(found_seg != nullptr, "Segment lookup failed");

    // Address in unmapped territory must return nullptr
    TEST_ASSERT(mapper.find_image_at(0x0000DEADBEEF0000ULL) == nullptr, "Unmapped addr must return null");

    std::cout << "  Passed (Found image and segment at target virtual address)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Image Unmapping Lifecycle
// -----------------------------------------------------------------------------
bool test_unmap_image_lifecycle() {
    std::cout << "[Test 6] Image Unmapping and Memory Reclamation...\n";

    auto math = MachOCorpusRegistry::create_math_compute();
    Memory mem(0x100000);
    SharedLibraryMapper mapper;

    std::string err;
    auto status = mapper.map_dylib(
        math.binary_data.data(), math.binary_data.size(),
        "libToUnmap.dylib", mem, 0x180000000ULL, 0, err);
    TEST_ASSERT(status == SharedLibMapStatus::Success, err.c_str());
    TEST_ASSERT(mapper.mapped_count() == 1, "Count must be 1 before unmap");

    // Unmap
    status = mapper.unmap_image("libToUnmap.dylib", mem, err);
    TEST_ASSERT(status == SharedLibMapStatus::Success, err.c_str());
    TEST_ASSERT(mapper.mapped_count() == 0, "Count must be 0 after unmap");
    TEST_ASSERT(mapper.find_image("libToUnmap.dylib") == nullptr, "Image must no longer be found");

    // Unmapping again must report ImageNotFound
    status = mapper.unmap_image("libToUnmap.dylib", mem, err);
    TEST_ASSERT(status == SharedLibMapStatus::ImageNotFound, "Must report ImageNotFound");

    std::cout << "  Passed (Unmapped image and reclaimed mapper state)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Multi-Image Address Space Isolation
// -----------------------------------------------------------------------------
bool test_multi_image_address_space_isolation() {
    std::cout << "[Test 7] Multi-Image Address Space Coexistence...\n";

    auto exe = MachOCorpusRegistry::create_hello_world();
    auto lib1 = MachOCorpusRegistry::create_math_compute();
    auto lib2 = MachOCorpusRegistry::create_file_io();

    Memory mem(0x400000);
    SharedLibraryMapper mapper(0x180000000ULL, 0x80000000ULL);

    std::string err;
    // Map main executable
    auto status = mapper.map_main_executable(exe.binary_data.data(), exe.binary_data.size(), mem, 0x10000ULL, err);
    TEST_ASSERT(status == SharedLibMapStatus::Success, err.c_str());

    // Map lib1 automatically
    status = mapper.map_dylib(lib1.binary_data.data(), lib1.binary_data.size(), "/usr/lib/lib1.dylib", mem, std::nullopt, 0, err);
    TEST_ASSERT(status == SharedLibMapStatus::Success, err.c_str());

    // Map lib2 automatically
    status = mapper.map_dylib(lib2.binary_data.data(), lib2.binary_data.size(), "/usr/lib/lib2.dylib", mem, std::nullopt, 0, err);
    TEST_ASSERT(status == SharedLibMapStatus::Success, err.c_str());

    TEST_ASSERT(mapper.mapped_count() == 3, "Must have 3 mapped images simultaneously");

    const auto* img0 = mapper.find_image("main_executable");
    const auto* img1 = mapper.find_image("/usr/lib/lib1.dylib");
    const auto* img2 = mapper.find_image("/usr/lib/lib2.dylib");

    TEST_ASSERT(img0 != nullptr && img1 != nullptr && img2 != nullptr, "All 3 images must exist");
    // Verify distinct base addresses
    TEST_ASSERT(img0->base_vaddr != img1->base_vaddr, "Image 0 and 1 must have distinct base addresses");
    TEST_ASSERT(img1->base_vaddr != img2->base_vaddr, "Image 1 and 2 must have distinct base addresses");

    std::cout << "  Passed (Coexisted 3 images in non-overlapping address spaces)\n";
    return true;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "  STAGE 178: SHARED LIBRARY MAPPING AND RUNTIME SUITE\n";
    std::cout << "================================================================\n";

    int passed = 0;
    int failed = 0;

    if (test_main_executable_mapping()) passed++; else failed++;
    if (test_dylib_automatic_address_allocation()) passed++; else failed++;
    if (test_dylib_explicit_base_mapping()) passed++; else failed++;
    if (test_segment_collision_detection()) passed++; else failed++;
    if (test_find_image_and_segment_by_vaddr()) passed++; else failed++;
    if (test_unmap_image_lifecycle()) passed++; else failed++;
    if (test_multi_image_address_space_isolation()) passed++; else failed++;

    std::cout << "----------------------------------------------------------------\n";
    std::cout << "Stage 178 Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "================================================================\n";

    return (failed == 0) ? 0 : 1;
}
