#include "cpu/cpu.h"
#include "cpu/psci.h"
#include "cpu/smp_cluster.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "trace/trace_config.h"
#include "trace/trace_file_reader.h"
#include "trace/trace_file_writer.h"
#include "trace/trace_manager.h"
#include "trace/trace_types.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#define TEST_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":"   \
                      << __LINE__ << std::endl;                                \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

using namespace winios;
using namespace winios::trace;

namespace {

std::vector<TraceEvent> pop_all_events(TraceManager& mgr) {
    std::vector<TraceEvent> results;
    TraceEvent ev{};
    auto* rb = const_cast<InMemoryRingBuffer*>(mgr.ring_buffer());
    if (!rb) {
        return results;
    }
    while (rb->try_pop(ev)) {
        results.push_back(ev);
    }
    return results;
}

constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

constexpr std::uint32_t enc_add_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0x91000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_svc(std::uint16_t imm16) {
    return 0xD4000001u | (static_cast<std::uint32_t>(imm16) << 5u);
}

constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}

constexpr std::uint32_t enc_msr_spsel_imm(unsigned imm1) {
    return 0xD50040BFu | ((imm1 & 1u) << 8u);
}

void setup_test_cpu(CPU& cpu, Memory& mem, unsigned el = 1, std::uint64_t vbar = 0x10000) {
    (void)mem;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(el);
    cpu.set_vbar_el1(vbar);
    cpu.set_vbar_el2(vbar + 0x10000);
    cpu.set_vbar_el3(vbar + 0x20000);
    cpu.set_daif(0);
}

// -----------------------------------------------------------------------------
// Test 30: Multi-core SMP boot sequence with file sink serialization
// -----------------------------------------------------------------------------
void test_smp_boot_file_sink() {
    std::cout << "  Test 30: Multi-core SMP boot sequence with file sink... " << std::flush;
    const std::filesystem::path trace_file = "test_smp_boot.wtr";
    if (std::filesystem::exists(trace_file)) {
        std::filesystem::remove(trace_file);
    }

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(trace_file));

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    SMPCluster cluster(4);
    for (std::size_t i = 0; i < 4; ++i) {
        cluster.get_core(i).bind_trace_manager(&mgr);
    }

    cluster.initialize_core(0, 0x1000);
    cluster.start_secondary_core(1, 0x2000);
    cluster.start_secondary_core(2, 0x3000);
    cluster.start_secondary_core(3, 0x4000);

    // Drain events to file writer
    TraceEvent ev{};
    auto* rb = const_cast<InMemoryRingBuffer*>(mgr.ring_buffer());
    while (rb->try_pop(ev)) {
        writer.write_event(ev);
    }
    writer.close();
    mgr.stop();

    // Verify with TraceFileReader
    TraceFileReader reader;
    TEST_ASSERT(reader.open(trace_file));
    TEST_ASSERT(reader.is_cleanly_finalized());
    TEST_ASSERT(reader.header().magic == TRACE_FILE_MAGIC);
    TEST_ASSERT(reader.footer().total_events == 4);

    std::vector<TraceEvent> events;
    TEST_ASSERT(reader.read_all_events(events) == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        TEST_ASSERT(events[i].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted));
        TEST_ASSERT(events[i].core_id == static_cast<std::uint16_t>(i));
        TEST_ASSERT(events[i].pc == 0x1000 + i * 0x1000);
    }
    reader.close();
    std::filesystem::remove(trace_file);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 31: Full end-to-end trace reader validation
// -----------------------------------------------------------------------------
void test_end_to_end_reader_validation() {
    std::cout << "  Test 31: Full end-to-end trace reader validation... " << std::flush;
    const std::filesystem::path trace_file = "test_end_to_end.wtr";
    if (std::filesystem::exists(trace_file)) {
        std::filesystem::remove(trace_file);
    }

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(trace_file));

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::ContextChanged, true);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    Memory mem(0x10000);
    mem.write_u32(0x1000, enc_add_imm(1, 0, 10));
    mem.write_u32(0x1004, enc_add_imm(2, 1, 20));

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);

    cpu.start();
    cpu.step(mem);
    cpu.step(mem);
    cpu.stop(0);

    TraceEvent ev{};
    auto* rb = const_cast<InMemoryRingBuffer*>(mgr.ring_buffer());
    while (rb->try_pop(ev)) {
        writer.write_event(ev);
    }
    writer.close();
    mgr.stop();

    TraceFileReader reader;
    TEST_ASSERT(reader.open(trace_file));
    TEST_ASSERT(reader.is_cleanly_finalized());

    std::vector<TraceEvent> events;
    TEST_ASSERT(reader.read_all_events(events) == 4);
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted));
    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    TEST_ASSERT(events[2].event_type == static_cast<std::uint16_t>(TraceEventType::InstructionRetired));
    TEST_ASSERT(events[3].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStopped));

    reader.close();
    std::filesystem::remove(trace_file);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 32: Secondary Core Wakeup and Shutdown Causal Ordering
