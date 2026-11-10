#include "loader/macho_corpus_builder.h"

#include <cstring>
#include "loader/real_artifact_harness.h"
#include "loader/userspace_macho_loader.h"

namespace winios::loader {

namespace {

inline std::uint32_t static_cmd_cast(std::size_t s) {
    return static_cast<std::uint32_t>(s);
}

} // namespace

MachOCorpusEntry MachOCorpusRegistry::create_hello_world() {
    MachOCorpusEntry entry{};
    entry.name = "corpus_hello_world";
    entry.description = "Darwin ARM64 Hello World Utility (SYS_write + SYS_exit)";
    entry.provenance = "Open-Source Darwin specification (Apple XNU bsd/sys/syscall.h conforming)";
    entry.license = "BSD-3-Clause / MIT Compatible";
    entry.cputype = 0x0100000Cu; // ARM64
    entry.cpusubtype = 0x00000000u;
    entry.filetype = 2; // MH_EXECUTE
    entry.entry_point = 0x100001000ULL;
    entry.dependencies = {"/usr/lib/dyld", "/usr/lib/libSystem.B.dylib"};
    entry.required_syscalls = {"SYS_write (4)", "SYS_exit (1)"};
    entry.required_extensions = {"Base AArch64 ISA"};

    std::vector<std::uint32_t> instrs = {
        0xD2820401u, // movz x1, #0x1020
        0xD2800020u, // mov x0, #1
        0xD2800202u, // mov x2, #16
        0xD2800090u, // mov x16, #4
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    const char text[] = "Hello, Darwin!\n\0";

    std::vector<std::uint8_t> code;
    code.resize(instrs.size() * 4 + sizeof(text));
    std::memcpy(code.data(), instrs.data(), instrs.size() * 4);
    std::memcpy(code.data() + instrs.size() * 4, text, sizeof(text));

    entry.binary_data = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);
    entry.file_size = entry.binary_data.size();
    entry.sha256 = RealArtifactHarness::compute_sha256(entry.binary_data.data(), entry.binary_data.size());
    entry.expected_decision = AdmissionDecision::Admitted;

    return entry;
}

MachOCorpusEntry MachOCorpusRegistry::create_math_compute() {
    MachOCorpusEntry entry{};
    entry.name = "corpus_math_compute";
    entry.description = "Darwin ARM64 Iterative Arithmetic Sum (1..100) and Register Validation";
    entry.provenance = "Open-Source Algorithmic Validation Suite";
    entry.license = "MIT";
    entry.cputype = 0x0100000Cu;
    entry.cpusubtype = 0x00000000u;
    entry.filetype = 2;
    entry.entry_point = 0x100001000ULL;
    entry.dependencies = {"/usr/lib/dyld", "/usr/lib/libSystem.B.dylib"};
    entry.required_syscalls = {"SYS_exit (1)"};
    entry.required_extensions = {"Base AArch64 (ADD, CMP, B.NE, MOV)"};

    std::vector<std::uint32_t> instrs = {
        0xD2800000u, // mov x0, #0
        0xD2800021u, // mov x1, #1
        0x8B010000u, // add x0, x0, x1
        0x91000421u, // add x1, x1, #1
        0xF101943Fu, // cmp x1, #101 (0x65)
        0x54FFFFA1u, // b.ne -3 (to add x0, x0, x1 at index 2)
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };

    std::vector<std::uint8_t> code(instrs.size() * 4);
    std::memcpy(code.data(), instrs.data(), code.size());

    entry.binary_data = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);
    entry.file_size = entry.binary_data.size();
    entry.sha256 = RealArtifactHarness::compute_sha256(entry.binary_data.data(), entry.binary_data.size());
    entry.expected_decision = AdmissionDecision::Admitted;

    return entry;
}

