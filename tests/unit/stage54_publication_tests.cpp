#include "runtime/artifact_publication.h"
#include "runtime/session_manifest.h"
#include "app/elf_session_cli.h"
#include "test_harness.h"
#include "../support/elf_fixture.h"
#include <atomic>
#include <barrier>
#include <fstream>
#include <iterator>
#include <thread>
#include <array>

namespace {
const std::array<std::uint8_t, 3> content{1, 2, 3};
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void unit_publication() {
    elf_test::File owner;
    const auto dir = owner.path().parent_path();
    const auto final = dir / "valid";
    {
        ArtifactPublication publication(final);
        publication.write(content);
        CHECK(!std::filesystem::exists(final));
        publication.commit();
        CHECK(!std::filesystem::exists(publication.temporary_path()));
    }
    CHECK(read(final) == std::string("\1\2\3", 3));
    CHECK_INVALID_ARGUMENT(ArtifactPublication{final});
    for (const auto failure : {PublicationFault::write, PublicationFault::flush,
                               PublicationFault::close, PublicationFault::publish}) {
        const auto output = dir / ("failed" + std::to_string(static_cast<int>(failure)));
        std::filesystem::path temporary;
        {
            ScopedPublicationFault fault(failure);
            CHECK_RUNTIME_ERROR(([&] {
                ArtifactPublication publication(output);
                temporary = publication.temporary_path();
                publication.write(content);
                publication.commit();
            })());
        }
        CHECK(!std::filesystem::exists(output));
        CHECK(!std::filesystem::exists(temporary));
        CHECK(read(final) == std::string("\1\2\3", 3));
    }
    {
        ArtifactPublication first(dir / "first", ".winios-fixed.tmp");
        first.write(content);
        CHECK_RUNTIME_ERROR((ArtifactPublication{dir / "second", ".winios-fixed.tmp"}));
        CHECK(read(first.temporary_path()) == std::string("\1\2\3", 3));
        CHECK(first.abort());
        CHECK(!std::filesystem::exists(first.temporary_path()));
    }
    const auto stale = dir / ".winios-stale.tmp";
    { std::ofstream file(stale); file << "not owned"; }
    {
        ArtifactPublication pending(dir / "interrupted");
        pending.write(content);
        CHECK(!std::filesystem::exists(dir / "interrupted"));
        CHECK(pending.abort());
        CHECK(pending.abort());
    }
    CHECK(read(stale) == "not owned");
    // A destination created after preflight must win over this transaction.
    {
        ArtifactPublication losing(dir / "late-collision");
        losing.write(content);
        { std::ofstream other(dir / "late-collision"); other << "existing valid data"; }
        CHECK_RUNTIME_ERROR(losing.commit());
        CHECK(read(dir / "late-collision") == "existing valid data");
    }
    CHECK(read(final) == std::string("\1\2\3", 3));
}
void concurrent_writers() {
    elf_test::File owner;
    const auto final = owner.path().parent_path() / "concurrent";
    std::barrier gate(4);
    std::atomic<int> winners{0}, failures{0};
    std::array<std::filesystem::path, 4> temporaries;
    std::array<std::thread, 4> workers;
    for (std::size_t i = 0; i < workers.size(); ++i) {
        workers[i] = std::thread([&, i] {
            bool arrived = false;
            try {
                ArtifactPublication publication(final);
                temporaries[i] = publication.temporary_path();
                const std::array<std::uint8_t, 3> value{static_cast<std::uint8_t>(i + 1), 9, 8};
                publication.write(value);
                arrived = true;
                gate.arrive_and_wait();
                publication.commit();
                ++winners;
            } catch (...) {
                if (!arrived) gate.arrive_and_drop();
                ++failures;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK(winners == 1); CHECK(failures == 3);
    const auto bytes = read(final);
    CHECK(bytes.size() == 3);
    CHECK(bytes[0] >= 1 && bytes[0] <= 4 && bytes[1] == 9 && bytes[2] == 8);
    for (const auto& temporary : temporaries) CHECK(!std::filesystem::exists(temporary));
    for (std::size_t i = 0; i < temporaries.size(); ++i)
        for (std::size_t j = i + 1; j < temporaries.size(); ++j) CHECK(temporaries[i] != temporaries[j]);
}
int cli(std::vector<std::string> args) {
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    try { return run_elf_session_cli(static_cast<int>(argv.size()), argv.data()); }
    catch (const std::exception&) { return 1; }
}
void workflow_failures() {
    for (const auto extension : {".wsnap", ".sndl", ".manifest"}) {
        for (const auto failure : {PublicationFault::write, PublicationFault::flush,
                                   PublicationFault::close, PublicationFault::publish}) {
            elf_test::File owner;
            owner.write(elf_test::image());
            const auto dir = owner.path().parent_path();
            std::vector<std::string> args{"win_ios", "elf-session", "record", "--elf", owner.path().string(),
                "--snapshot", (dir / "out.wsnap").string(), "--log", (dir / "out.sndl").string(),
                "--manifest", (dir / "out.manifest").string(), "--memory-size", "0x8000", "--max-instructions", "10"};
            {
                ScopedPublicationFault fault(failure, extension);
                CHECK(cli(args) == 1);
            }
            CHECK(!std::filesystem::exists(dir / "out.manifest"));
            for (const auto& entry : std::filesystem::directory_iterator(dir))
                CHECK(!entry.path().filename().string().starts_with(".winios-publish-"));
            if (std::string(extension) == ".wsnap") CHECK(!std::filesystem::exists(dir / "out.wsnap"));
            if (std::string(extension) != ".manifest") CHECK(!std::filesystem::exists(dir / "out.sndl"));
        }
    }
    elf_test::File owner;
    owner.write(elf_test::image());
    const auto dir = owner.path().parent_path();
    std::vector<std::string> args{"win_ios", "elf-session", "record", "--elf", owner.path().string(),
        "--snapshot", (dir / "out.wsnap").string(), "--log", (dir / "out.sndl").string(),
        "--manifest", (dir / "out.manifest").string(), "--memory-size", "0x8000", "--max-instructions", "10"};
    CHECK(cli(args) == 0);
    const auto manifest = load_session_manifest(dir / "out.manifest");
    verify_session_manifest(manifest, owner.path(), dir / "out.wsnap", dir / "out.sndl");
    CHECK(manifest.replay_verified);
    const auto before = read(dir / "out.manifest");
    CHECK(cli(args) == 1);
    CHECK(read(dir / "out.manifest") == before);
    { std::ofstream partial(dir / "bad.manifest"); partial << "winios_elf_manifest=1\n"; }
    CHECK_INVALID_ARGUMENT(load_session_manifest(dir / "bad.manifest"));
}
}
int main(int argc, char** argv) {
    const std::string suite = argc > 1 ? argv[1] : "all";
    if (suite != "integration") { unit_publication(); concurrent_writers(); }
    if (suite != "unit") workflow_failures();
    return test::summarize("stage54_publication_tests");
}