// -----------------------------------------------------------------------------
void test_secondary_core_wakeup_shutdown_ordering() {
    std::cout << "  Test 32: Secondary core wakeup and shutdown causal ordering... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    SMPCluster cluster(2);
    cluster.enable_psci();
    cluster.get_core(0).bind_trace_manager(&mgr);
    cluster.get_core(1).bind_trace_manager(&mgr);

    cluster.initialize_core(0, 0x1000);
    cluster.start_secondary_core(1, 0x2000);

    cluster.get_core(1).write_register(0, winios::PSCIService::kCpuOff);
    cluster.get_psci_service()->handle_smc(cluster.get_core(1), 1);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 3);

    // Core 0 started, Core 1 started, Core 1 stopped
    TEST_ASSERT(events[0].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted));
    TEST_ASSERT(events[0].core_id == 0);

    TEST_ASSERT(events[1].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted));
    TEST_ASSERT(events[1].core_id == 1);

    TEST_ASSERT(events[2].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStopped));
    TEST_ASSERT(events[2].core_id == 1);
    TEST_ASSERT(events[2].data == 1); // PSCI_CPU_OFF
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 33: Multi-threaded trace pipeline under parallel execution engine
// -----------------------------------------------------------------------------
void test_parallel_engine_trace_pipeline() {
    std::cout << "  Test 33: Multi-threaded trace pipeline under parallel engine... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 4096;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    SMPCluster cluster(2, 0x40000);
    for (std::size_t i = 0; i < 2; ++i) {
        cluster.get_core(i).bind_trace_manager(&mgr);
    }

    cluster.initialize_core(0, 0x1000);
    cluster.start_secondary_core(1, 0x2000);

    // Setup NOP instructions for both cores
    Memory& mem = cluster.get_memory();
    for (std::size_t addr = 0x1000; addr < 0x3000; addr += 4) {
        mem.write_u32(addr, enc_nop());
    }

    // Run parallel execution for 50 instructions
    cluster.start_parallel();
    std::size_t retired = cluster.run_parallel(50);
    cluster.stop_parallel();

    TEST_ASSERT(retired > 0);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(!events.empty());
    // Both cores must have emitted CPUStarted
    bool core0_started = false;
    bool core1_started = false;
    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted)) {
            if (ev.core_id == 0) core0_started = true;
            if (ev.core_id == 1) core1_started = true;
        }
    }
    TEST_ASSERT(core0_started);
    TEST_ASSERT(core1_started);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 34: Sequential Lifecycle Pipeline: Start -> Workload -> SVC -> ERET -> Stop
// -----------------------------------------------------------------------------
void test_sequential_lifecycle_svc_pipeline() {
    std::cout << "  Test 34: Sequential pipeline (Start -> Workload -> SVC -> ERET -> Stop)... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::ContextChanged, true);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    Memory mem(0x40000);
    mem.write_u32(0x1000, enc_add_imm(0, 0, 5));
    mem.write_u32(0x1004, enc_svc(0));
    // SVC from EL1h branches to VBAR_EL1 + 0x200
    mem.write_u32(0x10200, enc_add_imm(1, 1, 1));
    mem.write_u32(0x10204, enc_eret());

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);

    // 1. Start CPU
    cpu.start();

    // 2. Step ADD
    cpu.step(mem);

    // 3. Step SVC
    cpu.step(mem);

    // 4. Step Handler ADD
    cpu.step(mem);

    // 5. Step ERET
    cpu.step(mem);

    // 6. Stop CPU
    cpu.stop(0);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(!events.empty());

    // Verify initial is CPUStarted and final is CPUStopped
    TEST_ASSERT(events.front().event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted));
    TEST_ASSERT(events.back().event_type == static_cast<std::uint16_t>(TraceEventType::CPUStopped));

    // Verify presence of ExceptionEntered, ContextChanged(Entry), ExceptionReturned, ContextChanged(Return)
    bool has_exc_entry = false;
    bool has_cc_entry = false;
    bool has_exc_ret = false;
    bool has_cc_ret = false;

    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered)) {
            has_exc_entry = true;
        } else if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionReturned)) {
            has_exc_ret = true;
        } else if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            if (ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::ExceptionEntry)) {
                has_cc_entry = true;
            } else if (ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::ExceptionReturn)) {
                has_cc_ret = true;
            }
        }
    }

    TEST_ASSERT(has_exc_entry);
    TEST_ASSERT(has_cc_entry);
    TEST_ASSERT(has_exc_ret);
    TEST_ASSERT(has_cc_ret);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 35: Interrupted Lifecycle Pipeline: Start -> IRQ -> ERET -> Stop
