#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "replay/replay_engine.h"
#include "replay/replay_state_hash.h"
#include "sndl/sndl_recorder.h"
#include "sndl/sndl_source_hooks.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/snapshot_serializer.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

constexpr std::uint32_t kCycleBit = 1u << 31u;
constexpr std::uint32_t kMRS_CNTVCT_X0 = 0xD53BE040u;
constexpr std::uint32_t kNOP = 0xD503201Fu;
constexpr std::size_t kInstructions = 12;

struct FixturePaths {
    std::filesystem::path snapshot{"pmu_recording_fixture.wsnap"};
    std::filesystem::path recording{"pmu_recording_fixture.sndl"};
    std::filesystem::path truncated{"pmu_recording_fixture_truncated.sndl"};
    std::filesystem::path mismatch{"pmu_recording_fixture_mismatch.sndl"};
};

void cleanup(const FixturePaths& paths) {
    std::filesystem::remove(paths.snapshot);
    std::filesystem::remove(paths.recording);
    std::filesystem::remove(paths.truncated);
    std::filesystem::remove(paths.mismatch);
}

void configure(CPU& cpu, Memory& memory, winios::VirtualTime& vt, winios::GICv3& gic) {
    cpu.reset();
    cpu.bind_virtual_time(&vt, 0);
    cpu.set_architectural_exceptions(false);
    cpu.set_current_el(1);
    cpu.get_pmu().write_pmcr(winios::kPMCREnable);
    cpu.get_pmu().write_pmcntenset(kCycleBit | 0x3u);
    cpu.get_pmu().write_pmevtyper(0, winios::ARMV8_EVENT_INST_RETIRED);
    cpu.get_pmu().write_pmevtyper(1, winios::ARMV8_EVENT_BR_IMMED_RETIRED);
    cpu.get_pmu().write_pmevcntr(0, 0xFFFFFFFFu);
    cpu.get_pmu().write_pmintenset(1u);
    cpu.set_program_counter(0);
    for (std::size_t i = 0; i < kInstructions; ++i) {
        memory.write_u32(i * 4, (i % 3 == 2) ? kMRS_CNTVCT_X0 : kNOP);
    }
    (void)gic;
}

void check_pmu_equal(const winios::PMU& a, const winios::PMU& b) {
    assert(a.read_pmccntr() == b.read_pmccntr());
    assert(a.read_pmcntenset() == b.read_pmcntenset());
    assert(a.read_pmovsset() == b.read_pmovsset());
    assert(a.read_pmintenset() == b.read_pmintenset());
    assert(a.read_pmccfiltr() == b.read_pmccfiltr());
    for (std::size_t i = 0; i < winios::kNumEventCounters; ++i) {
        assert(a.read_pmevcntr(i) == b.read_pmevcntr(i));
        assert(a.read_pmevtyper(i) == b.read_pmevtyper(i));
    }
    assert(a.has_pending_interrupt() == b.has_pending_interrupt());
}

void make_truncated_copy(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(target, std::ios::binary);
    const auto bytes = std::vector<char>((std::istreambuf_iterator<char>(input)),
                                         std::istreambuf_iterator<char>());
    assert(bytes.size() > 64);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size() - 1));
}

} // namespace

int main() {
    const FixturePaths paths;
    cleanup(paths);

    CPU direct;
    Memory direct_memory(0x10000);
    winios::VirtualTime direct_time;
    winios::GICv3 direct_gic;
    configure(direct, direct_memory, direct_time, direct_gic);

    winios::snapshot::SnapshotRestoreContext context{};
    context.primary_cpu = &direct;
    context.memory = &direct_memory;
    context.virtual_time = &direct_time;
    context.gic = &direct_gic;
    winios::snapshot::SnapshotCaptureOptions options{};
    options.description = "Stage 40 PMU recording/replay fixture";
    assert(winios::snapshot::SnapshotManager::capture_to_file(context, paths.snapshot, options).success);
    const auto baseline_pmu = direct.get_pmu();

    winios::sndl::SndlRecorder recorder;
    assert(recorder.start_recording(paths.recording, 1, 0).is_success());
    winios::sndl::set_active_recorder(&recorder);
    for (std::size_t i = 0; i < kInstructions; ++i) {
        direct.step(direct_memory);
    }
    winios::sndl::set_active_recorder(nullptr);
    assert(recorder.stop_recording(kInstructions).is_success());
    assert(recorder.total_recorded() >= 3);

    CPU restored;
    Memory restored_memory(0x10000);
    winios::VirtualTime restored_time;
    winios::GICv3 restored_gic;
    winios::snapshot::SnapshotRestoreContext restored_context{};
    restored_context.primary_cpu = &restored;
    restored_context.memory = &restored_memory;
    restored_context.virtual_time = &restored_time;
    restored_context.gic = &restored_gic;
    assert(winios::snapshot::SnapshotManager::restore_from_file(
               paths.snapshot, restored_context).success);
    check_pmu_equal(baseline_pmu, restored.get_pmu());

    const auto direct_hash = winios::replay::ReplayStateHash::compute_state_hashes(
        direct, direct_memory, &direct_time, &direct_gic);
    winios::replay::ReplayConfig replay_config{};
    replay_config.max_instructions = kInstructions;
    winios::replay::ReplayExecutionStats replay_stats{};
    const auto replay_result = winios::replay::ReplayEngine::run(
        paths.snapshot, paths.recording, replay_config, replay_stats);
    assert(replay_result.is_success());
    assert(!replay_stats.diverged);
    assert(replay_stats.instructions_executed == kInstructions);
    assert(replay_stats.final_state_hashes == direct_hash);

    const auto recording_validation = winios::replay::ReplayEngine::validate(
        paths.snapshot, paths.recording);
    assert(recording_validation.is_success());
    make_truncated_copy(paths.recording, paths.truncated);
    assert(winios::replay::ReplayEngine::validate(paths.snapshot, paths.truncated).is_failure());

    replay_config.max_instructions = kInstructions - 1;
    winios::replay::ReplayExecutionStats mismatch_stats{};
    const auto mismatch = winios::replay::ReplayEngine::run(
        paths.snapshot, paths.recording, replay_config, mismatch_stats);
    assert(mismatch.is_failure() || mismatch_stats.instructions_executed != kInstructions);

    std::cout << "PMU recording/replay integration: passed\n";
    std::cout << "  recorded SNDL records: " << recorder.total_recorded() << "\n";
    std::cout << "  direct/replay instructions: " << kInstructions << "\n";
    std::cout << "  overflow mask: 0x" << std::hex << direct.get_pmu().read_pmovsset() << std::dec << "\n";
    std::cout << "  canonical replay hash: 0x" << std::hex
              << replay_stats.final_state_hashes.combined_hash << std::dec << "\n";

    cleanup(paths);
    return 0;
}
