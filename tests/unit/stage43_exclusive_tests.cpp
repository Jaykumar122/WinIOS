#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/smp_exclusive_monitor.h"
#include "memory/memory.h"
#include "replay/replay_engine.h"
#include "replay/replay_state_hash.h"
#include "sndl/sndl_recorder.h"
#include "sndl/sndl_source_hooks.h"
#include "snapshot/snapshot_manager.h"

namespace {

constexpr std::uint32_t kLdxr = 0xC85F7C20u;
constexpr std::uint32_t kStxr = 0xC8027C20u; // STXR W2, X0, [X1]
constexpr std::uint32_t kLdxp = 0xC87F0440u;
constexpr std::uint32_t kStxp = 0xC8230440u;
constexpr std::uint32_t kClrex = 0xD5033F5Fu;
constexpr std::uint32_t kStlr = 0xC89FFC22u;
constexpr std::uint32_t kLdar = 0xC8DFFC20u;
constexpr std::uint32_t kNop = 0xD503201Fu;

void run(CPU& cpu, Memory& memory, std::uint32_t instruction) {
    memory.write_u32(cpu.get_program_counter(), instruction);
    cpu.step(memory);
}

struct ReferenceMonitor {
    bool valid{false};
    std::uint64_t address{0};
    std::size_t size{0};
    void load(std::uint64_t a, std::size_t s) { valid = true; address = a; size = s; }
    bool store(std::uint64_t a, std::size_t s) {
        const bool success = valid && address == a && size == s;
        valid = false;
        return success;
    }
    void clear() { valid = false; }
};

void test_single_and_clrex() {
    CPU cpu;
    Memory memory(0x400);
    cpu.write_register(1, 0x100);
    memory.write_u64(0x100, 0x1122334455667788ULL);
    run(cpu, memory, kLdxr);
    assert(cpu.exclusive_monitor_valid());
    assert(cpu.read_register(0) == 0x1122334455667788ULL);
    cpu.write_register(0, 0xAABBCCDDEEFF0011ULL);
    run(cpu, memory, kStxr);
    assert(cpu.read_register(2) == 0);
    assert(cpu.read_register(0) == 0xAABBCCDDEEFF0011ULL);
    assert(memory.read_u64(0x100) == 0xAABBCCDDEEFF0011ULL);

    run(cpu, memory, kLdxr);
    run(cpu, memory, kClrex);
    cpu.write_register(0, 7);
    run(cpu, memory, kStxr);
    assert(cpu.read_register(2) == 1);
    assert(memory.read_u64(0x100) == 0xAABBCCDDEEFF0011ULL);
}

void test_failure_and_pair() {
    CPU cpu;
    Memory memory(0x400);
    cpu.write_register(1, 0x100);
    cpu.write_register(0, 9);
    run(cpu, memory, kStxr);
    assert(cpu.read_register(2) == 1);
    assert(memory.read_u64(0x100) == 0);

    memory.write_u64(0x120, 1);
    memory.write_u64(0x128, 2);
    cpu.write_register(2, 0x120);
    run(cpu, memory, kLdxp);
    assert(cpu.read_register(0) == 1 && cpu.read_register(1) == 2);
    cpu.write_register(0, 3);
    cpu.write_register(1, 4);
    run(cpu, memory, kStxp);
    assert(cpu.read_register(3) == 0);
    assert(memory.read_u64(0x120) == 3 && memory.read_u64(0x128) == 4);
}

void test_acquire_release_and_reference() {
    CPU cpu;
    Memory memory(0x400);
    ReferenceMonitor reference;
    cpu.write_register(1, 0x180);
    cpu.write_register(2, 0xCAFEBABE);
    run(cpu, memory, kStlr);
    run(cpu, memory, kLdar);
    assert(cpu.read_register(0) == 0xCAFEBABE);
    reference.load(0x180, 8);
    assert(reference.store(0x180, 8));
    assert(!reference.store(0x180, 8));
}

void test_granule_invalidation() {
    winios::SmpExclusiveMonitorManager manager(2);
    manager.load_exclusive(0, 0x200, 8);
    manager.load_exclusive(1, 0x208, 8);
    manager.invalidate_reservations(2, 0x240, 8);
    assert(manager.is_reservation_valid(0, 0x200, 8));
    manager.invalidate_reservations(1, 0x200, 8);
    assert(!manager.is_reservation_valid(0, 0x200, 8));
    assert(manager.is_reservation_valid(1, 0x208, 8));
    manager.clear_reservation(1);
    assert(!manager.is_reservation_valid(1, 0x208, 8));
}

void test_snapshot_and_replay() {
    const std::filesystem::path snapshot = "stage43_exclusive.wsnap";
    const std::filesystem::path recording = "stage43_exclusive.sndl";
    CPU cpu;
    Memory memory(0x400);
    winios::VirtualTime time;
    winios::GICv3 gic;
    cpu.bind_virtual_time(&time, 0);
    cpu.write_register(1, 0x100);
    memory.write_u64(0x100, 5);
    cpu.set_program_counter(0);
    memory.write_u32(0, kLdxr);
    memory.write_u32(4, kStxr);
    memory.write_u32(8, kNop);
    winios::snapshot::SnapshotRestoreContext context{};
    context.primary_cpu = &cpu;
    context.memory = &memory;
    context.virtual_time = &time;
    context.gic = &gic;
    assert(winios::snapshot::SnapshotManager::capture_to_file(context, snapshot).success);

    CPU restored;
    Memory restored_memory(0x400);
    winios::VirtualTime restored_time;
    winios::GICv3 restored_gic;
    winios::snapshot::SnapshotRestoreContext restore_context{};
    restore_context.primary_cpu = &restored;
    restore_context.memory = &restored_memory;
    restore_context.virtual_time = &restored_time;
    restore_context.gic = &restored_gic;
    assert(winios::snapshot::SnapshotManager::restore_from_file(snapshot, restore_context).success);
    run(cpu, memory, kLdxr);
    assert(cpu.exclusive_monitor_valid());
    cpu.set_program_counter(0);
    winios::sndl::SndlRecorder recorder;
    assert(recorder.start_recording(recording, 1, 0).is_success());
    winios::sndl::set_active_recorder(&recorder);
    run(cpu, memory, kLdxr);
    run(cpu, memory, kStxr);
    run(cpu, memory, kNop);
    winios::sndl::set_active_recorder(nullptr);
    assert(recorder.stop_recording(3).is_success());
    const auto direct_hash = winios::replay::ReplayStateHash::compute_state_hashes(
        cpu, memory, &time, &gic);
    winios::replay::ReplayConfig config{};
    config.max_instructions = 3;
    winios::replay::ReplayExecutionStats stats{};
    const auto result = winios::replay::ReplayEngine::run(snapshot, recording, config, stats);
    assert(result.is_success());
    assert(stats.instructions_executed == 3);
    assert(stats.final_state_hashes.memory_hash == direct_hash.memory_hash);
    std::filesystem::remove(snapshot);
    std::filesystem::remove(recording);
}

}

int main() {
    test_single_and_clrex();
    test_failure_and_pair();
    test_acquire_release_and_reference();
    test_granule_invalidation();
    test_snapshot_and_replay();
    std::cout << "Stage 43 exclusive, pair, CLREX, acquire/release, monitor, SMP, snapshot/replay, and reference tests passed\n";
}