// -----------------------------------------------------------------------------
void test_interrupted_lifecycle_pipeline() {
    std::cout << "  Test 35: Interrupted pipeline (Start -> IRQ -> ERET -> Stop)... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::ContextChanged, true);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    Memory mem(0x40000);
    // IRQ vector at VBAR_EL1 + 0x280
    mem.write_u32(0x10280, enc_eret());

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);

    cpu.start();

    // Fire timer IRQ
    cpu.set_phys_timer(TimerState{true, false, true, 0});
    cpu.step(mem); // IRQ accepted

    // Clear timer and step ERET
    cpu.set_phys_timer(TimerState{false, false, false, 0});
    cpu.step(mem);

    cpu.stop(0);

    auto events = pop_all_events(mgr);
    bool found_irq_delivery = false;
    bool found_irq_context_change = false;
    bool found_eret_context_change = false;

    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::InterruptDelivered)) {
            found_irq_delivery = true;
        } else if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            if (ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::ExceptionEntry)) {
                if ((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsInterrupt)) != 0) {
                    found_irq_context_change = true;
                }
            } else if (ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::ExceptionReturn)) {
                found_eret_context_change = true;
            }
        }
    }

    TEST_ASSERT(found_irq_delivery);
    TEST_ASSERT(found_irq_context_change);
    TEST_ASSERT(found_eret_context_change);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 36: Register Bank Switching Pipeline: Start -> MSR SPSel -> Stop
// -----------------------------------------------------------------------------
void test_register_bank_switching_pipeline() {
    std::cout << "  Test 36: Register bank switching pipeline... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::ContextChanged, true);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    Memory mem(0x10000);
    mem.write_u32(0x1000, enc_msr_spsel_imm(0)); // SP_EL0
    mem.write_u32(0x1004, enc_msr_spsel_imm(1)); // SP_ELx

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_sp_sel(true);

    cpu.start();
    cpu.step(mem);
    cpu.step(mem);
    cpu.stop(0);

    auto events = pop_all_events(mgr);
    int bank_select_count = 0;
    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged) &&
            ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::RegisterBankSelect)) {
            bank_select_count++;
        }
    }
    TEST_ASSERT(bank_select_count == 2);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 37: Multi-Core PSCI SYSTEM_OFF Pipeline
// -----------------------------------------------------------------------------
void test_multicore_system_off_pipeline() {
    std::cout << "  Test 37: Multi-core PSCI SYSTEM_OFF pipeline... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    SMPCluster cluster(4);
    cluster.enable_psci();
    for (std::size_t i = 0; i < 4; ++i) {
        cluster.get_core(i).bind_trace_manager(&mgr);
    }

    cluster.initialize_core(0, 0x1000);
    cluster.start_secondary_core(1, 0x2000);
    cluster.start_secondary_core(2, 0x3000);
    cluster.start_secondary_core(3, 0x4000);

    cluster.get_core(0).write_register(0, winios::PSCIService::kSystemOff);
    cluster.get_psci_service()->handle_smc(cluster.get_core(0), 0);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 8); // 4 started + 4 stopped

    int stop_count = 0;
    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::CPUStopped)) {
            stop_count++;
            TEST_ASSERT(ev.data == 2); // PSCI_SYSTEM_OFF
        }
    }
    TEST_ASSERT(stop_count == 4);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 38: Multi-block trace file spanning multiple 64 KiB blocks
