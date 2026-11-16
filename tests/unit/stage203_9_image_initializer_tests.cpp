// ==============================================================================
// WinIOS Campaign 203 Stage 203.9: Image Initializer Metadata Tests
// ==============================================================================
// Independent verification of:
// 1. Discovery and parsing of __mod_init_func section pointers in Mach-O binaries
// 2. Initializer function pointer validation against executable segment boundaries
// 3. Rejection of null or non-executable initializer targets
// 4. Deterministic multi-image initializer sequencing (dependencies before dependees)
// 5. Prevention of duplicate initializer execution
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "loader/dyld_engine.h"
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
// Test 1: __mod_init_func Section Discovery in Mach-O
// -----------------------------------------------------------------------------
void test_mod_init_func_discovery() {
    std::cout << "[Stage 203.9 Test 1] __mod_init_func Section Discovery...\n";

    // Build synthetic binary with __TEXT and __DATA(__mod_init_func)
    std::vector<std::uint8_t> binary;

    MachOHeader64 hdr{};
    hdr.magic = kMachOMagic64;
    hdr.cputype = kCpuTypeARM64;
    hdr.cpusubtype = kCpuSubtypeARM64All;
    hdr.filetype = kMachOFileTypeDylib;
    hdr.ncmds = 2; // __TEXT and __DATA
    hdr.flags = 0;
    hdr.reserved = 0;

    // Segment 1: __TEXT
    MachOSegmentCommand64 text_seg{};
    text_seg.cmd = kLoadCmdSegment64;
    text_seg.cmdsize = sizeof(MachOSegmentCommand64);
    std::memcpy(text_seg.segname, "__TEXT", 6);
    text_seg.vmaddr = 0x100000000ULL;
    text_seg.vmsize = 0x4000ULL;
    text_seg.fileoff = 0;
    text_seg.filesize = 0;
    text_seg.maxprot = 7;
    text_seg.initprot = 5; // Read | Execute
    text_seg.nsects = 0;

    // Segment 2: __DATA with 1 section (__mod_init_func)
    MachOSegmentCommand64 data_seg{};
    data_seg.cmd = kLoadCmdSegment64;
    data_seg.cmdsize = static_cast<std::uint32_t>(sizeof(MachOSegmentCommand64) + sizeof(MachOSection64));
    std::memcpy(data_seg.segname, "__DATA", 6);
    data_seg.vmaddr = 0x100004000ULL;
    data_seg.vmsize = 0x4000ULL;
    data_seg.fileoff = 0x200; // payload located at offset 0x200 in binary
    data_seg.filesize = 0x100;
    data_seg.maxprot = 7;
    data_seg.initprot = 3; // Read | Write
    data_seg.nsects = 1;

    MachOSection64 init_sec{};
    std::memcpy(init_sec.sectname, "__mod_init_func", 15);
    std::memcpy(init_sec.segname, "__DATA", 6);
    init_sec.addr = 0x100004000ULL;
    init_sec.size = 16; // Two 64-bit function pointers
    init_sec.offset = 0x200;
    init_sec.align = 3;
    init_sec.reloff = 0;
    init_sec.nreloc = 0;
    init_sec.flags = 0x09u; // S_MOD_INIT_FUNC_POINTERS

    hdr.sizeofcmds = text_seg.cmdsize + data_seg.cmdsize;

    binary.resize(0x400, 0); // 1KB binary buffer
    std::memcpy(binary.data(), &hdr, sizeof(hdr));
    std::size_t cur = sizeof(hdr);
    std::memcpy(binary.data() + cur, &text_seg, sizeof(text_seg));
    cur += sizeof(text_seg);
    std::memcpy(binary.data() + cur, &data_seg, sizeof(data_seg));
    cur += sizeof(data_seg);
    std::memcpy(binary.data() + cur, &init_sec, sizeof(init_sec));

    // Write function pointers at offset 0x200
    std::uint64_t fn1 = 0x100001080ULL;
    std::uint64_t fn2 = 0x1000011C0ULL;
    std::memcpy(binary.data() + 0x200, &fn1, sizeof(fn1));
    std::memcpy(binary.data() + 0x208, &fn2, sizeof(fn2));

    MachOLoadResult res;
    bool ok = MachOLoader::parse_metadata(binary.data(), binary.size(), res);
    TEST_CHECK(ok, "Parsing binary with __mod_init_func must succeed");
    TEST_CHECK(res.mod_init_funcs.size() == 2, "Must discover exactly 2 initializers");
    if (res.mod_init_funcs.size() >= 2) {
        TEST_CHECK(res.mod_init_funcs[0] == fn1, "Initializer 0 match");
        TEST_CHECK(res.mod_init_funcs[1] == fn2, "Initializer 1 match");
    }

    std::cout << "  -> __mod_init_func section discovery verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Initializer Validation Against Executable Boundaries
// -----------------------------------------------------------------------------
void test_initializer_boundary_validation() {
    std::cout << "[Stage 203.9 Test 2] Initializer Boundary Validation...\n";

    DyldEngine engine;
    std::string err;

    // Segment definition: __TEXT (0x100000000..0x100004000, Executable)
    // Segment definition: __DATA (0x100004000..0x100008000, Non-Executable)
    std::vector<MachOSegmentInfo> segs;
    MachOSegmentInfo s_text;
    s_text.name = "__TEXT";
    s_text.vmaddr = 0x100000000ULL;
    s_text.vmsize = 0x4000ULL;
    s_text.initprot = kVmProtRead | kVmProtExecute;
    segs.push_back(s_text);

    MachOSegmentInfo s_data;
    s_data.name = "__DATA";
    s_data.vmaddr = 0x100004000ULL;
    s_data.vmsize = 0x4000ULL;
    s_data.initprot = kVmProtRead | kVmProtWrite;
    segs.push_back(s_data);

    // 1. Valid Initializer in __TEXT
    DyldLoadedModule mod_valid;
    mod_valid.name = "libValid.dylib";
    mod_valid.initializers = {0x100001000ULL, 0x100002000ULL};
    bool ok1 = engine.validate_initializers(mod_valid, segs, err);
    TEST_CHECK(ok1, "Initializers in executable segment must pass validation");

    // 2. Invalid Initializer in __DATA (Non-executable)
    DyldLoadedModule mod_data;
    mod_data.name = "libDataTarget.dylib";
    mod_data.initializers = {0x100005000ULL}; // In __DATA!
    bool ok2 = engine.validate_initializers(mod_data, segs, err);
    TEST_CHECK(!ok2, "Initializer in non-executable segment must fail validation");

    // 3. Invalid Null Initializer
    DyldLoadedModule mod_null;
    mod_null.name = "libNullTarget.dylib";
    mod_null.initializers = {0x0ULL};
    bool ok3 = engine.validate_initializers(mod_null, segs, err);
    TEST_CHECK(!ok3, "Null initializer must fail validation");

    std::cout << "  -> Initializer boundary validation verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Deterministic Sequencing & Duplicate Run Prevention
// -----------------------------------------------------------------------------
void test_deterministic_initializer_sequencing() {
    std::cout << "[Stage 203.9 Test 3] Deterministic Sequencing & Idempotency...\n";

    DyldEngine engine;
    engine.clear_modules();

    // Module 1: libLeaf (base 0x1000, slide 0x100)
    DyldLoadedModule libLeaf;
    libLeaf.name = "libLeaf.dylib";
    libLeaf.slide = 0x100;
    libLeaf.initializers = {0x1000ULL};
    engine.register_module(libLeaf);

    // Module 2: libMid (base 0x2000, slide 0x200)
    DyldLoadedModule libMid;
    libMid.name = "libMid.dylib";
    libMid.slide = 0x200;
    libMid.initializers = {0x2000ULL};
    engine.register_module(libMid);

    // Module 3: App (base 0x3000, slide 0)
    DyldLoadedModule app;
    app.name = "App";
    app.slide = 0;
    app.initializers = {0x3000ULL};
    engine.register_module(app);

    // Topological load order: libLeaf -> libMid -> App
    std::vector<std::string> load_order = {"libLeaf.dylib", "libMid.dylib", "App"};
    std::vector<std::string> init_mods;

    auto funcs = engine.get_initialization_order(load_order, init_mods);

    TEST_CHECK(funcs.size() == 3, "Must collect 3 initializers");
    TEST_CHECK(init_mods.size() == 3, "Must initialize 3 modules");

    // Verify ordering and slide:
    // libLeaf: 0x1000 + 0x100 = 0x1100
    // libMid:  0x2000 + 0x200 = 0x2200
    // App:     0x3000 + 0     = 0x3000
    if (funcs.size() == 3) {
        TEST_CHECK(funcs[0] == 0x1100ULL, "libLeaf initializer first with slide");
        TEST_CHECK(funcs[1] == 0x2200ULL, "libMid initializer second with slide");
        TEST_CHECK(funcs[2] == 0x3000ULL, "App initializer last");
    }

    // Call again to verify idempotency / duplicate run prevention:
    std::vector<std::string> init_mods2;
    auto funcs2 = engine.get_initialization_order(load_order, init_mods2);
    TEST_CHECK(funcs2.empty(), "Already-initialized modules must yield 0 additional initializers");
    TEST_CHECK(init_mods2.empty(), "No modules re-initialized");

    std::cout << "  -> Deterministic sequencing & idempotency verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.9: Image Initializer Metadata Tests\n";
    std::cout << "==================================================================\n";

    test_mod_init_func_discovery();
    test_initializer_boundary_validation();
    test_deterministic_initializer_sequencing();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.9 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
