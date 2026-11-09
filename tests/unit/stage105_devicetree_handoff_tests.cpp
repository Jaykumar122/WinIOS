#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include "device/apple_devicetree.h"
#include "device/apple_devicetree_handoff.h"
#include "firmware/apple_boot_args.h"
#include "memory/memory.h"
#include "runtime/apple_kernel_execution_engine.h"

using namespace winios::device;
using namespace winios::firmware;
using namespace winios::runtime;

#define STAGE105_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] FAILED: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// -----------------------------------------------------------------------------
// Test 1: Staging Contract Validation
// -----------------------------------------------------------------------------
bool test_staging_contract_validation() {
    std::cout << "[RUN] Test 1: Staging contract validation ... ";
    std::string err;

    AppleDeviceTreeStagingContract valid_contract;
    valid_contract.dtb_phys = 0x48000000ULL;
    valid_contract.dtb_len = 0x4000;
    valid_contract.ram_base = 0x40000000ULL;
    valid_contract.ram_size = 0x40000000ULL; // 1 GB RAM
    valid_contract.kernel_phys_start = 0x40000000ULL;
    valid_contract.kernel_phys_top   = 0x41000000ULL;
    valid_contract.boot_args_phys    = 0x43F80000ULL;
    valid_contract.boot_args_size    = 1152;
    valid_contract.framebuffer_phys  = 0x50000000ULL; // Outside or at high RAM
    valid_contract.framebuffer_size  = 0x1000000;

    STAGE105_ASSERT(AppleDeviceTreeHandoffManager::validate_staging_contract(valid_contract, &err),
                    "Valid contract must pass");

    // Null pointer
    auto c_null = valid_contract;
    c_null.dtb_phys = 0;
    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_staging_contract(c_null, &err),
                    "Null dtb_phys must fail");

    // Zero length
    auto c_zero = valid_contract;
    c_zero.dtb_len = 0;
    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_staging_contract(c_zero, &err),
                    "Zero dtb_len must fail");

    // Header size smaller than 8 bytes
    auto c_small = valid_contract;
    c_small.dtb_len = 7;
    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_staging_contract(c_small, &err),
                    "Length < 8 must fail");

    // Unaligned pointer (not 8-byte aligned)
    auto c_unaligned = valid_contract;
    c_unaligned.dtb_phys = 0x48000004ULL;
    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_staging_contract(c_unaligned, &err),
                    "Non-8-byte aligned pointer must fail");

    // Address overflow
    auto c_overflow = valid_contract;
    c_overflow.dtb_phys = 0xFFFFFFFFFFFFFFF8ULL;
    c_overflow.dtb_len = 0x1000;
    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_staging_contract(c_overflow, &err),
                    "Overflowing dtb address must fail");

    // Outside RAM
    auto c_out = valid_contract;
    c_out.dtb_phys = 0x90000000ULL;
    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_staging_contract(c_out, &err),
                    "Address outside physical RAM must fail");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Overlap Prevention with Kernel, BootArgs and Framebuffer
// -----------------------------------------------------------------------------
bool test_staging_contract_overlap_prevention() {
    std::cout << "[RUN] Test 2: Overlap prevention ... ";
    std::string err;

    AppleDeviceTreeStagingContract contract;
    contract.dtb_phys = 0x40800000ULL; // Inside kernel [0x40000000, 0x41000000)
    contract.dtb_len = 0x4000;
    contract.ram_base = 0x40000000ULL;
    contract.ram_size = 0x40000000ULL;
    contract.kernel_phys_start = 0x40000000ULL;
    contract.kernel_phys_top   = 0x41000000ULL;
    contract.boot_args_phys    = 0x43F80000ULL;
    contract.boot_args_size    = 1152;
    contract.framebuffer_phys  = 0x42000000ULL;
    contract.framebuffer_size  = 0x100000;

    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_staging_contract(contract, &err),
                    "Overlap with kernel must be rejected");
    STAGE105_ASSERT(err.find("kernel") != std::string::npos, "Error must mention kernel");

    // Overlap with boot_args
    contract.dtb_phys = 0x43F80000ULL;
    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_staging_contract(contract, &err),
                    "Overlap with boot_args must be rejected");
    STAGE105_ASSERT(err.find("boot_args") != std::string::npos, "Error must mention boot_args");

    // Overlap with framebuffer
    contract.dtb_phys = 0x42000000ULL;
    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_staging_contract(contract, &err),
                    "Overlap with framebuffer must be rejected");
    STAGE105_ASSERT(err.find("framebuffer") != std::string::npos, "Error must mention framebuffer");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Staged Memory Validation Success
