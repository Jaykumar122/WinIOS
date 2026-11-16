// ==============================================================================
// WinIOS Campaign 202 Stage 23: Unified End-to-End Darwin Workload Tests
// ==============================================================================
// Synthesizes all Campaign 202 architectural subsystems into a single cohesive pipeline:
// 1. Mach-O dyld chained fixups (DYLD_CHAINED_PTR_ARM64E) & PAC signing
// 2. Mach IPC client/server rendezvous with mach_msg_overwrite & reply rights
// 3. ARM64 EL2 Stage-2 translation fault trap, syndrome validation & hypervisor mapping
// 4. Optimized zero-copy DMA ring batch execution (execute_ring_batch)
// 5. Memory signature commitment and cross-subsystem verification: 0x2020C0DE
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/gicv3.h"
#include "cpu/pointer_auth.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"
#include "loader/dyld_engine.h"
#include "memory/memory.h"
#include "memory/stage2_mmu.h"
#include "runtime/darwin_mach_ipc.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::cpu;
using namespace winios::runtime;
using namespace winios::mmu;
using namespace winios::device;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

constexpr std::uint64_t kCampaign202MagicSignature = 0x2020C0DEULL;

void test_unified_end_to_end_darwin_workload() {
    std::cout << "[Step 1] Initializing Guest Physical Memory & Subsystems...\n";

    constexpr std::size_t kRamSize = 16 * 1024 * 1024; // 16 MB guest RAM
    auto shared_mem = std::make_shared<SmpSharedMemory>(kRamSize, 4);
    Memory& mem = shared_mem->raw_memory();

    // Attach PAC Engine
    Arm64ePointerAuth pac;
    pac.set_key(PacKeyType::InstructionA, 0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL);

    // Attach GICv3 and Exclusive Monitor
    auto gic = std::make_shared<GICv3>();
    SmpExclusiveMonitorManager monitor(4);
    shared_mem->attach_exclusive_monitor(&monitor);

    std::cout << "[Step 2] Stage-O & dyld ARM64e Chained Fixups & Pointer Authentication...\n";

    DyldEngine dyld;
    dyld.clear_modules();
    dyld.set_pointer_auth(&pac);

    // Register dynamic library
    DyldLoadedModule libkernel;
    libkernel.name = "/usr/lib/system/libsystem_kernel.dylib";
    libkernel.install_name = "/usr/lib/system/libsystem_kernel.dylib";
    libkernel.base_address = 0x180000000ULL;
    libkernel.exports["_mach_msg_trap"] = 0x180001000ULL;
    dyld.register_module(libkernel);

    // Setup chained fixups at guest vaddr 0x40000
    const std::uint64_t binary_base_vaddr = 0x40000ULL;
    const std::uint64_t slide = 0x10000ULL;

    // Fixup 1: Auth Bind to _mach_msg_trap at 0x40020
    const std::uint64_t auth_bind = encode_dyld_chained_ptr_arm64e_auth_bind(
        0 /* ordinal */, 0x1111 /* diversity */, true /* addr_div */, 0 /* key IA */, 1 /* next */);

    // Fixup 2: Auth Rebase to internal handler at 0x40028
    const std::uint64_t auth_rebase = encode_dyld_chained_ptr_arm64e_auth_rebase(
        0x5000 /* target */, 0x2222 /* diversity */, false /* addr_div */, 0 /* key IA */, 0 /* next */);

    mem.write_u64(0x40020, auth_bind);
    mem.write_u64(0x40028, auth_rebase);

    DyldFixtureBuilder::SyntheticSegmentFixups seg;
    seg.segment_offset = binary_base_vaddr;
    seg.page_size = 0x4000;
    seg.pointer_format = DYLD_CHAINED_PTR_ARM64E;
    seg.page_starts = { 0x20 };

    std::vector<std::string> imports = { "_mach_msg_trap" };
    auto fixups_blob = DyldFixtureBuilder::create_synthetic_chained_fixups_with_segments(imports, { seg });

    std::string err;
    auto link_st = dyld.process_chained_fixups(fixups_blob.data(), fixups_blob.size(), mem, binary_base_vaddr, slide, err);
    TEST_CHECK(link_st == DyldLinkStatus::Success, "dyld chained fixups processing must succeed");

    // Verify PAC authenticated pointers in guest memory
    const std::uint64_t signed_bind_ptr = mem.read_u64(0x40020);
    const std::uint64_t signed_rebase_ptr = mem.read_u64(0x40028);

    TEST_CHECK((signed_bind_ptr >> 48u) != 0, "PAC signed bind pointer must contain non-zero PAC tag");
    TEST_CHECK((signed_rebase_ptr >> 48u) != 0, "PAC signed rebase pointer must contain non-zero PAC tag");

    const std::uint64_t bind_mod = 0x40020ULL ^ 0x1111ULL;
    const std::uint64_t authed_bind = pac.authenticate_pointer(signed_bind_ptr, bind_mod, PacKeyType::InstructionA);
    TEST_CHECK(!pac.has_fault(), "PAC auth for bind pointer must pass");
    TEST_CHECK(authed_bind == 0x180001000ULL, "Authed bind address must match _mach_msg_trap address");

    std::cout << "[Step 3] Mach IPC Rendezvous & Overwrite Trap Delivery...\n";

    MachIpcSubsystem ipc;
    constexpr std::uint32_t kServerTask = 10;
    constexpr std::uint32_t kClientTask = 20;

    auto srv_space = ipc.create_task_port_space(kServerTask);
    auto cli_space = ipc.create_task_port_space(kClientTask);

    auto srv_obj = std::make_shared<MachPortObject>(1001, kServerTask);
    mach_port_t srv_recv = srv_space->allocate_port(srv_obj, kMachPortRightReceive);
    TEST_CHECK(ipc.bootstrap_register(kServerTask, "com.apple.win.dmaservice", srv_recv),
               "Server must register service in launchd catalog");

    mach_port_t cli_srv = ipc.bootstrap_look_up(kClientTask, "com.apple.win.dmaservice");
    TEST_CHECK(cli_srv != kMachPortNull, "Client must resolve service port");

    mach_port_t cli_reply = ipc.mach_reply_port(kClientTask);
    TEST_CHECK(cli_reply != kMachPortNull, "Client reply port allocated");

    // Client prepares request message
    struct DmaWorkRequest {
        MachMsgHeaderRaw hdr;
        std::uint64_t    src_ipa;
        std::uint64_t    dst_ipa;
        std::uint32_t    transfer_size;
    };

    const std::uint64_t cli_req_vaddr = 0x60000ULL;
    const std::uint64_t srv_rcv_vaddr = 0x70000ULL;

    const std::uint64_t dma_src_ipa = 0x100000ULL;
    const std::uint64_t dma_dst_ipa = 0x200000ULL; // Will trigger Stage-2 translation fault!
    constexpr std::uint32_t kDmaBytes = 4096;

    DmaWorkRequest req{};
    req.hdr.msgh_size = sizeof(DmaWorkRequest);
    req.hdr.msgh_remote_port = cli_srv;
    req.hdr.msgh_local_port = cli_reply;
    req.hdr.msgh_id = 0x202;
    req.src_ipa = dma_src_ipa;
    req.dst_ipa = dma_dst_ipa;
    req.transfer_size = kDmaBytes;

    std::memcpy(mem.raw_data() + cli_req_vaddr, &req, sizeof(req));

    // Client sends request
    auto st_send = ipc.mach_msg(kClientTask, cli_req_vaddr, kMachSendMsg, sizeof(req), 0, 0, 0, mem);
    TEST_CHECK(st_send == kMachMsgSuccess, "Client message send must succeed");

    // Server receives request via mach_msg_overwrite
    auto st_rcv = ipc.mach_msg_overwrite(
        kServerTask,
        0,
        kMachRcvMsg,
        0,
        sizeof(DmaWorkRequest),
        srv_recv,
        0,
        mem,
        srv_rcv_vaddr,
        sizeof(DmaWorkRequest));
    TEST_CHECK(st_rcv == kMachMsgSuccess, "Server mach_msg_overwrite receive must succeed");

    DmaWorkRequest received_req{};
    std::memcpy(&received_req, mem.raw_data() + srv_rcv_vaddr, sizeof(received_req));
    TEST_CHECK(received_req.hdr.msgh_id == 0x202, "Received message ID must be 0x202");
    mach_port_t srv_reply_port = received_req.hdr.msgh_remote_port;
    TEST_CHECK(srv_reply_port != kMachPortNull, "Translated reply port must be non-null");

    std::cout << "[Step 4] EL2 Stage-2 Fault Injection, Trap, and Dynamic Hypervisor Remap...\n";

    Stage2TranslationEngine s2;
    // 16KB granule, Level 2 start, 48-bit IPA
    s2.set_vtcr((0b10ULL << 14u) | (0b01ULL << 6u) | 16ULL);
    const std::uint64_t s2_vttbr = 0x10000ULL;
    s2.set_vttbr((1ULL << 48u) | s2_vttbr); // VMID 1
    s2.set_hcr(0x1ULL); // Enable Stage 2

    // Inject Stage-2 translation fault on dma_dst_ipa (0x200000)
    Stage2FaultInjector injector;
    s2.set_fault_injector(&injector);

    Stage2InjectedFault fault_rule{};
    fault_rule.ipa = dma_dst_ipa;
    fault_rule.ipa_mask = 0xFFFFFFFFFFFFF000ULL;
    fault_rule.fault_type = ::mmu::MmuFaultType::Translation;
    fault_rule.level = 3;
    fault_rule.active = true;
    fault_rule.max_triggers = 1;
    injector.add_rule(fault_rule);

    // Initial translation attempt of destination IPA triggers fault
    Stage2Fault s2_fault{};
    auto trans_fail = s2.translate(mem, dma_dst_ipa, Stage2AccessType::Write, &s2_fault);
    TEST_CHECK(!trans_fail.has_value(), "Destination IPA translation must fault to EL2");

    // Hypervisor checks syndrome report
    const std::uint64_t guest_va = 0x0000700000200000ULL;
    auto syn = make_stage2_syndrome_report(s2_fault, guest_va);
    TEST_CHECK((syn.esr_el2 >> 26u) == 0x24u, "ESR_EL2 EC must be Data Abort");
    TEST_CHECK((syn.esr_el2 & (1u << 6u)) != 0, "ESR_EL2 WnR must be 1 for Write fault");
    TEST_CHECK(syn.far_el2 == guest_va, "FAR_EL2 must match guest VA");
    TEST_CHECK(syn.hpfar_el2 == ((dma_dst_ipa >> 8u) & 0x00000FFFFFFFF000ULL), "HPFAR_EL2 must match ipa >> 8");

    // Hypervisor establishes physical mapping in Stage-2 page table
    const std::uint64_t dma_src_pa = 0x300000ULL;
    const std::uint64_t dma_dst_pa = 0x400000ULL;

    TEST_CHECK(Stage2TranslationEngine::map_page_16k(mem, s2_vttbr, dma_src_ipa, dma_src_pa, true, true, true),
               "Map source page in Stage-2");
    TEST_CHECK(Stage2TranslationEngine::map_page_16k(mem, s2_vttbr, dma_dst_ipa, dma_dst_pa, true, true, true),
               "Map destination page in Stage-2");

    // Replay translation after hypervisor ERET
    Stage2Fault replay_fault{};
    auto trans_ok = s2.translate(mem, dma_dst_ipa, Stage2AccessType::Write, &replay_fault);
    TEST_CHECK(trans_ok.has_value(), "Replay translation must succeed");
    TEST_CHECK(trans_ok->physical_address == dma_dst_pa, "Translated PA must match backing destination PA");

    std::cout << "[Step 5] Optimized Zero-Copy DMA Ring Execution & Coherency...\n";

    // Seed source physical memory
    std::uint8_t* raw_mem = shared_mem->raw_memory().raw_data();
    for (std::uint32_t i = 0; i < kDmaBytes; ++i) {
        raw_mem[dma_src_pa + i] = static_cast<std::uint8_t>((0x42u + i * 3u) & 0xFFu);
    }
    std::memset(raw_mem + dma_dst_pa, 0, kDmaBytes);

    // Setup DMA Engine with Ring Buffer
    VirtualDmaEngine dma(shared_mem);
    dma.set_gic(gic, 48);

    const std::uint64_t ring_base_pa = 0x80000ULL;
    VirtualDmaDescriptor dma_desc{};
    dma_desc.descriptor_id = 99;
    dma_desc.src_paddr = dma_src_pa;
    dma_desc.dst_paddr = dma_dst_pa;
    dma_desc.length_bytes = kDmaBytes;
    dma_desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    dma_desc.flags = kDmaFlagInterruptOnComplete;

    std::memcpy(raw_mem + ring_base_pa, &dma_desc, sizeof(dma_desc));

    VirtualDmaRingConfig ring_cfg{};
    ring_cfg.ring_base_paddr = ring_base_pa;
    ring_cfg.ring_capacity = 4;
    ring_cfg.start_index = 0;
    ring_cfg.batch_count = 1;

    auto dma_res = dma.execute_ring_batch(ring_cfg);
    TEST_CHECK(dma_res.is_success(), "DMA ring batch execution must succeed");
    TEST_CHECK(dma_res.bytes_transferred == kDmaBytes, "Transferred bytes must match 4096");

    // Verify zero-copy destination memory contents
    TEST_CHECK(std::memcmp(raw_mem + dma_src_pa, raw_mem + dma_dst_pa, kDmaBytes) == 0,
               "DMA destination memory must match source memory");

    // Commit Campaign 202 magic signature into destination buffer
    *reinterpret_cast<std::uint64_t*>(raw_mem + dma_dst_pa) = kCampaign202MagicSignature;

    std::cout << "[Step 6] Final Reply Verification & Memory Signature Commit...\n";

    // Server sends response to client with committed signature
    struct DmaWorkResponse {
        MachMsgHeaderRaw hdr;
        std::uint64_t    status;
        std::uint64_t    signature;
    };

    const std::uint64_t srv_resp_vaddr = 0x90000ULL;
    const std::uint64_t cli_resp_vaddr = 0xA0000ULL;

    DmaWorkResponse resp{};
    resp.hdr.msgh_size = sizeof(DmaWorkResponse);
    resp.hdr.msgh_remote_port = srv_reply_port;
    resp.hdr.msgh_id = 0x203;
    resp.status = 0;
    resp.signature = *reinterpret_cast<std::uint64_t*>(raw_mem + dma_dst_pa);

    std::memcpy(raw_mem + srv_resp_vaddr, &resp, sizeof(resp));
    auto st_resp_send = ipc.mach_msg(kServerTask, srv_resp_vaddr, kMachSendMsg, sizeof(resp), 0, 0, 0, mem);
    TEST_CHECK(st_resp_send == kMachMsgSuccess, "Server reply send must succeed");

    // Client receives response
    auto st_resp_rcv = ipc.mach_msg(kClientTask, cli_resp_vaddr, kMachRcvMsg, 0, sizeof(DmaWorkResponse), cli_reply, 0, mem);
    TEST_CHECK(st_resp_rcv == kMachMsgSuccess, "Client reply receive must succeed");

    DmaWorkResponse cli_received_resp{};
    std::memcpy(&cli_received_resp, raw_mem + cli_resp_vaddr, sizeof(cli_received_resp));
    TEST_CHECK(cli_received_resp.hdr.msgh_id == 0x203, "Client received response ID 0x203");
    TEST_CHECK(cli_received_resp.signature == kCampaign202MagicSignature,
               "Client received committed Campaign 202 magic signature: 0x2020C0DE");

    std::cout << "  -> Unified End-to-End Darwin Workload passed with signature 0x"
              << std::hex << cli_received_resp.signature << std::dec << "!\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Running Stage 202.23 Unified End-to-End Darwin Workload Tests ===\n";
    test_unified_end_to_end_darwin_workload();

    if (g_failures != 0) {
        std::cerr << "Stage 202.23 tests FAILED with " << g_failures << " error(s).\n";
        return 1;
    }
    std::cout << "All Stage 202.23 tests passed successfully!\n";
    return 0;
}
