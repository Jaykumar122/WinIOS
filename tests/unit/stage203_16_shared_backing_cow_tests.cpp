// ==============================================================================
// WinIOS Campaign 203 Stage 203.16: Shared Backing & COW Exploration Tests
// ==============================================================================
// Independent verification of:
// 1. Inter-process shared memory mapping updates (MAP_SHARED / map_object)
// 2. Fork Copy-On-Write (COW) page duplication upon write fault
// 3. Complete memory isolation between Parent and Child after COW break
// 4. Preservation of MAP_SHARED semantics across clone_for_fork (no COW break)
// 5. Multi-generation COW branch isolation (Parent -> Child1 -> Child2)
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
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
// Test 1: Inter-Process Shared Backing Updates
// -----------------------------------------------------------------------------
void test_shared_backing_across_processes() {
    std::cout << "[Stage 203.16 Test 1] Inter-Process Shared Backing (MAP_SHARED)...\n";

    DarwinVmSpace proc_a(501, 0x100000000ULL);
    DarwinVmSpace proc_b(502, 0x200000000ULL);

    // Create shared VM object
    auto shm_obj = std::make_shared<DarwinVmObject>(0x4000, VmMappingType::Shared, true);

    std::uint64_t addr_a = proc_a.map_object(
        shm_obj, 0, 0x4000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapShared);
    std::uint64_t addr_b = proc_b.map_object(
        shm_obj, 0, 0x4000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapShared);

    TEST_CHECK(addr_a != 0, "Process A mapping must succeed");
    TEST_CHECK(addr_b != 0, "Process B mapping must succeed");

    const DarwinVmEntry* e_a = proc_a.find_entry(addr_a);
    const DarwinVmEntry* e_b = proc_b.find_entry(addr_b);
    TEST_CHECK(e_a && e_b, "Both entries must exist");
    TEST_CHECK(e_a->object->id() == e_b->object->id(), "Both processes share identical object ID");

    // Proc A writes signature
    std::uint8_t sig_a[16]{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    TEST_CHECK(proc_a.mach_vm_write(addr_a, sig_a, sizeof(sig_a)) == kKernSuccess, "Proc A write");

    // Proc B reads and verifies signature immediately
    std::uint8_t read_b[16]{0};
    TEST_CHECK(proc_b.mach_vm_read(addr_b, read_b, sizeof(read_b)) == kKernSuccess, "Proc B read");
    TEST_CHECK(std::memcmp(read_b, sig_a, sizeof(sig_a)) == 0, "Proc B observes Proc A write immediately");

    // Proc B writes new data at offset 32
    std::uint8_t sig_b[8]{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
    TEST_CHECK(proc_b.mach_vm_write(addr_b + 32, sig_b, sizeof(sig_b)) == kKernSuccess, "Proc B write at offset 32");

    // Proc A reads and verifies Proc B write
    std::uint8_t read_a[8]{0};
    TEST_CHECK(proc_a.mach_vm_read(addr_a + 32, read_a, sizeof(read_a)) == kKernSuccess, "Proc A read at offset 32");
    TEST_CHECK(std::memcmp(read_a, sig_b, sizeof(sig_b)) == 0, "Proc A observes Proc B write immediately");

    std::cout << "  -> Shared backing updates verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Fork Copy-On-Write (COW) Isolation
// -----------------------------------------------------------------------------
void test_cow_fork_and_write_isolation() {
    std::cout << "[Stage 203.16 Test 2] Fork Copy-On-Write (COW) & Process Isolation...\n";

    DarwinVmSpace parent(510, 0x100000000ULL);

    // Allocate 2 private pages (0x8000)
    std::uint64_t addr = parent.mmap(
        0, 0x8000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapAnonymous | kDarwinMapPrivate);
    TEST_CHECK(addr != 0, "Parent allocation must succeed");

    std::uint8_t p0_init[8]{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
    std::uint8_t p1_init[8]{0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22};

    TEST_CHECK(parent.mach_vm_write(addr, p0_init, sizeof(p0_init)) == kKernSuccess, "Parent write P0");
    TEST_CHECK(parent.mach_vm_write(addr + 0x4000, p1_init, sizeof(p1_init)) == kKernSuccess, "Parent write P1");

    // Fork: clone_for_fork
    auto child = parent.clone_for_fork(511);
    TEST_CHECK(child != nullptr, "Child space cloned");

    const DarwinVmEntry* parent_e = parent.find_entry(addr);
    const DarwinVmEntry* child_e = child->find_entry(addr);
    TEST_CHECK(parent_e && child_e, "Both parent and child have mapping entry");

    // Before write: both marked COW, sharing same object ID
    TEST_CHECK(parent_e->is_cow, "Parent entry marked COW");
    TEST_CHECK(child_e->is_cow, "Child entry marked COW");
    std::uint64_t shared_obj_id = parent_e->object->id();
    TEST_CHECK(child_e->object->id() == shared_obj_id, "Both point to initial shared object");

    // Child writes to P0 -> triggers COW break
    std::uint8_t p0_child[8]{0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99};
    int cwr = child->mach_vm_write(addr, p0_child, sizeof(p0_child));
    TEST_CHECK(cwr == kKernSuccess, "Child write to P0 succeeds");

    // Re-query child entry after write
    child_e = child->find_entry(addr);
    TEST_CHECK(!child_e->is_cow, "Child is_cow reset to false after COW write");
    TEST_CHECK(child_e->object->id() != shared_obj_id, "Child has distinct cloned object ID");

    // Child reads its new value
    std::uint8_t read_buf[8]{0};
    TEST_CHECK(child->mach_vm_read(addr, read_buf, sizeof(read_buf)) == kKernSuccess, "Child read P0");
    TEST_CHECK(std::memcmp(read_buf, p0_child, sizeof(p0_child)) == 0, "Child sees its new value");

    // CRITICAL: Parent MUST STILL read its original value (isolation preserved!)
    TEST_CHECK(parent.mach_vm_read(addr, read_buf, sizeof(read_buf)) == kKernSuccess, "Parent read P0");
    TEST_CHECK(std::memcmp(read_buf, p0_init, sizeof(p0_init)) == 0, "Parent memory unchanged by child write");

    // Parent still sees P1 initial value
    TEST_CHECK(parent.mach_vm_read(addr + 0x4000, read_buf, sizeof(read_buf)) == kKernSuccess, "Parent read P1");
    TEST_CHECK(std::memcmp(read_buf, p1_init, sizeof(p1_init)) == 0, "Parent P1 intact");

    // Child also retained P1 initial value from copy
    TEST_CHECK(child->mach_vm_read(addr + 0x4000, read_buf, sizeof(read_buf)) == kKernSuccess, "Child read P1");
    TEST_CHECK(std::memcmp(read_buf, p1_init, sizeof(p1_init)) == 0, "Child P1 intact from initial copy");

    std::cout << "  -> Fork COW isolation verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Shared Mapping Preservation Across Fork (No COW Break)
// -----------------------------------------------------------------------------
void test_shared_mapping_preservation_across_fork() {
    std::cout << "[Stage 203.16 Test 3] MAP_SHARED Preservation Across Fork...\n";

    DarwinVmSpace parent(520, 0x100000000ULL);

    // Allocate shared mapping
    std::uint64_t addr = parent.mmap(
        0, 0x4000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapShared);
    TEST_CHECK(addr != 0, "Parent mmap MAP_SHARED succeeds");

    std::uint8_t init_sig[8]{0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33};
    TEST_CHECK(parent.mach_vm_write(addr, init_sig, sizeof(init_sig)) == kKernSuccess, "Parent write");

    // Fork
    auto child = parent.clone_for_fork(521);
    const DarwinVmEntry* pe = parent.find_entry(addr);
    const DarwinVmEntry* ce = child->find_entry(addr);

    // MAP_SHARED regions must NOT be marked COW!
    TEST_CHECK(!pe->is_cow, "Parent shared region not marked COW");
    TEST_CHECK(!ce->is_cow, "Child shared region not marked COW");
    TEST_CHECK(pe->object->id() == ce->object->id(), "Both point to exact same object");

    // Child writes to shared region
    std::uint8_t child_sig[8]{0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
    TEST_CHECK(child->mach_vm_write(addr, child_sig, sizeof(child_sig)) == kKernSuccess, "Child write to shared");

    // Parent must see child's write immediately (shared backing!)
    std::uint8_t parent_read[8]{0};
    TEST_CHECK(parent.mach_vm_read(addr, parent_read, sizeof(parent_read)) == kKernSuccess, "Parent read shared");
    TEST_CHECK(std::memcmp(parent_read, child_sig, sizeof(child_sig)) == 0, "Parent observes child write to shared memory");

    std::cout << "  -> MAP_SHARED preservation across fork verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Multi-Generation COW Hierarchy (Parent -> Child1 -> Child2)
// -----------------------------------------------------------------------------
void test_multi_generation_cow() {
    std::cout << "[Stage 203.16 Test 4] Multi-Generation COW Hierarchy...\n";

    DarwinVmSpace parent(530, 0x100000000ULL);
    std::uint64_t addr = parent.mmap(
        0, 0x4000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapAnonymous | kDarwinMapPrivate);
    TEST_CHECK(addr != 0, "Parent allocate");

    std::uint8_t base_data[4]{0x01, 0x02, 0x03, 0x04};
    TEST_CHECK(parent.mach_vm_write(addr, base_data, sizeof(base_data)) == kKernSuccess, "Parent write");

    // Generation 1: Child 1
    auto child1 = parent.clone_for_fork(531);

    // Generation 2: Child 2 (from Child 1)
    auto child2 = child1->clone_for_fork(532);

    // Modify Child 2 only
    std::uint8_t c2_data[4]{0xAA, 0xBB, 0xCC, 0xDD};
    TEST_CHECK(child2->mach_vm_write(addr, c2_data, sizeof(c2_data)) == kKernSuccess, "Child 2 write");

    // Child 2 sees new data
    std::uint8_t read_buf[4]{0};
    TEST_CHECK(child2->mach_vm_read(addr, read_buf, sizeof(read_buf)) == kKernSuccess, "Child 2 read");
    TEST_CHECK(std::memcmp(read_buf, c2_data, sizeof(c2_data)) == 0, "Child 2 sees c2_data");

    // Parent and Child 1 both still see base_data!
    TEST_CHECK(parent.mach_vm_read(addr, read_buf, sizeof(read_buf)) == kKernSuccess, "Parent read");
    TEST_CHECK(std::memcmp(read_buf, base_data, sizeof(base_data)) == 0, "Parent retains base_data");

    TEST_CHECK(child1->mach_vm_read(addr, read_buf, sizeof(read_buf)) == kKernSuccess, "Child 1 read");
    TEST_CHECK(std::memcmp(read_buf, base_data, sizeof(base_data)) == 0, "Child 1 retains base_data");

    std::cout << "  -> Multi-generation COW hierarchy verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.16: Shared Backing & COW Exploration Tests\n";
    std::cout << "==================================================================\n";

    test_shared_backing_across_processes();
    test_cow_fork_and_write_isolation();
    test_shared_mapping_preservation_across_fork();
    test_multi_generation_cow();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.16 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
