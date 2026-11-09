#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "replay/replay_engine.h"
#include "replay/replay_state_hash.h"
#include "sndl/sndl_recorder.h"
#include "sndl/sndl_source_hooks.h"
#include "snapshot/snapshot_manager.h"

namespace {

constexpr std::uint32_t kNop = 0xD503201Fu;
constexpr std::uint32_t kMrsCntvctX0 = 0xD53BE040u;

struct ReferenceTimer {
    std::uint64_t counter{0};
    std::uint64_t offset{0};
    std::uint64_t cval{~0ULL};
    bool enable{false};
    bool imask{false};
    bool status{false};

    void advance(std::uint64_t ticks) {
        counter += ticks;
        status = enable && static_cast<std::int64_t>(counter - cval) >= 0;
    }

    std::uint32_t tval() const {
        return static_cast<std::uint32_t>(
            static_cast<std::int64_t>(cval) - static_cast<std::int64_t>(counter));
    }

    bool irq() const { return enable && status && !imask; }
};

void test_registers_and_permissions() {
    winios::VirtualTime time(24000000, 2);
    assert(time.get_frequency() == 24000000);
    assert(time.get_physical_counter() == 0);
    time.set_frequency(50000000);
    assert(time.get_frequency() == 50000000);
    time.set_cntkctl_el1(0, 0);
    time.set_cnthctl_el2(0, 0);
    const auto key_cntpct = static_cast<std::uint16_t>((1u << 14) | (3u << 11) | (14u << 7) | 1u);
    const auto trap = time.check_trap(0, 0, key_cntpct, true);
    assert(trap.trapped && trap.target_el == 1);
}

void test_physical_virtual_and_gic() {
    CPU cpu;
    cpu.set_architectural_exceptions(false);
    cpu.set_guest_counter(100);
    cpu.set_phys_timer(winios::TimerState{true, false, false, 105});
    cpu.set_virt_timer(winios::TimerState{true, false, false, 52});
    cpu.set_cntvoff_el2(50);
    cpu.set_guest_counter(105);
    cpu.check_timer_status();
    assert(cpu.get_phys_timer().istatus);
    assert(cpu.get_virt_timer().istatus);
    assert(cpu.get_virtual_time().get_phys_timer_irq(0));
    assert(cpu.get_virtual_time().get_virt_timer_irq(0));
    cpu.get_phys_timer().imask = true;
    cpu.set_phys_timer(cpu.get_phys_timer());
    assert(!cpu.get_virtual_time().get_phys_timer_irq(0));
    cpu.set_virt_timer(winios::TimerState{false, false, false, 52});
    assert(!cpu.get_virtual_time().get_virt_timer_irq(0));
}

void test_wraparound() {
    winios::VirtualTime time;
    time.set_physical_counter(~0ULL - 1);
    auto timer = time.get_phys_timer(0);
    timer.enable = true;
    timer.cval = ~0ULL;
    time.set_phys_timer(0, timer);
    time.advance_physical_counter(1);
    assert(time.get_phys_timer(0).istatus);
}

void test_smp_isolation() {
    winios::VirtualTime time(24000000, 2);
    time.set_cntvoff_el2(0, 10);
    time.set_cntvoff_el2(1, 20);
    auto first = time.get_phys_timer(0);
    first.enable = true;
    first.cval = 5;
    time.set_phys_timer(0, first);
    assert(time.get_phys_timer(1).enable == false);
    assert(time.get_virtual_counter(0) != time.get_virtual_counter(1));
}

void test_snapshot_and_recording_replay() {
    const std::filesystem::path snapshot = "stage41_timer.wsnap";
    const std::filesystem::path recording = "stage41_timer.sndl";
    CPU direct;
    Memory memory(0x10000);
    winios::VirtualTime time;
    winios::GICv3 gic;
    direct.bind_virtual_time(&time, 0);
    direct.set_architectural_exceptions(false);
    direct.set_program_counter(0);
    direct.set_guest_counter(10);
    direct.set_phys_timer(winios::TimerState{true, false, false, 14});
    direct.set_virt_timer(winios::TimerState{true, false, false, 16});
    direct.set_cntvoff_el2(2);
    direct.set_cnthctl_el2(0x100000003ULL);
    memory.write_u32(0, kMrsCntvctX0);
    for (std::size_t i = 1; i < 6; ++i) memory.write_u32(i * 4, kNop);

    winios::snapshot::SnapshotRestoreContext context{};
    context.primary_cpu = &direct;
    context.memory = &memory;
    context.virtual_time = &time;
    context.gic = &gic;
    assert(winios::snapshot::SnapshotManager::capture_to_file(context, snapshot).success);

    winios::sndl::SndlRecorder recorder;
    assert(recorder.start_recording(recording, 1, 0).is_success());
    winios::sndl::set_active_recorder(&recorder);
    for (std::size_t i = 0; i < 6; ++i) direct.step(memory);
    winios::sndl::set_active_recorder(nullptr);
    assert(recorder.stop_recording(6).is_success());
    const auto direct_hash = winios::replay::ReplayStateHash::compute_state_hashes(
        direct, memory, &time, &gic);

    winios::replay::ReplayConfig config{};
    config.max_instructions = 6;
    winios::replay::ReplayExecutionStats stats{};
    assert(winios::replay::ReplayEngine::run(snapshot, recording, config, stats).is_success());
    assert(!stats.diverged);
    assert(stats.instructions_executed == 6);
    assert(stats.final_state_hashes == direct_hash);

    CPU restored;
    Memory restored_memory(0x10000);
    winios::VirtualTime restored_time;
    winios::GICv3 restored_gic;
    winios::snapshot::SnapshotRestoreContext restore_context{};
    restore_context.primary_cpu = &restored;
    restore_context.memory = &restored_memory;
    restore_context.virtual_time = &restored_time;
    restore_context.gic = &restored_gic;
    assert(winios::snapshot::SnapshotManager::restore_from_file(
               snapshot, restore_context).success);
    assert(restored_time.get_cntvoff_el2(0) == 2);
    assert(restored_time.get_cnthctl_el2(0) == 3);

    std::filesystem::remove(snapshot);
    std::filesystem::remove(recording);
}

} // namespace

int main() {
    test_registers_and_permissions();
    test_physical_virtual_and_gic();
    test_wraparound();
    test_smp_isolation();
    test_snapshot_and_recording_replay();
    ReferenceTimer reference{};
    reference.counter = 10;
    reference.cval = 12;
    reference.enable = true;
    reference.advance(2);
    assert(reference.status && reference.irq());
    assert(reference.tval() == 0);
    std::cout << "Stage 41 timer register, physical, virtual, privilege, GIC, SMP, snapshot/replay, and reference tests passed\n";
    return 0;
}
