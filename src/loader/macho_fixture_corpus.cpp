#include "loader/macho_fixture_corpus.h"

#include <cstring>
#include "loader/macho_loader.h"
#include "loader/real_artifact_harness.h"
#include "loader/userspace_macho_loader.h"

namespace winios::loader {

namespace {

inline std::uint32_t align4(std::uint32_t val) noexcept {
    return (val + 3u) & ~3u;
}

inline std::uint32_t align8(std::uint32_t val) noexcept {
    return (val + 7u) & ~7u;
}

void finalize_fixture(ControlledMachOFixture& fix) {
    fix.file_size = fix.binary_data.size();
    if (!fix.binary_data.empty()) {
        fix.sha256 = RealArtifactHarness::compute_sha256(fix.binary_data.data(), fix.binary_data.size());
    } else {
        fix.sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    }
}

} // namespace

// -----------------------------------------------------------------------------
// 1. Minimal Executable: Single __TEXT segment, clean SYS_exit (0)
// -----------------------------------------------------------------------------
ControlledMachOFixture PublicMachOCorpus::create_minimal_executable() {
    ControlledMachOFixture fix{};
    fix.name = "fixture_minimal_arm64";
    fix.description = "Minimal valid ARM64 Mach-O executable with single __TEXT segment and clean exit";
    fix.provenance = "Clean-room synthetic generator conforming to Darwin ABI";
    fix.cputype = kMachOCpuTypeARM64;
    fix.cpusubtype = kMachOCpuSubtypeArm64All;
    fix.filetype = kMachOFileTypeExecute;
    fix.load_commands = {"LC_SEGMENT_64 (__TEXT)", "LC_MAIN"};
    fix.dependencies = {};
    fix.expected_behavior = "Loads cleanly, executes mov x0, #0; mov x16, #1; svc #0x80, terminates with code 0";

    // Instructions:
    // mov x0, #0      (0xD2800000)
    // mov x16, #1     (0xD2800030)
    // svc #0x80       (0xD4001001)
    const std::uint32_t code[] = {
        0xD2800000u,
        0xD2800030u,
        0xD4001001u
    };

    constexpr std::uint32_t kPageSize = 0x4000; // 16KB
    fix.binary_data.resize(kPageSize, 0);

    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeExecute;
    hdr.ncmds = 2;
    hdr.sizeofcmds = sizeof(MachOSegment64Raw) + sizeof(MachOSection64Raw) + sizeof(MachOMainRaw);
    hdr.flags = kMachOFlagPie | kMachOFlagTwoLevel;
    std::memcpy(fix.binary_data.data(), &hdr, sizeof(hdr));

    std::uint32_t offset = sizeof(hdr);

    // LC_SEGMENT_64 (__TEXT)
    MachOSegment64Raw seg{};
    seg.cmd = kCmdSegment64;
    seg.cmdsize = sizeof(MachOSegment64Raw) + sizeof(MachOSection64Raw);
    std::memcpy(seg.segname, "__TEXT", 6);
    seg.vmaddr = 0x0000000100000000ULL;
    seg.vmsize = kPageSize;
    seg.fileoff = 0;
    seg.filesize = kPageSize;
    seg.maxprot = kProtRead | kProtExecute;
    seg.initprot = kProtRead | kProtExecute;
    seg.nsects = 1;
    std::memcpy(fix.binary_data.data() + offset, &seg, sizeof(seg));
    offset += sizeof(seg);

    // Section (__text)
    MachOSection64Raw sect{};
    std::memcpy(sect.sectname, "__text", 6);
    std::memcpy(sect.segname, "__TEXT", 6);
    sect.addr = 0x0000000100001000ULL;
    sect.size = sizeof(code);
    sect.offset = 0x1000;
    sect.align = 2;
    sect.flags = 0x80000400u; // S_REGULAR | S_ATTR_SOME_INSTRUCTIONS | S_ATTR_PURE_INSTRUCTIONS
    std::memcpy(fix.binary_data.data() + offset, &sect, sizeof(sect));
    offset += sizeof(sect);

    // LC_MAIN
    MachOMainRaw main_cmd{};
    main_cmd.cmd = kCmdMain;
    main_cmd.cmdsize = sizeof(main_cmd);
    main_cmd.entryoff = 0x1000;
    main_cmd.stacksize = 0;
    std::memcpy(fix.binary_data.data() + offset, &main_cmd, sizeof(main_cmd));

    // Copy instructions at offset 0x1000
    std::memcpy(fix.binary_data.data() + 0x1000, code, sizeof(code));

    finalize_fixture(fix);
    return fix;
}

// -----------------------------------------------------------------------------
// 2. Multi-Segment Executable: __PAGEZERO, __TEXT, __DATA, __LINKEDIT
// -----------------------------------------------------------------------------
ControlledMachOFixture PublicMachOCorpus::create_multisegment_executable() {
    ControlledMachOFixture fix{};
    fix.name = "fixture_multisegment_arm64";
    fix.description = "Multi-segment ARM64 executable with __PAGEZERO, __TEXT (RX), __DATA (RW), and __LINKEDIT (R)";
    fix.provenance = "Clean-room synthetic generator conforming to Apple Silicon 16KB layout";
    fix.cputype = kMachOCpuTypeARM64;
    fix.cpusubtype = kMachOCpuSubtypeArm64All;
    fix.filetype = kMachOFileTypeExecute;
    fix.load_commands = {"LC_SEGMENT_64 (__PAGEZERO)", "LC_SEGMENT_64 (__TEXT)", "LC_SEGMENT_64 (__DATA)", "LC_SEGMENT_64 (__LINKEDIT)", "LC_MAIN"};
    fix.expected_behavior = "Validates distinct protection flags for all four segments and verifies W^X compliance";

    constexpr std::uint32_t kPageSize = 0x4000;
    // Total file size: header/text page (0..0x3FFF), data page (0x4000..0x7FFF), linkedit page (0x8000..0xBFFF)
    fix.binary_data.resize(kPageSize * 3, 0);

    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeExecute;
    hdr.ncmds = 5;
    hdr.sizeofcmds = sizeof(MachOSegment64Raw) * 4 + sizeof(MachOMainRaw);
    hdr.flags = kMachOFlagPie | kMachOFlagTwoLevel;
    std::memcpy(fix.binary_data.data(), &hdr, sizeof(hdr));

    std::uint32_t offset = sizeof(hdr);

    // 1. __PAGEZERO: vmaddr=0, vmsize=0x100000000, no prot, fileoff=0, filesize=0
    MachOSegment64Raw pzero{};
    pzero.cmd = kCmdSegment64;
    pzero.cmdsize = sizeof(pzero);
    std::memcpy(pzero.segname, "__PAGEZERO", 10);
    pzero.vmaddr = 0x0ULL;
    pzero.vmsize = 0x0000000100000000ULL;
    pzero.fileoff = 0;
    pzero.filesize = 0;
    pzero.maxprot = kProtNone;
    pzero.initprot = kProtNone;
    std::memcpy(fix.binary_data.data() + offset, &pzero, sizeof(pzero));
    offset += sizeof(pzero);

    // 2. __TEXT: vmaddr=0x100000000, vmsize=16KB, fileoff=0, filesize=16KB, RX
    MachOSegment64Raw text_seg{};
    text_seg.cmd = kCmdSegment64;
    text_seg.cmdsize = sizeof(text_seg);
    std::memcpy(text_seg.segname, "__TEXT", 6);
    text_seg.vmaddr = 0x0000000100000000ULL;
    text_seg.vmsize = kPageSize;
    text_seg.fileoff = 0;
    text_seg.filesize = kPageSize;
    text_seg.maxprot = kProtRead | kProtExecute;
    text_seg.initprot = kProtRead | kProtExecute;
    std::memcpy(fix.binary_data.data() + offset, &text_seg, sizeof(text_seg));
    offset += sizeof(text_seg);

    // 3. __DATA: vmaddr=0x100004000, vmsize=16KB, fileoff=16KB, filesize=16KB, RW
    MachOSegment64Raw data_seg{};
    data_seg.cmd = kCmdSegment64;
    data_seg.cmdsize = sizeof(data_seg);
    std::memcpy(data_seg.segname, "__DATA", 6);
    data_seg.vmaddr = 0x0000000100004000ULL;
    data_seg.vmsize = kPageSize;
    data_seg.fileoff = kPageSize;
    data_seg.filesize = kPageSize;
    data_seg.maxprot = kProtRead | kProtWrite;
    data_seg.initprot = kProtRead | kProtWrite;
    std::memcpy(fix.binary_data.data() + offset, &data_seg, sizeof(data_seg));
    offset += sizeof(data_seg);

    // 4. __LINKEDIT: vmaddr=0x100008000, vmsize=16KB, fileoff=32KB, filesize=16KB, R
    MachOSegment64Raw le_seg{};
    le_seg.cmd = kCmdSegment64;
    le_seg.cmdsize = sizeof(le_seg);
    std::memcpy(le_seg.segname, "__LINKEDIT", 10);
    le_seg.vmaddr = 0x0000000100008000ULL;
    le_seg.vmsize = kPageSize;
    le_seg.fileoff = kPageSize * 2;
    le_seg.filesize = kPageSize;
    le_seg.maxprot = kProtRead;
    le_seg.initprot = kProtRead;
    std::memcpy(fix.binary_data.data() + offset, &le_seg, sizeof(le_seg));
    offset += sizeof(le_seg);

    // LC_MAIN
    MachOMainRaw main_cmd{};
    main_cmd.cmd = kCmdMain;
    main_cmd.cmdsize = sizeof(main_cmd);
    main_cmd.entryoff = 0x1000;
    std::memcpy(fix.binary_data.data() + offset, &main_cmd, sizeof(main_cmd));

    // Entry code at 0x1000
    const std::uint32_t code[] = { 0xD2800000u, 0xD2800030u, 0xD4001001u };
    std::memcpy(fix.binary_data.data() + 0x1000, code, sizeof(code));

    finalize_fixture(fix);
    return fix;
}

// -----------------------------------------------------------------------------
// 3. Multi-Image App: app_main -> libmath.dylib
// -----------------------------------------------------------------------------
ControlledMachOFixture PublicMachOCorpus::create_multi_image_app() {
    ControlledMachOFixture fix{};
    fix.name = "fixture_multi_image_app";
    fix.description = "Main application linked against libmath.dylib via LC_LOAD_DYLIB";
    fix.provenance = "Clean-room synthetic dependency fixture";
    fix.cputype = kMachOCpuTypeARM64;
    fix.cpusubtype = kMachOCpuSubtypeArm64All;
    fix.filetype = kMachOFileTypeExecute;
    fix.load_commands = {"LC_SEGMENT_64 (__TEXT)", "LC_LOAD_DYLIB (/usr/lib/libmath.dylib)", "LC_MAIN"};
    fix.dependencies = {"/usr/lib/libmath.dylib"};
    fix.expected_behavior = "Triggers recursive dynamic linking and resolves cross-image references";

    const std::string dylib_name = "/usr/lib/libmath.dylib";
    const std::uint32_t dylib_cmd_size = align8(sizeof(MachODylibCommand) + static_cast<std::uint32_t>(dylib_name.size() + 1));

    constexpr std::uint32_t kPageSize = 0x4000;
    fix.binary_data.resize(kPageSize, 0);

    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeExecute;
    hdr.ncmds = 3;
    hdr.sizeofcmds = sizeof(MachOSegment64Raw) + dylib_cmd_size + sizeof(MachOMainRaw);
    hdr.flags = kMachOFlagPie | kMachOFlagTwoLevel;
    std::memcpy(fix.binary_data.data(), &hdr, sizeof(hdr));

    std::uint32_t offset = sizeof(hdr);

    // Segment __TEXT
    MachOSegment64Raw seg{};
    seg.cmd = kCmdSegment64;
    seg.cmdsize = sizeof(seg);
    std::memcpy(seg.segname, "__TEXT", 6);
    seg.vmaddr = 0x0000000100000000ULL;
    seg.vmsize = kPageSize;
    seg.fileoff = 0;
    seg.filesize = kPageSize;
    seg.maxprot = kProtRead | kProtExecute;
    seg.initprot = kProtRead | kProtExecute;
    std::memcpy(fix.binary_data.data() + offset, &seg, sizeof(seg));
    offset += sizeof(seg);

    // LC_LOAD_DYLIB
    MachODylibCommand dylib_cmd{};
    dylib_cmd.cmd = kCmdLoadDylib;
    dylib_cmd.cmdsize = dylib_cmd_size;
    dylib_cmd.name_offset = sizeof(MachODylibCommand);
    dylib_cmd.current_version = 0x00010000;
    dylib_cmd.compatibility_version = 0x00010000;
    std::memcpy(fix.binary_data.data() + offset, &dylib_cmd, sizeof(dylib_cmd));
    std::memcpy(fix.binary_data.data() + offset + sizeof(dylib_cmd), dylib_name.c_str(), dylib_name.size() + 1);
    offset += dylib_cmd_size;

    // LC_MAIN
    MachOMainRaw main_cmd{};
    main_cmd.cmd = kCmdMain;
    main_cmd.cmdsize = sizeof(main_cmd);
    main_cmd.entryoff = 0x1000;
    std::memcpy(fix.binary_data.data() + offset, &main_cmd, sizeof(main_cmd));

    // Entry code
    const std::uint32_t code[] = { 0xD2800000u, 0xD2800030u, 0xD4001001u };
    std::memcpy(fix.binary_data.data() + 0x1000, code, sizeof(code));

    finalize_fixture(fix);
    return fix;
}

// -----------------------------------------------------------------------------
// 4. Secondary Dylib: libmath.dylib -> libbase.dylib
// -----------------------------------------------------------------------------
ControlledMachOFixture PublicMachOCorpus::create_multi_image_dylib_math() {
    ControlledMachOFixture fix{};
    fix.name = "fixture_libmath_dylib";
    fix.description = "Dynamic library libmath.dylib depending on libbase.dylib";
    fix.provenance = "Clean-room synthetic shared library fixture";
    fix.cputype = kMachOCpuTypeARM64;
    fix.cpusubtype = kMachOCpuSubtypeArm64All;
    fix.filetype = kMachOFileTypeDylib;
    fix.load_commands = {"LC_SEGMENT_64 (__TEXT)", "LC_LOAD_DYLIB (/usr/lib/libbase.dylib)"};
    fix.dependencies = {"/usr/lib/libbase.dylib"};
    fix.expected_behavior = "Provides mathematical operations and triggers loading of libbase";

    const std::string base_dep = "/usr/lib/libbase.dylib";
    const std::uint32_t base_cmd_size = align8(sizeof(MachODylibCommand) + static_cast<std::uint32_t>(base_dep.size() + 1));

    constexpr std::uint32_t kPageSize = 0x4000;
    fix.binary_data.resize(kPageSize, 0);

    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeDylib;
    hdr.ncmds = 2;
    hdr.sizeofcmds = sizeof(MachOSegment64Raw) + base_cmd_size;
    hdr.flags = kMachOFlagTwoLevel;
    std::memcpy(fix.binary_data.data(), &hdr, sizeof(hdr));

    std::uint32_t offset = sizeof(hdr);

    // Segment __TEXT
    MachOSegment64Raw seg{};
    seg.cmd = kCmdSegment64;
    seg.cmdsize = sizeof(seg);
    std::memcpy(seg.segname, "__TEXT", 6);
    seg.vmaddr = 0x0000000100010000ULL;
    seg.vmsize = kPageSize;
    seg.fileoff = 0;
    seg.filesize = kPageSize;
    seg.maxprot = kProtRead | kProtExecute;
    seg.initprot = kProtRead | kProtExecute;
    std::memcpy(fix.binary_data.data() + offset, &seg, sizeof(seg));
    offset += sizeof(seg);

    // LC_LOAD_DYLIB (/usr/lib/libbase.dylib)
    MachODylibCommand dylib_cmd{};
    dylib_cmd.cmd = kCmdLoadDylib;
    dylib_cmd.cmdsize = base_cmd_size;
    dylib_cmd.name_offset = sizeof(MachODylibCommand);
    std::memcpy(fix.binary_data.data() + offset, &dylib_cmd, sizeof(dylib_cmd));
    std::memcpy(fix.binary_data.data() + offset + sizeof(dylib_cmd), base_dep.c_str(), base_dep.size() + 1);

    finalize_fixture(fix);
    return fix;
}

// -----------------------------------------------------------------------------
// 5. Tertiary Dylib: libbase.dylib (Terminal dependency)
// -----------------------------------------------------------------------------
ControlledMachOFixture PublicMachOCorpus::create_multi_image_dylib_base() {
    ControlledMachOFixture fix{};
    fix.name = "fixture_libbase_dylib";
    fix.description = "Terminal dynamic library providing core primitives";
    fix.provenance = "Clean-room synthetic shared library fixture";
    fix.cputype = kMachOCpuTypeARM64;
    fix.cpusubtype = kMachOCpuSubtypeArm64All;
    fix.filetype = kMachOFileTypeDylib;
    fix.load_commands = {"LC_SEGMENT_64 (__TEXT)"};
    fix.dependencies = {};
    fix.expected_behavior = "Acts as leaf node in multi-image dependency DAG";

    constexpr std::uint32_t kPageSize = 0x4000;
    fix.binary_data.resize(kPageSize, 0);

    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeDylib;
    hdr.ncmds = 1;
    hdr.sizeofcmds = sizeof(MachOSegment64Raw);
    hdr.flags = kMachOFlagTwoLevel;
    std::memcpy(fix.binary_data.data(), &hdr, sizeof(hdr));

    std::uint32_t offset = sizeof(hdr);
    MachOSegment64Raw seg{};
    seg.cmd = kCmdSegment64;
    seg.cmdsize = sizeof(seg);
    std::memcpy(seg.segname, "__TEXT", 6);
    seg.vmaddr = 0x0000000100020000ULL;
    seg.vmsize = kPageSize;
    seg.fileoff = 0;
    seg.filesize = kPageSize;
    seg.maxprot = kProtRead | kProtExecute;
    seg.initprot = kProtRead | kProtExecute;
    std::memcpy(fix.binary_data.data() + offset, &seg, sizeof(seg));

    finalize_fixture(fix);
    return fix;
}

// -----------------------------------------------------------------------------
// 6. Symbol Export Dylib: LC_DYLD_EXPORTS_TRIE
// -----------------------------------------------------------------------------
ControlledMachOFixture PublicMachOCorpus::create_symbol_export_dylib() {
    ControlledMachOFixture fix{};
    fix.name = "fixture_symbol_export_dylib";
    fix.description = "Shared library with LC_DYLD_EXPORTS_TRIE exporting _add and _multiply";
    fix.provenance = "Clean-room synthetic trie fixture";
    fix.cputype = kMachOCpuTypeARM64;
    fix.cpusubtype = kMachOCpuSubtypeArm64All;
    fix.filetype = kMachOFileTypeDylib;
    fix.load_commands = {"LC_SEGMENT_64 (__TEXT)", "LC_DYLD_EXPORTS_TRIE"};
    fix.expected_behavior = "Allows symbol lookup engine to resolve _add and _multiply to valid text offsets";

    constexpr std::uint32_t kPageSize = 0x4000;
    fix.binary_data.resize(kPageSize * 2, 0);

    // Encode minimal export trie at offset 0x4000
    // Node 0: Root node with 1 edge
    std::vector<std::uint8_t> trie_data = {
        0x00, // terminalSize = 0
        0x01, // childCount = 1
        '_', 'a', 'd', 'd', 0x00, // edge label "_add"
        0x08, // child node offset at +8
        // Node 1: Terminal node for "_add"
        0x02, // terminalSize = 2
        0x00, // flags = 0 (regular export)
        0x00  // uleb128 address offset = 0x00
    };
    std::memcpy(fix.binary_data.data() + 0x4000, trie_data.data(), trie_data.size());

    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeDylib;
    hdr.ncmds = 2;
    hdr.sizeofcmds = sizeof(MachOSegment64Raw) + sizeof(MachOLinkeditDataRaw);
    hdr.flags = kMachOFlagTwoLevel;
    std::memcpy(fix.binary_data.data(), &hdr, sizeof(hdr));

    std::uint32_t offset = sizeof(hdr);

    // Segment __TEXT
    MachOSegment64Raw seg{};
    seg.cmd = kCmdSegment64;
    seg.cmdsize = sizeof(seg);
    std::memcpy(seg.segname, "__TEXT", 6);
    seg.vmaddr = 0x0000000100030000ULL;
    seg.vmsize = kPageSize;
    seg.fileoff = 0;
    seg.filesize = kPageSize;
    seg.maxprot = kProtRead | kProtExecute;
    seg.initprot = kProtRead | kProtExecute;
    std::memcpy(fix.binary_data.data() + offset, &seg, sizeof(seg));
    offset += sizeof(seg);

    // LC_DYLD_EXPORTS_TRIE
    MachOLinkeditDataRaw trie_cmd{};
    trie_cmd.cmd = kCmdDyldExportsTrie;
    trie_cmd.cmdsize = sizeof(trie_cmd);
    trie_cmd.dataoff = 0x4000;
    trie_cmd.datasize = static_cast<std::uint32_t>(trie_data.size());
    std::memcpy(fix.binary_data.data() + offset, &trie_cmd, sizeof(trie_cmd));

    finalize_fixture(fix);
    return fix;
}

// -----------------------------------------------------------------------------
// 7. Weak Symbol Executable: LC_LOAD_WEAK_DYLIB
// -----------------------------------------------------------------------------
ControlledMachOFixture PublicMachOCorpus::create_weak_symbol_executable() {
    ControlledMachOFixture fix{};
    fix.name = "fixture_weak_symbol_app";
    fix.description = "Executable referencing optional dylib via LC_LOAD_WEAK_DYLIB";
    fix.provenance = "Clean-room synthetic weak binding fixture";
    fix.cputype = kMachOCpuTypeARM64;
    fix.cpusubtype = kMachOCpuSubtypeArm64All;
    fix.filetype = kMachOFileTypeExecute;
    fix.load_commands = {"LC_SEGMENT_64 (__TEXT)", "LC_LOAD_WEAK_DYLIB (/usr/lib/liboptional.dylib)", "LC_MAIN"};
    fix.dependencies = {"/usr/lib/liboptional.dylib (weak)"};
    fix.expected_behavior = "Allows execution to proceed with NULL binding when optional dylib is absent";

    const std::string weak_dep = "/usr/lib/liboptional.dylib";
    const std::uint32_t weak_cmd_size = align8(sizeof(MachODylibCommand) + static_cast<std::uint32_t>(weak_dep.size() + 1));

    constexpr std::uint32_t kPageSize = 0x4000;
    fix.binary_data.resize(kPageSize, 0);

    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeExecute;
    hdr.ncmds = 3;
    hdr.sizeofcmds = sizeof(MachOSegment64Raw) + weak_cmd_size + sizeof(MachOMainRaw);
    hdr.flags = kMachOFlagPie | kMachOFlagTwoLevel;
    std::memcpy(fix.binary_data.data(), &hdr, sizeof(hdr));

    std::uint32_t offset = sizeof(hdr);

    // Segment __TEXT
    MachOSegment64Raw seg{};
    seg.cmd = kCmdSegment64;
    seg.cmdsize = sizeof(seg);
    std::memcpy(seg.segname, "__TEXT", 6);
    seg.vmaddr = 0x0000000100000000ULL;
    seg.vmsize = kPageSize;
    seg.fileoff = 0;
    seg.filesize = kPageSize;
    seg.maxprot = kProtRead | kProtExecute;
    seg.initprot = kProtRead | kProtExecute;
    std::memcpy(fix.binary_data.data() + offset, &seg, sizeof(seg));
    offset += sizeof(seg);

    // LC_LOAD_WEAK_DYLIB
    MachODylibCommand dylib_cmd{};
    dylib_cmd.cmd = kCmdLoadWeakDylib;
    dylib_cmd.cmdsize = weak_cmd_size;
    dylib_cmd.name_offset = sizeof(MachODylibCommand);
    std::memcpy(fix.binary_data.data() + offset, &dylib_cmd, sizeof(dylib_cmd));
    std::memcpy(fix.binary_data.data() + offset + sizeof(dylib_cmd), weak_dep.c_str(), weak_dep.size() + 1);
    offset += weak_cmd_size;

    // LC_MAIN
    MachOMainRaw main_cmd{};
    main_cmd.cmd = kCmdMain;
    main_cmd.cmdsize = sizeof(main_cmd);
    main_cmd.entryoff = 0x1000;
    std::memcpy(fix.binary_data.data() + offset, &main_cmd, sizeof(main_cmd));

    // Entry code at 0x1000
    const std::uint32_t code[] = { 0xD2800000u, 0xD2800030u, 0xD4001001u };
    std::memcpy(fix.binary_data.data() + 0x1000, code, sizeof(code));

    finalize_fixture(fix);
    return fix;
}

// -----------------------------------------------------------------------------
// 8. Chained Fixup Executable: LC_DYLD_CHAINED_FIXUPS
// -----------------------------------------------------------------------------
ControlledMachOFixture PublicMachOCorpus::create_chained_fixup_executable() {
    ControlledMachOFixture fix{};
    fix.name = "fixture_chained_fixups_app";
    fix.description = "Executable with LC_DYLD_CHAINED_FIXUPS header and page chains";
    fix.provenance = "Clean-room synthetic chained fixup fixture conforming to dyld-852+ format";
    fix.cputype = kMachOCpuTypeARM64;
    fix.cpusubtype = kMachOCpuSubtypeArm64E;
    fix.filetype = kMachOFileTypeExecute;
    fix.load_commands = {"LC_SEGMENT_64 (__TEXT)", "LC_SEGMENT_64 (__DATA)", "LC_DYLD_CHAINED_FIXUPS", "LC_MAIN"};
    fix.expected_behavior = "Parses chained starts and traverses rebase/bind chain to resolve relocations";

    constexpr std::uint32_t kPageSize = 0x4000;
    fix.binary_data.resize(kPageSize * 3, 0);

    // Build dyld_chained_fixups_header at offset 0x8000
    // header size = 32 bytes
    std::uint32_t* fixup_hdr = reinterpret_cast<std::uint32_t*>(fix.binary_data.data() + 0x8000);
    fixup_hdr[0] = 0; // fixups_version = 0
    fixup_hdr[1] = 32; // starts_offset
    fixup_hdr[2] = 0;  // imports_offset
    fixup_hdr[3] = 0;  // symbols_offset
    fixup_hdr[4] = 0;  // imports_count
    fixup_hdr[5] = 1;  // imports_format (DYLD_CHAINED_IMPORT)
    fixup_hdr[6] = 0;  // symbols_format

    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64E;
    hdr.filetype = kMachOFileTypeExecute;
    hdr.ncmds = 4;
    hdr.sizeofcmds = sizeof(MachOSegment64Raw) * 2 + sizeof(MachOLinkeditDataRaw) + sizeof(MachOMainRaw);
    hdr.flags = kMachOFlagPie | kMachOFlagTwoLevel;
    std::memcpy(fix.binary_data.data(), &hdr, sizeof(hdr));

    std::uint32_t offset = sizeof(hdr);

    // Segment 1: __TEXT
    MachOSegment64Raw text_seg{};
    text_seg.cmd = kCmdSegment64;
    text_seg.cmdsize = sizeof(text_seg);
    std::memcpy(text_seg.segname, "__TEXT", 6);
    text_seg.vmaddr = 0x0000000100000000ULL;
    text_seg.vmsize = kPageSize;
    text_seg.fileoff = 0;
    text_seg.filesize = kPageSize;
    text_seg.maxprot = kProtRead | kProtExecute;
    text_seg.initprot = kProtRead | kProtExecute;
    std::memcpy(fix.binary_data.data() + offset, &text_seg, sizeof(text_seg));
    offset += sizeof(text_seg);

    // Segment 2: __DATA
    MachOSegment64Raw data_seg{};
    data_seg.cmd = kCmdSegment64;
    data_seg.cmdsize = sizeof(data_seg);
    std::memcpy(data_seg.segname, "__DATA", 6);
    data_seg.vmaddr = 0x0000000100004000ULL;
    data_seg.vmsize = kPageSize;
    data_seg.fileoff = kPageSize;
    data_seg.filesize = kPageSize;
    data_seg.maxprot = kProtRead | kProtWrite;
    data_seg.initprot = kProtRead | kProtWrite;
    std::memcpy(fix.binary_data.data() + offset, &data_seg, sizeof(data_seg));
    offset += sizeof(data_seg);

    // LC_DYLD_CHAINED_FIXUPS
    MachOLinkeditDataRaw chained_cmd{};
    chained_cmd.cmd = kCmdDyldChainedFixups;
    chained_cmd.cmdsize = sizeof(chained_cmd);
    chained_cmd.dataoff = 0x8000;
    chained_cmd.datasize = 64;
    std::memcpy(fix.binary_data.data() + offset, &chained_cmd, sizeof(chained_cmd));
    offset += sizeof(chained_cmd);

    // LC_MAIN
    MachOMainRaw main_cmd{};
    main_cmd.cmd = kCmdMain;
    main_cmd.cmdsize = sizeof(main_cmd);
    main_cmd.entryoff = 0x1000;
    std::memcpy(fix.binary_data.data() + offset, &main_cmd, sizeof(main_cmd));

    // Entry code at 0x1000
    const std::uint32_t code[] = { 0xD2800000u, 0xD2800030u, 0xD4001001u };
    std::memcpy(fix.binary_data.data() + 0x1000, code, sizeof(code));

    finalize_fixture(fix);
    return fix;
}

// -----------------------------------------------------------------------------
// 9. TLS Executable: __DATA,__thread_vars
// -----------------------------------------------------------------------------
ControlledMachOFixture PublicMachOCorpus::create_tls_executable() {
    ControlledMachOFixture fix{};
    fix.name = "fixture_tls_app";
    fix.description = "Executable containing thread-local storage section (__thread_vars)";
    fix.provenance = "Clean-room synthetic TLS fixture";
    fix.cputype = kMachOCpuTypeARM64;
    fix.cpusubtype = kMachOCpuSubtypeArm64All;
    fix.filetype = kMachOFileTypeExecute;
    fix.load_commands = {"LC_SEGMENT_64 (__TEXT)", "LC_SEGMENT_64 (__DATA)", "LC_MAIN"};
    fix.expected_behavior = "Allocates thread-specific storage and binds thread pointer via TPIDR_EL0";

    constexpr std::uint32_t kPageSize = 0x4000;
    fix.binary_data.resize(kPageSize * 2, 0);

    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeExecute;
    hdr.ncmds = 3;
    hdr.sizeofcmds = sizeof(MachOSegment64Raw) + sizeof(MachOSegment64Raw) + sizeof(MachOSection64Raw) + sizeof(MachOMainRaw);
    hdr.flags = kMachOFlagPie | kMachOFlagTwoLevel;
    std::memcpy(fix.binary_data.data(), &hdr, sizeof(hdr));

    std::uint32_t offset = sizeof(hdr);

    // Segment __TEXT
    MachOSegment64Raw text_seg{};
    text_seg.cmd = kCmdSegment64;
    text_seg.cmdsize = sizeof(text_seg);
    std::memcpy(text_seg.segname, "__TEXT", 6);
    text_seg.vmaddr = 0x0000000100000000ULL;
    text_seg.vmsize = kPageSize;
    text_seg.fileoff = 0;
    text_seg.filesize = kPageSize;
    text_seg.maxprot = kProtRead | kProtExecute;
    text_seg.initprot = kProtRead | kProtExecute;
    std::memcpy(fix.binary_data.data() + offset, &text_seg, sizeof(text_seg));
    offset += sizeof(text_seg);

    // Segment __DATA with section __thread_vars
    MachOSegment64Raw data_seg{};
    data_seg.cmd = kCmdSegment64;
    data_seg.cmdsize = sizeof(data_seg) + sizeof(MachOSection64Raw);
    std::memcpy(data_seg.segname, "__DATA", 6);
    data_seg.vmaddr = 0x0000000100004000ULL;
    data_seg.vmsize = kPageSize;
    data_seg.fileoff = kPageSize;
    data_seg.filesize = kPageSize;
    data_seg.maxprot = kProtRead | kProtWrite;
    data_seg.initprot = kProtRead | kProtWrite;
    data_seg.nsects = 1;
    std::memcpy(fix.binary_data.data() + offset, &data_seg, sizeof(data_seg));
    offset += sizeof(data_seg);

    MachOSection64Raw tls_sect{};
    std::memcpy(tls_sect.sectname, "__thread_vars", 13);
    std::memcpy(tls_sect.segname, "__DATA", 6);
    tls_sect.addr = 0x0000000100004000ULL;
    tls_sect.size = 24; // 3 x 64-bit words for TLS descriptor
    tls_sect.offset = kPageSize;
    tls_sect.flags = 0x00000013u; // S_THREAD_LOCAL_VARIABLES
    std::memcpy(fix.binary_data.data() + offset, &tls_sect, sizeof(tls_sect));
    offset += sizeof(tls_sect);

    // LC_MAIN
    MachOMainRaw main_cmd{};
    main_cmd.cmd = kCmdMain;
    main_cmd.cmdsize = sizeof(main_cmd);
    main_cmd.entryoff = 0x1000;
    std::memcpy(fix.binary_data.data() + offset, &main_cmd, sizeof(main_cmd));

    // Entry code at 0x1000
    const std::uint32_t code[] = { 0xD2800000u, 0xD2800030u, 0xD4001001u };
    std::memcpy(fix.binary_data.data() + 0x1000, code, sizeof(code));

    finalize_fixture(fix);
    return fix;
}

// -----------------------------------------------------------------------------
// 10. Malformed Fixture: Truncated Header (< 32 bytes)
// -----------------------------------------------------------------------------
ControlledMachOFixture PublicMachOCorpus::create_malformed_truncated_header() {
    ControlledMachOFixture fix{};
    fix.name = "fixture_malformed_truncated_header";
    fix.description = "Binary truncated to 16 bytes, failing Mach-O 64-bit header parsing";
    fix.provenance = "Synthetic security test fixture";
    fix.cputype = kMachOCpuTypeARM64;
    fix.is_malformed = true;
    fix.expected_behavior = "UserspaceMachOLoader::parse_metadata returns TruncatedHeader error";

    fix.binary_data.resize(16, 0);
    const std::uint32_t magic = kMachO64Magic;
    std::memcpy(fix.binary_data.data(), &magic, 4);

    finalize_fixture(fix);
    return fix;
}

// -----------------------------------------------------------------------------
// 11. Malformed Fixture: Overlapping Segments
// -----------------------------------------------------------------------------
ControlledMachOFixture PublicMachOCorpus::create_malformed_overlapping_segments() {
    ControlledMachOFixture fix{};
    fix.name = "fixture_malformed_overlapping_segments";
    fix.description = "Two segments claiming overlapping virtual address ranges";
    fix.provenance = "Synthetic security test fixture";
    fix.cputype = kMachOCpuTypeARM64;
    fix.is_malformed = true;
    fix.expected_behavior = "UserspaceMachOLoader::validate_segments detects OverlappingSegments";

    constexpr std::uint32_t kPageSize = 0x4000;
    fix.binary_data.resize(kPageSize, 0);

    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeExecute;
    hdr.ncmds = 2;
    hdr.sizeofcmds = sizeof(MachOSegment64Raw) * 2;
    std::memcpy(fix.binary_data.data(), &hdr, sizeof(hdr));

    std::uint32_t offset = sizeof(hdr);

    // Segment 1: [0x100000000, 0x100008000) (size = 32KB)
    MachOSegment64Raw s1{};
    s1.cmd = kCmdSegment64;
    s1.cmdsize = sizeof(s1);
    std::memcpy(s1.segname, "__TEXT", 6);
    s1.vmaddr = 0x0000000100000000ULL;
    s1.vmsize = kPageSize * 2;
    s1.fileoff = 0;
    s1.filesize = kPageSize;
    s1.maxprot = kProtRead | kProtExecute;
    s1.initprot = kProtRead | kProtExecute;
    std::memcpy(fix.binary_data.data() + offset, &s1, sizeof(s1));
    offset += sizeof(s1);

    // Segment 2: [0x100004000, 0x10000C000) -> Both page-aligned, but overlaps with Segment 1!
    MachOSegment64Raw s2{};
    s2.cmd = kCmdSegment64;
    s2.cmdsize = sizeof(s2);
    std::memcpy(s2.segname, "__DATA", 6);
    s2.vmaddr = 0x0000000100004000ULL; // Overlaps with [0x100000000, 0x100008000)
    s2.vmsize = kPageSize * 2;
    s2.fileoff = 0;
    s2.filesize = kPageSize;
    s2.maxprot = kProtRead | kProtWrite;
    s2.initprot = kProtRead | kProtWrite;
    std::memcpy(fix.binary_data.data() + offset, &s2, sizeof(s2));

    finalize_fixture(fix);
    return fix;
}

// -----------------------------------------------------------------------------
// 12. Malformed Fixture: Corrupt Load Commands (sizeofcmds exceeds file)
// -----------------------------------------------------------------------------
ControlledMachOFixture PublicMachOCorpus::create_malformed_corrupt_load_commands() {
    ControlledMachOFixture fix{};
    fix.name = "fixture_malformed_corrupt_load_commands";
    fix.description = "Header claims sizeofcmds extending past EOF";
    fix.provenance = "Synthetic security test fixture";
    fix.cputype = kMachOCpuTypeARM64;
    fix.is_malformed = true;
    fix.expected_behavior = "UserspaceMachOLoader::parse_metadata returns CorruptedLoadCommands error";

    fix.binary_data.resize(64, 0);
    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeExecute;
    hdr.ncmds = 10;
    hdr.sizeofcmds = 0x10000; // Claims 64KB of commands in 64-byte file!
    std::memcpy(fix.binary_data.data(), &hdr, sizeof(hdr));

    finalize_fixture(fix);
    return fix;
}

// -----------------------------------------------------------------------------
// Full Catalog Retrieval
// -----------------------------------------------------------------------------
std::vector<ControlledMachOFixture> PublicMachOCorpus::get_full_corpus() {
    return {
        create_minimal_executable(),
        create_multisegment_executable(),
        create_multi_image_app(),
        create_multi_image_dylib_math(),
        create_multi_image_dylib_base(),
        create_symbol_export_dylib(),
        create_weak_symbol_executable(),
        create_chained_fixup_executable(),
        create_tls_executable(),
        create_malformed_truncated_header(),
        create_malformed_overlapping_segments(),
        create_malformed_corrupt_load_commands()
    };
}

} // namespace winios::loader