// -----------------------------------------------------------------------------
void test_multiblock_trace_file() {
    std::cout << "  Test 38: Multi-block trace file across 64 KiB boundaries... " << std::flush;
    const std::filesystem::path trace_file = "test_multiblock.wtr";
    if (std::filesystem::exists(trace_file)) {
        std::filesystem::remove(trace_file);
    }

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(trace_file));

    // 64 KiB block fits 1024 64-byte events. Write 2050 events to cross multiple blocks.
    constexpr std::size_t kEventCount = 2050;
    for (std::size_t i = 0; i < kEventCount; ++i) {
        TraceEvent ev{};
        ev.sequence_number = i + 1;
        ev.event_type = (i % 2 == 0)
            ? static_cast<std::uint16_t>(TraceEventType::CPUStarted)
            : static_cast<std::uint16_t>(TraceEventType::CPUStopped);
        ev.core_id = static_cast<std::uint16_t>(i % 4);
        ev.pc = 0x1000 + i * 4;
        writer.write_event(ev);
    }
    writer.close();

    TraceFileReader reader;
    TEST_ASSERT(reader.open(trace_file));
    TEST_ASSERT(reader.is_cleanly_finalized());
    TEST_ASSERT(reader.footer().total_events == kEventCount);
    TEST_ASSERT(reader.footer().total_blocks > 1);

    std::vector<TraceEvent> loaded;
    TEST_ASSERT(reader.read_all_events(loaded) == kEventCount);
    for (std::size_t i = 0; i < kEventCount; ++i) {
        TEST_ASSERT(loaded[i].sequence_number == i + 1);
    }

    reader.close();
    std::filesystem::remove(trace_file);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 39: Corrupted Trace File Block Detection
// -----------------------------------------------------------------------------
void test_corrupted_block_detection() {
    std::cout << "  Test 39: Corrupted block CRC32C failure detection... " << std::flush;
    const std::filesystem::path trace_file = "test_corrupt.wtr";
    if (std::filesystem::exists(trace_file)) {
        std::filesystem::remove(trace_file);
    }

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(trace_file));

    for (std::size_t i = 0; i < 1100; ++i) {
        TraceEvent ev{};
        ev.sequence_number = i + 1;
        ev.event_type = static_cast<std::uint16_t>(TraceEventType::InstructionRetired);
        writer.write_event(ev);
    }
    writer.close();

    // Corrupt one byte inside block 1
    {
        std::fstream stream(trace_file, std::ios::in | std::ios::out | std::ios::binary);
        TEST_ASSERT(stream.is_open());
        // Seek past file header (64 bytes) into block payload
        stream.seekp(100, std::ios::beg);
        char bad = static_cast<char>(0xFF);
        stream.write(&bad, 1);
    }

    TraceFileReader reader;
    TEST_ASSERT(reader.open(trace_file));
    std::vector<TraceEvent> loaded;
    // Reader should encounter CRC mismatch and fail or truncate
    bool failed = false;
    try {
        std::size_t n = reader.read_all_events(loaded);
        if (n < 1100) {
            failed = true;
        }
    } catch (...) {
        failed = true;
    }
    TEST_ASSERT(failed || reader.footer().total_events == 0 || loaded.size() < 1100);

    reader.close();
    std::filesystem::remove(trace_file);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 40: Mixed Event Stream with all Phase 52 Event Types
