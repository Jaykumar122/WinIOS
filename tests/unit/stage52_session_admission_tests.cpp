#include "runtime/elf_session.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/snapshot_validation.h"
#include "sndl/sndl_writer.h"
#include "test_harness.h"
#include "../support/elf_fixture.h"

#include <limits>

namespace {
using namespace winios;
RuntimeConfig config() { return {0x8000, 0, 0, 0x6000, 0x1000, 20, std::nullopt, false}; }
void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
struct Source {
    CPU cpu;
    Memory memory{0x8000};
    VirtualTime time;

    Source() {
        memory.write_u32(0x1000, 0xD4400000u);
        cpu.initialize_entry(0x1000, memory);
        cpu.bind_virtual_time(&time, 0);
        cpu.set_stack_pointer(0x7000);
    }
    snapshot::SnapshotRestoreContext context() { return {&cpu, &memory, &time, &cpu.get_gic(), {}}; }
};
void partial_and_duplicate_snapshots() {
    elf_test::File file; file.write(elf_test::image());
    ElfSession target(config());
    CHECK(target.load(file.path()).status == ExecutionStatus::completed);
    const auto original = target.hashes();
    const auto path = file.path().parent_path() / "partial.wsnap";
    Source source;
    for (unsigned missing = 0; missing < 4; ++missing) {
        snapshot::SnapshotCaptureOptions options;
        options.include_timers = missing != 0;
        options.include_gic = missing != 1;
        options.include_pmu = missing != 2;
        options.include_debug = missing != 3;
        CHECK(snapshot::SnapshotManager::capture_to_file(source.context(), path, options).success);
        CHECK(snapshot::SnapshotManager::validate_file(path).valid); // Generic format permits partial state.
        CHECK(!target.restore(path).success);
        CHECK(target.hashes() == original);
    }
    std::vector<std::uint8_t> bytes;
    CHECK(snapshot::SnapshotManager::capture_to_buffer(source.context(), bytes).success);
    snapshot::WSnapHeader header;
    CHECK(snapshot::decode_wsnap_header(bytes.data(), bytes.size(), header).success);
    std::vector<snapshot::WSnapSectionDescriptor> directory;
    CHECK(snapshot::parse_and_validate_directory(bytes.data(), bytes.size(), header, directory).success);
    for (std::size_t i = 0; i < directory.size(); ++i) {
        if (directory[i].section_type == snapshot::SnapshotSectionType::Metadata) {
            directory[i].section_type = snapshot::SnapshotSectionType::TimerState;
            snapshot::encode_section_descriptor(directory[i], bytes.data() + header.section_dir_offset + i * 40);
        }
    }
    CHECK(snapshot::validate_snapshot_buffer(bytes.data(), bytes.size()).valid);
    write_bytes(path, bytes);
    CHECK(!target.restore(path).success);
    CHECK(target.hashes() == original);
    header.memory_capacity = 0x10000;
    snapshot::encode_wsnap_header(header, bytes.data());
    write_bytes(path, bytes);
    CHECK(!target.restore(path).success);
    CHECK(target.hashes() == original);
}
void retirement_overflow() {
    elf_test::File file;
    const auto dir = file.path().parent_path();
    Source source;
    source.cpu.set_instruction_retirement_count(std::numeric_limits<std::uint64_t>::max());
    const auto path = dir / "maximum.wsnap";
    CHECK(snapshot::SnapshotManager::capture_to_file(source.context(), path).success);
    ElfSession target(config());
    CHECK(target.restore(path).success);
    const auto initial = target.hashes();
    CHECK(target.run(1).status == ExecutionStatus::invalid_configuration);
    CHECK(target.retired() == std::numeric_limits<std::uint64_t>::max());
    CHECK(target.hashes() == initial);
    CHECK(!target.record(dir / "new.wsnap", dir / "new.sndl", 1).success);
    CHECK(!std::filesystem::exists(dir / "new.wsnap"));
    snapshot::SnapshotInspection info;
    CHECK(snapshot::SnapshotManager::inspect_file(path, info).success);
    sndl::WSndlHeader header;
    header.base_snapshot_hash = info.header.header_crc32c;
    sndl::SndlWriter writer;
    CHECK(writer.open(dir / "maximum.sndl", header).is_success());
    CHECK(writer.finalize(sndl::SndlFinalizationStatus::Completed, 1).is_success());
    replay::ReplayExecutionStats stats;
    CHECK(ElfSession::verify(path, dir / "maximum.sndl", initial, 1, stats).code
          == replay::ReplayErrorCode::InvalidConfiguration);
    CHECK(target.hashes() == initial);
}
void authoritative_gic_continuation() {
    elf_test::File file;
    const auto dir = file.path().parent_path();
    Source source;
    source.cpu.get_gic().write_icc_pmr(0x60);
    const auto expected = replay::ReplayStateHash::compute_state_hashes(
        source.cpu, source.memory, &source.time, &source.cpu.get_gic());
    CHECK(snapshot::SnapshotManager::capture_to_file(source.context(), dir / "gic.wsnap").success);
    ElfSession restored(config());
    CHECK(restored.restore(dir / "gic.wsnap").success);
    CHECK(restored.hashes() == expected);
    const auto recorded = restored.record(dir / "gic-base.wsnap", dir / "gic.sndl", 1);
    CHECK(recorded.success);
    source.cpu.step(source.memory);
    CHECK(recorded.final_hashes == replay::ReplayStateHash::compute_state_hashes(
        source.cpu, source.memory, &source.time, &source.cpu.get_gic()));
    replay::ReplaySession replayed;
    replay::ReplayConfig limits; limits.max_instructions = 1;
    limits.divergence_report_path.clear();
    CHECK(replayed.prepare(dir / "gic-base.wsnap", dir / "gic.sndl", limits).is_success());
    CHECK(replayed.gic() == &replayed.cpu()->get_gic());
    CHECK(replayed.cpu()->get_gic().read_icc_pmr() == 0x60);
    CHECK(replayed.run().is_success());
    CHECK(replayed.stats().final_state_hashes == recorded.final_hashes);
    replay::ReplayExecutionStats stats;
    CHECK(ElfSession::verify(dir / "gic-base.wsnap", dir / "gic.sndl",
                            recorded.final_hashes, 1, stats).is_success());
}
void strict_association_and_input_limits() {
    elf_test::File file; file.write(elf_test::image());
    const auto dir = file.path().parent_path();
    ElfSession target(config());
    CHECK(target.load(file.path()).status == ExecutionStatus::completed);
    const auto recorded = target.record(dir / "valid.wsnap", dir / "valid.sndl", 5);
    CHECK(recorded.success);
    replay::ReplayExecutionStats stats;
    CHECK(ElfSession::verify(dir / "valid.wsnap", dir / "valid.sndl", recorded.final_hashes, 1, stats).is_success());
    for (unsigned mode = 0; mode < 2; ++mode) {
        sndl::WSndlHeader header;
        header.core_count = mode == 0 ? 1 : 2;
        header.base_snapshot_hash = 0;
        sndl::SndlWriter writer;
        const auto path = dir / ("unbound" + std::to_string(mode) + ".sndl");
        CHECK(writer.open(path, header).is_success());
        CHECK(writer.finalize(sndl::SndlFinalizationStatus::Completed, 1).is_success());
        CHECK(ElfSession::verify(dir / "valid.wsnap", path, recorded.final_hashes, 1, stats).is_failure());
    }
    const auto before = target.hashes();
    const auto oversized = dir / "oversized.wsnap";
    {
        std::ofstream output(oversized, std::ios::binary);
        output.seekp(0x8000 + 1024 * 1024);
        output.put('x');
    }
    CHECK(!target.restore(oversized).success);
    CHECK(target.hashes() == before);
    CHECK(!target.restore(dir).success);
    CHECK(!target.restore(dir / "missing").success);
    CHECK(ElfSession::verify(dir / "valid.wsnap", dir / "missing", recorded.final_hashes, 1, stats).is_failure());
}
}
int main(int argc, char** argv) {
    const std::string suite = argc > 1 ? argv[1] : "all";
    if (suite != "integration") { partial_and_duplicate_snapshots(); retirement_overflow(); }
    if (suite != "unit") { strict_association_and_input_limits(); authoritative_gic_continuation(); }
    return test::summarize("stage52_session_admission_tests");
}
