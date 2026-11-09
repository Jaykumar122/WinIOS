#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winios::cpu {

// -----------------------------------------------------------------------------
// ARM64e Pointer Authentication Keys (128-bit per key)
// -----------------------------------------------------------------------------
struct PacKey128 {
    std::uint64_t lo{0};
    std::uint64_t hi{0};

    [[nodiscard]] constexpr bool operator==(const PacKey128& other) const noexcept {
        return lo == other.lo && hi == other.hi;
    }
    [[nodiscard]] constexpr bool operator!=(const PacKey128& other) const noexcept {
        return !(*this == other);
    }
};

enum class PacKeyType {
    InstructionA, // APIAKey
    InstructionB, // APIBKey
    DataA,        // APDAKey
    DataB,        // APDBKey
    Generic       // APGAKey
};

[[nodiscard]] const char* pac_key_name(PacKeyType key) noexcept;

// Complete 5-key PAC Keyring for a guest thread/process or CPU core
struct PacKeyRing {
    PacKey128 apia{};
    PacKey128 apib{};
    PacKey128 apda{};
    PacKey128 apdb{};
    PacKey128 apga{};

    [[nodiscard]] const PacKey128& get_key(PacKeyType type) const noexcept {
        switch (type) {
            case PacKeyType::InstructionA: return apia;
            case PacKeyType::InstructionB: return apib;
            case PacKeyType::DataA:        return apda;
            case PacKeyType::DataB:        return apdb;
            case PacKeyType::Generic:      return apga;
        }
        return apia;
    }

    void set_key(PacKeyType type, const PacKey128& k) noexcept {
        switch (type) {
            case PacKeyType::InstructionA: apia = k; break;
            case PacKeyType::InstructionB: apib = k; break;
            case PacKeyType::DataA:        apda = k; break;
            case PacKeyType::DataB:        apdb = k; break;
            case PacKeyType::Generic:      apga = k; break;
        }
    }
};

// -----------------------------------------------------------------------------
// PAC Configuration & Exception Syndromes
// -----------------------------------------------------------------------------
enum class PacFaultMode {
    CorruptUpperBits, // Pre-ARMv8.6: Inject error bits into upper PAC field
    TrapException     // ARMv8.6 FPAC (Apple A16 Bionic default): Trigger sync exception
};

struct PacSyndrome {
    bool          has_fault{false};
    std::uint32_t exception_class{0x1C}; // EC 0x1C: Synchronous PAC Failure Trap
    std::uint32_t iss{0};                // Instruction-specific syndrome
    std::uint64_t fault_pc{0};
    std::uint64_t bad_pointer{0};
    PacKeyType    failed_key{PacKeyType::InstructionA};
    std::string   description;
};

// -----------------------------------------------------------------------------
// Arm64ePointerAuth: Reference Implementation for A16 Bionic PAC
// -----------------------------------------------------------------------------
class Arm64ePointerAuth {
public:
    static constexpr std::uint64_t kDefaultAddressMask = 0x0000FFFFFFFFFFFFULL; // 48-bit VA
    static constexpr std::uint64_t kPacMask            = 0xFFFF000000000000ULL; // Top 16-bits
    static constexpr unsigned      kPacShift           = 48u;
    static constexpr std::uint64_t kPacErrorBit        = (1ULL << 62); // Bit 62 set on auth error

    explicit Arm64ePointerAuth(PacFaultMode mode = PacFaultMode::TrapException);

    // Keyring Management
    void set_keyring(const PacKeyRing& keyring) noexcept { keyring_ = keyring; }
    [[nodiscard]] const PacKeyRing& get_keyring() const noexcept { return keyring_; }
    void set_key(PacKeyType type, std::uint64_t lo, std::uint64_t hi) noexcept;

    // Mode Configuration
    void set_fault_mode(PacFaultMode mode) noexcept { mode_ = mode; }
    [[nodiscard]] PacFaultMode get_fault_mode() const noexcept { return mode_; }

    // Stripping
    [[nodiscard]] static std::uint64_t strip_pointer(std::uint64_t ptr) noexcept;

    // Signing
    [[nodiscard]] std::uint64_t sign_pointer(
        std::uint64_t ptr,
        std::uint64_t modifier,
        PacKeyType key_type) const noexcept;

