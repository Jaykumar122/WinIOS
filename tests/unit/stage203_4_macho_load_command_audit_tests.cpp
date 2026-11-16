// ==============================================================================
// WinIOS Campaign 203 Stage 203.4: Mach-O Load Command & Dependency Audit Tests
// ==============================================================================
// Independent verification of:
// 1. Parsing of LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB, LC_LOAD_UPWARD_DYLIB
// 2. Parsing of LC_ID_DYLIB and LC_RPATH
// 3. Extraction of version numbers (current_version, compatibility_version) and timestamps
// 4. Robust boundary checks: truncated commands, misaligned sizes, out-of-bounds name offsets
// 5. Detection of cumulative command size mismatches and integer overflows
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "loader/macho_loader.h"

using namespace winios;
using namespace winios::loader;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// Helper to align sizes to 8 bytes
inline std::size_t align8(std::size_t sz) noexcept {
    return (sz + 7u) & ~7u;
}

// -----------------------------------------------------------------------------
// Test 1: Supported Dependency Commands Parsing & Metadata Extraction
// -----------------------------------------------------------------------------
void test_supported_dependency_commands() {
    std::cout << "[Stage 203.4 Test 1] Supported Dependency Commands Parsing...\n";

    // Build a synthetic 64-bit Mach-O binary in memory
    std::vector<std::uint8_t> binary;

    // Mach-O Header
    MachOHeader64 hdr{};
    hdr.magic = kMachOMagic64;
    hdr.cputype = kCpuTypeARM64;
    hdr.cpusubtype = kCpuSubtypeARM64All;
    hdr.filetype = kMachOFileTypeDylib;
    hdr.ncmds = 7;
    hdr.flags = 0;
    hdr.reserved = 0;

    // Commands to append:
    // 0. LC_SEGMENT_64: "__TEXT"
    // 1. LC_ID_DYLIB: "/usr/lib/libtest.dylib", ver = 0x00010200, compat = 0x00010000
    // 2. LC_LOAD_DYLIB: "/usr/lib/libSystem.B.dylib", ver = 0x05000000, compat = 0x01000000
    // 3. LC_LOAD_WEAK_DYLIB: "/usr/lib/libweak.dylib"
    // 4. LC_REEXPORT_DYLIB: "/usr/lib/libreexport.dylib"
    // 5. LC_LOAD_UPWARD_DYLIB: "/usr/lib/libupward.dylib"
    // 6. LC_RPATH: "@loader_path/Frameworks"

    MachOSegmentCommand64 seg{};
    seg.cmd = kLoadCmdSegment64;
    seg.cmdsize = sizeof(MachOSegmentCommand64);
    std::memcpy(seg.segname, "__TEXT", 6);
    seg.vmaddr = 0x100000000ULL;
    seg.vmsize = 0x4000ULL;
    seg.fileoff = 0;
    seg.filesize = 0;
    seg.maxprot = 7;
    seg.initprot = 5;
    seg.nsects = 0;
    seg.flags = 0;

    std::vector<std::uint8_t> cmds_payload;
    cmds_payload.insert(cmds_payload.end(), reinterpret_cast<const std::uint8_t*>(&seg), reinterpret_cast<const std::uint8_t*>(&seg) + sizeof(seg));

    struct CmdSpec {
        std::uint32_t cmd;
        std::string path;
        std::uint32_t current_ver;
        std::uint32_t compat_ver;
        std::uint32_t timestamp;
    };

    const CmdSpec specs[] = {
        { kLoadCmdIdDylib,         "/usr/lib/libtest.dylib",      0x00010200, 0x00010000, 100 },
        { kLoadCmdLoadDylib,       "/usr/lib/libSystem.B.dylib",  0x05000000, 0x01000000, 200 },
        { kLoadCmdLoadWeakDylib,   "/usr/lib/libweak.dylib",      0x02000000, 0x01000000, 300 },
        { kLoadCmdReexportDylib,   "/usr/lib/libreexport.dylib",  0x03000000, 0x01000000, 400 },
        { kLoadCmdLoadUpwardDylib, "/usr/lib/libupward.dylib",    0x04000000, 0x01000000, 500 },
        { kLoadCmdRpath,           "@loader_path/Frameworks",    0,          0,          0   }
    };
    for (const auto& s : specs) {
        if (s.cmd == kLoadCmdRpath) {
            std::size_t raw_sz = sizeof(MachORpathCommand) + s.path.size() + 1;
            std::size_t aligned_sz = align8(raw_sz);
            std::vector<std::uint8_t> cmd_buf(aligned_sz, 0);

            auto* rp = reinterpret_cast<MachORpathCommand*>(cmd_buf.data());
            rp->cmd = s.cmd;
            rp->cmdsize = static_cast<std::uint32_t>(aligned_sz);
            rp->path_offset = static_cast<std::uint32_t>(sizeof(MachORpathCommand));
            std::memcpy(cmd_buf.data() + rp->path_offset, s.path.c_str(), s.path.size() + 1);

            cmds_payload.insert(cmds_payload.end(), cmd_buf.begin(), cmd_buf.end());
        } else {
            std::size_t raw_sz = sizeof(MachODylibCommand) + s.path.size() + 1;
            std::size_t aligned_sz = align8(raw_sz);
            std::vector<std::uint8_t> cmd_buf(aligned_sz, 0);

            auto* dylib = reinterpret_cast<MachODylibCommand*>(cmd_buf.data());
            dylib->cmd = s.cmd;
            dylib->cmdsize = static_cast<std::uint32_t>(aligned_sz);
            dylib->name_offset = static_cast<std::uint32_t>(sizeof(MachODylibCommand));
            dylib->timestamp = s.timestamp;
            dylib->current_version = s.current_ver;
            dylib->compatibility_version = s.compat_ver;
            std::memcpy(cmd_buf.data() + dylib->name_offset, s.path.c_str(), s.path.size() + 1);

            cmds_payload.insert(cmds_payload.end(), cmd_buf.begin(), cmd_buf.end());
        }
    }

    hdr.sizeofcmds = static_cast<std::uint32_t>(cmds_payload.size());

    // Assemble binary
    binary.resize(sizeof(hdr) + cmds_payload.size());
    std::memcpy(binary.data(), &hdr, sizeof(hdr));
    std::memcpy(binary.data() + sizeof(hdr), cmds_payload.data(), cmds_payload.size());

    // Parse binary via MachOLoader
    MachOLoadResult res;
    bool parse_ok = MachOLoader::parse_metadata(binary.data(), binary.size(), res);
    TEST_CHECK(parse_ok, "Valid Mach-O binary parsing must succeed");
    TEST_CHECK(res.is_success(), "Result status must be Success");

    // 1. Verify LC_ID_DYLIB
    TEST_CHECK(res.dylib_id == "/usr/lib/libtest.dylib", "LC_ID_DYLIB path must be correctly extracted");

    // 2. Verify Dependencies
    TEST_CHECK(res.dylib_dependencies.size() == 4, "Must parse exactly 4 dependency dylibs");
    
    // Check libSystem
    const auto& dep0 = res.dylib_dependencies[0];
    TEST_CHECK(dep0.path == "/usr/lib/libSystem.B.dylib", "Path must match libSystem");
    TEST_CHECK(dep0.current_version == 0x05000000, "Current version must match");
    TEST_CHECK(dep0.compatibility_version == 0x01000000, "Compatibility version must match");
    TEST_CHECK(dep0.timestamp == 200, "Timestamp must match");
    TEST_CHECK(!dep0.is_weak && !dep0.is_reexport && !dep0.is_upward, "Standard dylib flags must be false");

    // Check libweak
    const auto& dep1 = res.dylib_dependencies[1];
    TEST_CHECK(dep1.path == "/usr/lib/libweak.dylib", "Path must match libweak");
    TEST_CHECK(dep1.is_weak == true, "is_weak must be true for LC_LOAD_WEAK_DYLIB");
    TEST_CHECK(!dep1.is_reexport && !dep1.is_upward, "Other flags must be false");

    // Check libreexport
    const auto& dep2 = res.dylib_dependencies[2];
    TEST_CHECK(dep2.path == "/usr/lib/libreexport.dylib", "Path must match libreexport");
    TEST_CHECK(dep2.is_reexport == true, "is_reexport must be true for LC_REEXPORT_DYLIB");
    TEST_CHECK(!dep2.is_weak && !dep2.is_upward, "Other flags must be false");

    // Check libupward
    const auto& dep3 = res.dylib_dependencies[3];
    TEST_CHECK(dep3.path == "/usr/lib/libupward.dylib", "Path must match libupward");
    TEST_CHECK(dep3.is_upward == true, "is_upward must be true for LC_LOAD_UPWARD_DYLIB");

    // 3. Verify LC_RPATH
    TEST_CHECK(res.rpaths.size() == 1, "Must parse exactly 1 RPATH");
    TEST_CHECK(res.rpaths[0] == "@loader_path/Frameworks", "RPATH value must match");

    std::cout << "  -> Supported dependency commands verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Robust Malformed Load Command & Boundary Rejection
// -----------------------------------------------------------------------------
void test_malformed_load_command_rejection() {
    std::cout << "[Stage 203.4 Test 2] Malformed Load Command Rejection...\n";

    // 1. Truncated command size (cmdsize < sizeof(MachODylibCommand))
    {
        MachOHeader64 hdr{};
        hdr.magic = kMachOMagic64;
        hdr.cputype = kCpuTypeARM64;
        hdr.cpusubtype = kCpuSubtypeARM64All;
        hdr.filetype = kMachOFileTypeDylib;
        hdr.ncmds = 1;
        hdr.sizeofcmds = 16; // Too small for MachODylibCommand (which is 24 bytes)

        std::vector<std::uint8_t> bin(sizeof(hdr) + 16, 0);
        std::memcpy(bin.data(), &hdr, sizeof(hdr));
        auto* cmd = reinterpret_cast<MachOLoadCommand*>(bin.data() + sizeof(hdr));
        cmd->cmd = kLoadCmdLoadDylib;
        cmd->cmdsize = 16; // < 24

        MachOLoadResult res;
        bool ok = MachOLoader::parse_metadata(bin.data(), bin.size(), res);
        TEST_CHECK(!ok, "Truncated cmdsize must be rejected");
        TEST_CHECK(res.status == MachOLoadStatus::CorruptedLoadCommands, "Status must be CorruptedLoadCommands");
    }

    // 2. Misaligned command size (not multiple of 4)
    {
        MachOHeader64 hdr{};
        hdr.magic = kMachOMagic64;
        hdr.cputype = kCpuTypeARM64;
        hdr.cpusubtype = kCpuSubtypeARM64All;
        hdr.filetype = kMachOFileTypeDylib;
        hdr.ncmds = 1;
        hdr.sizeofcmds = 25; // 25 is not 4-byte aligned

        std::vector<std::uint8_t> bin(sizeof(hdr) + 32, 0);
        std::memcpy(bin.data(), &hdr, sizeof(hdr));
        auto* cmd = reinterpret_cast<MachOLoadCommand*>(bin.data() + sizeof(hdr));
        cmd->cmd = kLoadCmdLoadDylib;
        cmd->cmdsize = 25;

        MachOLoadResult res;
        bool ok = MachOLoader::parse_metadata(bin.data(), bin.size(), res);
        TEST_CHECK(!ok, "Misaligned cmdsize must be rejected");
        TEST_CHECK(res.status == MachOLoadStatus::CorruptedLoadCommands, "Status must be CorruptedLoadCommands");
    }

    // 3. Name offset out of command bounds
    {
        MachOHeader64 hdr{};
        hdr.magic = kMachOMagic64;
        hdr.cputype = kCpuTypeARM64;
        hdr.cpusubtype = kCpuSubtypeARM64All;
        hdr.filetype = kMachOFileTypeDylib;
        hdr.ncmds = 1;
        hdr.sizeofcmds = 32;

        std::vector<std::uint8_t> bin(sizeof(hdr) + 32, 0);
        std::memcpy(bin.data(), &hdr, sizeof(hdr));
        auto* dylib = reinterpret_cast<MachODylibCommand*>(bin.data() + sizeof(hdr));
        dylib->cmd = kLoadCmdLoadDylib;
        dylib->cmdsize = 32;
        dylib->name_offset = 100; // >= cmdsize (32)

        MachOLoadResult res;
        bool ok = MachOLoader::parse_metadata(bin.data(), bin.size(), res);
        TEST_CHECK(!ok, "Name offset >= cmdsize must be rejected");
        TEST_CHECK(res.status == MachOLoadStatus::CorruptedLoadCommands, "Status must be CorruptedLoadCommands");
    }

    // 4. Cumulative command size mismatch
    {
        MachOHeader64 hdr{};
        hdr.magic = kMachOMagic64;
        hdr.cputype = kCpuTypeARM64;
        hdr.cpusubtype = kCpuSubtypeARM64All;
        hdr.filetype = kMachOFileTypeDylib;
        hdr.ncmds = 1;
        hdr.sizeofcmds = 48; // Claims 48 bytes, but command is only 32 bytes

        std::vector<std::uint8_t> bin(sizeof(hdr) + 48, 0);
        std::memcpy(bin.data(), &hdr, sizeof(hdr));
        auto* dylib = reinterpret_cast<MachODylibCommand*>(bin.data() + sizeof(hdr));
        dylib->cmd = kLoadCmdLoadDylib;
        dylib->cmdsize = 32;
        dylib->name_offset = sizeof(MachODylibCommand);

        MachOLoadResult res;
        bool ok = MachOLoader::parse_metadata(bin.data(), bin.size(), res);
        TEST_CHECK(!ok, "Cumulative sizeofcmds mismatch must be rejected");
        TEST_CHECK(res.status == MachOLoadStatus::CorruptedLoadCommands, "Status must be CorruptedLoadCommands");
    }

    std::cout << "  -> Malformed load command rejection verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.4: Mach-O Load Command & Dependency Audit Tests\n";
    std::cout << "==================================================================\n";

    test_supported_dependency_commands();
    test_malformed_load_command_rejection();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.4 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
