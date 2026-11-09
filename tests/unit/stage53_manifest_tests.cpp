#include "runtime/session_manifest.h"
#include "snapshot/snapshot_manager.h"
#include "test_harness.h"
#include "../support/elf_fixture.h"

namespace {
SessionManifest recording(elf_test::File& file) {
    elf_test::Segment code; code.words = {0xD503201Fu, 0xD53BE020u, 0xD4400000u};
    file.write(elf_test::image({code}));
    const auto dir = file.path().parent_path();
    RuntimeConfig cfg{0x8000, 0, 0, 0x6000, 0x1000, 10, std::nullopt, false};
    ElfSession session(cfg);
    CHECK(session.load(file.path()).status == ExecutionStatus::completed);
    SessionManifest m;
    m.elf = session_file_identity(file.path(), ElfSession::kMaxMemory);
    m.initial = session.hashes(); m.memory = cfg.memory_size; m.budget = cfg.max_instructions;
    const auto result = session.record(dir / "base.wsnap", dir / "events.sndl", 10);
    CHECK(result.success);
    m.final = result.final_hashes; m.final_retired = session.retired();
    m.steps = result.execution.instructions_executed; m.events = result.records;
    m.snapshot = session_file_identity(dir / "base.wsnap", ElfSession::kMaxMemory);
    m.log = session_file_identity(dir / "events.sndl", ElfSession::kMaxMemory);
    winios::snapshot::SnapshotInspection info;
    CHECK(winios::snapshot::SnapshotManager::inspect_file(dir / "base.wsnap", info).success);
    m.snapshot_association = info.header.header_crc32c;
    return m;
}
void malformed_manifests() {
    elf_test::File file;
    const auto m = recording(file);
    const auto text = serialize_session_manifest(m);
    CHECK(serialize_session_manifest(parse_session_manifest(text)) == text);
    for (std::size_t length = 0; length < text.size(); ++length) {
        CHECK_INVALID_ARGUMENT(parse_session_manifest(std::string_view(text).substr(0, length)));
    }
    const auto replace = [&](const std::string& from, const std::string& to) {
        auto invalid = text; invalid.replace(invalid.find(from), from.size(), to);
        CHECK_INVALID_ARGUMENT(parse_session_manifest(invalid));
    };
    replace("winios_elf_manifest=1", "winios_elf_manifest=2");
    replace("memory=32768", "memory=18446744073709551616");
    replace("memory=32768", "memory=-1");
    replace("memory=32768", "memory=032768");
    replace("memory=32768", "memory=32768\nmemory=32768");
    replace("memory=32768", "unknown=32768");
    replace("initial_cpu=", "initial_cpu=g");
    replace("replay_verified=true", "replay_verified=false");
    CHECK_INVALID_ARGUMENT(parse_session_manifest(text + "extra=1\n"));
    CHECK_INVALID_ARGUMENT(parse_session_manifest(std::string(16385, 'x')));
    auto invalid = m; invalid.initial_retired = UINT64_MAX;
    CHECK_INVALID_ARGUMENT(serialize_session_manifest(invalid));
    invalid = m; invalid.final_retired++;
    CHECK_INVALID_ARGUMENT(serialize_session_manifest(invalid));
    invalid = m; invalid.budget = 0;
    CHECK_INVALID_ARGUMENT(serialize_session_manifest(invalid));
    const auto manifest = file.path().parent_path() / "expected.manifest";
    save_session_manifest(manifest, m);
    CHECK(serialize_session_manifest(load_session_manifest(manifest)) == text);
    CHECK_INVALID_ARGUMENT(save_session_manifest(manifest, m));
}
void expected_state_and_artifact_validation() {
    elf_test::File file;
    const auto m = recording(file);
    const auto dir = file.path().parent_path();
    const auto verify = [&](const SessionManifest& value) {
        verify_session_manifest(value, file.path(), dir / "base.wsnap", dir / "events.sndl");
    };
    verify(m); verify(m);
    CHECK(m.events == 1);
    auto wrong = m; wrong.final.memory_hash ^= 1;
    CHECK_INVALID_ARGUMENT(verify(wrong));
    wrong = m; wrong.initial.gic_hash ^= 1;
    CHECK_INVALID_ARGUMENT(verify(wrong));
    wrong = m; wrong.initial_retired++; wrong.final_retired++;
    CHECK_INVALID_ARGUMENT(verify(wrong));
    wrong = m; wrong.snapshot_association ^= 1;
    CHECK_INVALID_ARGUMENT(verify(wrong));
    wrong = m; wrong.events++;
    CHECK_INVALID_ARGUMENT(verify(wrong));
    wrong = m; wrong.completion = ExecutionStatus::instruction_limit_reached; wrong.budget = wrong.steps;
    CHECK_INVALID_ARGUMENT(verify(wrong));
    wrong = m; wrong.elf.crc32c ^= 1;
    CHECK_INVALID_ARGUMENT(verify(wrong));
    wrong = m; wrong.snapshot.crc32c ^= 1;
    CHECK_INVALID_ARGUMENT(verify(wrong));
    wrong = m; wrong.log.crc32c ^= 1;
    CHECK_INVALID_ARGUMENT(verify(wrong));
    file.write({0, 1, 2});
    CHECK_INVALID_ARGUMENT(verify(m));
}
}
int main() {
    malformed_manifests();
    expected_state_and_artifact_validation();
    return test::summarize("stage53_manifest_tests");
}
