// ==============================================================================
// WinIOS Campaign 203 Stage 203.22: TLS and Thread Runtime Foundation Tests
// ==============================================================================
// Independent verification of:
// 1. Darwin pthread control block structure, TSD slot layout and alignments
// 2. TPIDRRO_EL0 and TPIDR_EL0 system register emulation and EL0/EL1 privilege rules
// 3. Synthetic instruction execution for pthread_self() and *__error()
// 4. Mach-O Thread Local Variable (TLV) descriptor resolution
// 5. Multi-thread context switching and TLS register isolation
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "runtime/darwin_thread_runtime.h"
#include "cpu/cpu.h"
#include "memory/memory.h"

using namespace winios::runtime;
using namespace winios;

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
// Test 1: Darwin Pthread Structure and TSD Offset Layout
// -----------------------------------------------------------------------------
void test_darwin_pthread_structure_offsets() {
    std::cout << "[Stage 203.22 Test 1] Darwin Pthread Structure and TSD Offset Layout...\n";

    TEST_CHECK(sizeof(DarwinPthreadHeader) == 184, "DarwinPthreadHeader size is exactly 184 bytes");

    Memory mem(0x100000); // 1MB memory
    const std::uint64_t pthread_va = 0x20000ULL;
    const std::uint64_t tid = 101ULL;
    const std::uint64_t stack_base = 0x10000ULL;
    const std::uint64_t stack_size = 0x8000ULL;
    const std::uint32_t mig_reply_port = 0x103;
    const std::uint32_t mach_thread_self = 0x203;
    const std::uint64_t tls_base = 0x30000ULL;
    const std::uint64_t tls_size = 0x1000ULL;

    bool ok = DarwinThreadRuntime::initialize_thread_structure(
        mem,
        pthread_va,
        tid,
        stack_base,
        stack_size,
        mig_reply_port,
        mach_thread_self,
        tls_base,
        tls_size);
    TEST_CHECK(ok, "initialize_thread_structure succeeds");

    DarwinPthreadHeader hdr{};
    bool r_ok = DarwinThreadRuntime::read_pthread_header(mem, pthread_va, hdr);
    TEST_CHECK(r_ok, "read_pthread_header succeeds");

    // Verify TSD Slots
    TEST_CHECK(hdr.tsd_slots[kDarwinTsdThreadSelf] == pthread_va, "TSD[0] points to pthread_t self");
    TEST_CHECK(hdr.tsd_slots[kDarwinTsdErrno] == (pthread_va + offsetof(DarwinPthreadHeader, errno_val)),
               "TSD[1] points to errno_val integer");
    TEST_CHECK(hdr.tsd_slots[kDarwinTsdMigReplyPort] == mig_reply_port, "TSD[2] holds MIG reply port");
    TEST_CHECK(hdr.tsd_slots[kDarwinTsdMachThreadSelf] == mach_thread_self, "TSD[3] holds Mach thread self");
    TEST_CHECK(hdr.tsd_slots[kDarwinTsdThreadQos] == 0x21, "TSD[4] holds default interactive QoS class");

    // Verify Control Fields
    TEST_CHECK(hdr.magic == kDarwinPthreadMagic, "Pthread magic header matches");
    TEST_CHECK(hdr.thread_id == tid, "Thread ID matches");
    TEST_CHECK(hdr.stack_base == stack_base, "Stack base matches");
    TEST_CHECK(hdr.stack_size == stack_size, "Stack size matches");
    TEST_CHECK(hdr.errno_val == 0, "Initial errno is 0");
    TEST_CHECK(hdr.tls_base == tls_base, "TLS base VA matches");
    TEST_CHECK(hdr.tls_size == tls_size, "TLS size matches");

    // Test updating errno
    bool e_ok = DarwinThreadRuntime::set_thread_errno(mem, pthread_va, 22); // EINVAL
    TEST_CHECK(e_ok, "set_thread_errno succeeds");

    // Read back through errno pointer slot
    std::uint64_t errno_ptr = mem.read_u64(pthread_va + 8);
    std::uint32_t current_errno = mem.read_u32(static_cast<std::size_t>(errno_ptr));
    TEST_CHECK(current_errno == 22, "Errno read back through pointer slot equals 22 (EINVAL)");

    std::cout << "  -> Darwin pthread structure offsets verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: System Register TPIDRRO_EL0 and TPIDR_EL0 Emulation & Privileges
// -----------------------------------------------------------------------------
void test_sysreg_tpidr_emulation_and_privileges() {
    std::cout << "[Stage 203.22 Test 2] TPIDRRO_EL0 and TPIDR_EL0 Privileges and Emulation...\n";

    CPU cpu;
    const std::uint64_t pthread_va = 0x20000ULL;
    const std::uint64_t tls_base = 0x30000ULL;

    DarwinThreadRuntime::bind_cpu_thread(cpu, pthread_va, tls_base);
    TEST_CHECK(cpu.get_tpidrro_el0() == pthread_va, "cpu.get_tpidrro_el0() holds pthread_va");
    TEST_CHECK(cpu.get_tpidr_el0() == tls_base, "cpu.get_tpidr_el0() holds tls_base");

    // Instruction encodings:
    // MRS X0, TPIDRRO_EL0: 0xD53BD060
    // MSR TPIDRRO_EL0, X0: 0xD51BD060
    // MRS X1, TPIDR_EL0:   0xD53BD041
    // MSR TPIDR_EL0, X1:   0xD51BD041

    Memory mem(0x10000);
    cpu.set_current_el(0); // Userspace EL0
    cpu.set_architectural_exceptions(true);

    // 1. MRS X0, TPIDRRO_EL0 at EL0 -> Must succeed
    mem.write_u32(0x1000, 0xD53BD060u); // mrs x0, TPIDRRO_EL0
    cpu.set_program_counter(0x1000);
    cpu.write_register(0, 0);
    cpu.step(mem);
    TEST_CHECK(cpu.read_register(0) == pthread_va, "EL0 MRS X0, TPIDRRO_EL0 reads correct pthread_t");

    // 2. MSR TPIDRRO_EL0, X0 at EL0 -> Must be rejected (read-only at EL0)
    mem.write_u32(0x1004, 0xD51BD060u); // msr TPIDRRO_EL0, x0
    cpu.set_program_counter(0x1004);
    cpu.write_register(0, 0xBADF00DULL);
    bool threw_exception = false;
    try {
        cpu.step(mem);
    } catch (...) {
        threw_exception = true;
    }
    // The CPU either raises an exception or branches to vector, but tpidrro_el0 must NOT be updated
    TEST_CHECK(cpu.get_tpidrro_el0() == pthread_va, "EL0 write attempt to TPIDRRO_EL0 does NOT alter register");

    // 3. MRS and MSR TPIDR_EL0 at EL0 -> Read/Write allowed
    cpu.set_current_el(0);
    cpu.write_register(1, 0x5555AAAAULL);
    mem.write_u32(0x1008, 0xD51BD041u); // msr TPIDR_EL0, x1
    cpu.set_program_counter(0x1008);
    cpu.step(mem);
    TEST_CHECK(cpu.get_tpidr_el0() == 0x5555AAAAULL, "EL0 MSR TPIDR_EL0, X1 successfully updates TPIDR_EL0");

    mem.write_u32(0x100C, 0xD53BD042u); // mrs x2, TPIDR_EL0
    cpu.set_program_counter(0x100C);
    cpu.step(mem);
    TEST_CHECK(cpu.read_register(2) == 0x5555AAAAULL, "EL0 MRS X2, TPIDR_EL0 reads updated value");

    // 4. MSR TPIDRRO_EL0 at EL1 (Kernel privilege) -> Allowed
    cpu.set_current_el(1);
    cpu.write_register(0, 0x60000ULL);
    mem.write_u32(0x1010, 0xD51BD060u); // msr TPIDRRO_EL0, x0
    cpu.set_program_counter(0x1010);
    cpu.step(mem);
    TEST_CHECK(cpu.get_tpidrro_el0() == 0x60000ULL, "EL1 kernel successfully configures TPIDRRO_EL0");

    std::cout << "  -> System register TPIDR privileges and emulation verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Synthetic Guest Instruction Sequences (pthread_self and *__error)
// -----------------------------------------------------------------------------
void test_guest_instruction_sequences() {
    std::cout << "[Stage 203.22 Test 3] Synthetic Guest Instruction Sequences...\n";

    Memory mem(0x100000);
    CPU cpu;

    const std::uint64_t pthread_va = 0x40000ULL;
    bool init_ok = DarwinThreadRuntime::initialize_thread_structure(
        mem, pthread_va, 42, 0x10000, 0x8000, 0, 0, 0x50000, 0x1000);
    TEST_CHECK(init_ok, "initialize_thread_structure succeeds");
    DarwinThreadRuntime::bind_cpu_thread(cpu, pthread_va, 0x50000);
    bool err_ok = DarwinThreadRuntime::set_thread_errno(mem, pthread_va, 34); // ERANGE
    TEST_CHECK(err_ok, "set_thread_errno succeeds");

    cpu.set_current_el(0);

    // Sequence A: pthread_self()
    // 0x1000: mrs x0, TPIDRRO_EL0 (0xD53BD060)
    // 0x1004: ldr x0, [x0, #0]    (0xF9400000)
    mem.write_u32(0x1000, 0xD53BD060u);
    mem.write_u32(0x1004, 0xF9400000u);

    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    TEST_CHECK(cpu.read_register(0) == pthread_va, "Instruction 1: MRS loads TPIDRRO_EL0 into X0");

    cpu.step(mem);
    TEST_CHECK(cpu.read_register(0) == pthread_va, "Instruction 2: LDR X0, [X0] dereferences self-pointer");

    // Sequence B: *__error()
    // 0x2000: mrs x0, TPIDRRO_EL0 (0xD53BD060)
    // 0x2004: ldr x1, [x0, #8]    (0xF9400401)  (ldr x1, [x0, #8])
    // 0x2008: ldr w0, [x1, #0]    (0xB9400020)  (ldr w0, [x1, #0])
    mem.write_u32(0x2000, 0xD53BD060u);
    mem.write_u32(0x2004, 0xF9400401u);
    mem.write_u32(0x2008, 0xB9400020u);

    cpu.set_program_counter(0x2000);
    cpu.step(mem);
    cpu.step(mem);
    std::uint64_t errno_ptr = cpu.read_register(1);
    TEST_CHECK(errno_ptr == (pthread_va + offsetof(DarwinPthreadHeader, errno_val)), "X1 holds errno pointer");

    cpu.step(mem);
    TEST_CHECK(cpu.read_register(0) == 34, "X0 holds dereferenced errno 34 (ERANGE)");

    std::cout << "  -> Guest instruction sequences verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Mach-O Thread Local Variable (TLV) Descriptor Resolution
// -----------------------------------------------------------------------------
void test_macho_tlv_resolution() {
    std::cout << "[Stage 203.22 Test 4] Mach-O TLV Descriptor Resolution...\n";

    Memory mem(0x100000);
    CPU cpu;

    const std::uint64_t pthread_va = 0x20000ULL;
    const std::uint64_t tls_base = 0x60000ULL;
    bool tlv_init_ok = DarwinThreadRuntime::initialize_thread_structure(
        mem, pthread_va, 99, 0x10000, 0x8000, 0, 0, tls_base, 0x1000);
    TEST_CHECK(tlv_init_ok, "initialize_thread_structure succeeds");
    DarwinThreadRuntime::bind_cpu_thread(cpu, pthread_va, tls_base);

    // 1. TLV Descriptor with Key 0 (TPIDR_EL0 based)
    const std::uint64_t tlv_desc_va = 0x70000ULL;
    DarwinTlvDescriptorRaw desc0{};
    desc0.thunk = 0x1000ULL;
    desc0.key = 0; // TPIDR_EL0 relative
    desc0.offset = 0x28ULL; // Variable at tls_base + 0x28 = 0x60028
    std::memcpy(mem.raw_data() + tlv_desc_va, &desc0, sizeof(desc0));

    // Write a test value at the target variable location
    mem.write_u64(static_cast<std::size_t>(tls_base + 0x28), 0xFEEDFACECAFEBEEFULL);

    std::uint64_t resolved_addr0 = DarwinThreadRuntime::resolve_tlv_address(cpu, mem, tlv_desc_va);
    TEST_CHECK(resolved_addr0 == 0x60028ULL, "Resolved address matches tls_base + offset");
    TEST_CHECK(mem.read_u64(static_cast<std::size_t>(resolved_addr0)) == 0xFEEDFACECAFEBEEFULL,
               "Read back correct value from resolved TLV address");

    // 2. TLV Descriptor with Key 1 (TPIDRRO_EL0 based, accessing thread_id at offset 0x88)
    const std::uint64_t tlv_desc_va1 = 0x70020ULL;
    DarwinTlvDescriptorRaw desc1{};
    desc1.thunk = 0x1000ULL;
    desc1.key = 1; // TPIDRRO_EL0 relative
    desc1.offset = offsetof(DarwinPthreadHeader, thread_id);
    std::memcpy(mem.raw_data() + tlv_desc_va1, &desc1, sizeof(desc1));

    std::uint64_t resolved_addr1 = DarwinThreadRuntime::resolve_tlv_address(cpu, mem, tlv_desc_va1);
    TEST_CHECK(resolved_addr1 == (pthread_va + offsetof(DarwinPthreadHeader, thread_id)),
               "Resolved address matches pthread_va + thread_id offset");
    TEST_CHECK(mem.read_u64(static_cast<std::size_t>(resolved_addr1)) == 99,
               "Read back TID 99 from resolved TLV address");

    std::cout << "  -> Mach-O TLV descriptor resolution verified.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Multi-Thread Context Switching and TLS Register Isolation
// -----------------------------------------------------------------------------
void test_thread_context_switching_isolation() {
    std::cout << "[Stage 203.22 Test 5] Multi-Thread Context Switching and Isolation...\n";

    Memory mem(0x100000);
    CPU cpu;

    // Thread 1
    const std::uint64_t pthread1_va = 0x20000ULL;
    const std::uint64_t tls1_base = 0x30000ULL;
    bool t1_init_ok = DarwinThreadRuntime::initialize_thread_structure(
        mem, pthread1_va, 101, 0x10000, 0x8000, 0x11, 0x21, tls1_base, 0x1000);
    TEST_CHECK(t1_init_ok, "Thread 1 initialize_thread_structure succeeds");

    // Thread 2
    const std::uint64_t pthread2_va = 0x40000ULL;
    const std::uint64_t tls2_base = 0x50000ULL;
    bool t2_init_ok = DarwinThreadRuntime::initialize_thread_structure(
        mem, pthread2_va, 102, 0x38000, 0x8000, 0x12, 0x22, tls2_base, 0x1000);
    TEST_CHECK(t2_init_ok, "Thread 2 initialize_thread_structure succeeds");

    // Switch to Thread 1
    DarwinThreadRuntime::bind_cpu_thread(cpu, pthread1_va, tls1_base);
    TEST_CHECK(cpu.get_tpidrro_el0() == pthread1_va, "Thread 1 TPIDRRO_EL0 active");
    TEST_CHECK(cpu.get_tpidr_el0() == tls1_base, "Thread 1 TPIDR_EL0 active");

    // Modify Thread 1 TLS user pointer at EL0
    cpu.set_tpidr_el0(0x30100ULL);

    // Context switch to Thread 2
    std::uint64_t t1_saved_tpidr_el0 = cpu.get_tpidr_el0();
    DarwinThreadRuntime::bind_cpu_thread(cpu, pthread2_va, tls2_base);
    TEST_CHECK(cpu.get_tpidrro_el0() == pthread2_va, "Thread 2 TPIDRRO_EL0 active");
    TEST_CHECK(cpu.get_tpidr_el0() == tls2_base, "Thread 2 TPIDR_EL0 active");

    // Context switch back to Thread 1
    DarwinThreadRuntime::bind_cpu_thread(cpu, pthread1_va, t1_saved_tpidr_el0);
    TEST_CHECK(cpu.get_tpidrro_el0() == pthread1_va, "Thread 1 restored TPIDRRO_EL0");
    TEST_CHECK(cpu.get_tpidr_el0() == 0x30100ULL, "Thread 1 restored modified TPIDR_EL0");

    std::cout << "  -> Multi-thread context switching verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.22: TLS and Thread Runtime Foundation Tests\n";
    std::cout << "==================================================================\n";

    test_darwin_pthread_structure_offsets();
    test_sysreg_tpidr_emulation_and_privileges();
    test_guest_instruction_sequences();
    test_macho_tlv_resolution();
    test_thread_context_switching_isolation();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.22 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
