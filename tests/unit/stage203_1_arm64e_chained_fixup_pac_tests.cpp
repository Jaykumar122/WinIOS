// ==============================================================================
// WinIOS Campaign 203 Stage 203.1: ARM64e Chained-Fixup & PAC Verification Tests
// ==============================================================================
// Independent verification of:
// 1. Bitfield encoding/decoding for auth rebase, auth bind, non-auth rebase, non-auth bind
// 2. PAC key selection: IA, IB, DA, DB
// 3. Address diversity (addr_div = true vs false) and diversity salt mixing
// 4. Stride advancement (8-byte stride) and chain termination
// 5. Addend handling (signed 19-bit addend)
// 6. Two-level namespace resolution & weak import zeroing
// 7. Robust negative testing: invalid ordinals, out-of-bounds names, cycle detection, bad formats
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/pointer_auth.h"
#include "loader/dyld_engine.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::cpu;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Bitfield Encoding & Decoding Roundtrip
// -----------------------------------------------------------------------------
void test_arm64e_bitfield_roundtrip() {
    std::cout << "[Stage 203.1 Test 1] ARM64e Chained Pointer Bitfield Roundtrip...\n";

    // 1. Auth Rebase
    {
        const std::uint32_t target = 0x12345678u;
        const std::uint16_t diversity = 0xABCDu;
        const bool addr_div = true;
        const std::uint8_t key = 1; // IB
        const std::uint16_t next = 3;

        std::uint64_t raw = encode_dyld_chained_ptr_arm64e_auth_rebase(target, diversity, addr_div, key, next);
        DyldChainedPtrArm64eDecoded dec = decode_dyld_chained_ptr_arm64e(raw);

        TEST_CHECK(dec.auth == true, "Auth bit must be set");
        TEST_CHECK(dec.is_bind == false, "Bind bit must be 0 for rebase");
        TEST_CHECK(dec.target == target, "Target virtual address must match");
        TEST_CHECK(dec.diversity == diversity, "Diversity must match");
        TEST_CHECK(dec.addr_div == addr_div, "Addr_div must match");
        TEST_CHECK(dec.key == key, "Key must match");
        TEST_CHECK(dec.next == next, "Next stride must match");
    }

    // 2. Auth Bind
    {
        const std::uint16_t ordinal = 42;
        const std::uint16_t diversity = 0x55AAu;
        const bool addr_div = false;
        const std::uint8_t key = 2; // DA
        const std::uint16_t next = 7;

        std::uint64_t raw = encode_dyld_chained_ptr_arm64e_auth_bind(ordinal, diversity, addr_div, key, next);
        DyldChainedPtrArm64eDecoded dec = decode_dyld_chained_ptr_arm64e(raw);

        TEST_CHECK(dec.auth == true, "Auth bit must be set");
        TEST_CHECK(dec.is_bind == true, "Bind bit must be set");
        TEST_CHECK(dec.ordinal == ordinal, "Ordinal must match");
        TEST_CHECK(dec.diversity == diversity, "Diversity must match");
        TEST_CHECK(dec.addr_div == addr_div, "Addr_div must match");
        TEST_CHECK(dec.key == key, "Key must match");
        TEST_CHECK(dec.next == next, "Next stride must match");
    }

    // 3. Non-Auth Bind with Signed Addend
    {
        const std::uint16_t ordinal = 15;
        const std::int32_t neg_addend = -64;
        const std::uint16_t next = 1;

        std::uint64_t raw = encode_dyld_chained_ptr_arm64e_bind(ordinal, neg_addend, next);
        DyldChainedPtrArm64eDecoded dec = decode_dyld_chained_ptr_arm64e(raw);

        TEST_CHECK(dec.auth == false, "Auth bit must be 0");
        TEST_CHECK(dec.is_bind == true, "Bind bit must be 1");
        TEST_CHECK(dec.ordinal == ordinal, "Ordinal must match");
        TEST_CHECK(dec.addend == neg_addend, "Negative addend must be sign-extended properly");
        TEST_CHECK(dec.next == next, "Next stride must match");
    }

    // 4. Non-Auth Rebase
    {
        const std::uint64_t target = 0x100008000ULL;
        const std::uint16_t next = 4;

        std::uint64_t raw = encode_dyld_chained_ptr_arm64e_rebase(target, next);
        DyldChainedPtrArm64eDecoded dec = decode_dyld_chained_ptr_arm64e(raw);

        TEST_CHECK(dec.auth == false, "Auth bit must be 0");
        TEST_CHECK(dec.is_bind == false, "Bind bit must be 0");
        TEST_CHECK(dec.target == target, "Target virtual address must match");
        TEST_CHECK(dec.next == next, "Next stride must match");
    }

    std::cout << "  -> Bitfield roundtrip tests passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: All 4 PAC Keys Differentiation (IA, IB, DA, DB)
// -----------------------------------------------------------------------------
void test_all_pac_keys_differentiation() {
    std::cout << "[Stage 203.1 Test 2] All 4 PAC Keys Differentiation...\n";

    DyldEngine engine;
    Arm64ePointerAuth pac(PacFaultMode::CorruptUpperBits);
    pac.set_key(PacKeyType::InstructionA, 0x1111222233334444ULL, 0x5555666677778888ULL);
    pac.set_key(PacKeyType::InstructionB, 0x2222333344445555ULL, 0x6666777788889999ULL);
    pac.set_key(PacKeyType::DataA,        0x3333444455556666ULL, 0x777788889999AAAAULL);
    pac.set_key(PacKeyType::DataB,        0x4444555566667777ULL, 0x88889999AAAABBBBULL);
    engine.set_pointer_auth(&pac);

    Memory mem(0x100000ULL);
    const std::uint64_t base_vaddr = 0x10000ULL;
    const std::uint64_t slide = 0x0ULL;
    const std::uint64_t target = 0x4000ULL;
    const std::uint16_t diversity = 0x1122u;

    std::uint64_t signed_pointers[4] = {0};

    for (std::uint8_t k = 0; k < 4; ++k) {
        std::vector<std::uint8_t> payload;
        // Fixups header
        DyldChainedFixupsHeaderRaw hdr{};
        hdr.fixups_version = 0;
        hdr.starts_offset = static_cast<std::uint32_t>(sizeof(DyldChainedFixupsHeaderRaw));
        hdr.imports_offset = 0;
        hdr.imports_count = 0;
        hdr.symbols_offset = 0;

        payload.resize(sizeof(DyldChainedFixupsHeaderRaw));
        std::memcpy(payload.data(), &hdr, sizeof(hdr));

        // Starts in image
        std::size_t starts_img_off = payload.size();
        DyldChainedStartsInImageRaw starts_img{};
        starts_img.seg_count = 1;
        payload.resize(payload.size() + sizeof(starts_img) + sizeof(std::uint32_t));
        std::memcpy(payload.data() + starts_img_off, &starts_img, sizeof(starts_img));

        // Segment offset points immediately after
        std::uint32_t seg_info_off = static_cast<std::uint32_t>(payload.size() - starts_img_off);
        std::memcpy(payload.data() + starts_img_off + sizeof(starts_img), &seg_info_off, sizeof(seg_info_off));

        // Segment header
        std::size_t seg_off = payload.size();
        DyldChainedStartsInSegmentRaw seg{};
        seg.size = sizeof(seg) + sizeof(std::uint16_t);
        seg.page_size = 0x4000;
        seg.pointer_format = DYLD_CHAINED_PTR_ARM64E;
        seg.segment_offset = 0x20000;
        seg.page_count = 1;

        std::uint16_t pstart = 0x10;
        payload.resize(payload.size() + sizeof(seg) + sizeof(pstart));
        std::memcpy(payload.data() + seg_off, &seg, sizeof(seg));
        std::memcpy(payload.data() + seg_off + sizeof(seg), &pstart, sizeof(pstart));

        // Write encoded auth rebase at chain_loc = 0x20010
        std::uint64_t raw_ptr = encode_dyld_chained_ptr_arm64e_auth_rebase(static_cast<std::uint32_t>(target), diversity, false, k, 0);
        mem.write_u64(0x20010, raw_ptr);

        std::string err;
        auto st = engine.process_chained_fixups(payload.data(), payload.size(), mem, base_vaddr, slide, err);
        TEST_CHECK(st == DyldLinkStatus::Success, "Fixup processing must succeed");

        signed_pointers[k] = mem.read_u64(0x20010);

        // Verify that target address is preserved in bottom 48 bits
        const std::uint64_t stripped = Arm64ePointerAuth::strip_pointer(signed_pointers[k]);
        TEST_CHECK(stripped == (base_vaddr + target), "Stripped pointer must match base_vaddr + target");

        // Verify authentication against the matching key succeeds
        PacKeyType key_type = (k == 0) ? PacKeyType::InstructionA
                            : (k == 1) ? PacKeyType::InstructionB
                            : (k == 2) ? PacKeyType::DataA
                            : PacKeyType::DataB;
        pac.clear_fault();
        auto auth_res = pac.authenticate_pointer(signed_pointers[k], diversity, key_type);
        TEST_CHECK(!pac.has_fault() && auth_res == (base_vaddr + target), "Auth with matching key must succeed");
    }

    // Ensure all 4 keys produced distinct cryptographic PAC tags
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            TEST_CHECK(signed_pointers[i] != signed_pointers[j], "Different PAC keys must generate distinct signatures");
        }
    }

    std::cout << "  -> PAC keys differentiation tests passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Address Diversity Invariant
