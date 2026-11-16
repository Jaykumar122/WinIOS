#include <iostream>
#include <chrono>
#include <cstdint>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"
#include "runtime/kernel_boot_coordinator.h"
#include "loader/xnu_genuine_init_builder.h"

using namespace winios;
using namespace winios::memory;
using namespace winios::loader;
using namespace mmu;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_performance_instrumentation() {
    std::cout << "[Test 1] Stage 209.23: Performance & Micro-Benchmark Instrumentation...\n";

    // 1. Benchmark MMU Page Table Walk Throughput
    MMU mmu;
    Memory mem(0x800000);
    mmu.set_tcr_el1((0b10ULL << 14u) | (28ULL));
    mmu.set_ttbr0_el1(0x20000ULL);
    mmu.set_mair_el1(0x00000000000004FFULL);
    mem.write_u64(0x20000ULL, 0x0000000000000701ULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    const auto t0 = std::chrono::high_resolution_clock::now();
    std::size_t translations = 10000;
    TranslationFault fault{};
    for (std::size_t i = 0; i < translations; ++i) {
        auto tr = mmu.translate(mem, 0x4000ULL + ((i % 128) * 0x4000ULL), AccessKind::Read, 1, &fault);
        (void)tr;
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    const double dur_mmu_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    const double mmu_ns_per_op = (dur_mmu_us * 1000.0) / static_cast<double>(translations);

    std::cout << "  -> MMU Translation Latency: " << mmu_ns_per_op << " ns/lookup ("
              << (translations / (dur_mmu_us / 1000000.0)) << " lookups/sec)\n";
    TEST_CHECK(mmu_ns_per_op < 50000.0, "MMU translation latency within micro-benchmark envelope");

    // 2. Benchmark Physical Page Allocator Throughput
    XnuPhysicalPageAllocator alloc;
    alloc.initialize(0x0ULL, 0x800000ULL);
    alloc.reserve_range(0x0ULL, 0x40000ULL, "boot");

    const auto t2 = std::chrono::high_resolution_clock::now();
    for (int cycle = 0; cycle < 100; ++cycle) {
        auto p = alloc.allocate_pages(4);
        if (p.has_value()) {
            alloc.free_pages(p.value(), 4);
        }
    }
    const auto t3 = std::chrono::high_resolution_clock::now();
    const double dur_alloc_us = std::chrono::duration<double, std::micro>(t3 - t2).count();
    std::cout << "  -> Allocator Allocate/Free Cycle: " << (dur_alloc_us / 100.0) << " us/cycle\n";
    TEST_CHECK(dur_alloc_us < 100000.0, "Allocator operations execute under bound");

    // 3. Snapshot Serialization Throughput
    const auto t4 = std::chrono::high_resolution_clock::now();
    const auto snap = alloc.serialize();
    const auto t5 = std::chrono::high_resolution_clock::now();
    const double dur_snap_us = std::chrono::duration<double, std::micro>(t5 - t4).count();
    std::cout << "  -> Snapshot Serialization Latency: " << dur_snap_us << " us (" << snap.size() << " bytes)\n";
    TEST_CHECK(snap.size() > 0 && dur_snap_us < 50000.0, "Snapshot serialization completed under bound");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.23 Performance Instrumentation Tests ===\n";
    test_performance_instrumentation();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.23: All performance instrumentation tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.23: " << g_failures << " tests failed.\n";
        return 1;
    }
}