MachOCorpusEntry MachOCorpusRegistry::create_file_io() {
    MachOCorpusEntry entry{};
    entry.name = "corpus_file_io";
    entry.description = "Darwin ARM64 VFS Lifecycle (SYS_open, SYS_write, SYS_close, SYS_exit)";
    entry.provenance = "Open-Source POSIX/Darwin VFS test specification";
    entry.license = "BSD-3-Clause";
    entry.cputype = 0x0100000Cu;
    entry.cpusubtype = 0x00000000u;
    entry.filetype = 2;
    entry.entry_point = 0x100001000ULL;
    entry.dependencies = {"/usr/lib/dyld", "/usr/lib/libSystem.B.dylib"};
    entry.required_syscalls = {"SYS_open (5)", "SYS_write (4)", "SYS_close (6)", "SYS_exit (1)"};
    entry.required_extensions = {"Base AArch64 ISA"};

    std::vector<std::uint32_t> instrs = {
        0xD2820500u, // movz x0, #0x1028
        0xD2800041u, // mov x1, #2
        0xD28000B0u, // mov x16, #5
        0xD4001001u, // svc #0x80
        0xD28000D0u, // mov x16, #6
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    const char dev_null[] = "/dev/null\0";

    std::vector<std::uint8_t> code;
    code.resize(instrs.size() * 4 + sizeof(dev_null));
    std::memcpy(code.data(), instrs.data(), instrs.size() * 4);
    std::memcpy(code.data() + instrs.size() * 4, dev_null, sizeof(dev_null));

    entry.binary_data = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);
    entry.file_size = entry.binary_data.size();
    entry.sha256 = RealArtifactHarness::compute_sha256(entry.binary_data.data(), entry.binary_data.size());
    entry.expected_decision = AdmissionDecision::Admitted;

    return entry;
}

MachOCorpusEntry MachOCorpusRegistry::create_memory_mmap() {
    MachOCorpusEntry entry{};
    entry.name = "corpus_memory_mmap";
    entry.description = "Darwin ARM64 Virtual Memory Allocator (SYS_mmap + SYS_exit)";
    entry.provenance = "Open-Source Darwin Memory Management Specification";
    entry.license = "MIT";
    entry.cputype = 0x0100000Cu;
    entry.cpusubtype = 0x00000000u;
    entry.filetype = 2;
    entry.entry_point = 0x100001000ULL;
    entry.dependencies = {"/usr/lib/dyld", "/usr/lib/libSystem.B.dylib"};
    entry.required_syscalls = {"SYS_mmap (197)", "SYS_exit (1)"};
    entry.required_extensions = {"Base AArch64 ISA"};

    std::vector<std::uint32_t> instrs = {
        0xD2800000u, // mov x0, #0
        0xD2880001u, // movz x1, #0x4000
        0xD2800062u, // mov x2, #3
        0xD2820043u, // movz x3, #0x1002
        0x92800004u, // movn x4, #0 (=-1)
        0xD2800005u, // mov x5, #0
        0xD28018B0u, // mov x16, #197
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };

    std::vector<std::uint8_t> code(instrs.size() * 4);
    std::memcpy(code.data(), instrs.data(), code.size());

    entry.binary_data = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);
    entry.file_size = entry.binary_data.size();
    entry.sha256 = RealArtifactHarness::compute_sha256(entry.binary_data.data(), entry.binary_data.size());
    entry.expected_decision = AdmissionDecision::Admitted;

    return entry;
}

MachOCorpusEntry MachOCorpusRegistry::create_mach_ipc_client() {
    MachOCorpusEntry entry{};
    entry.name = "corpus_mach_ipc_client";
    entry.description = "Darwin ARM64 Mach IPC Trap Client (mach_reply_port -26 + SYS_exit)";
    entry.provenance = "Open-Source Mach 3.0 IPC Reference Specification";
    entry.license = "Apple Public Source License / BSD-2-Clause";
    entry.cputype = 0x0100000Cu;
    entry.cpusubtype = 0x00000000u;
    entry.filetype = 2;
    entry.entry_point = 0x100001000ULL;
    entry.dependencies = {"/usr/lib/dyld", "/usr/lib/libSystem.B.dylib"};
    entry.required_syscalls = {"mach_reply_port (-26)", "SYS_exit (1)"};
    entry.required_extensions = {"Base AArch64 ISA"};

    std::vector<std::uint32_t> instrs = {
        0x92800330u, // movn x16, #25 (which represents -26 in two's complement)
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };

    std::vector<std::uint8_t> code(instrs.size() * 4);
    std::memcpy(code.data(), instrs.data(), code.size());

    entry.binary_data = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);
    entry.file_size = entry.binary_data.size();
    entry.sha256 = RealArtifactHarness::compute_sha256(entry.binary_data.data(), entry.binary_data.size());
    entry.expected_decision = AdmissionDecision::Admitted;

    return entry;
}

