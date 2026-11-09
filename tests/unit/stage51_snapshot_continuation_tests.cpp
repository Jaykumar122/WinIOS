#include "runtime/elf_session.h"
#include "snapshot/snapshot_serializer.h"
#include "test_harness.h"
#include "../support/elf_fixture.h"

#include <limits>

namespace {
RuntimeConfig config() { return {0x8000, 0, 0, 0x6000, 0x1000, 100, std::nullopt, false}; }
void retirement_and_legacy_payload() {
    CPU original, target;
    original.set_instruction_retirement_count(std::numeric_limits<std::uint64_t>::max());
    CHECK(winios::snapshot::serialize_cpu_state(original).size() == 1128);
    auto bytes = winios::snapshot::serialize_cpu_state_with_retirement(original);
    CHECK(bytes.size() == 1136);
    CHECK(winios::snapshot::deserialize_cpu_state(bytes.data(), bytes.size(), target).success);
    CHECK(target.get_instruction_retirement_count() == std::numeric_limits<std::uint64_t>::max());
    for (std::size_t size = 1129; size < 1136; ++size) {
        target.write_register(0, 0xAABB);
        CHECK(!winios::snapshot::deserialize_cpu_state(bytes.data(), size, target).success);
        CHECK(target.read_register(0) == 0xAABB);
        CHECK(target.get_instruction_retirement_count() == std::numeric_limits<std::uint64_t>::max());
    }
    bytes.resize(1128);
    CHECK(winios::snapshot::deserialize_cpu_state(bytes.data(), bytes.size(), target).success);
    CHECK(target.get_instruction_retirement_count() == 0);
}
void distinct_wait_states() {
    for (unsigned state = 0; state < 4; ++state) {
        CPU source, target;
        source.set_waiting_for_interrupt((state & 1) != 0);
        source.set_waiting_for_event((state & 2) != 0);
        auto bytes = winios::snapshot::serialize_cpu_state(source);
        CHECK(winios::snapshot::deserialize_cpu_state(bytes.data(), bytes.size(), target).success);
        CHECK(target.is_waiting_for_interrupt() == source.is_waiting_for_interrupt());
        CHECK(target.is_waiting_for_event() == source.is_waiting_for_event());
    }
}
void mid_execution_record_replay() {
    elf_test::File file;
    elf_test::Segment code;
    code.words = {0xD503201Fu, 0xD503201Fu, 0xD53BE020u, 0x91001400u, 0xD4400000u};
    file.write(elf_test::image({code}));
    const auto dir = file.path().parent_path();
    ElfSession live(config()), split(config()), restored(config());
    CHECK(live.load(file.path()).status == ExecutionStatus::completed);
    CHECK(split.load(file.path()).status == ExecutionStatus::completed);
    CHECK(live.run(20).status == ExecutionStatus::completed);
    CHECK(split.run(2).instructions_executed == 2);
    CHECK(split.retired() == 2);
    CHECK(split.capture(dir / "middle.wsnap").success);
    CHECK(restored.restore(dir / "middle.wsnap").success);
    CHECK(restored.retired() == 2);
    CHECK(restored.hashes() == split.hashes());
    const auto recorded = restored.record(dir / "base.wsnap", dir / "events.sndl", 20);
    CHECK(recorded.success);
    CHECK(recorded.records == 1);
    CHECK(recorded.execution.instructions_executed == 3);
    CHECK(recorded.execution.final_registers[0] == 7);
    CHECK(recorded.final_hashes == live.hashes());
    CHECK(restored.retired() == live.retired());
    for (int repeat = 0; repeat < 3; ++repeat) {
        winios::replay::ReplayExecutionStats stats;
        const auto result = ElfSession::verify(dir / "base.wsnap", dir / "events.sndl",
            recorded.final_hashes, 3, stats);
        if (result.is_failure()) std::cerr << result.message << '\n';
        CHECK(result.is_success());
        CHECK(stats.events_consumed == 1);
        CHECK(stats.final_state_hashes == live.hashes());
    }
}
void budget_and_faulted_continuations() {
    elf_test::File file;
    elf_test::Segment code; code.words = {0xD503201Fu, 0};
    file.write(elf_test::image({code}));
    ElfSession session(config());
    CHECK(session.load(file.path()).status == ExecutionStatus::completed);
    CHECK(session.run(1).instructions_executed == 1);
    const auto dir = file.path().parent_path();
    const auto result = session.record(dir / "fault.wsnap", dir / "fault.sndl", 5);
    CHECK(!result.success);
    CHECK(result.execution.status == ExecutionStatus::unsupported_instruction);
    CHECK(session.retired() == 1);
    code.words = {0xD503201Fu, 0x14000000u};
    file.write(elf_test::image({code}));
    CHECK(session.load(file.path()).status == ExecutionStatus::completed);
    CHECK(session.run(1).instructions_executed == 1);
    const auto bounded = session.record(dir / "loop.wsnap", dir / "loop.sndl", 4);
    CHECK(bounded.success);
    CHECK(bounded.execution.status == ExecutionStatus::instruction_limit_reached);
    CHECK(session.retired() == 5);
    winios::replay::ReplayExecutionStats stats;
    CHECK(ElfSession::verify(dir / "loop.wsnap", dir / "loop.sndl",
                            bounded.final_hashes, 4, stats).is_success());
}
}
int main(int argc, char** argv) {
    const std::string suite = argc > 1 ? argv[1] : "all";
    if (suite != "integration") { retirement_and_legacy_payload(); distinct_wait_states(); }
    if (suite != "unit") { mid_execution_record_replay(); budget_and_faulted_continuations(); }
    return test::summarize("stage51_snapshot_continuation_tests");
}
