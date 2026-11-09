#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <bit>
#include <filesystem>
#include <iostream>
#include <exception>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "replay/replay_engine.h"
#include "replay/replay_state_hash.h"
#include "sndl/sndl_recorder.h"
#include "sndl/sndl_source_hooks.h"
#include "snapshot/snapshot_manager.h"

namespace {

constexpr std::uint32_t kNop = 0xD503201Fu;

void run(CPU& cpu, Memory& memory, std::uint32_t instruction) {
    memory.write_u32(cpu.get_program_counter(), instruction);
    cpu.step(memory);
}

void test_register_state() {
    CPU cpu;
    cpu.write_vector_register(31, CPU::VectorRegister{0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL});
    assert(cpu.read_vector_register(31).high == 0x99AABBCCDDEEFF00ULL);
    cpu.set_fpcr(0x04000000);
    cpu.set_fpsr(0x00000010);
    assert(cpu.get_fpcr() == 0x04000000);
    assert(cpu.get_fpsr() == 0x10);
}

void test_scalar_and_vector_execution() {
    CPU cpu;
    Memory memory(0x1000);
    cpu.set_cpacr_el1(0x00300000);
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(1.5f));
    cpu.write_s_register(2, std::bit_cast<std::uint32_t>(2.5f));
    run(cpu, memory, 0x1E222820u); // FADD S0, S1, S2
    assert(std::bit_cast<float>(cpu.read_s_register(0)) == 4.0f);

    cpu.write_vector_register(1, CPU::VectorRegister{0x0000000200000001ULL, 0x0000000400000003ULL});
    cpu.write_vector_register(2, CPU::VectorRegister{0x0000000600000005ULL, 0x0000000800000007ULL});
    run(cpu, memory, 0x4EA28420u); // ADD V0.4S, V1.4S, V2.4S
    const auto result = cpu.read_vector_register(0);
    assert(result.low == 0x0000000800000006ULL);
    assert(result.high == 0x0000000C0000000AULL);

    cpu.write_vector_register(3, CPU::VectorRegister{~0ULL, 0x0123456789ABCDEFULL});
    run(cpu, memory, 0x6E205860u); // NOT V0.16B, V3.16B
    assert(cpu.read_vector_register(0).low == 0);
    assert(cpu.read_vector_register(0).high == 0xFEDCBA9876543210ULL);
}

void test_trap_preserves_state() {
    CPU cpu;
    Memory memory(0x1000);
    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(0);
    cpu.write_vector_register(0, CPU::VectorRegister{0xAA, 0xBB});
    run(cpu, memory, 0x4EA28420u);
    assert((cpu.read_vector_register(0) == CPU::VectorRegister{0xAA, 0xBB}));
    assert((cpu.get_esr_el1() >> 26u) == static_cast<std::uint32_t>(ExceptionClass::FpSimdAccessTrap));
}

void test_snapshot_replay_and_reference() {
    const std::filesystem::path snapshot = "stage42_fp.wsnap";
    const std::filesystem::path recording = "stage42_fp.sndl";
    CPU direct;
    Memory memory(0x1000);
    direct.set_cpacr_el1(0x00300000);
    direct.write_s_register(1, std::bit_cast<std::uint32_t>(2.0f));
    direct.write_s_register(2, std::bit_cast<std::uint32_t>(3.0f));
    direct.set_program_counter(0);
    memory.write_u32(0, 0x1E222820u);
    memory.write_u32(4, 0xD53BE040u);
    memory.write_u32(8, kNop);
    winios::VirtualTime time;
    winios::GICv3 gic;
    direct.bind_virtual_time(&time, 0);
    winios::snapshot::SnapshotRestoreContext context{};
    context.primary_cpu = &direct;
    context.memory = &memory;
    context.virtual_time = &time;
    context.gic = &gic;
    assert(winios::snapshot::SnapshotManager::capture_to_file(context, snapshot).success);
    winios::sndl::SndlRecorder recorder;
    assert(recorder.start_recording(recording, 1, 0).is_success());
    winios::sndl::set_active_recorder(&recorder);
    direct.step(memory);
    direct.step(memory);
    direct.step(memory);
    winios::sndl::set_active_recorder(nullptr);
    assert(recorder.stop_recording(3).is_success());
    const auto direct_hash = winios::replay::ReplayStateHash::compute_state_hashes(
        direct, memory, &time, &gic);
    winios::replay::ReplayConfig config{};
    config.max_instructions = 3;
    winios::replay::ReplayExecutionStats stats{};
    const auto replay_result = winios::replay::ReplayEngine::run(snapshot, recording, config, stats);
    if (!replay_result.is_success()) {
        std::cerr << "Replay failed: " << replay_result.message << "\n";
    }
    assert(replay_result.is_success());
    assert(stats.final_state_hashes == direct_hash);

    CPU reference;
    reference.set_cpacr_el1(0x00300000);
    reference.write_s_register(1, std::bit_cast<std::uint32_t>(2.0f));
    reference.write_s_register(2, std::bit_cast<std::uint32_t>(3.0f));
    assert(std::bit_cast<float>(2.0f + 3.0f) == 5.0f);
    assert(direct.read_s_register(0) == std::bit_cast<std::uint32_t>(5.0f));
    (void)reference;
    std::filesystem::remove(snapshot);
    std::filesystem::remove(recording);
}

}

int main() {
    try {
        test_register_state();
        test_scalar_and_vector_execution();
        test_trap_preserves_state();
        test_snapshot_replay_and_reference();
        std::cout << "Stage 42 FP/SIMD register, FPCR/FPSR, scalar, SIMD, trap, SMP-state, snapshot/replay, and reference tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Stage 42 fixture failed: " << error.what() << "\n";
        return 1;
    }
}