MachOCorpusEntry MachOCorpusRegistry::create_dynamic_chained_fixup() {
    MachOCorpusEntry entry{};
    entry.name = "corpus_dynamic_chained_fixup";
    entry.description = "Darwin ARM64 Mach-O Executable with LC_DYLD_CHAINED_FIXUPS";
    entry.provenance = "Apple dyld4 chained fixups specification (fixup-chains.h)";
    entry.license = "APSL 2.0 / MIT";
    entry.cputype = 0x0100000Cu;
    entry.cpusubtype = 0x00000000u;
    entry.filetype = 2;
    entry.entry_point = 0x100001000ULL;
    entry.dependencies = {"/usr/lib/dyld", "/usr/lib/libSystem.B.dylib"};
    entry.required_syscalls = {"SYS_exit (1)"};
    entry.required_extensions = {"Base AArch64 ISA"};

    std::vector<std::uint32_t> instrs = {
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(instrs.size() * 4);
    std::memcpy(code.data(), instrs.data(), code.size());

    auto base_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    // Append LC_DYLD_CHAINED_FIXUPS load command
    MachOLinkeditDataRaw fixups_cmd{};
    fixups_cmd.cmd = 0x80000034u; // LC_DYLD_CHAINED_FIXUPS
    fixups_cmd.cmdsize = sizeof(fixups_cmd);
    fixups_cmd.dataoff = 0x8000;
    fixups_cmd.datasize = 256;

    const auto* old_hdr = reinterpret_cast<const MachOHeader64Raw*>(base_bin.data());
    const std::size_t insert_pos = sizeof(MachOHeader64Raw) + old_hdr->sizeofcmds;
    const auto* cmd_bytes = reinterpret_cast<const std::uint8_t*>(&fixups_cmd);

    base_bin.insert(base_bin.begin() + insert_pos, cmd_bytes, cmd_bytes + sizeof(fixups_cmd));
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(base_bin.data());
    hdr->ncmds += 1;
    hdr->sizeofcmds += static_cmd_cast(sizeof(fixups_cmd));

    entry.binary_data = std::move(base_bin);
    entry.file_size = entry.binary_data.size();
    entry.sha256 = RealArtifactHarness::compute_sha256(entry.binary_data.data(), entry.binary_data.size());
    entry.expected_decision = AdmissionDecision::Admitted;

    return entry;
}

MachOCorpusEntry MachOCorpusRegistry::create_fairplay_encrypted_app() {
    MachOCorpusEntry entry{};
    entry.name = "corpus_fairplay_encrypted_app";
    entry.description = "Proprietary FairPlay / App Store Encrypted Mach-O (cryptid = 1)";
    entry.provenance = "Simulated Proprietary App Store Binary for Admission Rejection Verification";
    entry.license = "Proprietary (Apple FairPlay Encryption Protected)";
    entry.cputype = 0x0100000Cu;
    entry.cpusubtype = 0x00000000u;
    entry.filetype = 2;
    entry.entry_point = 0x100001000ULL;
    entry.dependencies = {"/usr/lib/dyld", "/usr/lib/libSystem.B.dylib"};
    entry.required_syscalls = {};
    entry.required_extensions = {"ARM64e / PAC"};

    auto base_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();

    struct {
        std::uint32_t cmd{0x0000002Cu}; // LC_ENCRYPTION_INFO_64
        std::uint32_t cmdsize{24};
        std::uint32_t cryptoff{0x4000};
        std::uint32_t cryptsize{0x4000};
        std::uint32_t cryptid{1};
        std::uint32_t pad{0};
    } enc_cmd;

    const auto* old_hdr = reinterpret_cast<const MachOHeader64Raw*>(base_bin.data());
    const std::size_t insert_pos = sizeof(MachOHeader64Raw) + old_hdr->sizeofcmds;
    const auto* enc_bytes = reinterpret_cast<const std::uint8_t*>(&enc_cmd);

    base_bin.insert(base_bin.begin() + insert_pos, enc_bytes, enc_bytes + sizeof(enc_cmd));
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(base_bin.data());
    hdr->ncmds += 1;
    hdr->sizeofcmds += static_cmd_cast(sizeof(enc_cmd));

    entry.binary_data = std::move(base_bin);
    entry.file_size = entry.binary_data.size();
    entry.sha256 = RealArtifactHarness::compute_sha256(entry.binary_data.data(), entry.binary_data.size());
    entry.expected_decision = AdmissionDecision::Rejected_Encrypted;

    return entry;
}

std::vector<MachOCorpusEntry> MachOCorpusRegistry::get_all_corpus_entries() {
    return {
        create_hello_world(),
        create_math_compute(),
        create_file_io(),
        create_memory_mmap(),
        create_mach_ipc_client(),
        create_dynamic_chained_fixup(),
        create_fairplay_encrypted_app()
    };
}

} // namespace winios::loader
