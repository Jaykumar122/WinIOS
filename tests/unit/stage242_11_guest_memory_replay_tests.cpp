// ============================================================================
// Stage 242.11: Guest Memory Replay Hashing, State Contract & Perturbation Tests
// Verifies replay contract state hashing over guest memory, mapping metadata,
// and ensures a single 1-bit memory perturbation immediately triggers divergence.
// ============================================================================

#include "runtime/replay_state.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::runtime;
using namespace winios::memory;

static ReplayStateContract capture_state(UserAddressSpace& uas, const std::vector<uint64_t>& mapped_vas, Memory& mem) {
    (void)mem;
    ReplayStateContract st;
    st.cpu.pc = 0x10000;
    st.cpu.sp = 0x7FFF0000;
    st.cpu.instruction_count = 12500;
    st.cpu.exit_code = 0;
    st.cpu.gpr[0] = 42;

    st.memory.root_table_pa = uas.root_table_pa();
    st.memory.asid = uas.asid();

    std::vector<uint8_t> all_memory_bytes;
    for (uint64_t va : mapped_vas) {
        uint64_t pa = uas.translate(va);
        if (pa != 0) {
            std::vector<uint8_t> page_buf(0x4000);
            uas.copyout(reinterpret_cast<const void*>(va), page_buf.data(), page_buf.size());
            uint64_t phash = ReplayStateContract::fnv1a_hash(page_buf.data(), page_buf.size());
            st.memory.page_hashes.push_back({va, phash, 0x7});
            all_memory_bytes.insert(all_memory_bytes.end(), page_buf.begin(), page_buf.end());
        }
    }
    st.memory.overall_memory_hash = ReplayStateContract::fnv1a_hash(all_memory_bytes.data(), all_memory_bytes.size());
    return st;
}

static void test_replay_state_exact_match() {
    std::cout << "[Stage 242.11 - Test 1] Identical runs produce exact match...\n";
    Memory mem(0x2000000ULL);
    PhysicalPageManager ppm(mem, 0x100000ULL, 0x1000000ULL, 0x80000ULL);
    ppm.initialize();

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "rep_uas");
    uas.set_asid(15);

    uint64_t va = 0x20000;
    uint64_t pa = ppm.allocate_page();
    uas.map_user_page(va, pa, PagePermissions::UserReadWrite);

    std::vector<uint8_t> data(0x4000, 0x77);
    uas.copyin(va, data.data(), data.size());

    auto state1 = capture_state(uas, {va}, mem);
    auto state2 = capture_state(uas, {va}, mem);

    auto div = ReplayStateContract::compare(state1, state2);
    TEST_ASSERT(!div.has_diverged, "Identical states must not diverge");
}

static void test_one_bit_memory_perturbation_divergence() {
    std::cout << "[Stage 242.11 - Test 2] 1-bit guest memory perturbation detection...\n";
    Memory mem(0x2000000ULL);
    PhysicalPageManager ppm(mem, 0x100000ULL, 0x1000000ULL, 0x80000ULL);
    ppm.initialize();

    uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "rep_uas");
    uas.set_asid(16);

    uint64_t va = 0x20000;
    uint64_t pa = ppm.allocate_page();
    uas.map_user_page(va, pa, PagePermissions::UserReadWrite);

    std::vector<uint8_t> data(0x4000, 0x55);
    uas.copyin(va, data.data(), data.size());

    auto baseline_state = capture_state(uas, {va}, mem);

    // Perturb exactly 1 bit in guest memory at offset 0x123
    uint8_t byte_val = mem.read_u8(static_cast<size_t>(pa + 0x123));
    mem.write_u8(static_cast<size_t>(pa + 0x123), byte_val ^ 0x01); // Flip 1 bit

    auto perturbed_state = capture_state(uas, {va}, mem);

    auto div = ReplayStateContract::compare(baseline_state, perturbed_state);
    TEST_ASSERT(div.has_diverged, "1-bit perturbation must cause divergence");
    TEST_ASSERT(div.field_name == "memory.overall_memory_hash", "Divergence identified in memory hash");
}

static void test_serialization_roundtrip() {
    std::cout << "[Stage 242.11 - Test 3] Replay state contract serialization and deserialization...\n";
    ReplayStateContract orig;
    orig.cpu.gpr[0] = 0x1234;
    orig.cpu.pc = 0x8000;
    orig.cpu.instruction_count = 500;
    orig.memory.root_table_pa = 0x100000;
    orig.memory.overall_memory_hash = 0xAABBCCDDEEFF0011ULL;
    orig.memory.page_hashes.push_back({0x4000, 0x99998888, 0x3});
    orig.syscalls.push_back({1, 0x10, 0x20, 0x30, 0, 0});

    auto bytes = orig.serialize();
    TEST_ASSERT(!bytes.empty(), "Serialized bytes non-empty");

    auto restored_opt = ReplayStateContract::deserialize(bytes.data(), bytes.size());
    TEST_ASSERT(restored_opt.has_value(), "Deserialization succeeds");

    auto div = ReplayStateContract::compare(orig, *restored_opt);
    TEST_ASSERT(!div.has_diverged, "Restored state must match original");
}

int main() {
    std::cout << "=== Running Stage 242.11 Guest Memory Replay Tests ===\n";
    test_replay_state_exact_match();
    test_one_bit_memory_perturbation_divergence();
    test_serialization_roundtrip();
    std::cout << "Stage 242.11: All tests passed successfully.\n";
    return 0;
}