// -----------------------------------------------------------------------------
void test_mixed_event_stream() {
    std::cout << "  Test 40: Mixed event stream covering all Phase 52 types... " << std::flush;
    const std::filesystem::path trace_file = "test_mixed.wtr";
    if (std::filesystem::exists(trace_file)) {
        std::filesystem::remove(trace_file);
    }

    TraceFileWriter writer;
    TEST_ASSERT(writer.open(trace_file));

    const TraceEventType all_types[] = {
        TraceEventType::InstructionRetired,
        TraceEventType::BranchTaken,
        TraceEventType::ExceptionEntered,
        TraceEventType::ExceptionReturned,
        TraceEventType::InterruptDelivered,
        TraceEventType::SystemRegisterAccess,
        TraceEventType::MemoryRead,
        TraceEventType::MemoryWrite,
        TraceEventType::DebugEvent,
        TraceEventType::CPUStarted,
        TraceEventType::CPUStopped,
        TraceEventType::ContextChanged
    };

    std::uint64_t seq = 1;
    for (auto type : all_types) {
        TraceEvent ev{};
        ev.sequence_number = seq++;
        ev.event_type = static_cast<std::uint16_t>(type);
        ev.core_id = 0;
        ev.pc = 0x1000 + seq * 4;
        writer.write_event(ev);
    }
    writer.close();

    TraceFileReader reader;
    TEST_ASSERT(reader.open(trace_file));
    TEST_ASSERT(reader.is_cleanly_finalized());
    TEST_ASSERT(reader.footer().total_events == sizeof(all_types)/sizeof(all_types[0]));

    std::vector<TraceEvent> events;
    TEST_ASSERT(reader.read_all_events(events) == sizeof(all_types)/sizeof(all_types[0]));
    for (std::size_t i = 0; i < events.size(); ++i) {
        TEST_ASSERT(events[i].event_type == static_cast<std::uint16_t>(all_types[i]));
    }

    reader.close();
    std::filesystem::remove(trace_file);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 41: Rapid PSCI CPU_ON / CPU_OFF Cycles
// -----------------------------------------------------------------------------
void test_rapid_psci_cycles() {
    std::cout << "  Test 41: Rapid PSCI CPU_ON / CPU_OFF cycles... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 2048;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    SMPCluster cluster(2);
    cluster.enable_psci();
    cluster.get_core(1).bind_trace_manager(&mgr);

    for (int iter = 0; iter < 50; ++iter) {
        cluster.start_secondary_core(1, 0x2000 + iter * 4);
        cluster.get_core(1).write_register(0, winios::PSCIService::kCpuOff);
    cluster.get_psci_service()->handle_smc(cluster.get_core(1), 1);
        cluster.get_core(1).reset();
    }

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == 100); // 50 cycles * 2 events
    for (int iter = 0; iter < 50; ++iter) {
        TEST_ASSERT(events[iter * 2].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStarted));
        TEST_ASSERT(events[iter * 2 + 1].event_type == static_cast<std::uint16_t>(TraceEventType::CPUStopped));
    }
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 42: Concurrency Stress on 4 Cores with Context Changes
// -----------------------------------------------------------------------------
void test_concurrency_stress_4_cores() {
    std::cout << "  Test 42: Concurrency stress across 4 cores... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::ContextChanged, true);
    cfg.ring_capacity = 8192;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    SMPCluster cluster(4);
    cluster.enable_psci();
    for (std::size_t i = 0; i < 4; ++i) {
        cluster.get_core(i).bind_trace_manager(&mgr);
        cluster.initialize_core(i, 0x1000 + i * 0x100);
    }

    for (int step = 0; step < 20; ++step) {
        for (std::size_t i = 0; i < 4; ++i) {
            cluster.get_core(i).set_sp_sel(step % 2 == 1);
            cluster.get_core(i).emit_context_changed_trace(
                0x1000, 0x1004, 1, 1, step % 2 == 0, step % 2 == 1,
                ContextChangeReason::RegisterBankSelect);
        }
    }

    cluster.get_core(0).write_register(0, winios::PSCIService::kSystemOff);
    cluster.get_psci_service()->handle_smc(cluster.get_core(0), 0);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() >= 88); // 4 started + 80 context changes + 4 stopped
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 43: High-throughput trace generation and buffer drain
// -----------------------------------------------------------------------------
void test_high_throughput_buffer_drain() {
    std::cout << "  Test 43: High-throughput trace generation and drain... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 16384;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.start();

    constexpr std::size_t kIterations = 10000;
    for (std::size_t i = 0; i < kIterations; ++i) {
        cpu.emit_instruction_retired_trace(0x1000, 0xD503201Fu);
    }
    cpu.stop(0);

    auto events = pop_all_events(mgr);
    TEST_ASSERT(events.size() == kIterations + 2); // 1 start + 10000 retired + 1 stop
    std::cout << "PASS\n";
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 52 Stage 13: Lifecycle Trace Pipeline & Integration Tests ===\n\n";

    test_smp_boot_file_sink();
    test_end_to_end_reader_validation();
    test_secondary_core_wakeup_shutdown_ordering();
    test_parallel_engine_trace_pipeline();
    test_sequential_lifecycle_svc_pipeline();
    test_interrupted_lifecycle_pipeline();
    test_register_bank_switching_pipeline();
    test_multicore_system_off_pipeline();
    test_multiblock_trace_file();
    test_corrupted_block_detection();
    test_mixed_event_stream();
    test_rapid_psci_cycles();
    test_concurrency_stress_4_cores();
    test_high_throughput_buffer_drain();

    std::cout << "\nAll 14 lifecycle trace pipeline integration tests PASSED.\n";
    return 0;
}
