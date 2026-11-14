#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>

#include "cpu/cpu.h"
#include "memory/memory.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif

namespace winios::kernel {

// Magic identifiers for context structures
inline constexpr std::uint64_t kExceptionFrameMagic = 0x455843504652414DULL; // "EXCPFRAM"
inline constexpr std::uint64_t kExtendedContextMagic = 0x435055434E545854ULL; // "CPUCNTXT"

#pragma pack(push, 1)

// 128-bit SIMD/Vector register representation
struct alignas(16) GuestVectorReg {
    std::uint64_t low{0};
    std::uint64_t high{0};

    bool operator==(const GuestVectorReg& other) const noexcept {
        return low == other.low && high == other.high;
    }
    bool operator!=(const GuestVectorReg& other) const noexcept {
        return !(*this == other);
    }
};

// Full asynchronous exception frame saved on kernel stack upon architectural interrupt / exception
// Size is exactly 896 bytes (56 * 16 = 896, strictly 16-byte aligned)
struct alignas(16) ExceptionFrame {
    std::uint64_t magic{kExceptionFrameMagic};  // 0x000: Frame magic
    std::uint64_t frame_size{sizeof(ExceptionFrame)}; // 0x008: Frame byte size
    std::uint64_t elr_el1{0};                   // 0x010: Exception return PC (interrupted PC)
    std::uint64_t spsr_el1{0};                  // 0x018: Saved PSTATE (NZCV, DAIF, EL, SPSel)
    std::uint64_t esr_el1{0};                   // 0x020: Exception syndrome register
    std::uint64_t far_el1{0};                   // 0x028: Fault address register
    std::uint64_t sp_el0{0};                    // 0x030: User stack pointer
    std::uint64_t sp_el1{0};                    // 0x038: Kernel stack pointer before exception
    std::uint64_t tpidr_el0{0};                 // 0x040: EL0 thread pointer
    std::uint64_t tpidr_el1{0};                 // 0x048: EL1 thread pointer
    std::uint64_t asid{0};                      // 0x050: Address Space ID (TTBR0/TCR)
    std::uint64_t ttbr0_el1{0};                 // 0x058: Base address of user page table
    std::uint32_t fpcr{0};                      // 0x060: Floating-point control register
    std::uint32_t fpsr{0};                      // 0x064: Floating-point status register
    std::uint32_t exception_origin_el{0};       // 0x068: 0 = from EL0, 1 = from EL1
    std::uint32_t simd_saved{1};                // 0x06C: 1 if Q0-Q31 were preserved
    std::uint64_t gpr[31]{0};                   // 0x070 - 0x168: X0..X30 (248 bytes)
    std::uint64_t pad0{0};                      // 0x168: Align vector regs to 16-byte boundary (offset 0x170)
    GuestVectorReg q_regs[32]{};                // 0x170 - 0x370: Q0..Q31 (32 * 16 = 512 bytes)
    std::uint8_t reserved[16]{0};               // 0x370 - 0x380: Reserved padding to 896 bytes (0x380)
};

#pragma pack(pop)
static_assert(sizeof(ExceptionFrame) == 896, "ExceptionFrame must be exactly 896 bytes");
static_assert(sizeof(ExceptionFrame) % 16 == 0, "ExceptionFrame must be 16-byte aligned");

// Complete architectural thread context encompassing both basic TCB state and extended SIMD/FP
struct ArchitecturalCpuContext {
    std::uint64_t gpr[31]{0};
    std::uint64_t pc{0};
    std::uint64_t sp_el0{0};
    std::uint64_t sp_el1{0};
    std::uint64_t spsr_el1{0};
    std::uint32_t nzcv{0};
    std::uint32_t daif{0};
    std::uint64_t tpidr_el0{0};
    std::uint64_t tpidr_el1{0};
    std::uint64_t asid{0};
    std::uint64_t ttbr0_el1{0};
    std::uint32_t fpcr{0};
    std::uint32_t fpsr{0};
    std::uint64_t cpacr_el1{0x00300000ULL}; // FP/SIMD access enabled
    GuestVectorReg q_regs[32]{};

    // Capture full CPU state into this context
    void capture(const CPU& cpu);

    // Apply this context to the CPU
    void restore(CPU& cpu) const;

    // Compare two contexts for equality
    [[nodiscard]] bool equals(const ArchitecturalCpuContext& other) const noexcept;

    // Write exception frame directly into guest RAM
    bool write_frame_to_memory(Memory& mem, std::uint64_t guest_addr) const;

    // Read exception frame from guest RAM
    bool read_frame_from_memory(const Memory& mem, std::uint64_t guest_addr);
};

} // namespace winios::kernel

#ifdef _MSC_VER
#pragma warning(pop)
#endif
