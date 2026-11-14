// ============================================================================
// Stage 194.3: Guest Physical Memory Manager (16KB Page Allocator) Tests
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/macho_loader.h"
#include "loader/real_artifact_harness.h"
#include "loader/synthetic_kernel_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;

static constexpr const char* kArtifactPath = "tests/artifacts/bin/guest_physical_allocator";
static constexpr const char* kExpectedSha256 = "b51b7c0cc31d02a469b1b6dff7029dd8413bd8592650c2ad572cef388161f9e4";
static constexpr std::size_t kExpectedSize = 50888;

static std::string resolve_path(const std::string& rel) {
    const std::vector<std::string> candidates = {
        rel,
        "WinIOS/" + rel,
        "../" + rel,
        "../../" + rel,
        "D:/WinISO26/WinIOS/" + rel,
        "D:/WinISO26/" + rel
    };
    for (const auto& c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f.is_open()) return c;
    }
    return rel;
}

// -----------------------------------------------------------------------------
// Test 1: Physical Bitmap Initialization, Page Counting, and Range Reservation
// -----------------------------------------------------------------------------
static bool test_physical_bitmap_initialization_and_reservation() {
    Memory mem(0x400000ULL); // 4 MB guest memory
    const std::uint64_t managed_base = 0x0ULL;
    const std::uint64_t managed_size = 0x400000ULL; // 256 pages of 16KB
    const std::uint64_t bitmap_addr = 0x1000ULL;

    PhysicalPageManager ppm(mem, managed_base, managed_size, bitmap_addr);
    CHECK(ppm.initialize(false), "PPM initialization must succeed");

    auto stats = ppm.get_stats();
    CHECK(stats.total_pages == 256, "Total pages must be 256 for 4 MB");
    CHECK(stats.free_pages == 256, "Initial free pages must equal 256");
    CHECK(stats.allocated_pages == 0, "Initial allocated pages must be 0");
    CHECK(stats.bitmap_size_bytes == 32, "Bitmap size must be 32 bytes (256/8)");

    // Reserve first 64 KB (4 pages: 0, 1, 2, 3)
    CHECK(ppm.reserve_range(0x0, 0x10000), "Reserving first 64KB must succeed");

    stats = ppm.get_stats();
    CHECK(stats.allocated_pages == 4, "Allocated pages after reservation must be 4");
    CHECK(stats.free_pages == 252, "Free pages after reservation must be 252");

    CHECK(ppm.is_page_allocated(0x0), "Page 0 (0x0) must be marked allocated");
    CHECK(ppm.is_page_allocated(0x4000), "Page 1 (0x4000) must be marked allocated");
    CHECK(ppm.is_page_allocated(0x8000), "Page 2 (0x8000) must be marked allocated");
    CHECK(ppm.is_page_allocated(0xC000), "Page 3 (0xC000) must be marked allocated");
    CHECK(!ppm.is_page_allocated(0x10000), "Page 4 (0x10000) must be free");

    std::cout << "[PASS] test_physical_bitmap_initialization_and_reservation\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Contiguous Multi-Page Allocation, Freeing, and Re-allocation
// -----------------------------------------------------------------------------
static bool test_page_allocation_and_free_cycles() {
    Memory mem(0x400000ULL);
    PhysicalPageManager ppm(mem, 0x0, 0x400000ULL, 0x2000ULL);
    CHECK(ppm.initialize(false), "Initialize");
    ppm.reserve_range(0x0, 0x10000); // Reserve pages 0..3

    // Allocate 1 page
    const std::uint64_t p1 = ppm.allocate_page();
    CHECK(p1 == 0x10000, "First free page must be 0x10000 (page 4)");
    CHECK(ppm.is_page_allocated(p1), "Page 4 must be allocated");

    // Allocate 1 page
    const std::uint64_t p2 = ppm.allocate_page();
    CHECK(p2 == 0x14000, "Second free page must be 0x14000 (page 5)");

    // Allocate contiguous 4 pages
    const std::uint64_t block4 = ppm.allocate_pages(4);
    CHECK(block4 == 0x18000, "Contiguous 4-page block must start at 0x18000 (pages 6..9)");
    for (std::uint64_t i = 0; i < 4; ++i) {
        CHECK(ppm.is_page_allocated(block4 + (i * kPhysicalPageSize16KB)), "Block page must be allocated");
    }

    // Free p1 (0x10000)
    CHECK(ppm.free_page(p1), "Freeing p1 must succeed");
    CHECK(!ppm.is_page_allocated(p1), "p1 must now be free");

    // Re-allocate 1 page -> should reuse 0x10000
    const std::uint64_t reused = ppm.allocate_page();
    CHECK(reused == 0x10000, "Allocator must reuse lowest free page 0x10000");

    // Free 4-page block
    CHECK(ppm.free_pages(block4, 4), "Freeing 4-page block must succeed");
    for (std::uint64_t i = 0; i < 4; ++i) {
        CHECK(!ppm.is_page_allocated(block4 + (i * kPhysicalPageSize16KB)), "Block page must be free");
    }

    std::cout << "[PASS] test_page_allocation_and_free_cycles\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Allocation Exhaustion & Alignment Safety
// -----------------------------------------------------------------------------
static bool test_allocation_exhaustion_and_misaligned_frees() {
    Memory mem(0x100000ULL); // 1 MB = 64 pages
    PhysicalPageManager ppm(mem, 0x0, 0x100000ULL, 0x1000ULL);
    CHECK(ppm.initialize(false), "Initialize");

    // Allocate all 64 pages
    std::uint64_t block64 = ppm.allocate_pages(64);
    CHECK(block64 == 0x0, "Allocating entire 64-page pool must succeed starting at 0");

    auto stats = ppm.get_stats();
    CHECK(stats.free_pages == 0, "No free pages remaining");

    // Next allocation must fail gracefully (return 0)
    CHECK(ppm.allocate_page() == 0, "Allocation when full must return 0");
    CHECK(ppm.allocate_pages(2) == 0, "Multi-page allocation when full must return 0");

    // Misaligned free address must fail
    CHECK(!ppm.free_page(0x4001), "Misaligned free must fail");
    CHECK(!ppm.free_page(0x100), "Misaligned free must fail");

    // Out of bounds free must fail
    CHECK(!ppm.free_page(0x200000), "Out of bounds free must fail");

    std::cout << "[PASS] test_allocation_exhaustion_and_misaligned_frees\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Guest ARM64 EL1 Machine Code Allocator Routine Execution
// -----------------------------------------------------------------------------
static bool test_guest_arm64_alloc_routine_execution() {
    Memory mem(0x200000ULL);
    CPU cpu;
    cpu.set_architectural_exceptions(true);

    const std::uint64_t code_addr = 0x8000ULL;
    const std::uint64_t bitmap_addr = 0x10000ULL;
    const std::uint64_t total_pages = 64ULL; // 8 bytes bitmap

    // Clear bitmap in memory
    for (std::size_t i = 0; i < 8; ++i) {
        mem.write_u8(static_cast<std::size_t>(bitmap_addr + i), 0);
    }

    // Emit and write guest alloc routine into guest RAM
    auto alloc_code = PhysicalPageManager::emit_guest_alloc_routine();
    for (std::size_t i = 0; i < alloc_code.size(); ++i) {
        mem.write_u32(static_cast<std::size_t>(code_addr + (i * 4)), alloc_code[i]);
    }

    // Configure CPU at EL1 to execute guest alloc routine
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(code_addr);
    cpu.set_stack_pointer(0x70000ULL);
    cpu.write_register(0, bitmap_addr);
    cpu.write_register(1, total_pages);
    cpu.write_register(30, 0x9000ULL); // LR = halt address

    // Step CPU through guest allocation routine
    std::size_t steps = 0;
    while (cpu.get_program_counter() != 0x9000ULL && steps < 100) {
        cpu.step(mem);
        ++steps;
    }

    CHECK(cpu.get_program_counter() == 0x9000ULL, "Routine must return to LR");
    CHECK(cpu.read_register(0) == 0, "First allocated page must be page 0");
    CHECK(mem.read_u8(static_cast<std::size_t>(bitmap_addr)) == 0x01, "Bitmap bit 0 in guest RAM must be set to 1");

    std::cout << "[PASS] test_guest_arm64_alloc_routine_execution (Retired " << steps << " instructions)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Authentic Independently Compiled guest_physical_allocator Binary
// -----------------------------------------------------------------------------
static bool test_guest_physical_allocator_artifact_execution() {
    const std::string resolved = resolve_path(kArtifactPath);
    std::ifstream file(resolved, std::ios::binary | std::ios::ate);
    CHECK(file.is_open(), "Artifact file must exist on disk");

    const auto sz = static_cast<std::size_t>(file.tellg());
    CHECK(sz == kExpectedSize, "Artifact size must match 50,888 bytes");
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(sz);
    file.read(reinterpret_cast<char*>(bytes.data()), sz);

    const std::string sha = RealArtifactHarness::compute_sha256(bytes.data(), bytes.size());
    CHECK(sha == kExpectedSha256, "SHA-256 must match authentic value");

    Memory mem(0x400000ULL); // 4 MB guest memory
    MachOLoadResult load_res = MachOLoader::load_from_buffer(
        bytes.data(), bytes.size(), mem, 0x0ULL, 0x400000ULL);
    CHECK(load_res.is_success(), "Mach-O loader must map artifact successfully");

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(load_res.entry_point);
    cpu.set_stack_pointer(0x70000ULL);

    std::size_t steps = 0;
    while (steps < 500) {
        if (mem.read_u64(0x40028) == 1 && cpu.read_register(0) == 77) {
            break;
        }
        cpu.step(mem);
        ++steps;
    }

    CHECK(mem.read_u64(0x40028) == 1, "Completion flag at 0x40028 must be 1");
    CHECK(mem.read_u64(0x40000) == 0, "Page 0 allocated at index 0");
    CHECK(mem.read_u64(0x40008) == 1, "Page 1 allocated at index 1");
    CHECK(mem.read_u64(0x40010) == 2, "Page 2 allocated at index 2");
    CHECK(mem.read_u64(0x40018) == 0, "Page 1 freed successfully");
    CHECK(mem.read_u64(0x40020) == 1, "Page 1 re-allocated upon reuse");
    CHECK(cpu.read_register(0) == 77, "Exit status in X0 must be 77");
    CHECK(steps > 50, "Must retire genuine guest instructions");

    std::cout << "[PASS] test_guest_physical_allocator_artifact_execution (Retired " << steps << " instructions, SHA: " << sha << ")\n";
    return true;
}

int main() {
    std::cout << "Running Stage 194.3 Guest Physical Allocator Tests...\n";

    if (!test_physical_bitmap_initialization_and_reservation()) return 1;
    if (!test_page_allocation_and_free_cycles()) return 1;
    if (!test_allocation_exhaustion_and_misaligned_frees()) return 1;
    if (!test_guest_arm64_alloc_routine_execution()) return 1;
    if (!test_guest_physical_allocator_artifact_execution()) return 1;

    std::cout << "\nAll Stage 194.3 Guest Physical Allocator tests PASSED successfully.\n";
    return 0;
}
