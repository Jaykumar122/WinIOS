#include "runtime/elf_session.h"
#include "sndl/sndl_source_hooks.h"
#include "test_harness.h"
#include "../support/elf_fixture.h"

#include <fstream>
#include <iterator>
#include <thread>

namespace {
RuntimeConfig config() { return {0x8000, 0, 0, 0x6000, 0x1000, 100, std::nullopt, false}; }
void corrupt(const std::filesystem::path& source, const std::filesystem::path& dest) {
    std::ifstream input(source, std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
    bytes.at(bytes.size() / 2) ^= 0x55;
    std::ofstream output(dest, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
void snapshot_and_continuation() {
    elf_test::File file;
    elf_test::Segment code;
    code.words = {0xD2800540u, 0x91001400u, 0xD2802001u, 0xF9000020u, 0xD4400000u};
    file.write(elf_test::image({code}));
    const auto dir = file.path().parent_path();
    ElfSession live(config()), restored(config());
    CHECK(live.load(file.path()).status == ExecutionStatus::completed);
    const auto initial = live.hashes();
    CHECK(live.capture(dir / "initial.wsnap").success);
    CHECK(restored.restore(dir / "initial.wsnap").success);
    CHECK(restored.hashes() == initial);
    CHECK(live.run(2).status == ExecutionStatus::instruction_limit_reached);
    CHECK(live.capture(dir / "middle.wsnap").success);
    CHECK(restored.restore(dir / "middle.wsnap").success);
    const auto first = live.run(20), second = restored.run(20);
    CHECK(first.status == ExecutionStatus::completed);
    CHECK(second.status == ExecutionStatus::completed);
    CHECK(first.instructions_executed == 3);
    CHECK(first.final_registers == second.final_registers);
    CHECK(first.memory_at_0x100 == 47);
    CHECK(live.hashes() == restored.hashes());
    CHECK(restored.run(1).instructions_executed == 0);
}
void record_and_replay(bool counter) {
    elf_test::File file;
    elf_test::Segment code;
    code.words = counter
        ? std::vector<std::uint32_t>{0xD503201Fu, 0xD53BE020u, 0xD4400000u}
        : std::vector<std::uint32_t>{0xD2800540u, 0xD4400000u};
    file.write(elf_test::image({code}));
    const auto dir = file.path().parent_path();
    ElfSession session(config());
    CHECK(session.load(file.path()).status == ExecutionStatus::completed);
    const auto recording = session.record(dir / "base.wsnap", dir / "events.sndl", 20);
    CHECK(recording.success);
    CHECK(recording.execution.status == ExecutionStatus::completed);
    CHECK(recording.records == (counter ? 1u : 0u));
    for (int repeat = 0; repeat < 3; ++repeat) {
        winios::replay::ReplayExecutionStats stats;
        const auto result = ElfSession::verify(dir / "base.wsnap", dir / "events.sndl",
            recording.final_hashes, recording.execution.instructions_executed, stats);
        if (result.is_failure()) std::cerr << "Replay failure: " << result.message << '\n';
        CHECK(result.is_success());
        CHECK(stats.final_state_hashes == recording.final_hashes);
        CHECK(stats.events_consumed == recording.records);
    }
    corrupt(dir / "base.wsnap", dir / "bad.wsnap");
    const auto before = session.hashes();
    CHECK(!session.restore(dir / "bad.wsnap").success);
    CHECK(session.hashes() == before);
    corrupt(dir / "events.sndl", dir / "bad.sndl");
    winios::replay::ReplayExecutionStats stats;
    CHECK(ElfSession::verify(dir / "base.wsnap", dir / "bad.sndl",
                            recording.final_hashes, 20, stats).is_failure());
    auto wrong = recording.final_hashes; wrong.memory_hash ^= 1;
    CHECK(ElfSession::verify(dir / "base.wsnap", dir / "events.sndl", wrong,
        recording.execution.instructions_executed, stats).code
        == winios::replay::ReplayErrorCode::StateVerificationFailed);
    CHECK(!session.capture(dir / "base.wsnap").success);
}
void invalid_inputs_and_budgets() {
    auto cfg = config(); cfg.memory_size = ElfSession::kMaxMemory + 1;
    CHECK_INVALID_ARGUMENT(ElfSession{cfg});
    cfg = config(); cfg.max_instructions = 0;
    CHECK_INVALID_ARGUMENT(ElfSession{cfg});
    elf_test::File file;
    ElfSession session(config());
    const auto initial = session.hashes();
    CHECK(session.run(1).status == ExecutionStatus::invalid_configuration);
    CHECK(!session.restore(file.path()).success);
    CHECK(session.hashes() == initial);
    file.write(elf_test::image());
    CHECK(session.load(file.path()).status == ExecutionStatus::completed);
    CHECK(session.run(0).status == ExecutionStatus::invalid_configuration);
    CHECK(session.run(101).status == ExecutionStatus::invalid_configuration);
    const auto before = session.hashes();
    file.write({0, 1, 2});
    CHECK(session.load(file.path()).status == ExecutionStatus::load_error);
    CHECK(session.hashes() == before);
    const auto dir = file.path().parent_path();
    CHECK(!session.record(dir / "same", dir / "same", 1).success);
    CHECK(!std::filesystem::exists(dir / "same"));
    CHECK(!session.record(dir / "unused", file.path(), 1).success);
    CHECK(!std::filesystem::exists(dir / "unused"));
    winios::replay::ReplayExecutionStats stats;
    CHECK(ElfSession::verify(file.path(), file.path(), {}, 0, stats).is_failure());
}
void thread_binding() {
    winios::sndl::SndlRecorder first, second;
    CHECK(winios::sndl::get_active_recorder() == nullptr);
    {
        winios::sndl::ScopedThreadRecorder outer(&first);
        CHECK(winios::sndl::get_active_recorder() == &first);
        {
            winios::sndl::ScopedThreadRecorder inner(&second);
            CHECK(winios::sndl::get_active_recorder() == &second);
        }
        CHECK(winios::sndl::get_active_recorder() == &first);
        bool isolated = false;
        std::thread worker([&] { isolated = winios::sndl::get_active_recorder() == nullptr; });
        worker.join();
        CHECK(isolated);
    }
    CHECK(winios::sndl::get_active_recorder() == nullptr);
}
}
int main(int argc, char** argv) {
    const std::string suite = argc > 1 ? argv[1] : "all";
    if (suite != "integration") { invalid_inputs_and_budgets(); thread_binding(); }
    if (suite != "unit") { snapshot_and_continuation(); record_and_replay(false); record_and_replay(true); }
    return test::summarize("stage50_elf_session_tests");
}