// -----------------------------------------------------------------------------
bool test_staged_memory_validation_success() {
    std::cout << "[RUN] Test 3: Staged memory validation success ... ";
    std::string err;

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    Memory mem(0x10000000ULL); // 256 MB buffer offset from 0x40000000 -> size 0x10000000
    // Memory starts at 0 internally (size 256MB). Let's use offset 0x08000000 (128MB) for dtb_phys
    const std::size_t mem_offset = 0x04000000;
    std::memcpy(mem.raw_data() + mem_offset, dtb.data(), dtb.size());

    STAGE105_ASSERT(AppleDeviceTreeHandoffManager::validate_staged_memory(mem, mem_offset, static_cast<std::uint32_t>(dtb.size()), &err),
                    "Valid staged DTB in memory must pass");
    STAGE105_ASSERT(err.empty(), "Error should be empty");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Staged Memory Truncation Detection
// -----------------------------------------------------------------------------
bool test_staged_memory_truncation_detection() {
    std::cout << "[RUN] Test 4: Staged memory truncation detection ... ";
    std::string err;

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    Memory mem(0x1000000);
    std::memcpy(mem.raw_data(), dtb.data(), dtb.size());

    // Truncate length by 16 bytes
    const std::uint32_t truncated_len = static_cast<std::uint32_t>(dtb.size()) - 16;
    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_staged_memory(mem, 0, truncated_len, &err),
                    "Truncated DTB must be rejected");
    STAGE105_ASSERT(err.find("exceeds") != std::string::npos || err.find("truncated") != std::string::npos,
                    "Error must describe truncation or size limit");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Staged Memory Corrupted Property Header
// -----------------------------------------------------------------------------
bool test_staged_memory_corrupted_property_header() {
    std::cout << "[RUN] Test 5: Corrupted property header rejection ... ";
    std::string err;

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    Memory mem(0x1000000);
    std::memcpy(mem.raw_data(), dtb.data(), dtb.size());

    // Property header offset = 8 (after root nProperties and nChildren). Length is at offset 8 + 32 = 40.
    // Overwrite property length with a huge value
    mem.write_u32(40, 0x0FFFFFFF);

    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_staged_memory(mem, 0, static_cast<std::uint32_t>(dtb.size()), &err),
                    "Corrupted property length exceeding stream must be rejected");
    STAGE105_ASSERT(err.find("exceeds") != std::string::npos, "Error must describe payload exceeding DTB");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Staged Memory Excessive Header Counts
// -----------------------------------------------------------------------------
bool test_staged_memory_corrupted_header_counts() {
    std::cout << "[RUN] Test 6: Excessive header count rejection ... ";
    std::string err;

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    Memory mem(0x1000000);
    std::memcpy(mem.raw_data(), dtb.data(), dtb.size());

    // Overwrite nProperties with 5000 (> 1024)
    mem.write_u32(0, 5000);

    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_staged_memory(mem, 0, static_cast<std::uint32_t>(dtb.size()), &err),
                    "Excessive property count must be rejected");
    STAGE105_ASSERT(err.find("Excessive") != std::string::npos, "Error must describe excessive count");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: iPhone 15 Required Nodes Verification
// -----------------------------------------------------------------------------
bool test_iphone15_required_nodes_verification() {
    std::cout << "[RUN] Test 7: iPhone 15 required nodes verification ... ";
    std::string err;

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    auto root = AppleDeviceTreeBuilder::deserialize(dtb.data(), dtb.size(), &err);
    STAGE105_ASSERT(root != nullptr, "Deserialization must succeed");

    STAGE105_ASSERT(AppleDeviceTreeHandoffManager::validate_required_iphone15_nodes(*root, &err),
                    "Standard iPhone 15 DTB must satisfy all required nodes");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: iPhone 15 Model Validation Failure
// -----------------------------------------------------------------------------
bool test_iphone15_model_validation_failure() {
    std::cout << "[RUN] Test 8: Non-iPhone15 model rejection ... ";
    std::string err;

    AppleDeviceTreeNode root("");
    root.add_property_string("name", "");
    root.add_property_string("model", "iPad13,1"); // Wrong model
    root.add_property_string("target-type", "D37AP");
    root.add_property_u32("chip-id", 0x8120u);

    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_required_iphone15_nodes(root, &err),
                    "Non-iPhone15 model must be rejected");
    STAGE105_ASSERT(err.find("iPhone15,4") != std::string::npos, "Error must mention iPhone15,4");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: iPhone 15 Target Type Failure
// -----------------------------------------------------------------------------
bool test_iphone15_target_type_failure() {
    std::cout << "[RUN] Test 9: Non-D37AP target-type rejection ... ";
    std::string err;

    AppleDeviceTreeNode root("");
    root.add_property_string("name", "");
    root.add_property_string("model", "iPhone15,4");
    root.add_property_string("target-type", "D73AP"); // Wrong board for iPhone 15
    root.add_property_u32("chip-id", 0x8120u);

    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_required_iphone15_nodes(root, &err),
                    "Non-D37AP target-type must be rejected");
    STAGE105_ASSERT(err.find("D37AP") != std::string::npos, "Error must mention D37AP");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: iPhone 15 Chip ID Failure
// -----------------------------------------------------------------------------
bool test_iphone15_chip_id_failure() {
    std::cout << "[RUN] Test 10: Non-T8120 chip-id rejection ... ";
    std::string err;

    AppleDeviceTreeNode root("");
    root.add_property_string("name", "");
    root.add_property_string("model", "iPhone15,4");
    root.add_property_string("target-type", "D37AP");
    root.add_property_u32("chip-id", 0x8030u); // A13 instead of A16 (0x8120)

    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_required_iphone15_nodes(root, &err),
                    "Non-0x8120 chip-id must be rejected");
    STAGE105_ASSERT(err.find("0x8120") != std::string::npos, "Error must mention 0x8120");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 11: iPhone 15 CPU Topology Validation (A16 6-Core)
// -----------------------------------------------------------------------------
bool test_iphone15_cpu_topology_validation() {
    std::cout << "[RUN] Test 11: A16 6-core topology validation ... ";
    std::string err;

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    auto root = AppleDeviceTreeBuilder::deserialize(dtb.data(), dtb.size());
    STAGE105_ASSERT(root != nullptr, "Tree must deserialize");

    const auto* cpus = root->find_path("/cpus");
    STAGE105_ASSERT(cpus != nullptr, "Must find /cpus");
    STAGE105_ASSERT(cpus->children().size() == 6, "A16 must have 6 cores");

    // Performance cores
    const auto* c0 = cpus->find_child("cpu0");
    const auto* c1 = cpus->find_child("cpu1");
    STAGE105_ASSERT(c0 && c1, "Must find cpu0 and cpu1");
    STAGE105_ASSERT(std::string(reinterpret_cast<const char*>(c0->find_property("compatible")->data.data())).find("everest") != std::string::npos,
                    "cpu0 is Everest");

    // Efficiency cores
    const auto* c2 = cpus->find_child("cpu2");
    const auto* c5 = cpus->find_child("cpu5");
    STAGE105_ASSERT(c2 && c5, "Must find cpu2 and cpu5");
    STAGE105_ASSERT(std::string(reinterpret_cast<const char*>(c2->find_property("compatible")->data.data())).find("sawtooth") != std::string::npos,
                    "cpu2 is Sawtooth");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 12: iPhone 15 Peripheral Validation (AIC 2, PMGR, UART0, DISP0)
// -----------------------------------------------------------------------------
bool test_iphone15_peripheral_validation() {
    std::cout << "[RUN] Test 12: iPhone 15 peripheral bus validation ... ";
    std::string err;

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    auto root = AppleDeviceTreeBuilder::deserialize(dtb.data(), dtb.size());
    STAGE105_ASSERT(root != nullptr, "Tree must deserialize");

    const auto* armio = root->find_path("/arm-io");
    STAGE105_ASSERT(armio != nullptr, "Must find /arm-io");
    STAGE105_ASSERT(armio->find_child("aic") != nullptr, "Must find /arm-io/aic");
    STAGE105_ASSERT(armio->find_child("pmgr") != nullptr, "Must find /arm-io/pmgr");
    STAGE105_ASSERT(armio->find_child("uart0") != nullptr, "Must find /arm-io/uart0");
    STAGE105_ASSERT(armio->find_child("disp0") != nullptr, "Must find /arm-io/disp0");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 13: Guest CPU Boot Contract Execution Reading DTB Pointer
// -----------------------------------------------------------------------------
bool test_guest_cpu_boot_contract_execution() {
    std::cout << "[RUN] Test 13: Guest CPU boot contract execution ... ";

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024; // 64 MB RAM
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.devicetree_blob = dtb;
    cfg.force_synthetic = true;
    cfg.record_memory_loads = true;
    cfg.kernel_data = AppleDeviceTreeHandoffManager::create_synthetic_devicetree_traversal_kernel();

    AppleKernelExecutionEngine engine(cfg);
    STAGE105_ASSERT(engine.initialize(), "Engine initialization must succeed");

    auto ev = engine.run();
    STAGE105_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI, "Kernel must halt at WFI");
    STAGE105_ASSERT(ev.final_state.x[0] == 0, "X0 must be 0 (Success exit status code)");
    STAGE105_ASSERT(ev.instructions_retired >= 18, "At least 18 instructions retired");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 14: Guest CPU Stack Inspection of Retrieved DTB Tokens
// -----------------------------------------------------------------------------
bool test_guest_cpu_stack_tokens() {
    std::cout << "[RUN] Test 14: Guest CPU stack token inspection ... ";

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.instruction_budget = 50000ULL;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.devicetree_blob = dtb;
    cfg.force_synthetic = true;
    cfg.kernel_data = AppleDeviceTreeHandoffManager::create_synthetic_devicetree_traversal_kernel();

    AppleKernelExecutionEngine engine(cfg);
    STAGE105_ASSERT(engine.initialize(), "Engine initialization must succeed");

    auto ev = engine.run();
    STAGE105_ASSERT(ev.final_state.x[0] == 0, "X0 must be 0");

    // Inspect stack at boot_stack_phys
    // [SP, #0] = root nProperties
    // [SP, #4] = root nChildren
    const std::uint32_t stack_nprops = engine.memory().read_u32(cfg.boot_stack_phys);
    const std::uint32_t stack_nchildren = engine.memory().read_u32(cfg.boot_stack_phys + 4);

    // Verify against actual DTB root node values
    auto root = AppleDeviceTreeBuilder::deserialize(dtb.data(), dtb.size());
    STAGE105_ASSERT(root != nullptr, "Deserialization must succeed");
    STAGE105_ASSERT(stack_nprops == root->properties().size(), "Stack nProperties must match root node");
    STAGE105_ASSERT(stack_nchildren == root->children().size(), "Stack nChildren must match root node");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 15: Guest CPU Memory Access Trace Analysis
// -----------------------------------------------------------------------------
bool test_guest_cpu_trace_analysis() {
    std::cout << "[RUN] Test 15: Guest CPU memory access trace analysis ... ";

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.instruction_budget = 50000ULL;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.devicetree_blob = dtb;
    cfg.force_synthetic = true;
    cfg.record_memory_loads = true;
    cfg.kernel_data = AppleDeviceTreeHandoffManager::create_synthetic_devicetree_traversal_kernel();

    AppleKernelExecutionEngine engine(cfg);
    STAGE105_ASSERT(engine.initialize(), "Engine initialization must succeed");

    auto ev = engine.run();
    STAGE105_ASSERT(ev.final_state.x[0] == 0, "X0 must be 0");

    auto report = AppleDeviceTreeHandoffManager::analyze_guest_consumption(
        ev.memory_access_trace, cfg.devicetree_phys, static_cast<std::uint32_t>(dtb.size()));

    STAGE105_ASSERT(report.handoff_verified, "Handoff must be verified from trace");
    STAGE105_ASSERT(report.total_dtb_reads >= 3, "At least 3 reads in DTB space");
    STAGE105_ASSERT(report.header_reads >= 2, "Both nProperties and nChildren read");
    STAGE105_ASSERT(report.property_reads >= 1, "At least 1 property read");
    STAGE105_ASSERT(report.min_read_address >= cfg.devicetree_phys, "Reads must start at or after DTB base");
    STAGE105_ASSERT(report.max_read_address < cfg.devicetree_phys + dtb.size(), "Reads must stay within DTB bounds");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 16: Null DeviceTree Pointer Guest Error Handling
// -----------------------------------------------------------------------------
bool test_guest_null_dtb_pointer() {
    std::cout << "[RUN] Test 16: Null DeviceTree pointer guest error handling ... ";

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.instruction_budget = 50000ULL;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.devicetree_blob = dtb;
    cfg.force_synthetic = true;
    cfg.kernel_data = AppleDeviceTreeHandoffManager::create_synthetic_devicetree_traversal_kernel();

    AppleKernelExecutionEngine engine(cfg);
    STAGE105_ASSERT(engine.initialize(), "Engine initialization must succeed");

    // Nullify boot_args->deviceTreeP in RAM at offset 96
    engine.memory().write_u64(cfg.boot_args_phys + offsetof(AppleBootArgs, deviceTreeP), 0);

    auto ev = engine.run();
    STAGE105_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI, "Kernel must halt at WFI");
    STAGE105_ASSERT(ev.final_state.x[0] == 1, "X0 must be 1 (Error status code after CBZ)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 17: Undersized DeviceTree Length Guest Error Handling
// -----------------------------------------------------------------------------
bool test_guest_undersized_dtb_length() {
    std::cout << "[RUN] Test 17: Undersized DeviceTree length guest error handling ... ";

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.instruction_budget = 50000ULL;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.devicetree_blob = dtb;
    cfg.force_synthetic = true;
    cfg.kernel_data = AppleDeviceTreeHandoffManager::create_synthetic_devicetree_traversal_kernel();

    AppleKernelExecutionEngine engine(cfg);
    STAGE105_ASSERT(engine.initialize(), "Engine initialization must succeed");

    // Overwrite boot_args->deviceTreeLength in RAM with 8 (< 16)
    engine.memory().write_u32(cfg.boot_args_phys + offsetof(AppleBootArgs, deviceTreeLength), 8);

    auto ev = engine.run();
    STAGE105_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI, "Kernel must halt at WFI");
    STAGE105_ASSERT(ev.final_state.x[0] == 1, "X0 must be 1 (Error status code after CMP/B.LO)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 18: DTB Staging Bit-Exact Roundtrip and Memory Isolation
// -----------------------------------------------------------------------------
bool test_dtb_staging_roundtrip_and_memory_isolation() {
    std::cout << "[RUN] Test 18: DTB staging roundtrip and memory isolation ... ";

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    const std::size_t dtb_size = dtb.size();

    Memory mem(0x100000);
    // Fill memory with sentinel pattern 0xAA
    std::memset(mem.raw_data(), 0xAA, mem.size());

    const std::size_t stage_offset = 0x10000;
    std::memcpy(mem.raw_data() + stage_offset, dtb.data(), dtb_size);

    // Verify bit-exact roundtrip
    STAGE105_ASSERT(std::memcmp(mem.raw_data() + stage_offset, dtb.data(), dtb_size) == 0,
                    "Staged memory must match original DTB bit-for-bit");

    // Verify memory before and after staging region remains unchanged (sentinel 0xAA)
    bool pre_ok = true;
    for (std::size_t i = 0; i < stage_offset; ++i) {
        if (mem.read_u8(i) != 0xAA) {
            pre_ok = false;
            break;
        }
    }
    STAGE105_ASSERT(pre_ok, "Pre-staging memory must remain untouched");

    bool post_ok = true;
    for (std::size_t i = stage_offset + dtb_size; i < stage_offset + dtb_size + 256; ++i) {
        if (mem.read_u8(i) != 0xAA) {
            post_ok = false;
            break;
        }
    }
    STAGE105_ASSERT(post_ok, "Post-staging memory must remain untouched");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 19: iPhone 14 Pro Profile Validation (iPhone15,2 / D73AP)
// -----------------------------------------------------------------------------
bool test_iphone14_pro_required_nodes_verification() {
    std::cout << "[RUN] Test 19: iPhone 14 Pro required nodes verification ... ";
    std::string err;

    auto dtb = AppleDeviceTreeBuilder::create_iphone14_pro_devicetree();
    auto root = AppleDeviceTreeBuilder::deserialize(dtb.data(), dtb.size(), &err);
    STAGE105_ASSERT(root != nullptr, "iPhone 14 Pro DTB must deserialize successfully");

    STAGE105_ASSERT(AppleDeviceTreeHandoffManager::validate_required_iphone14_pro_nodes(*root, &err),
                    "iPhone 14 Pro DTB must satisfy all required nodes");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 20: iPhone 14 Pro Target Type Failure
// -----------------------------------------------------------------------------
bool test_iphone14_pro_target_type_failure() {
    std::cout << "[RUN] Test 20: Non-D73AP iPhone 14 Pro rejection ... ";
    std::string err;

    AppleDeviceTreeNode root("");
    root.add_property_string("name", "");
    root.add_property_string("model", "iPhone15,2");
    root.add_property_string("target-type", "D37AP"); // Wrong board for iPhone 14 Pro
    root.add_property_u32("chip-id", 0x8120u);

    STAGE105_ASSERT(!AppleDeviceTreeHandoffManager::validate_required_iphone14_pro_nodes(root, &err),
                    "Non-D73AP target-type must be rejected for iPhone 14 Pro");
    STAGE105_ASSERT(err.find("D73AP") != std::string::npos, "Error must mention D73AP");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 21: Guest CPU Recursive DeviceTree Traversal Execution
// -----------------------------------------------------------------------------
bool test_guest_cpu_recursive_devicetree_traversal() {
    std::cout << "[RUN] Test 21: Guest CPU recursive DeviceTree traversal execution ... ";

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.instruction_budget = 50000ULL;
    cfg.max_trace_entries = 50000ULL;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.devicetree_blob = dtb;
    cfg.force_synthetic = true;
    cfg.record_memory_loads = true;
    cfg.kernel_data = AppleDeviceTreeHandoffManager::create_recursive_devicetree_traversal_kernel();

    AppleKernelExecutionEngine engine(cfg);
    STAGE105_ASSERT(engine.initialize(), "Engine initialization must succeed");

    auto ev = engine.run();
    STAGE105_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI, "Kernel must halt at WFI");
    STAGE105_ASSERT(ev.final_state.x[0] == 0, "X0 must be 0 (Successful recursive traversal)");

    auto report = AppleDeviceTreeHandoffManager::analyze_guest_consumption(
        ev.memory_access_trace, cfg.devicetree_phys, static_cast<std::uint32_t>(dtb.size()));

    STAGE105_ASSERT(report.handoff_verified, "Handoff must be verified");
    STAGE105_ASSERT(report.recursive_traversal_verified, "Recursive traversal must be verified");
    STAGE105_ASSERT(report.total_nodes_discovered >= 4, "Must discover at least 4 nodes");
    STAGE105_ASSERT(report.max_depth_reached >= 1, "Must reach at least recursion depth 1");
    STAGE105_ASSERT(report.verified_within_bounds, "All accesses must be within DTB bounds");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 22: Recursive Traversal Truncation Bounds Check
// -----------------------------------------------------------------------------
bool test_guest_cpu_recursive_traversal_bounds_truncation() {
    std::cout << "[RUN] Test 22: Recursive traversal truncation bounds check ... ";

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.instruction_budget = 50000ULL;
    cfg.max_trace_entries = 50000ULL;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.devicetree_blob = dtb;
    cfg.force_synthetic = true;
    cfg.record_memory_loads = true;
    cfg.kernel_data = AppleDeviceTreeHandoffManager::create_recursive_devicetree_traversal_kernel();

    AppleKernelExecutionEngine engine(cfg);
    STAGE105_ASSERT(engine.initialize(), "Engine initialization must succeed");

    // Artificially truncate deviceTreeLength to 32 bytes (< root header + properties)
    engine.memory().write_u32(cfg.boot_args_phys + offsetof(AppleBootArgs, deviceTreeLength), 32);

    auto ev = engine.run();
    STAGE105_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI, "Kernel must halt at WFI");
    STAGE105_ASSERT(ev.final_state.x[0] == 1, "X0 must be 1 (Error status code after bounds violation)");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 105: Real DeviceTree Handoff & Consumption Test Suite    \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)()) {
        if (test_fn()) {
            passed++;
        } else {
            failed++;
        }
    };

    run_test(test_staging_contract_validation);
    run_test(test_staging_contract_overlap_prevention);
    run_test(test_staged_memory_validation_success);
    run_test(test_staged_memory_truncation_detection);
    run_test(test_staged_memory_corrupted_property_header);
    run_test(test_staged_memory_corrupted_header_counts);
    run_test(test_iphone15_required_nodes_verification);
    run_test(test_iphone15_model_validation_failure);
    run_test(test_iphone15_target_type_failure);
    run_test(test_iphone15_chip_id_failure);
    run_test(test_iphone15_cpu_topology_validation);
    run_test(test_iphone15_peripheral_validation);
    run_test(test_guest_cpu_boot_contract_execution);
    run_test(test_guest_cpu_stack_tokens);
    run_test(test_guest_cpu_trace_analysis);
    run_test(test_guest_null_dtb_pointer);
    run_test(test_guest_undersized_dtb_length);
    run_test(test_dtb_staging_roundtrip_and_memory_isolation);
    run_test(test_iphone14_pro_required_nodes_verification);
    run_test(test_iphone14_pro_target_type_failure);
    run_test(test_guest_cpu_recursive_devicetree_traversal);
    run_test(test_guest_cpu_recursive_traversal_bounds_truncation);

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << passed << " / " << (passed + failed) << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? 0 : 1;
}
