#include "cpu/pointer_auth.h"

#include <iomanip>
#include <sstream>

namespace winios::cpu {

const char* pac_key_name(PacKeyType key) noexcept {
    switch (key) {
        case PacKeyType::InstructionA: return "APIAKey";
        case PacKeyType::InstructionB: return "APIBKey";
        case PacKeyType::DataA:        return "APDAKey";
        case PacKeyType::DataB:        return "APDBKey";
        case PacKeyType::Generic:      return "APGAKey";
    }
    return "UnknownKey";
}

Arm64ePointerAuth::Arm64ePointerAuth(PacFaultMode mode)
    : mode_(mode) {
    // Initialize default non-zero architectural keys for deterministic operation
    keyring_.apia = PacKey128{0x847291a5e301bfcdULL, 0x1928374655aa33ffULL};
    keyring_.apib = PacKey128{0x923481726aef1002ULL, 0x883344112277bbccULL};
    keyring_.apda = PacKey128{0xa019284756efdcbaULL, 0x55aa66bb77cc88ddULL};
    keyring_.apdb = PacKey128{0xb18273645feeda01ULL, 0x1122334455667788ULL};
    keyring_.apga = PacKey128{0xc9871234abcd5678ULL, 0xfeebdaed10293847ULL};
}

void Arm64ePointerAuth::set_key(PacKeyType type, std::uint64_t lo, std::uint64_t hi) noexcept {
    keyring_.set_key(type, PacKey128{lo, hi});
}

std::uint64_t Arm64ePointerAuth::strip_pointer(std::uint64_t ptr) noexcept {
    // In standard ARM64 48-bit VA configuration, bit 47 indicates TTBR0 (0) vs TTBR1 (1) range.
    // The PAC signature field occupies bits [63:48].
    const bool is_upper_range = ((ptr >> 47u) & 1u) != 0;
    if (is_upper_range) {
        return (ptr & kDefaultAddressMask) | kPacMask;
    }
    return (ptr & kDefaultAddressMask);
}

std::uint16_t Arm64ePointerAuth::compute_pac_tag(
    std::uint64_t canonical_ptr,
    std::uint64_t modifier,
    const PacKey128& key) const noexcept {

    std::uint64_t v0 = key.lo ^ 0x736f6d6570736575ULL;
    std::uint64_t v1 = key.hi ^ 0x646f72616e646f6dULL;
    std::uint64_t v2 = canonical_ptr ^ 0x6c7967656e657261ULL;
    std::uint64_t v3 = modifier ^ 0x7465646279746573ULL;

    auto sip_round = [&]() noexcept {
        v0 += v1; v1 = (v1 << 13u) | (v1 >> 51u); v1 ^= v0;
        v0 = (v0 << 32u) | (v0 >> 32u);
        v2 += v3; v3 = (v3 << 16u) | (v3 >> 48u); v3 ^= v2;
        v0 += v3; v3 = (v3 << 21u) | (v3 >> 43u); v3 ^= v0;
        v2 += v1; v1 = (v1 << 17u) | (v1 >> 47u); v1 ^= v2;
        v2 = (v2 << 32u) | (v2 >> 32u);
    };

    sip_round();
    sip_round();
    sip_round();

    std::uint64_t h = v0 ^ v1 ^ v2 ^ v3;
    return static_cast<std::uint16_t>(h ^ (h >> 16u) ^ (h >> 32u) ^ (h >> 48u));
}

std::uint64_t Arm64ePointerAuth::sign_pointer(
    std::uint64_t ptr,
    std::uint64_t modifier,
    PacKeyType key_type) const noexcept {

    const std::uint64_t canonical = strip_pointer(ptr);
    const std::uint16_t tag = compute_pac_tag(canonical, modifier, keyring_.get_key(key_type));
    return (static_cast<std::uint64_t>(tag) << kPacShift) | (canonical & kDefaultAddressMask);
}

std::uint64_t Arm64ePointerAuth::authenticate_pointer(
    std::uint64_t ptr,
    std::uint64_t modifier,
    PacKeyType key_type,
    std::uint64_t pc) {

    const std::uint64_t canonical = strip_pointer(ptr);
    const std::uint16_t expected_tag = compute_pac_tag(canonical, modifier, keyring_.get_key(key_type));
    const auto actual_tag = static_cast<std::uint16_t>((ptr & kPacMask) >> kPacShift);

    if (actual_tag == expected_tag) {
        last_syndrome_ = PacSyndrome{};
        return canonical;
    }

    // Authentication failure detected
    last_syndrome_.has_fault = true;
    last_syndrome_.exception_class = 0x1C; // EC 0x1C: PAC failure trap
    last_syndrome_.iss = (key_type == PacKeyType::InstructionA || key_type == PacKeyType::InstructionB) ? 0x0u : 0x1u;
    last_syndrome_.fault_pc = pc;
    last_syndrome_.bad_pointer = ptr;
    last_syndrome_.failed_key = key_type;

    std::ostringstream oss;
    oss << "PAC authentication failure: key=" << pac_key_name(key_type)
        << " pointer=0x" << std::hex << ptr
        << " expected_tag=0x" << expected_tag
        << " actual_tag=0x" << actual_tag
        << " modifier=0x" << modifier;
    last_syndrome_.description = oss.str();

    if (mode_ == PacFaultMode::TrapException) {
        return 0; // Trapped, returning null target
    }

    // Corrupt upper bits with architectural error signature
    return canonical ^ kPacErrorBit;
}

std::uint64_t Arm64ePointerAuth::compute_generic_tag(
    std::uint64_t val1,
    std::uint64_t val2) const noexcept {

    std::uint16_t tag = compute_pac_tag(val1, val2, keyring_.apga);
    return (static_cast<std::uint64_t>(tag) << 32u);
}

bool Arm64ePointerAuth::execute_pacia(std::uint64_t& xd, std::uint64_t xn) noexcept {
    xd = sign_pointer(xd, xn, PacKeyType::InstructionA);
    return true;
}

bool Arm64ePointerAuth::execute_pacib(std::uint64_t& xd, std::uint64_t xn) noexcept {
    xd = sign_pointer(xd, xn, PacKeyType::InstructionB);
    return true;
}

bool Arm64ePointerAuth::execute_pacda(std::uint64_t& xd, std::uint64_t xn) noexcept {
    xd = sign_pointer(xd, xn, PacKeyType::DataA);
    return true;
}

bool Arm64ePointerAuth::execute_pacdb(std::uint64_t& xd, std::uint64_t xn) noexcept {
    xd = sign_pointer(xd, xn, PacKeyType::DataB);
    return true;
}

bool Arm64ePointerAuth::execute_paciza(std::uint64_t& xd) noexcept {
    xd = sign_pointer(xd, 0, PacKeyType::InstructionA);
    return true;
}

bool Arm64ePointerAuth::execute_pacizb(std::uint64_t& xd) noexcept {
    xd = sign_pointer(xd, 0, PacKeyType::InstructionB);
    return true;
}

bool Arm64ePointerAuth::execute_pacdza(std::uint64_t& xd) noexcept {
    xd = sign_pointer(xd, 0, PacKeyType::DataA);
    return true;
}

bool Arm64ePointerAuth::execute_pacdzb(std::uint64_t& xd) noexcept {
    xd = sign_pointer(xd, 0, PacKeyType::DataB);
    return true;
}

bool Arm64ePointerAuth::execute_autia(std::uint64_t& xd, std::uint64_t xn, std::uint64_t pc) {
    xd = authenticate_pointer(xd, xn, PacKeyType::InstructionA, pc);
    return !last_syndrome_.has_fault;
}

bool Arm64ePointerAuth::execute_autib(std::uint64_t& xd, std::uint64_t xn, std::uint64_t pc) {
    xd = authenticate_pointer(xd, xn, PacKeyType::InstructionB, pc);
    return !last_syndrome_.has_fault;
}

bool Arm64ePointerAuth::execute_autda(std::uint64_t& xd, std::uint64_t xn, std::uint64_t pc) {
    xd = authenticate_pointer(xd, xn, PacKeyType::DataA, pc);
    return !last_syndrome_.has_fault;
}

bool Arm64ePointerAuth::execute_autdb(std::uint64_t& xd, std::uint64_t xn, std::uint64_t pc) {
    xd = authenticate_pointer(xd, xn, PacKeyType::DataB, pc);
    return !last_syndrome_.has_fault;
}

bool Arm64ePointerAuth::execute_autiza(std::uint64_t& xd, std::uint64_t pc) {
    xd = authenticate_pointer(xd, 0, PacKeyType::InstructionA, pc);
    return !last_syndrome_.has_fault;
}

bool Arm64ePointerAuth::execute_autizb(std::uint64_t& xd, std::uint64_t pc) {
    xd = authenticate_pointer(xd, 0, PacKeyType::InstructionB, pc);
    return !last_syndrome_.has_fault;
}

bool Arm64ePointerAuth::execute_autdza(std::uint64_t& xd, std::uint64_t pc) {
    xd = authenticate_pointer(xd, 0, PacKeyType::DataA, pc);
    return !last_syndrome_.has_fault;
}

bool Arm64ePointerAuth::execute_autdzb(std::uint64_t& xd, std::uint64_t pc) {
    xd = authenticate_pointer(xd, 0, PacKeyType::DataB, pc);
    return !last_syndrome_.has_fault;
}

bool Arm64ePointerAuth::execute_xpaci(std::uint64_t& xd) noexcept {
    xd = strip_pointer(xd);
    return true;
}

bool Arm64ePointerAuth::execute_xpacd(std::uint64_t& xd) noexcept {
    xd = strip_pointer(xd);
    return true;
}

bool Arm64ePointerAuth::execute_braa(std::uint64_t xn, std::uint64_t xm, std::uint64_t& out_target, std::uint64_t pc) {
    out_target = authenticate_pointer(xn, xm, PacKeyType::InstructionA, pc);
    return !last_syndrome_.has_fault;
}

bool Arm64ePointerAuth::execute_brab(std::uint64_t xn, std::uint64_t xm, std::uint64_t& out_target, std::uint64_t pc) {
    out_target = authenticate_pointer(xn, xm, PacKeyType::InstructionB, pc);
    return !last_syndrome_.has_fault;
}

bool Arm64ePointerAuth::execute_blraa(
    std::uint64_t xn,
    std::uint64_t xm,
    std::uint64_t& lr,
    std::uint64_t& out_target,
    std::uint64_t pc) {

    out_target = authenticate_pointer(xn, xm, PacKeyType::InstructionA, pc);
    if (!last_syndrome_.has_fault) {
        lr = pc + 4;
        return true;
    }
    return false;
}

bool Arm64ePointerAuth::execute_blrab(
    std::uint64_t xn,
    std::uint64_t xm,
    std::uint64_t& lr,
    std::uint64_t& out_target,
    std::uint64_t pc) {

    out_target = authenticate_pointer(xn, xm, PacKeyType::InstructionB, pc);
    if (!last_syndrome_.has_fault) {
        lr = pc + 4;
        return true;
    }
    return false;
}

bool Arm64ePointerAuth::execute_retaa(std::uint64_t lr, std::uint64_t sp, std::uint64_t& out_target, std::uint64_t pc) {
    out_target = authenticate_pointer(lr, sp, PacKeyType::InstructionA, pc);
    return !last_syndrome_.has_fault;
}

bool Arm64ePointerAuth::execute_retab(std::uint64_t lr, std::uint64_t sp, std::uint64_t& out_target, std::uint64_t pc) {
    out_target = authenticate_pointer(lr, sp, PacKeyType::InstructionB, pc);
    return !last_syndrome_.has_fault;
}

} // namespace winios::cpu