    // Authenticating (returns clean stripped pointer on success; triggers fault on failure)
    [[nodiscard]] std::uint64_t authenticate_pointer(
        std::uint64_t ptr,
        std::uint64_t modifier,
        PacKeyType key_type,
        std::uint64_t pc = 0);

    // Generic signature computation (APGA)
    [[nodiscard]] std::uint64_t compute_generic_tag(
        std::uint64_t val1,
        std::uint64_t val2) const noexcept;

    // Last Fault Syndrome Inspection
    [[nodiscard]] bool has_fault() const noexcept { return last_syndrome_.has_fault; }
    [[nodiscard]] const PacSyndrome& last_syndrome() const noexcept { return last_syndrome_; }
    void clear_fault() noexcept { last_syndrome_ = PacSyndrome{}; }

    // Core Instruction Execution Emulation
    [[nodiscard]] bool execute_pacia(std::uint64_t& xd, std::uint64_t xn) noexcept;
    [[nodiscard]] bool execute_pacib(std::uint64_t& xd, std::uint64_t xn) noexcept;
    [[nodiscard]] bool execute_pacda(std::uint64_t& xd, std::uint64_t xn) noexcept;
    [[nodiscard]] bool execute_pacdb(std::uint64_t& xd, std::uint64_t xn) noexcept;

    [[nodiscard]] bool execute_paciza(std::uint64_t& xd) noexcept;
    [[nodiscard]] bool execute_pacizb(std::uint64_t& xd) noexcept;
    [[nodiscard]] bool execute_pacdza(std::uint64_t& xd) noexcept;
    [[nodiscard]] bool execute_pacdzb(std::uint64_t& xd) noexcept;

    [[nodiscard]] bool execute_autia(std::uint64_t& xd, std::uint64_t xn, std::uint64_t pc = 0);
    [[nodiscard]] bool execute_autib(std::uint64_t& xd, std::uint64_t xn, std::uint64_t pc = 0);
    [[nodiscard]] bool execute_autda(std::uint64_t& xd, std::uint64_t xn, std::uint64_t pc = 0);
    [[nodiscard]] bool execute_autdb(std::uint64_t& xd, std::uint64_t xn, std::uint64_t pc = 0);

    [[nodiscard]] bool execute_autiza(std::uint64_t& xd, std::uint64_t pc = 0);
    [[nodiscard]] bool execute_autizb(std::uint64_t& xd, std::uint64_t pc = 0);
    [[nodiscard]] bool execute_autdza(std::uint64_t& xd, std::uint64_t pc = 0);
    [[nodiscard]] bool execute_autdzb(std::uint64_t& xd, std::uint64_t pc = 0);

    [[nodiscard]] bool execute_xpaci(std::uint64_t& xd) noexcept;
    [[nodiscard]] bool execute_xpacd(std::uint64_t& xd) noexcept;

    // Branch & Return Instructions
    [[nodiscard]] bool execute_braa(std::uint64_t xn, std::uint64_t xm, std::uint64_t& out_target, std::uint64_t pc = 0);
    [[nodiscard]] bool execute_brab(std::uint64_t xn, std::uint64_t xm, std::uint64_t& out_target, std::uint64_t pc = 0);
    [[nodiscard]] bool execute_blraa(std::uint64_t xn, std::uint64_t xm, std::uint64_t& lr, std::uint64_t& out_target, std::uint64_t pc = 0);
    [[nodiscard]] bool execute_blrab(std::uint64_t xn, std::uint64_t xm, std::uint64_t& lr, std::uint64_t& out_target, std::uint64_t pc = 0);
    [[nodiscard]] bool execute_retaa(std::uint64_t lr, std::uint64_t sp, std::uint64_t& out_target, std::uint64_t pc = 0);
    [[nodiscard]] bool execute_retab(std::uint64_t lr, std::uint64_t sp, std::uint64_t& out_target, std::uint64_t pc = 0);

private:
    [[nodiscard]] std::uint16_t compute_pac_tag(
        std::uint64_t canonical_ptr,
        std::uint64_t modifier,
        const PacKey128& key) const noexcept;

    PacKeyRing   keyring_{};
    PacFaultMode mode_{PacFaultMode::TrapException};
    PacSyndrome  last_syndrome_{};
};

} // namespace winios::cpu
