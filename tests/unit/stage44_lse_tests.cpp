#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <iostream>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "replay/replay_engine.h"
#include "replay/replay_state_hash.h"
#include "sndl/sndl_recorder.h"
#include "sndl/sndl_source_hooks.h"
#include "snapshot/snapshot_manager.h"

namespace {

constexpr std::uint32_t kCasX = 0xC8A07C41u;   // CAS X0, X1, [X2]
constexpr std::uint32_t kCasalW = 0xB8E07C41u; // CASAL W0, W1, [X2]
constexpr std::uint32_t kLdaddW = 0xB82300A4u;
constexpr std::uint32_t kLdclrW = 0xB82310A4u;
constexpr std::uint32_t kLdeorW = 0xB82320A4u;
constexpr std::uint32_t kLdsetW = 0xB82330A4u;
constexpr std::uint32_t kSwpW = 0xB82380A4u;
constexpr std::uint32_t kNop = 0xD503201Fu;

void run(CPU& cpu, Memory& memory, std::uint32_t instruction) {
    memory.write_u32(cpu.get_program_counter(), instruction);
    cpu.step(memory);
}

void test_cas_success_failure_and_widths() {
    CPU cpu;
    Memory memory(0x400);
    cpu.write_register(2, 0x100);
    cpu.write_register(0, 0x11);
    cpu.write_register(1, 0x22);
    memory.write_u64(0x100, 0x11);
    run(cpu, memory, kCasX);
    assert(cpu.read_register(0) == 0x11);
    assert(memory.read_u64(0x100) == 0x22);

    cpu.set_program_counter(0);
    cpu.write_register(0, 0x33);
    cpu.write_register(1, 0x44);
    run(cpu, memory, kCasX);
    assert(cpu.read_register(0) == 0x22);
    assert(memory.read_u64(0x100) == 0x22);

    cpu.set_program_counter(0);
    cpu.write_register(2, 0x120);
    cpu.write_register(0, 0xFFFFFFFFu);
    cpu.write_register(1, 0x12345678u);
    memory.write_u32(0x120, 0xFFFFFFFFu);
    run(cpu, memory, kCasalW);
    assert(cpu.read_register(0) == 0xFFFFFFFFu);
    assert(memory.read_u32(0x120) == 0x12345678u);
}

void test_rmw_operations_and_swap() {
    CPU cpu;
    Memory memory(0x400);
    cpu.write_register(5, 0x140);
    cpu.write_register(3, 0x0F);
    memory.write_u32(0x140, 0xF0);
    run(cpu, memory, kLdaddW);
    assert(cpu.read_register(4) == 0xF0 && memory.read_u32(0x140) == 0xFF);

    cpu.set_program_counter(0);
    memory.write_u32(0x140, 0xFF);
    run(cpu, memory, kLdclrW);
    assert(cpu.read_register(4) == 0xFF && memory.read_u32(0x140) == 0xF0);

    cpu.set_program_counter(0);
    memory.write_u32(0x140, 0xF0);
    run(cpu, memory, kLdeorW);
    assert(cpu.read_register(4) == 0xF0 && memory.read_u32(0x140) == 0xFF);

    cpu.set_program_counter(0);
    memory.write_u32(0x140, 0xF0);
    run(cpu, memory, kLdsetW);
    assert(cpu.read_register(4) == 0xF0 && memory.read_u32(0x140) == 0xFF);

    cpu.set_program_counter(0);
    cpu.write_register(3, 0x55);
    memory.write_u32(0x140, 0xAA);
    run(cpu, memory, kSwpW);
    assert(cpu.read_register(4) == 0xAA && memory.read_u32(0x140) == 0x55);
}

void test_reservation_invalidation_and_faults() {
    CPU cpu;
    Memory memory(0x400);
    cpu.write_register(1, 0x180);
    cpu.write_register(0, 1);
    cpu.write_register(1, 2);
    memory.write_u64(0x180, 1);
    cpu.set_exclusive_monitor(true, 0x180, 8, 1, 0);
    assert(cpu.exclusive_monitor_valid());
    cpu.set_program_counter(4);
    cpu.write_register(2, 0x180);
    cpu.write_register(4, 3);
    run(cpu, memory, 0xB8238044u); // SWP W4, W3, [X2]
    assert(!cpu.exclusive_monitor_valid());

    CPU fault_cpu;
    Memory small(0x100);
    fault_cpu.write_register(2, 0x100);
    fault_cpu.write_register(0, 7);
    fault_cpu.write_register(1, 9);
    bool threw = false;
    try {
        run(fault_cpu, small, kCasX);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);
    assert(fault_cpu.read_register(0) == 7);
    assert(fault_cpu.get_program_counter() == 0);
}

void test_replay_and_reference() {
    const std::filesystem::path snapshot = "stage44_lse.wsnap";
    const std::filesystem::path recording = "stage44_lse.sndl";
    CPU cpu;
    Memory memory(0x400);
    winios::VirtualTime time;
    winios::GICv3 gic;
    cpu.bind_virtual_time(&time, 0);
    cpu.set_program_counter(0);
    cpu.write_register(2, 0x200);
    cpu.write_register(0, 1);
    cpu.write_register(1, 2);
    memory.write_u64(0x200, 1);
    memory.write_u32(0, kCasX);
    memory.write_u32(4, kNop);
    winios::snapshot::SnapshotRestoreContext context{};
    context.primary_cpu = &cpu;
    context.memory = &memory;
    context.virtual_time = &time;
    context.gic = &gic;
    assert(winios::snapshot::SnapshotManager::capture_to_file(context, snapshot).success);
    winios::sndl::SndlRecorder recorder;
    assert(recorder.start_recording(recording, 1, 0).is_success());
    winios::sndl::set_active_recorder(&recorder);
    run(cpu, memory, kCasX);
    run(cpu, memory, kNop);
    winios::sndl::set_active_recorder(nullptr);
    assert(recorder.stop_recording(2).is_success());
    const auto direct_hash = winios::replay::ReplayStateHash::compute_state_hashes(
        cpu, memory, &time, &gic);
    winios::replay::ReplayConfig config{};
    config.max_instructions = 2;
    winios::replay::ReplayExecutionStats stats{};
    const auto result = winios::replay::ReplayEngine::run(snapshot, recording, config, stats);
    assert(result.is_success());
    assert(stats.instructions_executed == 2);
    assert(stats.final_state_hashes.memory_hash == direct_hash.memory_hash);
    std::filesystem::remove(snapshot);
    std::filesystem::remove(recording);
}

}

int main() {
    test_cas_success_failure_and_widths();
    test_rmw_operations_and_swap();
    test_reservation_invalidation_and_faults();
    test_replay_and_reference();
    std::cout << "Stage 44 LSE CAS, SWP, RMW, ordering, invalidation, faults, replay, and reference tests passed\n";
}
