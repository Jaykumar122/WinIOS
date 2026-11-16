// ==============================================================================
// WinIOS Campaign 203 Stage 203.11: Synthetic VM Object Model Tests
// ==============================================================================
// Independent verification of:
// 1. 16KB Apple Silicon page-size alignment enforcement
// 2. VM object allocation, identification, and mapping types
// 3. Page read/write access integrity and boundary protection
// 4. Copy-on-Write shadow cloning and backing memory isolation
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "memory/darwin_vm_object.h"

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

// -----------------------------------------------------------------------------
// Test 1: 16KB Apple Page Alignment Enforcement
// -----------------------------------------------------------------------------
void test_page_alignment_enforcement() {
    std::cout << "[Stage 203.11 Test 1] 16KB Page Alignment Enforcement...\n";

    TEST_CHECK(kDarwinPageSize == 0x4000, "Darwin page size must be 16KB (0x4000)");
    TEST_CHECK(is_page_aligned(0x0), "0 is page-aligned");
    TEST_CHECK(is_page_aligned(0x4000), "0x4000 is page-aligned");
    TEST_CHECK(is_page_aligned(0x100000000ULL), "0x100000000 is page-aligned");
    TEST_CHECK(!is_page_aligned(0x1000), "0x1000 (4KB) is NOT page-aligned on Apple Silicon 16KB");
    TEST_CHECK(!is_page_aligned(0x4001), "0x4001 is NOT page-aligned");

    TEST_CHECK(align_page_up(1) == 0x4000, "1 byte rounds up to 16KB");
    TEST_CHECK(align_page_up(0x4000) == 0x4000, "16KB rounds up to 16KB");
    TEST_CHECK(align_page_up(0x4001) == 0x8000, "16KB + 1 byte rounds up to 32KB");

    DarwinVmObject obj(100);
    TEST_CHECK(obj.size() == 0x4000, "Object allocated with 100 bytes must be sized to 16KB");

    std::cout << "  -> 16KB page alignment verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: VM Object Identification and Mapping Types
// -----------------------------------------------------------------------------
void test_vm_object_types_and_ids() {
    std::cout << "[Stage 203.11 Test 2] VM Object Types and Identification...\n";

    DarwinVmObject obj_anon(0x4000, VmMappingType::Anonymous, false);
    DarwinVmObject obj_shared(0x8000, VmMappingType::Shared, true);

    TEST_CHECK(obj_anon.type() == VmMappingType::Anonymous, "Type is Anonymous");
    TEST_CHECK(!obj_anon.is_shared(), "obj_anon is not shared");
    TEST_CHECK(obj_shared.type() == VmMappingType::Shared, "Type is Shared");
    TEST_CHECK(obj_shared.is_shared(), "obj_shared is shared");

    TEST_CHECK(obj_anon.id() != 0, "obj_anon has non-zero ID");
    TEST_CHECK(obj_shared.id() > obj_anon.id(), "Object IDs must increment monotonically");

    std::cout << "  -> VM object types and IDs verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Data Access Integrity and Boundary Protection
// -----------------------------------------------------------------------------
void test_data_access_integrity() {
    std::cout << "[Stage 203.11 Test 3] Data Access Integrity...\n";

    DarwinVmObject obj(0x4000);

    const char test_str[] = "Darwin ARM64e Synthetic VM Object Subsystem";
    std::size_t len = sizeof(test_str);

    bool write_ok = obj.write_bytes(0x200, test_str, len);
    TEST_CHECK(write_ok, "Writing within bounds must succeed");

    char read_buf[64]{0};
    bool read_ok = obj.read_bytes(0x200, read_buf, len);
    TEST_CHECK(read_ok, "Reading within bounds must succeed");
    TEST_CHECK(std::strcmp(read_buf, test_str) == 0, "Read data must match written data");

    // Out-of-bounds protection:
    // Object size is 0x4000. Writing at 0x3FFE with len 10 extends beyond bounds.
    bool oob_write = obj.write_bytes(0x3FFE, test_str, len);
    TEST_CHECK(!oob_write, "Out-of-bounds write must be rejected");

    bool oob_read = obj.read_bytes(0x4000, read_buf, 4);
    TEST_CHECK(!oob_read, "Out-of-bounds read must be rejected");

    std::cout << "  -> Data access integrity verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Shadow COW Object Duplication
// -----------------------------------------------------------------------------
void test_shadow_cow_cloning() {
    std::cout << "[Stage 203.11 Test 4] Shadow COW Object Cloning...\n";

    DarwinVmObject original(0x4000);
    std::uint64_t initial_pattern = 0xDEADBEEFCAFEBABEULL;
    bool w1 = original.write_bytes(0x80, &initial_pattern, sizeof(initial_pattern));
    TEST_CHECK(w1, "Initial write must succeed");

    // Clone for COW
    auto cow_clone = original.clone_for_cow();
    TEST_CHECK(cow_clone != nullptr, "COW clone creation must succeed");
    TEST_CHECK(cow_clone->type() == VmMappingType::CopyOnWrite, "Clone type is CopyOnWrite");
    TEST_CHECK(cow_clone->size() == original.size(), "Clone size matches original");

    // Verify clone contains original data
    std::uint64_t clone_val = 0;
    bool r1 = cow_clone->read_bytes(0x80, &clone_val, sizeof(clone_val));
    TEST_CHECK(r1 && clone_val == initial_pattern, "Clone data matches original data initially");

    // Mutate clone
    std::uint64_t new_pattern = 0x1122334455667788ULL;
    bool w2 = cow_clone->write_bytes(0x80, &new_pattern, sizeof(new_pattern));
    TEST_CHECK(w2, "Clone write must succeed");

    // Verify original remains unmodified
    std::uint64_t orig_val = 0;
    bool r2 = original.read_bytes(0x80, &orig_val, sizeof(orig_val));
    TEST_CHECK(r2 && orig_val == initial_pattern, "Original memory must NOT be mutated by clone write");

    // Verify clone reflects mutation
    bool r3 = cow_clone->read_bytes(0x80, &clone_val, sizeof(clone_val));
    TEST_CHECK(r3 && clone_val == new_pattern, "Clone reflects its own independent write");

    std::cout << "  -> Shadow COW cloning verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.11: Synthetic VM Object Model Tests\n";
    std::cout << "==================================================================\n";

    test_page_alignment_enforcement();
    test_vm_object_types_and_ids();
    test_data_access_integrity();
    test_shadow_cow_cloning();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.11 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