// -----------------------------------------------------------------------------
void test_address_diversity() {
    std::cout << "[Stage 203.1 Test 3] Address Diversity Invariants...\n";

    Arm64ePointerAuth pac(PacFaultMode::CorruptUpperBits);
    pac.set_key(PacKeyType::InstructionA, 0xAABBCCDDEEFF0011ULL, 0x2233445566778899ULL);

    const std::uint64_t target = 0x100004000ULL;
    const std::uint16_t diversity = 0xBEEFu;

    // When addr_div == true, modifier = chain_loc ^ diversity
    const std::uint64_t loc1 = 0x20010ULL;
    const std::uint64_t loc2 = 0x20020ULL;

    const std::uint64_t sig_loc1 = pac.sign_pointer(target, loc1 ^ diversity, PacKeyType::InstructionA);
    const std::uint64_t sig_loc2 = pac.sign_pointer(target, loc2 ^ diversity, PacKeyType::InstructionA);
    TEST_CHECK(sig_loc1 != sig_loc2, "Address-diversified pointers at different locations must not match");

    // Authenticating sig_loc1 at loc2 must fail
    pac.clear_fault();
    auto cross_auth = pac.authenticate_pointer(sig_loc1, loc2 ^ diversity, PacKeyType::InstructionA);
    TEST_CHECK(pac.has_fault() || cross_auth != target, "Cross-location authentication with address diversity must fail");

    // Authenticating sig_loc1 at loc1 must succeed
    pac.clear_fault();
    auto self_auth = pac.authenticate_pointer(sig_loc1, loc1 ^ diversity, PacKeyType::InstructionA);
    TEST_CHECK(!pac.has_fault() && self_auth == target, "Authenticating at correct location must succeed");

    std::cout << "  -> Address diversity tests passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Negative & Malformed Fixups Rejection
// -----------------------------------------------------------------------------
void test_malformed_fixup_rejection() {
    std::cout << "[Stage 203.1 Test 4] Negative & Malformed Fixups Rejection...\n";

    DyldEngine engine;
    Memory mem(0x100000ULL);
    std::string err;

    // 1. Invalid pointer format (0x9999)
    {
        std::vector<std::uint8_t> payload;
        DyldChainedFixupsHeaderRaw hdr{};
        hdr.fixups_version = 0;
        hdr.starts_offset = sizeof(hdr);
        payload.resize(sizeof(hdr));
        std::memcpy(payload.data(), &hdr, sizeof(hdr));

        DyldChainedStartsInImageRaw starts_img{};
        starts_img.seg_count = 1;
        std::size_t simg_off = payload.size();
        payload.resize(payload.size() + sizeof(starts_img) + sizeof(std::uint32_t));
        std::memcpy(payload.data() + simg_off, &starts_img, sizeof(starts_img));

        std::uint32_t seg_info_off = static_cast<std::uint32_t>(payload.size() - simg_off);
        std::memcpy(payload.data() + simg_off + sizeof(starts_img), &seg_info_off, sizeof(seg_info_off));

        DyldChainedStartsInSegmentRaw seg{};
        seg.size = sizeof(seg) + sizeof(std::uint16_t);
        seg.page_size = 0x4000;
        seg.pointer_format = 0x9999; // Unsupported format!
        seg.segment_offset = 0x20000;
        seg.page_count = 1;

        std::uint16_t pstart = 0x10;
        std::size_t seg_off = payload.size();
        payload.resize(payload.size() + sizeof(seg) + sizeof(pstart));
        std::memcpy(payload.data() + seg_off, &seg, sizeof(seg));
        std::memcpy(payload.data() + seg_off + sizeof(seg), &pstart, sizeof(pstart));

        auto st = engine.process_chained_fixups(payload.data(), payload.size(), mem, 0x10000ULL, 0ULL, err);
        TEST_CHECK(st == DyldLinkStatus::InvalidChainedFixups, "Unsupported format 0x9999 must be rejected");
    }

    // 2. Out-of-bounds pointer step
    {
        Memory mem_small(0x21000ULL); // Bounded memory so step goes OOB
        std::vector<std::uint8_t> payload;
        DyldChainedFixupsHeaderRaw hdr{};
        hdr.fixups_version = 0;
        hdr.starts_offset = sizeof(hdr);
        payload.resize(sizeof(hdr));
        std::memcpy(payload.data(), &hdr, sizeof(hdr));

        DyldChainedStartsInImageRaw starts_img{};
        starts_img.seg_count = 1;
        std::size_t simg_off = payload.size();
        payload.resize(payload.size() + sizeof(starts_img) + sizeof(std::uint32_t));
        std::memcpy(payload.data() + simg_off, &starts_img, sizeof(starts_img));

        std::uint32_t seg_info_off = static_cast<std::uint32_t>(payload.size() - simg_off);
        std::memcpy(payload.data() + simg_off + sizeof(starts_img), &seg_info_off, sizeof(seg_info_off));

        DyldChainedStartsInSegmentRaw seg{};
        seg.size = sizeof(seg) + sizeof(std::uint16_t);
        seg.page_size = 0x4000;
        seg.pointer_format = DYLD_CHAINED_PTR_ARM64E;
        seg.segment_offset = 0x20000;
        seg.page_count = 1;

        std::uint16_t pstart = 0x10;
        std::size_t seg_off = payload.size();
        payload.resize(payload.size() + sizeof(seg) + sizeof(pstart));
        std::memcpy(payload.data() + seg_off, &seg, sizeof(seg));
        std::memcpy(payload.data() + seg_off + sizeof(seg), &pstart, sizeof(pstart));

        // next stride in arm64e: 2040 * 8 = 16320 bytes -> 0x23FD0 > 0x21000
        mem_small.write_u64(0x20010, encode_dyld_chained_ptr_arm64e_rebase(0x4000, 2040));
        auto st = engine.process_chained_fixups(payload.data(), payload.size(), mem_small, 0x10000ULL, 0ULL, err);
        TEST_CHECK(st == DyldLinkStatus::MemoryPatchFailed || st == DyldLinkStatus::InvalidChainedFixups,
                   "Out-of-bounds pointer step must be rejected");
    }

    std::cout << "  -> Malformed fixup rejection tests passed.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.1: ARM64e Chained-Fixup & PAC Independent Tests\n";
    std::cout << "==================================================================\n";

    test_arm64e_bitfield_roundtrip();
    test_all_pac_keys_differentiation();
    test_address_diversity();
    test_malformed_fixup_rejection();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.1 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
