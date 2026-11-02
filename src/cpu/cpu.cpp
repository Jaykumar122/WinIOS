#include "cpu.h"
#include "memory/memory.h"
#include <atomic>
#include <bit>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif
namespace {
// Phase 36: true while executing a guest instruction in architectural mode.
// In that mode guest address arithmetic wraps modulo 2^64 (as the architecture
// specifies); otherwise the historical checked-overflow behaviour is kept so
// that earlier phases observe exactly the errors they always did. The
// interpreter models one CPU and one flat RAM, so a file-local flag scoped to
// CPU::step is sufficient.
bool architectural_address_wrap = false;
struct ScopedArchitecturalAddressWrap {
    explicit ScopedArchitecturalAddressWrap(bool enabled)
        : previous(architectural_address_wrap) {
        architectural_address_wrap = enabled;
    }
    ~ScopedArchitecturalAddressWrap() { architectural_address_wrap = previous; }
    ScopedArchitecturalAddressWrap(const ScopedArchitecturalAddressWrap&) = delete;
    ScopedArchitecturalAddressWrap& operator=(const ScopedArchitecturalAddressWrap&) = delete;
    bool previous;
};
constexpr std::uint32_t kMovzMask = 0xFF800000u;
constexpr std::uint32_t kMovz64 = 0xD2800000u;
constexpr std::uint32_t kMovn64 = 0x92800000u;
constexpr std::uint32_t kMovk64 = 0xF2800000u;
constexpr std::uint32_t kAddImmediateMask = 0xFF800000u;
constexpr std::uint32_t kAddImmediate64 = 0x91000000u;
constexpr std::uint32_t kSubImmediate64 = 0xD1000000u;
constexpr std::uint32_t kLoadStoreUnsignedMask = 0xFFC00000u;
constexpr std::uint32_t kLdr64 = 0xF9400000u;
constexpr std::uint32_t kStr64 = 0xF9000000u;
constexpr std::uint32_t kLdr32 = 0xB9400000u;
constexpr std::uint32_t kStr32 = 0xB9000000u;
// Phase 19: LDR/STR (immediate, pre/post-index). Bit 21 is a fixed zero in
// this encoding, between opc (23:22) and imm9 (20:12), so it belongs in the
// mask; the unscaled forms have op2=00 and the unsigned-offset forms op2=10,
// while pre-index uses op2=11 and post-index op2=01.
constexpr std::uint32_t kLoadStoreIndexedMask = 0xFFE00C00u;
constexpr std::uint32_t kLdr64Pre = 0xF8400C00u;
constexpr std::uint32_t kStr64Pre = 0xF8000C00u;
constexpr std::uint32_t kLdr64Post = 0xF8400400u;
constexpr std::uint32_t kStr64Post = 0xF8000400u;
constexpr std::uint32_t kLdr32Pre = 0xB8400C00u;
constexpr std::uint32_t kStr32Pre = 0xB8000C00u;
constexpr std::uint32_t kLdr32Post = 0xB8400400u;
constexpr std::uint32_t kStr32Post = 0xB8000400u;
// Phase 20: LDUR/STUR (immediate, unscaled signed offset). These use opc2=00,
// which is disjoint from post-index (01), unsigned offset (10) and pre-index
// (11); bit 21 is a fixed zero and remains part of the mask.
constexpr std::uint32_t kLoadStoreUnscaledMask = 0xFFE00C00u;
constexpr std::uint32_t kLdur64 = 0xF8400000u;
constexpr std::uint32_t kStur64 = 0xF8000000u;
constexpr std::uint32_t kLdur32 = 0xB8400000u;
constexpr std::uint32_t kStur32 = 0xB8000000u;
// Phase 21: LDR/STR (register offset). Same immediate-family size/V/opc
// layout as Phases 19 and 20, but bit 21 is a fixed one and bits 11:10 are
// 10. option (15:13) is an operand: only encodings with option<1>==1 are
// allocated (UXTW, LSL/UXTX, SXTW, SXTX).
constexpr std::uint32_t kLoadStoreRegOffMask = 0xFFE00C00u;
constexpr std::uint32_t kLdr64RegOff = 0xF8600800u;
constexpr std::uint32_t kStr64RegOff = 0xF8200800u;
constexpr std::uint32_t kLdr32RegOff = 0xB8600800u;
constexpr std::uint32_t kStr32RegOff = 0xB8200800u;
// Phase 29: Sign-extending and PC-relative literal loads.
// Scaled unsigned offset:
constexpr std::uint32_t kLdrsbWOff = 0x39C00000u;
constexpr std::uint32_t kLdrsbXOff = 0x39800000u;
constexpr std::uint32_t kLdrshWOff = 0x79C00000u;
constexpr std::uint32_t kLdrshXOff = 0x79800000u;
constexpr std::uint32_t kLdrswXOff = 0xB9800000u;
// Unscaled signed immediate (LDUR):
constexpr std::uint32_t kLdursbW = 0x38C00000u;
constexpr std::uint32_t kLdursbX = 0x38800000u;
constexpr std::uint32_t kLdurshW = 0x78C00000u;
constexpr std::uint32_t kLdurshX = 0x78800000u;
constexpr std::uint32_t kLdurswX = 0xB8800000u;
// Pre-index immediate:
constexpr std::uint32_t kLdrsbWPre = 0x38C00C00u;
constexpr std::uint32_t kLdrsbXPre = 0x38800C00u;
constexpr std::uint32_t kLdrshWPre = 0x78C00C00u;
constexpr std::uint32_t kLdrshXPre = 0x78800C00u;
constexpr std::uint32_t kLdrswXPre = 0xB8800C00u;
// Post-index immediate:
constexpr std::uint32_t kLdrsbWPost = 0x38C00400u;
constexpr std::uint32_t kLdrsbXPost = 0x38800400u;
constexpr std::uint32_t kLdrshWPost = 0x78C00400u;
constexpr std::uint32_t kLdrshXPost = 0x78800400u;
constexpr std::uint32_t kLdrswXPost = 0xB8800400u;
// Register offset:
constexpr std::uint32_t kLdrsbWRegOff = 0x38E00800u;
constexpr std::uint32_t kLdrsbXRegOff = 0x38A00800u;
constexpr std::uint32_t kLdrshWRegOff = 0x78E00800u;
constexpr std::uint32_t kLdrshXRegOff = 0x78A00800u;
constexpr std::uint32_t kLdrswXRegOff = 0xB8A00800u;
// PC-relative literal loads:
constexpr std::uint32_t kLoadLiteralFamilyMask = 0x3F000000u;
constexpr std::uint32_t kLoadLiteralPattern    = 0x18000000u;
constexpr std::uint32_t kLdrWLiteral           = 0x18000000u;
constexpr std::uint32_t kLdrXLiteral           = 0x58000000u;
constexpr std::uint32_t kLdrswLiteral          = 0x98000000u;
constexpr std::uint32_t kPrfmLiteral           = 0xD8000000u;
// Phase 22: LDP/STP (load/store pair, signed scaled immediate).
// Mask covers bits 31:22: opc(31:30) | 101(29:27) | V=0(26) | mode(25:23) | L(22).
// Addressing modes in bits 25:23:
//   010: signed scaled offset (no writeback)
//   011: pre-index (writeback = base + offset, EA = writeback, base <- writeback)
//   001: post-index (EA = base, writeback = base + offset, base <- writeback)
constexpr std::uint32_t kLoadStorePairMask = 0xFFC00000u;
constexpr std::uint32_t kStp32Off  = 0x29000000u;
constexpr std::uint32_t kLdp32Off  = 0x29400000u;
constexpr std::uint32_t kStp32Pre  = 0x29800000u;
constexpr std::uint32_t kLdp32Pre  = 0x29C00000u;
constexpr std::uint32_t kStp32Post = 0x28800000u;
constexpr std::uint32_t kLdp32Post = 0x28C00000u;
constexpr std::uint32_t kStp64Off  = 0xA9000000u;
constexpr std::uint32_t kLdp64Off  = 0xA9400000u;
constexpr std::uint32_t kStp64Pre  = 0xA9800000u;
constexpr std::uint32_t kLdp64Pre  = 0xA9C00000u;
constexpr std::uint32_t kStp64Post = 0xA8800000u;
constexpr std::uint32_t kLdp64Post = 0xA8C00000u;
// Phase 30: Scalar byte/halfword loads/stores, LDPSW, and PRFM forms.
// Scaled unsigned offset:
constexpr std::uint32_t kStrbOff      = 0x39000000u;
constexpr std::uint32_t kLdrbOff      = 0x39400000u;
constexpr std::uint32_t kStrhOff      = 0x79000000u;
constexpr std::uint32_t kLdrhOff      = 0x79400000u;
constexpr std::uint32_t kPrfmImmOff   = 0xF9800000u;
// Unscaled signed immediate:
constexpr std::uint32_t kSturb        = 0x38000000u;
constexpr std::uint32_t kLdurb        = 0x38400000u;
constexpr std::uint32_t kSturh        = 0x78000000u;
constexpr std::uint32_t kLdurh        = 0x78400000u;
constexpr std::uint32_t kPrfmUnscaled = 0xF8800000u;
// Pre-index immediate:
constexpr std::uint32_t kStrbPre      = 0x38000C00u;
constexpr std::uint32_t kLdrbPre      = 0x38400C00u;
constexpr std::uint32_t kStrhPre      = 0x78000C00u;
constexpr std::uint32_t kLdrhPre      = 0x78400C00u;
// Post-index immediate:
constexpr std::uint32_t kStrbPost     = 0x38000400u;
constexpr std::uint32_t kLdrbPost     = 0x38400400u;
constexpr std::uint32_t kStrhPost     = 0x78000400u;
constexpr std::uint32_t kLdrhPost     = 0x78400400u;
// Register offset:
constexpr std::uint32_t kStrbRegOff   = 0x38200800u;
constexpr std::uint32_t kLdrbRegOff   = 0x38600800u;
constexpr std::uint32_t kStrhRegOff   = 0x78200800u;
constexpr std::uint32_t kLdrhRegOff   = 0x78600800u;
constexpr std::uint32_t kPrfmRegOff   = 0xF8A00800u;
// Load pair of signed words:
constexpr std::uint32_t kLdpswOff     = 0x69400000u;
constexpr std::uint32_t kLdpswPre     = 0x69C00000u;
constexpr std::uint32_t kLdpswPost    = 0x68C00000u;
// Phase 23: Conditional compare (register and immediate) and conditional select.
// CCMP / CCMN mask covers bits 31:21, 11:10, 4:
//   sf(31) | op(30) | S=1(29) | 11010010(28:21) | imm(11) | 0(10) | 0(4)
constexpr std::uint32_t kCcMask = 0xFFE00C10u;
constexpr std::uint32_t kCcmn32Reg = 0x3A400000u;
constexpr std::uint32_t kCcmp32Reg = 0x7A400000u;
constexpr std::uint32_t kCcmn64Reg = 0xBA400000u;
constexpr std::uint32_t kCcmp64Reg = 0xFA400000u;
constexpr std::uint32_t kCcmn32Imm = 0x3A400800u;
constexpr std::uint32_t kCcmp32Imm = 0x7A400800u;
constexpr std::uint32_t kCcmn64Imm = 0xBA400800u;
constexpr std::uint32_t kCcmp64Imm = 0xFA400800u;
// CSEL / CSINC / CSINV / CSNEG mask covers bits 31:21, 11:10:
//   sf(31) | op(30) | S=0(29) | 11010100(28:21) | 0(11) | op2(10)
constexpr std::uint32_t kCSelMask = 0xFFE00C00u;
constexpr std::uint32_t kCsel32   = 0x1A800000u;
constexpr std::uint32_t kCsinc32  = 0x1A800400u;
constexpr std::uint32_t kCsinv32  = 0x5A800000u;
constexpr std::uint32_t kCsneg32  = 0x5A800400u;
constexpr std::uint32_t kCsel64   = 0x9A800000u;
constexpr std::uint32_t kCsinc64  = 0x9A800400u;
constexpr std::uint32_t kCsinv64  = 0xDA800000u;
constexpr std::uint32_t kCsneg64  = 0xDA800400u;
// Phase 24: Bitfield move instructions: SBFM, BFM, UBFM (32-bit and 64-bit).
// Format: sf(31) | opc(30:29) | 100110(28:23) | N(22) | immr(21:16) | imms(15:10) | Rn(9:5) | Rd(4:0)
// Mask covers bits 31:22:
constexpr std::uint32_t kBitfieldMask = 0xFFC00000u;
constexpr std::uint32_t kSbfm32 = 0x13000000u; // sf=0, opc=00, N=0
constexpr std::uint32_t kBfm32  = 0x33000000u; // sf=0, opc=01, N=0
constexpr std::uint32_t kUbfm32 = 0x53000000u; // sf=0, opc=10, N=0
constexpr std::uint32_t kSbfm64 = 0x93400000u; // sf=1, opc=00, N=1
constexpr std::uint32_t kBfm64  = 0xB3400000u; // sf=1, opc=01, N=1
constexpr std::uint32_t kUbfm64 = 0xD3400000u; // sf=1, opc=10, N=1
constexpr std::uint32_t kBranchImmediateMask = 0x7C000000u;
constexpr std::uint32_t kBranchMask = 0xFC000000u;
constexpr std::uint32_t kBranch = 0x14000000u;
constexpr std::uint32_t kBranchLink = 0x94000000u;
constexpr std::uint32_t kBranchRegisterMask = 0xFFFFFC1Fu;
constexpr std::uint32_t kBranchRegister = 0xD61F0000u;
constexpr std::uint32_t kBranchLinkRegister = 0xD63F0000u;
constexpr std::uint32_t kReturnRegister = 0xD65F0000u;
constexpr std::uint32_t kConditionalBranchMask = 0xFF000010u;
constexpr std::uint32_t kConditionalBranch = 0x54000000u;
constexpr std::uint32_t kCompareBranchMask = 0x7E000000u;
constexpr std::uint32_t kCbz = 0x34000000u;
constexpr std::uint32_t kTestBitBranchMask = 0x7E000000u;
constexpr std::uint32_t kTbz = 0x36000000u;
constexpr std::uint32_t kAddSubImmediateMask = 0x1F800000u;
constexpr std::uint32_t kAddImmediate = 0x11000000u;
constexpr std::uint32_t kAddSubRegisterMask = 0x1F200000u;
constexpr std::uint32_t kAddSubRegister = 0x0B000000u;
constexpr std::uint32_t kLogicalShiftedRegisterMask = 0x1F000000u;
constexpr std::uint32_t kLogicalShiftedRegister = 0x0A000000u;
constexpr std::uint32_t kLogicalImmediateMask = 0x1F800000u;
constexpr std::uint32_t kLogicalImmediate = 0x12000000u;
constexpr std::uint32_t kDataProcessing3SourceMask = 0x7F000000u;
constexpr std::uint32_t kDataProcessing3Source = 0x1B000000u;
// Phase 27: Data-processing (2-source): UDIV, SDIV, LSLV, LSRV, ASRV, RORV
constexpr std::uint32_t kDataProcessing2SourceMask = 0x7FE00000u;
constexpr std::uint32_t kDataProcessing2Source = 0x1AC00000u;
// Phase 27: Data-processing (1-source): RBIT, REV16, REV32, REV, CLZ, CLS
constexpr std::uint32_t kDataProcessing1SourceMask = 0x7FFF0000u;
constexpr std::uint32_t kDataProcessing1Source = 0x5AC00000u;
// Phase 27: Extract: EXTR
constexpr std::uint32_t kExtractMask = 0x7FA00000u;
constexpr std::uint32_t kExtract = 0x13800000u;
// Phase 31: Load/store exclusive group and LSE atomic memory operations.
// Load/store exclusive: size(31:30) 001000 op(23:21) Rs o0 Rt2 Rn Rt.
constexpr std::uint32_t kExclusiveGroupMask = 0x3F000000u;
constexpr std::uint32_t kExclusiveGroup = 0x08000000u;
// LSE atomics: size(31:30) 111000 A R 1 Rs opc(15:12) 00 Rn Rt.
constexpr std::uint32_t kLseAtomicMask = 0x3F200C00u;
constexpr std::uint32_t kLseAtomicPattern = 0x38200000u;
constexpr std::uint8_t kNegative = 0x8u;
constexpr std::uint8_t kZero = 0x4u;
constexpr std::uint8_t kCarry = 0x2u;
constexpr std::uint8_t kOverflow = 0x1u;
// Phase 35: System register MRS/MSR and exception instructions.
// MRS Xt, sysreg:  1101 0101 0011 xxxx xxxx xxxx xxxx xxxx  (bit21=1, L=read)
// MSR sysreg, Xt:  1101 0101 0001 xxxx xxxx xxxx xxxx xxxx  (bit21=0, L=write)
// Both share bits[31:22] = 1101010100 = 0xD540_0000 with bits[21] distinguishing.
// Mask for detecting the system-register group (MRS or MSR-register):
//   bits[31:22] must be 1101010100 and bit[20] must be 1 (o0=1, i.e. op0=3).
// We use the wider mask 0xFFF80000 that checks bits[31:19] to select the
// "op0=3 system-register" sub-group, then check bit21 for direction.
constexpr std::uint32_t kSysRegMask = 0xFFF80000u; // bits [31:19]
constexpr std::uint32_t kSysRegBase = 0xD5380000u; // MRS prefix with o0=1, op1=0..7
// A broader mask that accepts both MRS (bit21=1) and MSR (bit21=0):
// bits[31:22]=1101010100, bit[20]=1 (o0), bits[21]=don't care in the "any" mask.
constexpr std::uint32_t kSysRegAnyMask  = 0xFFD80000u; // bits [31:22] + bit20, skip bit21
constexpr std::uint32_t kSysRegAnyBase  = 0xD5180000u; // with bit21 clear as base
// SVC: 1101 0100 000x xxxx xxxx xxxx xxx0 0001 (exception class = SVC/HVC/SMC based on opc)
// SVC from AArch64: bits[31:21]=1101 0100 000 (0xD4000000>>21), bits[4:0]=00001, imm16=[20:5]
constexpr std::uint32_t kSvcMask = 0xFFE0001Fu;
constexpr std::uint32_t kSvcPattern = 0xD4000001u;
// ERET: exact encoding 0xD69F03E0
constexpr std::uint32_t kEretEncoding = 0xD69F03E0u;
// MSR immediate (PSTATEfield): 1101 0101 0000 0[op1:3][CRm:4][op2:3] 1111 1111
// Used for MSR DAIFSet/DAIFClr and MSR SPSel.
// Bit pattern: 0xD500_4000 | (op1<<16) | (CRm<<8) | (op2<<5) | 0x1F
constexpr std::uint32_t kMsrImmMask = 0xFFF8F01Fu;
constexpr std::uint32_t kMsrImmBase = 0xD500401Fu; // bits fixed; op1/CRm/op2 variable
std::string register_index_message(std::size_t index) {
    return "Register index out of range: " + std::to_string(index)
           + " (valid range is 0.." + std::to_string(CPU::kRegisterCount - 1) + ")";
}
std::size_t pc_as_memory_offset(std::uint64_t pc) {
    if (pc > std::numeric_limits<std::size_t>::max()) {
        throw std::out_of_range("Instruction fetch address exceeds host memory offset range");
    }
    return static_cast<std::size_t>(pc);
}
std::uint64_t checked_add_address(std::uint64_t address,
                                  std::uint64_t displacement,
                                  const char* description) {
    if (architectural_address_wrap) {
        return address + displacement; // architectural modulo-2^64 wrap
    }
    if (displacement > std::numeric_limits<std::uint64_t>::max() - address) {
        throw std::out_of_range(std::string(description) + " overflows guest address space");
    }
    return address + displacement;
}
std::uint64_t checked_sub_address(std::uint64_t address,
                                  std::uint64_t displacement,
                                  const char* description) {
    if (architectural_address_wrap) {
        return address - displacement; // architectural modulo-2^64 wrap
    }
    if (displacement > address) {
        throw std::out_of_range(std::string(description) + " underflows guest address space");
    }
    return address - displacement;
}
std::uint64_t read_add_operand(const CPU& cpu, std::uint32_t register_index) {
    return register_index == CPU::kRegisterCount
               ? cpu.get_stack_pointer()
               : cpu.read_register(register_index);
}
std::uint64_t read_zero_register_operand(const CPU& cpu,
                                         std::uint32_t register_index) {
    return register_index == CPU::kRegisterCount
               ? 0
               : cpu.read_register(register_index);
}
std::uint64_t read_stack_pointer_operand(const CPU& cpu,
                                         std::uint32_t register_index) {
    return register_index == CPU::kRegisterCount
               ? cpu.get_stack_pointer()
               : cpu.read_register(register_index);
}
std::uint64_t width_mask(bool is64) {
    return is64 ? std::numeric_limits<std::uint64_t>::max() : 0xFFFFFFFFull;
}
// Phase 19: imm9 in the pre/post-index forms is a signed BYTE displacement,
// sign-extended from bit 8. It is never scaled by the access width.
std::int64_t decode_simm9(std::uint32_t field) {
    field &= 0x1FFu;
    return (field & 0x100u) != 0
               ? static_cast<std::int64_t>(field) - 0x200
               : static_cast<std::int64_t>(field);
}
// Phase 22: imm7 in load/store pair is a signed 7-bit immediate (bits 21:15),
// scaled by the element size (4 for 32-bit, 8 for 64-bit).
std::int64_t decode_simm7(std::uint32_t field) {
    field &= 0x7Fu;
    return (field & 0x40u) != 0
               ? static_cast<std::int64_t>(field) - 0x80
               : static_cast<std::int64_t>(field);
}
// Phase 29: imm19 in load literal is a signed 19-bit immediate (bits 23:5),
// scaled by 4 to form a byte displacement from the current instruction PC.
std::int64_t decode_simm19(std::uint32_t field) {
    field &= 0x7FFFFu;
    return (field & 0x40000u) != 0
               ? static_cast<std::int64_t>(field) - 0x80000
               : static_cast<std::int64_t>(field);
}
// Adds or subtracts a signed displacement with the same overflow/underflow
// guarantee as the existing address helpers.
std::uint64_t add_signed_address(std::uint64_t base, std::int64_t displacement,
                                 const char* description) {
    if (displacement >= 0) {
        return checked_add_address(base,
                                   static_cast<std::uint64_t>(displacement),
                                   description);
    }
    return checked_sub_address(base,
                               static_cast<std::uint64_t>(-displacement),
                               description);
}
// Add a register offset with the same overflow/underflow policy as the
// immediate forms. UXTW and LSL/UXTX are unsigned addends, so bit 63 set means
// the 64-bit addition overflows and must be rejected rather than silently
// wrapped. SXTW and SXTX are signed: bit 63 set is a negative offset and uses
// a checked subtraction so an address below zero is rejected.
std::uint64_t add_extended_offset_address(std::uint64_t base,
                                          std::uint64_t offset,
                                          bool signed_offset,
                                          const char* description) {
    if (signed_offset && (offset & (std::uint64_t{1} << 63u)) != 0) {
        return checked_sub_address(base, 0u - offset, description);
    }
    return checked_add_address(base, offset, description);
}
// Arm ARM ExtendReg for the four allocated load/store register-offset
// options, then a logical left shift of 0 or scale. option<1>==0 is
// unallocated and must not reach this helper.
std::uint64_t extend_register_offset(std::uint64_t rm_value,
                                     std::uint32_t option,
                                     std::uint32_t shift) {
    std::uint64_t value;
    switch (option) {
    case 2u: // UXTW: zero-extend Wm
        value = rm_value & 0xFFFFFFFFull;
        break;
    case 3u: // UXTX / LSL: full Xm
        value = rm_value;
        break;
    case 6u: // SXTW: sign-extend Wm
        value = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(static_cast<std::int32_t>(
                static_cast<std::uint32_t>(rm_value))));
        break;
    case 7u: // SXTX: full Xm (sign-extend of 64 bits is identity)
        value = rm_value;
        break;
    default:
        throw std::runtime_error("Unsupported AArch64 instruction encoding");
    }
    if (shift != 0u) {
        value <<= shift;
    }
    return value;
}
std::string transfer_register_name(std::uint32_t index, bool is32) {
    if (index == CPU::kRegisterCount) {
        return is32 ? "WZR" : "XZR";
    }
    return (is32 ? "W" : "X") + std::to_string(index);
}
std::string index_register_name(std::uint32_t index, bool is32) {
    if (index == CPU::kRegisterCount) {
        return is32 ? "WZR" : "XZR";
    }
    return (is32 ? "W" : "X") + std::to_string(index);
}
std::uint64_t shift_register_value(std::uint64_t value, bool is64,
                                   std::uint32_t shift_type,
                                   std::uint32_t amount) {
    const std::uint64_t mask = width_mask(is64);
    value &= mask;
    if (shift_type == 0u) {
        return amount == 0u ? value : (value << amount) & mask;
    }
    if (shift_type == 1u) {
        return value >> amount;
    }
    const unsigned width = is64 ? 64u : 32u;
    if (shift_type == 2u) {
        const std::uint64_t sign = is64 ? (std::uint64_t{1} << 63u)
                                        : (std::uint64_t{1} << 31u);
        if ((value & sign) == 0 || amount == 0u) {
            return value >> amount;
        }
        return (value >> amount) | ((mask << (width - amount)) & mask);
    }
    if (shift_type == 3u) {
        if (amount == 0u) {
            return value;
        }
        return (value >> amount) | ((value << (width - amount)) & mask);
    }
    throw std::runtime_error("Unsupported shift type");
}
bool decode_logical_immediate(std::uint32_t sf, std::uint32_t N,
                              std::uint32_t imms, std::uint32_t immr,
                              std::uint64_t& result) {
    if (sf == 0u && N != 0u) {
        return false;
    }
    const std::uint32_t combined = (N << 6u) | (~imms & 0x3Fu);
    int len = -1;
    for (int b = 6; b >= 0; --b) {
        if ((combined & (1u << b)) != 0u) {
            len = b;
            break;
        }
    }
    if (len < 1) {
        return false;
    }
    const std::uint32_t e = 1u << len;
    const std::uint32_t levels = e - 1u;
    const std::uint32_t s = imms & levels;
    const std::uint32_t r = immr & levels;
    if (s == levels) {
        return false;
    }
    std::uint64_t mask = (s == 63u) ? ~0ull : ((1ull << (s + 1u)) - 1ull);
    if (r != 0u) {
        const std::uint64_t emask = (e == 64u) ? ~0ull : ((1ull << e) - 1ull);
        mask = ((mask >> r) | (mask << (e - r))) & emask;
    }
    std::uint32_t cur_e = e;
    while (cur_e < 64u) {
        mask |= (mask << cur_e);
        cur_e *= 2u;
    }
    if (sf == 0u) {
        mask &= 0xFFFFFFFFull;
    }
    result = mask;
    return true;
}
inline std::uint64_t compute_umulh(std::uint64_t a, std::uint64_t b) {
#if defined(__SIZEOF_INT128__)
    const unsigned __int128 prod = static_cast<unsigned __int128>(a) * b;
    return static_cast<std::uint64_t>(prod >> 64);
#elif defined(_MSC_VER) && defined(_M_X64)
    std::uint64_t high = 0;
    _umul128(a, b, &high);
    return high;
#else
    const std::uint64_t a_lo = static_cast<std::uint32_t>(a);
    const std::uint64_t a_hi = a >> 32;
    const std::uint64_t b_lo = static_cast<std::uint32_t>(b);
    const std::uint64_t b_hi = b >> 32;
    const std::uint64_t p00 = a_lo * b_lo;
    const std::uint64_t p01 = a_lo * b_hi;
    const std::uint64_t p10 = a_hi * b_lo;
    const std::uint64_t p11 = a_hi * b_hi;
    const std::uint64_t mid = p01 + (p00 >> 32);
    const std::uint64_t mid1 = mid + p10;
    const std::uint64_t carry = (mid1 < mid) ? 1ull : 0ull;
    return p11 + (mid1 >> 32) + (carry << 32);
#endif
}
inline std::uint64_t compute_smulh(std::int64_t a, std::int64_t b) {
#if defined(__SIZEOF_INT128__)
    const __int128 prod = static_cast<__int128>(a) * b;
    return static_cast<std::uint64_t>(prod >> 64);
#elif defined(_MSC_VER) && defined(_M_X64)
    std::int64_t high = 0;
    _mul128(a, b, &high);
    return static_cast<std::uint64_t>(high);
#else
    const std::uint64_t u_a = static_cast<std::uint64_t>(a);
    const std::uint64_t u_b = static_cast<std::uint64_t>(b);
    std::uint64_t hi = compute_umulh(u_a, u_b);
    if (a < 0) {
        hi -= u_b;
    }
    if (b < 0) {
        hi -= u_a;
    }
    return hi;
#endif
}
inline std::uint32_t reverse_bits32(std::uint32_t x) noexcept {
    x = (((x & 0xAAAAAAAAu) >> 1u) | ((x & 0x55555555u) << 1u));
    x = (((x & 0xCCCCCCCCu) >> 2u) | ((x & 0x33333333u) << 2u));
    x = (((x & 0xF0F0F0F0u) >> 4u) | ((x & 0x0F0F0F0Fu) << 4u));
    x = (((x & 0xFF00FF00u) >> 8u) | ((x & 0x00FF00FFu) << 8u));
    return (x >> 16u) | (x << 16u);
}
inline std::uint64_t reverse_bits64(std::uint64_t x) noexcept {
    x = (((x & 0xAAAAAAAAAAAAAAAAull) >> 1u) | ((x & 0x5555555555555555ull) << 1u));
    x = (((x & 0xCCCCCCCCCCCCCCCCull) >> 2u) | ((x & 0x3333333333333333ull) << 2u));
    x = (((x & 0xF0F0F0F0F0F0F0F0ull) >> 4u) | ((x & 0x0F0F0F0F0F0F0F0Full) << 4u));
    x = (((x & 0xFF00FF00FF00FF00ull) >> 8u) | ((x & 0x00FF00FF00FF00FFull) << 8u));
    x = (((x & 0xFFFF0000FFFF0000ull) >> 16u) | ((x & 0x0000FFFF0000FFFFull) << 16u));
    return (x >> 32u) | (x << 32u);
}
inline std::uint32_t reverse_bytes16_32(std::uint32_t x) noexcept {
    return ((x & 0x00FF00FFu) << 8u) | ((x & 0xFF00FF00u) >> 8u);
}
inline std::uint64_t reverse_bytes16_64(std::uint64_t x) noexcept {
    return ((x & 0x00FF00FF00FF00FFull) << 8u) | ((x & 0xFF00FF00FF00FF00ull) >> 8u);
}
inline std::uint32_t reverse_bytes32(std::uint32_t x) noexcept {
#if defined(_MSC_VER)
    return _byteswap_ulong(x);
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(x);
#else
    return ((x & 0x000000FFu) << 24u) |
           ((x & 0x0000FF00u) << 8u) |
           ((x & 0x00FF0000u) >> 8u) |
           ((x & 0xFF000000u) >> 24u);
#endif
}
inline std::uint64_t reverse_bytes64(std::uint64_t x) noexcept {
#if defined(_MSC_VER)
    return _byteswap_uint64(x);
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(x);
#else
    return ((x & 0x00000000000000FFull) << 56u) |
           ((x & 0x000000000000FF00ull) << 40u) |
           ((x & 0x0000000000FF0000ull) << 24u) |
           ((x & 0x00000000FF000000ull) << 8u) |
           ((x & 0x000000FF00000000ull) >> 8u) |
           ((x & 0x0000FF0000000000ull) >> 24u) |
           ((x & 0x00FF000000000000ull) >> 40u) |
           ((x & 0xFF00000000000000ull) >> 56u);
#endif
}
inline std::uint64_t reverse_bytes32_in_64(std::uint64_t x) noexcept {
    const std::uint64_t hi = reverse_bytes32(static_cast<std::uint32_t>(x >> 32u));
    const std::uint64_t lo = reverse_bytes32(static_cast<std::uint32_t>(x & 0xFFFFFFFFu));
    return (hi << 32u) | lo;
}
inline std::uint32_t count_leading_zeros32(std::uint32_t x) noexcept {
    if (x == 0u) {
        return 32u;
    }
#if defined(_MSC_VER)
    unsigned long index = 0;
    _BitScanReverse(&index, x);
    return 31u - index;
#elif defined(__GNUC__) || defined(__clang__)
    return static_cast<std::uint32_t>(__builtin_clz(x));
#else
    std::uint32_t count = 0;
    for (int b = 31; b >= 0; --b) {
        if ((x & (1u << b)) != 0u) break;
        ++count;
    }
    return count;
#endif
}
inline std::uint64_t count_leading_zeros64(std::uint64_t x) noexcept {
    if (x == 0ull) {
        return 64ull;
    }
#if defined(_MSC_VER)
    unsigned long index = 0;
    _BitScanReverse64(&index, x);
    return 63ull - index;
#elif defined(__GNUC__) || defined(__clang__)
    return static_cast<std::uint64_t>(__builtin_clzll(x));
#else
    std::uint64_t count = 0;
    for (int b = 63; b >= 0; --b) {
        if ((x & (1ull << b)) != 0ull) break;
        ++count;
    }
    return count;
#endif
}
inline std::uint32_t count_leading_sign_bits32(std::uint32_t x) noexcept {
    const std::uint32_t y = ((x & 0x80000000u) != 0u) ? ~x : x;
    return count_leading_zeros32(y) - 1u;
}
inline std::uint64_t count_leading_sign_bits64(std::uint64_t x) noexcept {
    const std::uint64_t y = ((x & 0x8000000000000000ull) != 0ull) ? ~x : x;
    return count_leading_zeros64(y) - 1ull;
}
std::uint64_t read_width_operand(const CPU& cpu, std::uint32_t index,
                                 bool is64, bool stack_pointer_allowed) {
    const std::uint64_t value = index == CPU::kRegisterCount
        ? (stack_pointer_allowed ? cpu.get_stack_pointer() : 0)
        : cpu.read_register(index);
    return value & width_mask(is64);
}
void write_width_result(CPU& cpu, std::uint32_t index, std::uint64_t value,
                        bool is64, bool stack_pointer_allowed) {
    value &= width_mask(is64);
    if (index == CPU::kRegisterCount) {
        if (stack_pointer_allowed) {
            cpu.set_stack_pointer(value);
        }
    } else {
        cpu.write_register(index, value);
    }
}
std::uint8_t arithmetic_flags(std::uint64_t left, std::uint64_t right,
                              std::uint64_t result, bool subtract, bool is64) {
    const std::uint64_t mask = width_mask(is64);
    const std::uint64_t sign = is64 ? (1ull << 63u) : (1ull << 31u);
    left &= mask;
    right &= mask;
    result &= mask;
    std::uint8_t flags = (result & sign) != 0 ? kNegative : 0;
    if (result == 0) flags |= kZero;
    if (subtract ? left >= right : result < left) flags |= kCarry;
    const bool overflow = subtract
        ? (((left ^ right) & (left ^ result) & sign) != 0)
        : (((~(left ^ right)) & (left ^ result) & sign) != 0);
    if (overflow) flags |= kOverflow;
    return flags;
}
bool condition_holds(std::uint32_t condition, std::uint8_t nzcv) {
    const bool n = (nzcv & kNegative) != 0;
    const bool z = (nzcv & kZero) != 0;
    const bool c = (nzcv & kCarry) != 0;
    const bool v = (nzcv & kOverflow) != 0;
    switch (condition & 0xFu) {
    case 0x0: return z;
    case 0x1: return !z;
    case 0x2: return c;
    case 0x3: return !c;
    case 0x4: return n;
    case 0x5: return !n;
    case 0x6: return v;
    case 0x7: return !v;
    case 0x8: return c && !z;
    case 0x9: return !c || z;
    case 0xA: return n == v;
    case 0xB: return n != v;
    case 0xC: return !z && (n == v);
    case 0xD: return z || (n != v);
    case 0xE:
    case 0xF: return true;
    default: return false;
    }
}
void write_add_result(CPU& cpu, std::uint32_t register_index, std::uint64_t value) {
    if (register_index == CPU::kRegisterCount) {
        cpu.set_stack_pointer(value);
    } else {
        cpu.write_register(register_index, value);
    }
}
void write_zero_register_result(CPU& cpu, std::uint32_t register_index,
                                std::uint64_t value) {
    if (register_index != CPU::kRegisterCount) {
        cpu.write_register(register_index, value);
    }
}
// Phase 31 helpers. `size_field` is the AArch64 access-size encoding used by
// exclusives and LSE atomics: 0 = byte, 1 = halfword, 2 = word, 3 = doubleword.
std::size_t access_width_bytes(std::uint32_t size_field) {
    return std::size_t{1} << size_field;
}
std::uint64_t access_value_mask(std::uint32_t size_field) {
    if (size_field >= 3u) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return (std::uint64_t{1} << (8u * (1u << size_field))) - 1u;
}
// Phase 36: the two access helpers are templates so they work with either the
// plain Memory (legacy mode) or the checked wrapper that performs the
// architectural fault checks.
template <typename MemoryT>
std::uint64_t read_access_value(const MemoryT& memory, std::size_t offset,
                                std::uint32_t size_field) {
    switch (size_field) {
    case 0u: return memory.read_u8(offset);
    case 1u: return memory.read_u16(offset);
    case 2u: return memory.read_u32(offset);
    default: return memory.read_u64(offset);
    }
}
template <typename MemoryT>
void write_access_value(MemoryT& memory, std::size_t offset,
                        std::uint32_t size_field, std::uint64_t value) {
    switch (size_field) {
    case 0u: memory.write_u8(offset, static_cast<std::uint8_t>(value)); break;
    case 1u: memory.write_u16(offset, static_cast<std::uint16_t>(value)); break;
    case 2u: memory.write_u32(offset, static_cast<std::uint32_t>(value)); break;
    default: memory.write_u64(offset, value); break;
    }
}
std::int64_t signed_access_value(std::uint64_t value, std::uint32_t size_field) {
    switch (size_field) {
    case 0u: return static_cast<std::int8_t>(value);
    case 1u: return static_cast<std::int16_t>(value);
    case 2u: return static_cast<std::int32_t>(value);
    default: return static_cast<std::int64_t>(value);
    }
}
template <typename MemoryT>
void require_memory_range(const MemoryT& memory, std::size_t offset,
                          std::size_t width) {
    if (offset > memory.size() || width > memory.size() - offset) {
        std::ostringstream message;
        message << "Memory access out of range: offset " << offset
                << ", width " << width << ", size " << memory.size();
        throw std::out_of_range(message.str());
    }
}

// Phase 36: checked view of guest RAM used for every data access performed by
// an instruction. In legacy mode it forwards directly to Memory (identical
// behaviour to earlier phases). In architectural mode it first applies the
// reference's fault order for a Device-memory (MMU off) access:
//   1. natural alignment of the access width     -> alignment fault (FSC 0x21)
//   2. address at/above the implemented PA width -> address-size fault (FSC 0x00)
//   3. address outside the backed RAM            -> external abort (FSC 0x10)
// A failing access unwinds the instruction with SynchronousExceptionSignal, so
// no register, PC, or guest-memory effect of the faulting instruction commits.
class CheckedGuestMemory {
public:
    CheckedGuestMemory(Memory& memory, const CPU& cpu, bool architectural,
                       std::uint64_t instruction_address)
        : memory_(memory), cpu_(cpu), architectural_(architectural),
          instruction_address_(instruction_address) {}

    std::size_t size() const noexcept { return memory_.size(); }

    std::uint8_t read_u8(std::size_t offset) const {
        return static_cast<std::uint8_t>(read(offset, 1u));
    }
    std::uint16_t read_u16(std::size_t offset) const {
        return static_cast<std::uint16_t>(read(offset, 2u));
    }
    std::uint32_t read_u32(std::size_t offset) const {
        return static_cast<std::uint32_t>(read(offset, 4u));
    }
    std::uint64_t read_u64(std::size_t offset) const { return read(offset, 8u); }

    void write_u8(std::size_t offset, std::uint8_t value) { write(offset, 1u, value); }
    void write_u16(std::size_t offset, std::uint16_t value) { write(offset, 2u, value); }
    void write_u32(std::size_t offset, std::uint32_t value) { write(offset, 4u, value); }
    void write_u64(std::size_t offset, std::uint64_t value) { write(offset, 8u, value); }

    // Explicit check used by the pair/quadword paths that issue several
    // element accesses for one architectural access (and to give 128-bit
    // SIMD accesses their 16-byte alignment requirement).
    void check_access(std::uint64_t address, unsigned width,
                      AccessType access) const {
        if (!architectural_) {
            return;
        }
        if (!cpu_.get_mmu().is_stage1_enabled() && !cpu_.get_mmu().is_stage2_enabled(static_cast<unsigned>(cpu_.get_current_el()))) {
            check(address, width, access);
            return;
        }
        translate_or_fault(address, width, access);
        const std::uint64_t page1 = address & ~4095ULL;
        const std::uint64_t page2 = (address + width - 1u) & ~4095ULL;
        if (page1 != page2) {
            translate_or_fault(address + width - 1u, 1u, access);
        }
    }
    // Range/PA check without the alignment test (used where a wider
    // architectural access has a coarser alignment rule than its width).
    void check_range(std::uint64_t address, unsigned width,
                     AccessType access) const {
        if (!architectural_) {
            return;
        }
        if (!cpu_.get_mmu().is_stage1_enabled() && !cpu_.get_mmu().is_stage2_enabled(static_cast<unsigned>(cpu_.get_current_el()))) {
            check_address_size_and_range(address, width, access);
            return;
        }
        translate_or_fault(address, width, access);
        const std::uint64_t page1 = address & ~4095ULL;
        const std::uint64_t page2 = (address + width - 1u) & ~4095ULL;
        if (page1 != page2) {
            translate_or_fault(address + width - 1u, 1u, access);
        }
    }
    // Reports an alignment fault at an explicit address. Used for the
    // reference's exclusive-pair quirk, where FAR holds the second doubleword.
    [[noreturn]] void raise_alignment_fault(std::uint64_t address,
                                            unsigned width,
                                            AccessType access) const {
        fault(address, access, FaultStatus::Alignment, width);
    }

private:
    std::uint64_t translate_or_fault(std::uint64_t address, unsigned width, AccessType access) const {
        if (!architectural_) {
            return address;
        }
        if (!cpu_.get_mmu().is_stage1_enabled() && !cpu_.get_mmu().is_stage2_enabled(static_cast<unsigned>(cpu_.get_current_el()))) {
            if (width > 1u && (address & (std::uint64_t{width} - 1u)) != 0u) {
                fault(address, access, FaultStatus::Alignment, width);
            }
            check_address_size_and_range(address, width, access);
            return address;
        }

        if (cpu_.get_mmu().is_alignment_check_enabled()) {
            if (width > 1u && (address & (std::uint64_t{width} - 1u)) != 0u) {
                fault(address, access, FaultStatus::Alignment, width);
            }
        }

        mmu::TranslationFault mmu_fault;
        auto pa_opt = const_cast<CPU&>(cpu_).get_mmu().translate(
            memory_, address,
            (access == AccessType::Write) ? mmu::AccessKind::Write : mmu::AccessKind::Read,
            static_cast<unsigned>(cpu_.get_current_el()),
            &mmu_fault);
        if (!pa_opt.has_value()) {
            if (mmu_fault.is_stage2) {
                SynchronousException e = make_data_abort(
                    false /* from lower EL */, access, static_cast<FaultStatus>(mmu_fault.fsc()),
                    address, instruction_address_, width);
                e.has_fault_ipa = true;
                e.fault_ipa = mmu_fault.fault_ipa;
                e.target_el = 2;
                throw SynchronousExceptionSignal{e};
            }
            fault(address, access, static_cast<FaultStatus>(mmu_fault.fsc()), width);
        }

        const std::uint64_t pa = pa_opt.value();
        const std::uint64_t size = static_cast<std::uint64_t>(memory_.size());
        if (pa > size || static_cast<std::uint64_t>(width) > size - pa) {
            fault(address, access, FaultStatus::SynchronousExternal, width);
        }
        return pa;
    }

    std::uint64_t read(std::size_t offset, unsigned width) const {
        const std::uint64_t pa = translate_or_fault(offset, width, AccessType::Read);
        switch (width) {
        case 1u: return memory_.read_u8(static_cast<std::size_t>(pa));
        case 2u: return memory_.read_u16(static_cast<std::size_t>(pa));
        case 4u: return memory_.read_u32(static_cast<std::size_t>(pa));
        default: return memory_.read_u64(static_cast<std::size_t>(pa));
        }
    }
    void write(std::size_t offset, unsigned width, std::uint64_t value) {
        const std::uint64_t pa = translate_or_fault(offset, width, AccessType::Write);
        switch (width) {
        case 1u: memory_.write_u8(static_cast<std::size_t>(pa), static_cast<std::uint8_t>(value)); break;
        case 2u: memory_.write_u16(static_cast<std::size_t>(pa), static_cast<std::uint16_t>(value)); break;
        case 4u: memory_.write_u32(static_cast<std::size_t>(pa), static_cast<std::uint32_t>(value)); break;
        default: memory_.write_u64(static_cast<std::size_t>(pa), value); break;
        }
    }
    [[noreturn]] void fault(std::uint64_t address, AccessType access,
                            FaultStatus status, unsigned width) const {
        const bool same_el = cpu_.get_current_el() != 0;
        throw SynchronousExceptionSignal{make_data_abort(
            same_el, access, status, address, instruction_address_, width)};
    }
    void check(std::uint64_t address, unsigned width, AccessType access) const {
        if (!architectural_) {
            return;
        }
        if (width > 1u && (address & (std::uint64_t{width} - 1u)) != 0u) {
            fault(address, access, FaultStatus::Alignment, width);
        }
        check_address_size_and_range(address, width, access);
    }
    void check_address_size_and_range(std::uint64_t address, unsigned width,
                                      AccessType access) const {
        if (address >= kPhysicalAddressLimit) {
            fault(address, access, FaultStatus::AddressSizeLevel0, width);
        }
        const std::uint64_t size = static_cast<std::uint64_t>(memory_.size());
        if (address > size || static_cast<std::uint64_t>(width) > size - address) {
            fault(address, access, FaultStatus::SynchronousExternal, width);
        }
    }

    Memory& memory_;
    const CPU& cpu_;
    bool architectural_;
    std::uint64_t instruction_address_;
};
std::string access_register_name(std::uint32_t index, std::uint32_t size_field) {
    if (size_field >= 3u) {
        return index == CPU::kRegisterCount ? "XZR" : "X" + std::to_string(index);
    }
    return index == CPU::kRegisterCount ? "WZR" : "W" + std::to_string(index);
}
std::string stack_register_name(std::uint32_t index) {
    return index == CPU::kRegisterCount ? "SP" : "X" + std::to_string(index);
}
std::string register_name(std::uint32_t index, bool stack_pointer_allowed) {
    if (index == CPU::kRegisterCount) {
        return stack_pointer_allowed ? "SP" : "XZR";
    }
    return "X" + std::to_string(index);
}
std::string hex_word(std::uint32_t word) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8)
           << std::setfill('0') << word;
    return stream.str();
}

std::string hex_word64(std::uint64_t word) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(16)
           << std::setfill('0') << word;
    return stream.str();
}
// Returns true and fills `text` when the word is a load/store exclusive or LSE
// atomic operation this interpreter implements.
bool disassemble_exclusive_or_atomic(std::uint32_t instruction,
                                     std::string& text) {
    const std::uint32_t size_field = (instruction >> 30u) & 0x3u;
    if ((instruction & kExclusiveGroupMask) == kExclusiveGroup) {
        const std::uint32_t op = (instruction >> 21u) & 0x7u;
        const bool ordered = (instruction & (1u << 15u)) != 0;
        const std::uint32_t status = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t target2 = (instruction >> 10u) & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        if (op == 0u || op == 2u) {
            std::string mnemonic = op == 2u ? (ordered ? "LDAXR" : "LDXR")
                                            : (ordered ? "STLXR" : "STXR");
            if (size_field == 0u) {
                mnemonic += "B";
            } else if (size_field == 1u) {
                mnemonic += "H";
            }
            std::ostringstream stream;
            stream << mnemonic << ' ';
            if (op == 0u) {
                stream << (status == CPU::kRegisterCount
                               ? std::string("WZR")
                               : "W" + std::to_string(status))
                       << ", ";
            }
            stream << access_register_name(target, size_field) << ", ["
                   << stack_register_name(base) << ']';
            text = stream.str();
            return true;
        }
        if (op == 1u || op == 3u) {
            if ((size_field & 2u) == 0u) {
                return false;
            }
            const char* mnemonic = op == 3u
                ? (ordered ? "LDAXP" : "LDXP")
                : (ordered ? "STLXP" : "STXP");
            std::ostringstream stream;
            stream << mnemonic << ' ';
            if (op == 1u) {
                stream << (status == CPU::kRegisterCount
                               ? std::string("WZR")
                               : "W" + std::to_string(status))
                       << ", ";
            }
            stream << access_register_name(target, size_field) << ", "
                   << access_register_name(target2, size_field) << ", ["
                   << stack_register_name(base) << ']';
            text = stream.str();
            return true;
        }
        if (op == 4u || op == 6u) {
            if (status != 31u || target2 != 31u || !ordered) {
                return false;
            }
            std::string mnemonic = (op == 6u) ? "LDAR" : "STLR";
            if (size_field == 0u) {
                mnemonic += "B";
            } else if (size_field == 1u) {
                mnemonic += "H";
            }
            std::ostringstream stream;
            stream << mnemonic << ' ' << access_register_name(target, size_field)
                   << ", [" << stack_register_name(base) << ']';
            text = stream.str();
            return true;
        }
        return false;
    }
    if ((instruction & kLseAtomicMask) != kLseAtomicPattern) {
        return false;
    }
    const std::uint32_t opc = (instruction >> 12u) & 0xFu;
    if (opc > 8u) {
        return false;
    }
    static const char* const kBaseNames[] = {
        "LDADD", "LDCLR", "LDEOR", "LDSET", "LDSMAX",
        "LDSMIN", "LDUMAX", "LDUMIN", "SWP"};
    std::string mnemonic = kBaseNames[opc];
    if ((instruction & (1u << 23u)) != 0) {
        mnemonic += "A";
    }
    if ((instruction & (1u << 22u)) != 0) {
        mnemonic += "L";
    }
    if (size_field == 0u) {
        mnemonic += "B";
    } else if (size_field == 1u) {
        mnemonic += "H";
    }
    const std::uint32_t source = (instruction >> 16u) & 0x1Fu;
    const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
    const std::uint32_t target = instruction & 0x1Fu;
    std::ostringstream stream;
    stream << mnemonic << ' ' << access_register_name(source, size_field) << ", "
           << access_register_name(target, size_field) << ", ["
           << stack_register_name(base) << ']';
    text = stream.str();
    return true;
}

// Phase 32 Floating-Point & Advanced SIMD helpers and disassembler
uint64_t vfp_expand_imm(int size, std::uint8_t imm8) {
    uint64_t imm = 0;
    if (size == 3) { // Double
        imm = ((imm8 & 0x80u) ? 0x8000ull : 0ull) |
              ((imm8 & 0x40u) ? 0x3fc0ull : 0x4000ull) |
              (imm8 & 0x3Fu);
        imm <<= 48;
    } else if (size == 2) { // Single
        imm = ((imm8 & 0x80u) ? 0x8000ull : 0ull) |
              ((imm8 & 0x40u) ? 0x3e00ull : 0x4000ull) |
              ((imm8 & 0x3Fu) << 3);
        imm <<= 16;
    }
    return imm;
}

template <typename Float>
std::uint8_t fp_compare_flags(Float a, Float b) {
    if (std::isnan(a) || std::isnan(b)) {
        return 0x3; // C=1, V=1 (unordered)
    }
    if (a == b) {
        return 0x6; // Z=1, C=1 (equal)
    }
    if (a < b) {
        return 0x8; // N=1 (less)
    }
    return 0x2; // C=1 (greater)
}

template <typename Float>
Float fp_round_ties_even(Float val) {
    if (std::isnan(val) || std::isinf(val)) return val;
    Float truncated = std::trunc(val);
    Float diff = val - truncated;
    if (std::abs(diff) > Float(0.5)) {
        return truncated + (diff > Float(0.0) ? Float(1.0) : Float(-1.0));
    } else if (std::abs(diff) == Float(0.5)) {
        double half = std::abs(truncated) * 0.5;
        if (half - std::floor(half) != 0.0) {
            return truncated + (diff > Float(0.0) ? Float(1.0) : Float(-1.0));
        }
        return truncated;
    }
    return truncated;
}

template <typename Int, typename Float>
Int fcvt_to_signed_sat(Float val, std::uint32_t& fpsr) {
    if (std::isnan(val)) {
        fpsr |= 1u;
        return 0;
    }
    if (val >= static_cast<Float>(std::numeric_limits<Int>::max())) {
        fpsr |= 1u;
        return std::numeric_limits<Int>::max();
    }
    if (val <= static_cast<Float>(std::numeric_limits<Int>::min())) {
        fpsr |= 1u;
        return std::numeric_limits<Int>::min();
    }
    Int res = static_cast<Int>(val);
    if (static_cast<Float>(res) != val) {
        fpsr |= (1u << 4);
    }
    return res;
}

template <typename UInt, typename Float>
UInt fcvt_to_unsigned_sat(Float val, std::uint32_t& fpsr) {
    if (std::isnan(val) || val <= Float(0.0)) {
        if (std::isnan(val) || val < Float(0.0)) fpsr |= 1u;
        return 0;
    }
    if (val >= static_cast<Float>(std::numeric_limits<UInt>::max())) {
        fpsr |= 1u;
        return std::numeric_limits<UInt>::max();
    }
    UInt res = static_cast<UInt>(val);
    if (static_cast<Float>(res) != val) {
        fpsr |= (1u << 4);
    }
    return res;
}

inline std::uint64_t get_vec_lane(const CPU::VectorRegister& v, unsigned idx, unsigned esz) {
    switch (esz) {
        case 0: {
            if (idx < 8) return (v.low >> (idx * 8u)) & 0xFFu;
            return (v.high >> ((idx - 8u) * 8u)) & 0xFFu;
        }
        case 1: {
            if (idx < 4) return (v.low >> (idx * 16u)) & 0xFFFFu;
            return (v.high >> ((idx - 4u) * 16u)) & 0xFFFFu;
        }
        case 2: {
            if (idx == 0) return v.low & 0xFFFFFFFFu;
            if (idx == 1) return v.low >> 32u;
            if (idx == 2) return v.high & 0xFFFFFFFFu;
            return v.high >> 32u;
        }
        case 3: {
            return (idx == 0) ? v.low : v.high;
        }
    }
    return 0;
}

inline void set_vec_lane(CPU::VectorRegister& v, unsigned idx, unsigned esz, std::uint64_t val) {
    switch (esz) {
        case 0: {
            const std::uint64_t mask = 0xFFull << ((idx % 8u) * 8u);
            const std::uint64_t shifted = (val & 0xFFull) << ((idx % 8u) * 8u);
            if (idx < 8u) v.low = (v.low & ~mask) | shifted;
            else v.high = (v.high & ~mask) | shifted;
            break;
        }
        case 1: {
            const std::uint64_t mask = 0xFFFFull << ((idx % 4u) * 16u);
            const std::uint64_t shifted = (val & 0xFFFFull) << ((idx % 4u) * 16u);
            if (idx < 4u) v.low = (v.low & ~mask) | shifted;
            else v.high = (v.high & ~mask) | shifted;
            break;
        }
        case 2: {
            const std::uint64_t mask = 0xFFFFFFFFull << ((idx % 2u) * 32u);
            const std::uint64_t shifted = (val & 0xFFFFFFFFull) << ((idx % 2u) * 32u);
            if (idx < 2u) v.low = (v.low & ~mask) | shifted;
            else v.high = (v.high & ~mask) | shifted;
            break;
        }
        case 3: {
            if (idx == 0) v.low = val;
            else v.high = val;
            break;
        }
    }
}

inline bool decode_esz_idx(std::uint32_t imm5, unsigned& esz, unsigned& idx) {
    if (imm5 == 0) return false;
    unsigned ctz = 0;
    while ((imm5 & (1u << ctz)) == 0) {
        ctz++;
    }
    if (ctz > 3u) return false;
    esz = ctz;
    idx = imm5 >> (esz + 1u);
    return true;
}

// Phase 33 Advanced SIMD arithmetic, shift, and saturation helpers
inline std::int64_t sign_extend(std::uint64_t val, unsigned esz) {
    switch (esz) {
        case 0: return static_cast<std::int8_t>(val);
        case 1: return static_cast<std::int16_t>(val);
        case 2: return static_cast<std::int32_t>(val);
        case 3: return static_cast<std::int64_t>(val);
    }
    return static_cast<std::int64_t>(val);
}

inline std::uint64_t vec_cmp_mask(bool cond, unsigned esz) {
    if (!cond) return 0;
    switch (esz) {
        case 0: return 0xFFull;
        case 1: return 0xFFFFull;
        case 2: return 0xFFFFFFFFull;
        case 3: return ~0ull;
    }
    return ~0ull;
}

inline std::uint64_t sqadd(std::uint64_t a_raw, std::uint64_t b_raw, unsigned esz, bool& sat) {
    const std::int64_t a = sign_extend(a_raw, esz);
    const std::int64_t b = sign_extend(b_raw, esz);
    switch (esz) {
        case 0: {
            const std::int32_t r = static_cast<std::int32_t>(a + b);
            if (r > 127) { sat = true; return 127; }
            if (r < -128) { sat = true; return static_cast<std::uint8_t>(-128); }
            return static_cast<std::uint8_t>(r);
        }
        case 1: {
            const std::int32_t r = static_cast<std::int32_t>(a + b);
            if (r > 32767) { sat = true; return 32767; }
            if (r < -32768) { sat = true; return static_cast<std::uint16_t>(-32768); }
            return static_cast<std::uint16_t>(r);
        }
        case 2: {
            const std::int64_t r = a + b;
            if (r > 2147483647LL) { sat = true; return 2147483647LL; }
            if (r < -2147483648LL) { sat = true; return static_cast<std::uint32_t>(-2147483648LL); }
            return static_cast<std::uint32_t>(r);
        }
        case 3: {
            if (b > 0 && a > (INT64_MAX - b)) { sat = true; return static_cast<std::uint64_t>(INT64_MAX); }
            if (b < 0 && a < (INT64_MIN - b)) { sat = true; return static_cast<std::uint64_t>(INT64_MIN); }
            return static_cast<std::uint64_t>(a + b);
        }
    }
    return 0;
}

inline std::uint64_t uqadd(std::uint64_t a, std::uint64_t b, unsigned esz, bool& sat) {
    switch (esz) {
        case 0: {
            const std::uint32_t r = (a & 0xFFu) + (b & 0xFFu);
            if (r > 0xFFu) { sat = true; return 0xFFu; }
            return r;
        }
        case 1: {
            const std::uint32_t r = (a & 0xFFFFu) + (b & 0xFFFFu);
            if (r > 0xFFFFu) { sat = true; return 0xFFFFu; }
            return r;
        }
        case 2: {
            const std::uint64_t r = (a & 0xFFFFFFFFull) + (b & 0xFFFFFFFFull);
            if (r > 0xFFFFFFFFull) { sat = true; return 0xFFFFFFFFull; }
            return r;
        }
        case 3: {
            const std::uint64_t r = a + b;
            if (r < a) { sat = true; return ~0ull; }
            return r;
        }
    }
    return 0;
}

inline std::uint64_t sqsub(std::uint64_t a_raw, std::uint64_t b_raw, unsigned esz, bool& sat) {
    const std::int64_t a = sign_extend(a_raw, esz);
    const std::int64_t b = sign_extend(b_raw, esz);
    switch (esz) {
        case 0: {
            const std::int32_t r = static_cast<std::int32_t>(a - b);
            if (r > 127) { sat = true; return 127; }
            if (r < -128) { sat = true; return static_cast<std::uint8_t>(-128); }
            return static_cast<std::uint8_t>(r);
        }
        case 1: {
            const std::int32_t r = static_cast<std::int32_t>(a - b);
            if (r > 32767) { sat = true; return 32767; }
            if (r < -32768) { sat = true; return static_cast<std::uint16_t>(-32768); }
            return static_cast<std::uint16_t>(r);
        }
        case 2: {
            const std::int64_t r = a - b;
            if (r > 2147483647LL) { sat = true; return 2147483647LL; }
            if (r < -2147483648LL) { sat = true; return static_cast<std::uint32_t>(-2147483648LL); }
            return static_cast<std::uint32_t>(r);
        }
        case 3: {
            if (b < 0 && a > (INT64_MAX + b)) { sat = true; return static_cast<std::uint64_t>(INT64_MAX); }
            if (b > 0 && a < (INT64_MIN + b)) { sat = true; return static_cast<std::uint64_t>(INT64_MIN); }
            return static_cast<std::uint64_t>(a - b);
        }
    }
    return 0;
}

inline std::uint64_t uqsub(std::uint64_t a, std::uint64_t b, unsigned esz, bool& sat) {
    switch (esz) {
        case 0: {
            const std::uint32_t ua = a & 0xFFu, ub = b & 0xFFu;
            if (ua < ub) { sat = true; return 0; }
            return ua - ub;
        }
        case 1: {
            const std::uint32_t ua = a & 0xFFFFu, ub = b & 0xFFFFu;
            if (ua < ub) { sat = true; return 0; }
            return ua - ub;
        }
        case 2: {
            const std::uint64_t ua = a & 0xFFFFFFFFull, ub = b & 0xFFFFFFFFull;
            if (ua < ub) { sat = true; return 0; }
            return ua - ub;
        }
        case 3: {
            if (a < b) { sat = true; return 0; }
            return a - b;
        }
    }
    return 0;
}

inline std::uint64_t smax_op(std::uint64_t a, std::uint64_t b, unsigned esz) {
    const std::int64_t sa = sign_extend(a, esz);
    const std::int64_t sb = sign_extend(b, esz);
    return (sa > sb) ? a : b;
}
inline std::uint64_t smin_op(std::uint64_t a, std::uint64_t b, unsigned esz) {
    const std::int64_t sa = sign_extend(a, esz);
    const std::int64_t sb = sign_extend(b, esz);
    return (sa < sb) ? a : b;
}
inline std::uint64_t umax_op(std::uint64_t a, std::uint64_t b, unsigned /*esz*/) {
    return (a > b) ? a : b;
}
inline std::uint64_t umin_op(std::uint64_t a, std::uint64_t b, unsigned /*esz*/) {
    return (a < b) ? a : b;
}

inline std::uint64_t sshl_op(std::uint64_t val, std::uint64_t shift_reg, unsigned esz) {
    const std::int8_t shift = static_cast<std::int8_t>(shift_reg & 0xFFu);
    const unsigned elem_bits = 8u << esz;
    if (shift >= 0) {
        if (shift < static_cast<int>(elem_bits)) {
            return (val << shift) & vec_cmp_mask(true, esz);
        }
        return 0;
    } else {
        const std::int64_t sval = sign_extend(val, esz);
        const int rshift = (shift > -static_cast<int>(elem_bits)) ? -shift : (elem_bits - 1);
        return static_cast<std::uint64_t>(sval >> rshift) & vec_cmp_mask(true, esz);
    }
}

inline std::uint64_t ushl_op(std::uint64_t val, std::uint64_t shift_reg, unsigned esz) {
    const std::int8_t shift = static_cast<std::int8_t>(shift_reg & 0xFFu);
    const unsigned elem_bits = 8u << esz;
    if (shift >= 0) {
        if (shift < static_cast<int>(elem_bits)) {
            return (val << shift) & vec_cmp_mask(true, esz);
        }
        return 0;
    } else {
        if (-shift < static_cast<int>(elem_bits)) {
            return (val >> (-shift)) & vec_cmp_mask(true, esz);
        }
        return 0;
    }
}

inline std::uint64_t shift_right_imm_op(std::uint64_t val, unsigned shift, unsigned esz, bool is_signed, bool round) {
    const unsigned elem_bits = 8u << esz;
    const std::uint64_t mask = vec_cmp_mask(true, esz);
    val &= mask;
    if (shift == 0) return val;

    std::uint64_t round_bit = 0;
    if (round) {
        round_bit = (val >> (shift - 1)) & 1ull;
    }

    if (shift == elem_bits) {
        if (round) {
            return is_signed ? 0 : round_bit;
        } else {
            if (is_signed) {
                const std::int64_t sval = sign_extend(val, esz);
                return static_cast<std::uint64_t>(sval >> (elem_bits - 1)) & mask;
            }
            return 0;
        }
    }

    if (is_signed) {
        const std::int64_t sval = sign_extend(val, esz);
        const std::int64_t res = (sval >> shift) + static_cast<std::int64_t>(round_bit);
        return static_cast<std::uint64_t>(res) & mask;
    } else {
        const std::uint64_t res = (val >> shift) + round_bit;
        return res & mask;
    }
}

inline std::uint64_t narrow_op(std::uint64_t src, unsigned esz, int op, bool& sat) {
    if (op == 0) { // XTN (truncate)
        return src & vec_cmp_mask(true, esz);
    } else if (op == 1) { // SQXTN (signed saturating)
        const std::int64_t val = sign_extend(src, esz + 1);
        switch (esz) {
            case 0: {
                if (val > 127) { sat = true; return 127; }
                if (val < -128) { sat = true; return 0x80u; }
                return static_cast<std::uint8_t>(val);
            }
            case 1: {
                if (val > 32767) { sat = true; return 32767; }
                if (val < -32768) { sat = true; return 0x8000u; }
                return static_cast<std::uint16_t>(val);
            }
            case 2: {
                if (val > 2147483647LL) { sat = true; return 2147483647LL; }
                if (val < -2147483648LL) { sat = true; return 0x80000000u; }
                return static_cast<std::uint32_t>(val);
            }
        }
    } else if (op == 2) { // UQXTN (unsigned saturating)
        switch (esz) {
            case 0: {
                if (src > 0xFFu) { sat = true; return 0xFFu; }
                return static_cast<std::uint8_t>(src);
            }
            case 1: {
                if (src > 0xFFFFu) { sat = true; return 0xFFFFu; }
                return static_cast<std::uint16_t>(src);
            }
            case 2: {
                if (src > 0xFFFFFFFFull) { sat = true; return 0xFFFFFFFFull; }
                return static_cast<std::uint32_t>(src);
            }
        }
    }
    return 0;
}

// Phase 34 Advanced SIMD Floating-Point helpers
inline float fp_max_op(float a, float b) {
    if (std::isnan(a) || std::isnan(b)) {
        return std::bit_cast<float>(0x7FC00000u);
    }
    if (a == b) {
        if (a == 0.0f) {
            return (std::signbit(a) && !std::signbit(b)) ? b : a;
        }
        return a;
    }
    return (a > b) ? a : b;
}

inline double fp_max_op(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) {
        return std::bit_cast<double>(0x7FF8000000000000ull);
    }
    if (a == b) {
        if (a == 0.0) {
            return (std::signbit(a) && !std::signbit(b)) ? b : a;
        }
        return a;
    }
    return (a > b) ? a : b;
}

inline float fp_min_op(float a, float b) {
    if (std::isnan(a) || std::isnan(b)) {
        return std::bit_cast<float>(0x7FC00000u);
    }
    if (a == b) {
        if (a == 0.0f) {
            return (std::signbit(a) && !std::signbit(b)) ? a : b;
        }
        return a;
    }
    return (a < b) ? a : b;
}

inline double fp_min_op(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) {
        return std::bit_cast<double>(0x7FF8000000000000ull);
    }
    if (a == b) {
        if (a == 0.0) {
            return (std::signbit(a) && !std::signbit(b)) ? a : b;
        }
        return a;
    }
    return (a < b) ? a : b;
}

inline float fp_maxnm_op(float a, float b) {
    const bool nan_a = std::isnan(a);
    const bool nan_b = std::isnan(b);
    if (nan_a && nan_b) return std::bit_cast<float>(0x7FC00000u);
    if (nan_a) return b;
    if (nan_b) return a;
    if (a == b) {
        if (a == 0.0f) {
            return (std::signbit(a) && !std::signbit(b)) ? b : a;
        }
        return a;
    }
    return (a > b) ? a : b;
}

inline double fp_maxnm_op(double a, double b) {
    const bool nan_a = std::isnan(a);
    const bool nan_b = std::isnan(b);
    if (nan_a && nan_b) return std::bit_cast<double>(0x7FF8000000000000ull);
    if (nan_a) return b;
    if (nan_b) return a;
    if (a == b) {
        if (a == 0.0) {
            return (std::signbit(a) && !std::signbit(b)) ? b : a;
        }
        return a;
    }
    return (a > b) ? a : b;
}

inline float fp_minnm_op(float a, float b) {
    const bool nan_a = std::isnan(a);
    const bool nan_b = std::isnan(b);
    if (nan_a && nan_b) return std::bit_cast<float>(0x7FC00000u);
    if (nan_a) return b;
    if (nan_b) return a;
    if (a == b) {
        if (a == 0.0f) {
            return (std::signbit(a) && !std::signbit(b)) ? a : b;
        }
        return a;
    }
    return (a < b) ? a : b;
}

inline double fp_minnm_op(double a, double b) {
    const bool nan_a = std::isnan(a);
    const bool nan_b = std::isnan(b);
    if (nan_a && nan_b) return std::bit_cast<double>(0x7FF8000000000000ull);
    if (nan_a) return b;
    if (nan_b) return a;
    if (a == b) {
        if (a == 0.0) {
            return (std::signbit(a) && !std::signbit(b)) ? a : b;
        }
        return a;
    }
    return (a < b) ? a : b;
}

inline float fp_sqrt_op(float val, std::uint32_t& fpsr) {
    if (std::isnan(val)) return std::bit_cast<float>(0x7FC00000u);
    if (val < 0.0f) {
        fpsr |= 1u; // IOC
        return std::bit_cast<float>(0x7FC00000u);
    }
    if (val == 0.0f && std::signbit(val)) return val;
    return std::sqrt(val);
}

inline double fp_sqrt_op(double val, std::uint32_t& fpsr) {
    if (std::isnan(val)) return std::bit_cast<double>(0x7FF8000000000000ull);
    if (val < 0.0) {
        fpsr |= 1u; // IOC
        return std::bit_cast<double>(0x7FF8000000000000ull);
    }
    if (val == 0.0 && std::signbit(val)) return val;
    return std::sqrt(val);
}

inline float fp_div_op(float a, float b, std::uint32_t& fpsr) {
    if (b == 0.0f) {
        if (a == 0.0f || std::isnan(a)) {
            fpsr |= 1u; // IOC
            return std::bit_cast<float>(0x7FC00000u);
        } else {
            fpsr |= (1u << 1); // DZC (Divide by Zero)
        }
    }
    const float r = a / b;
    if (std::isnan(r)) {
        return std::bit_cast<float>(0x7FC00000u);
    }
    return r;
}

inline double fp_div_op(double a, double b, std::uint32_t& fpsr) {
    if (b == 0.0) {
        if (a == 0.0 || std::isnan(a)) {
            fpsr |= 1u; // IOC
            return std::bit_cast<double>(0x7FF8000000000000ull);
        } else {
            fpsr |= (1u << 1); // DZC
        }
    }
    const double r = a / b;
    if (std::isnan(r)) {
        return std::bit_cast<double>(0x7FF8000000000000ull);
    }
    return r;
}

std::string fp_reg(char prefix, std::uint32_t index) {
    return std::string(1, prefix) + std::to_string(index);
}

// Phase 35: System Register helpers.
// Extract the 15-bit sysreg field from bits [20:5] of an MRS/MSR instruction.
//   bit20  = o0 (op0 lsb; op0 = o0+2, so o0=0 -> op0=2, o0=1 -> op0=3)
//   bits[19:17] = op1 (3 bits)
//   bits[15:12] = CRn (4 bits)
//   bits[11:8]  = CRm (4 bits)
//   bits[7:5]   = op2 (3 bits)
// We pack into a 16-bit key: (o0<<14)|(op1<<11)|(CRn<<7)|(CRm<<3)|op2
inline std::uint16_t sysreg_key(std::uint32_t instruction) noexcept {
    const std::uint32_t o0   = (instruction >> 20u) & 0x1u;
    const std::uint32_t op1  = (instruction >> 16u) & 0x7u;
    const std::uint32_t crn  = (instruction >> 12u) & 0xFu;
    const std::uint32_t crm  = (instruction >>  8u) & 0xFu;
    const std::uint32_t op2  = (instruction >>  5u) & 0x7u;
    return static_cast<std::uint16_t>(
        (o0 << 14u) | (op1 << 11u) | (crn << 7u) | (crm << 3u) | op2);
}
// Well-known sysreg keys (o0=1 means op0=3; o0=0 means op0=2).
// op0=3, op1=3, CRn=4, CRm=2, op2=0  -> NZCV       key = (1<<14)|(3<<11)|(4<<7)|(2<<3)|0
constexpr std::uint16_t kSysregNZCV     = (1u<<14)|(3u<<11)|(4u<<7)|(2u<<3)|0u; // 0x5A10
// op0=3, op1=3, CRn=4, CRm=2, op2=1  -> DAIF       key = (1<<14)|(3<<11)|(4<<7)|(2<<3)|1
constexpr std::uint16_t kSysregDAIF     = (1u<<14)|(3u<<11)|(4u<<7)|(2u<<3)|1u; // 0x5A11
// op0=3, op1=0, CRn=4, CRm=2, op2=2  -> CurrentEL  key = (1<<14)|(0<<11)|(4<<7)|(2<<3)|2
constexpr std::uint16_t kSysregCurrentEL= (1u<<14)|(0u<<11)|(4u<<7)|(2u<<3)|2u; // 0x4212
// op0=3, op1=0, CRn=4, CRm=0, op2=0  -> SPSR_EL1   key = (1<<14)|(0<<11)|(4<<7)|(0<<3)|0
constexpr std::uint16_t kSysregSPSR_EL1 = (1u<<14)|(0u<<11)|(4u<<7)|(0u<<3)|0u; // 0x4200
// op0=3, op1=0, CRn=4, CRm=0, op2=1  -> ELR_EL1    key = (1<<14)|(0<<11)|(4<<7)|(0<<3)|1
constexpr std::uint16_t kSysregELR_EL1  = (1u<<14)|(0u<<11)|(4u<<7)|(0u<<3)|1u; // 0x4201
// op0=3, op1=0, CRn=4, CRm=1, op2=0  -> SP_EL0     key = (1<<14)|(0<<11)|(4<<7)|(1<<3)|0
constexpr std::uint16_t kSysregSP_EL0   = (1u<<14)|(0u<<11)|(4u<<7)|(1u<<3)|0u; // 0x4208
// op0=3, op1=0, CRn=4, CRm=2, op2=0  -> SPSel      key = (1<<14)|(0<<11)|(4<<7)|(2<<3)|0
constexpr std::uint16_t kSysregSPSel    = (1u<<14)|(0u<<11)|(4u<<7)|(2u<<3)|0u; // 0x4210
// op0=3, op1=0, CRn=5, CRm=2, op2=0  -> ESR_EL1    key = (1<<14)|(0<<11)|(5<<7)|(2<<3)|0
constexpr std::uint16_t kSysregESR_EL1  = (1u<<14)|(0u<<11)|(5u<<7)|(2u<<3)|0u; // 0x4290
// op0=3, op1=0, CRn=6, CRm=0, op2=0  -> FAR_EL1    key = (1<<14)|(0<<11)|(6<<7)|(0<<3)|0
constexpr std::uint16_t kSysregFAR_EL1  = (1u<<14)|(0u<<11)|(6u<<7)|(0u<<3)|0u; // 0x4300
// op0=3, op1=0, CRn=12, CRm=0, op2=0 -> VBAR_EL1   key = (1<<14)|(0<<11)|(12<<7)|(0<<3)|0
constexpr std::uint16_t kSysregVBAR_EL1 = (1u<<14)|(0u<<11)|(12u<<7)|(0u<<3)|0u; // 0x4600
// op0=3, op1=3, CRn=4, CRm=4, op2=0  -> FPCR       key = (1<<14)|(3<<11)|(4<<7)|(4<<3)|0
constexpr std::uint16_t kSysregFPCR     = (1u<<14)|(3u<<11)|(4u<<7)|(4u<<3)|0u; // 0x5A20
// op0=3, op1=3, CRn=4, CRm=4, op2=1  -> FPSR       key = (1<<14)|(3<<11)|(4<<7)|(4<<3)|1
constexpr std::uint16_t kSysregFPSR     = (1u<<14)|(3u<<11)|(4u<<7)|(4u<<3)|1u; // 0x5A21
// Phase 37: MMU system registers
// op0=3, op1=0, CRn=1, CRm=0, op2=0  -> SCTLR_EL1   key = (1<<14)|(0<<11)|(1<<7)|(0<<3)|0
constexpr std::uint16_t kSysregSCTLR_EL1 = (1u<<14)|(0u<<11)|(1u<<7)|(0u<<3)|0u; // 0x4080
// op0=3, op1=0, CRn=2, CRm=0, op2=2  -> TCR_EL1     key = (1<<14)|(0<<11)|(2<<7)|(0<<3)|2
constexpr std::uint16_t kSysregTCR_EL1   = (1u<<14)|(0u<<11)|(2u<<7)|(0u<<3)|2u; // 0x4102
// op0=3, op1=0, CRn=2, CRm=0, op2=0  -> TTBR0_EL1   key = (1<<14)|(0<<11)|(2<<7)|(0<<3)|0
constexpr std::uint16_t kSysregTTBR0_EL1 = (1u<<14)|(0u<<11)|(2u<<7)|(0u<<3)|0u; // 0x4100
// op0=3, op1=0, CRn=2, CRm=0, op2=1  -> TTBR1_EL1   key = (1<<14)|(0<<11)|(2<<7)|(0<<3)|1
constexpr std::uint16_t kSysregTTBR1_EL1 = (1u<<14)|(0u<<11)|(2u<<7)|(0u<<3)|1u; // 0x4101
// op0=3, op1=0, CRn=10, CRm=2, op2=0 -> MAIR_EL1    key = (1<<14)|(0<<11)|(10<<7)|(2<<3)|0
constexpr std::uint16_t kSysregMAIR_EL1  = (1u<<14)|(0u<<11)|(10u<<7)|(2u<<3)|0u; // 0x4510
// op0=3, op1=0, CRn=0, CRm=7, op2=0  -> ID_AA64MMFR0_EL1 key = (1<<14)|(0<<11)|(0<<7)|(7<<3)|0
constexpr std::uint16_t kSysregID_AA64MMFR0 = (1u<<14)|(0u<<11)|(0u<<7)|(7u<<3)|0u; // 0x4038
// op0=3, op1=0, CRn=7, CRm=4, op2=0  -> PAR_EL1     key = (1<<14)|(0<<11)|(7<<7)|(4<<3)|0
constexpr std::uint16_t kSysregPAR_EL1   = (1u<<14)|(0u<<11)|(7u<<7)|(4u<<3)|0u; // 0x43A0
// Phase 38: DCZID_EL0 (op0=3, op1=3, CRn=0, CRm=0, op2=7)
constexpr std::uint16_t kSysregDCZID_EL0 = (1u<<14)|(3u<<11)|(0u<<7)|(0u<<3)|7u; // 0x5807

// Phase 40: EL2 Virtualization system registers
constexpr std::uint16_t kSysregSP_EL1       = (1u<<14)|(4u<<11)|(4u<<7)|(1u<<3)|0u; // 0x6208
constexpr std::uint16_t kSysregSP_EL2       = (1u<<14)|(6u<<11)|(4u<<7)|(1u<<3)|0u; // 0x7208
constexpr std::uint16_t kSysregSPSR_EL2     = (1u<<14)|(4u<<11)|(4u<<7)|(0u<<3)|0u; // 0x6200
constexpr std::uint16_t kSysregELR_EL2      = (1u<<14)|(4u<<11)|(4u<<7)|(0u<<3)|1u; // 0x6201
constexpr std::uint16_t kSysregESR_EL2      = (1u<<14)|(4u<<11)|(5u<<7)|(2u<<3)|0u; // 0x6290
constexpr std::uint16_t kSysregFAR_EL2      = (1u<<14)|(4u<<11)|(6u<<7)|(0u<<3)|0u; // 0x6300
constexpr std::uint16_t kSysregHPFAR_EL2    = (1u<<14)|(4u<<11)|(6u<<7)|(0u<<3)|4u; // 0x6304
constexpr std::uint16_t kSysregVBAR_EL2     = (1u<<14)|(4u<<11)|(12u<<7)|(0u<<3)|0u; // 0x6600
constexpr std::uint16_t kSysregHCR_EL2      = (1u<<14)|(4u<<11)|(1u<<7)|(1u<<3)|0u; // 0x6088
constexpr std::uint16_t kSysregVTTBR_EL2    = (1u<<14)|(4u<<11)|(2u<<7)|(1u<<3)|0u; // 0x6108
constexpr std::uint16_t kSysregVTCR_EL2     = (1u<<14)|(4u<<11)|(2u<<7)|(1u<<3)|2u; // 0x610A
constexpr std::uint16_t kSysregCNTVOFF_EL2  = (1u<<14)|(4u<<11)|(14u<<7)|(0u<<3)|3u; // 0x6703
constexpr std::uint16_t kSysregCNTHCTL_EL2  = (1u<<14)|(4u<<11)|(14u<<7)|(1u<<3)|0u; // 0x6708
constexpr std::uint16_t kSysregCNTHP_TVAL   = (1u<<14)|(4u<<11)|(14u<<7)|(2u<<3)|0u; // 0x6710
constexpr std::uint16_t kSysregCNTHP_CTL    = (1u<<14)|(4u<<11)|(14u<<7)|(2u<<3)|1u; // 0x6711
constexpr std::uint16_t kSysregCNTHP_CVAL   = (1u<<14)|(4u<<11)|(14u<<7)|(2u<<3)|2u; // 0x6712

// Phase 39: Timer system registers
constexpr std::uint16_t kSysregCNTFRQ_EL0  = (1u<<14)|(3u<<11)|(14u<<7)|(0u<<3)|0u; // 0x5700
constexpr std::uint16_t kSysregCNTPCT_EL0  = (1u<<14)|(3u<<11)|(14u<<7)|(0u<<3)|1u; // 0x5701
constexpr std::uint16_t kSysregCNTVCT_EL0  = (1u<<14)|(3u<<11)|(14u<<7)|(0u<<3)|2u; // 0x5702
constexpr std::uint16_t kSysregCNTKCTL_EL1 = (1u<<14)|(0u<<11)|(14u<<7)|(1u<<3)|0u; // 0x4708
constexpr std::uint16_t kSysregCNTP_TVAL   = (1u<<14)|(3u<<11)|(14u<<7)|(2u<<3)|0u; // 0x5710
constexpr std::uint16_t kSysregCNTP_CTL    = (1u<<14)|(3u<<11)|(14u<<7)|(2u<<3)|1u; // 0x5711
constexpr std::uint16_t kSysregCNTP_CVAL   = (1u<<14)|(3u<<11)|(14u<<7)|(2u<<3)|2u; // 0x5712
constexpr std::uint16_t kSysregCNTV_TVAL   = (1u<<14)|(3u<<11)|(14u<<7)|(3u<<3)|0u; // 0x5718
constexpr std::uint16_t kSysregCNTV_CTL    = (1u<<14)|(3u<<11)|(14u<<7)|(3u<<3)|1u; // 0x5719
constexpr std::uint16_t kSysregCNTV_CVAL   = (1u<<14)|(3u<<11)|(14u<<7)|(3u<<3)|2u; // 0x571A

// Returns the canonical name for a sysreg key (for disassembly).
const char* sysreg_name(std::uint16_t key) noexcept {
    switch (key) {
    case kSysregNZCV:     return "NZCV";
    case kSysregDAIF:     return "DAIF";
    case kSysregCurrentEL:return "CurrentEL";
    case kSysregSPSR_EL1: return "SPSR_EL1";
    case kSysregELR_EL1:  return "ELR_EL1";
    case kSysregSP_EL0:   return "SP_EL0";
    case kSysregSPSel:    return "SPSel";
    case kSysregESR_EL1:  return "ESR_EL1";
    case kSysregFAR_EL1:  return "FAR_EL1";
    case kSysregVBAR_EL1: return "VBAR_EL1";
    case kSysregFPCR:     return "FPCR";
    case kSysregFPSR:     return "FPSR";
    case kSysregSCTLR_EL1:return "SCTLR_EL1";
    case kSysregTCR_EL1:  return "TCR_EL1";
    case kSysregTTBR0_EL1:return "TTBR0_EL1";
    case kSysregTTBR1_EL1:return "TTBR1_EL1";
    case kSysregMAIR_EL1: return "MAIR_EL1";
    case kSysregID_AA64MMFR0: return "ID_AA64MMFR0_EL1";
    case kSysregPAR_EL1:  return "PAR_EL1";
    case kSysregDCZID_EL0: return "DCZID_EL0";
    case kSysregCNTFRQ_EL0: return "CNTFRQ_EL0";
    case kSysregCNTPCT_EL0: return "CNTPCT_EL0";
    case kSysregCNTVCT_EL0: return "CNTVCT_EL0";
    case kSysregCNTKCTL_EL1:return "CNTKCTL_EL1";
    case kSysregCNTP_TVAL:  return "CNTP_TVAL_EL0";
    case kSysregCNTP_CTL:   return "CNTP_CTL_EL0";
    case kSysregCNTP_CVAL:  return "CNTP_CVAL_EL0";
    case kSysregCNTV_TVAL:  return "CNTV_TVAL_EL0";
    case kSysregCNTV_CTL:   return "CNTV_CTL_EL0";
    case kSysregCNTV_CVAL:  return "CNTV_CVAL_EL0";
    case kSysregSP_EL1:     return "SP_EL1";
    case kSysregSP_EL2:     return "SP_EL2";
    case kSysregSPSR_EL2:   return "SPSR_EL2";
    case kSysregELR_EL2:    return "ELR_EL2";
    case kSysregESR_EL2:    return "ESR_EL2";
    case kSysregFAR_EL2:    return "FAR_EL2";
    case kSysregHPFAR_EL2:  return "HPFAR_EL2";
    case kSysregVBAR_EL2:   return "VBAR_EL2";
    case kSysregHCR_EL2:    return "HCR_EL2";
    case kSysregVTTBR_EL2:  return "VTTBR_EL2";
    case kSysregVTCR_EL2:   return "VTCR_EL2";
    case kSysregCNTVOFF_EL2:return "CNTVOFF_EL2";
    case kSysregCNTHCTL_EL2:return "CNTHCTL_EL2";
    case kSysregCNTHP_TVAL: return "CNTHP_TVAL_EL2";
    case kSysregCNTHP_CTL:  return "CNTHP_CTL_EL2";
    case kSysregCNTHP_CVAL: return "CNTHP_CVAL_EL2";
    default:              return nullptr;
    }
}
// Detects the system-register (MRS/MSR) group:
// bits[31:22] = 1101010100 and bit20 = 1 (op0=3 sysreg group).
// Returns true if the instruction is in the Phase 35 system-register group.
inline bool is_sysreg_instr(std::uint32_t instr) noexcept {
    return ((instr & 0xFFC00000u) == 0xD5000000u)
        && ((instr & 0x00100000u) != 0u);
}
// Build SPSR_EL1 value from PSTATE.
// SPSR format (AArch64):
//   [31:28] = NZCV
//   [20]    = IL (illegal execution state)
//   [9:6]   = DAIF
//   [4:0]   = M (Mode): bit4=0 (AArch64), bits[3:2]=EL, bit0=SPSel
inline std::uint32_t encode_spsr(std::uint8_t nzcv, std::uint8_t daif,
                                  std::uint64_t el, bool sp_sel,
                                  bool il = false) noexcept {
    const std::uint32_t m = (el == 0)
        ? 0u
        : static_cast<std::uint32_t>(((el & 0x3u) << 2u) | (sp_sel ? 0x1u : 0x0u));
    return (static_cast<std::uint32_t>(nzcv & 0xFu) << 28u)
         | (il ? (1u << 20u) : 0u)
         | (static_cast<std::uint32_t>(daif & 0xFu) << 6u)
         | m;
}

bool disassemble_fp_simd(std::uint32_t instruction, std::string& text) {
    // 1. SIMD/FP Load/Store
    if ((instruction & 0x3E000000u) == 0x3C000000u) {
        const std::uint32_t size = (instruction >> 30u) & 0x3u;
        const std::uint32_t opc = (instruction >> 22u) & 0x3u;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rt = instruction & 0x1Fu;
        char prefix = ' ';
        std::size_t access_bytes = 0;
        bool is_load = false;
        if (opc == 0b00) {
            access_bytes = std::size_t{1} << size;
            is_load = false;
            prefix = "BHSD"[size];
        } else if (opc == 0b01) {
            access_bytes = std::size_t{1} << size;
            is_load = true;
            prefix = "BHSD"[size];
        } else if (opc == 0b10 && size == 0) {
            access_bytes = 16;
            is_load = false;
            prefix = 'Q';
        } else if (opc == 0b11 && size == 0) {
            access_bytes = 16;
            is_load = true;
            prefix = 'Q';
        } else {
            return false;
        }

        std::ostringstream stream;
        const std::string mnemonic = is_load ? "LDR" : "STR";
        // Unsigned scaled immediate
        if ((instruction & 0x3F000000u) == 0x3D000000u) {
            const std::uint64_t imm12 = (instruction >> 10u) & 0xFFFu;
            const std::uint64_t offset = imm12 * access_bytes;
            stream << mnemonic << ' ' << fp_reg(prefix, rt) << ", [" << stack_register_name(rn);
            if (offset != 0) stream << ", #" << offset;
            stream << ']';
            text = stream.str();
            return true;
        }
        // Register offset
        if ((instruction & 0x3F200C00u) == 0x3C200800u) {
            const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
            const std::uint32_t option = (instruction >> 13u) & 0x7u;
            const bool scale = (instruction & (1u << 12u)) != 0;
            stream << mnemonic << ' ' << fp_reg(prefix, rt) << ", [" << stack_register_name(rn)
                   << ", " << transfer_register_name(rm, (option & 1u) == 0);
            const char* ext_names[] = {"", "", "UXTW", "LSL", "", "", "SXTW", "SXTX"};
            if (option < 8 && ext_names[option][0] != '\0') {
                if (option != 3 || scale) {
                    stream << ", " << ext_names[option];
                    if (scale) {
                        stream << " #" << std::countr_zero(access_bytes);
                    }
                }
            }
            stream << ']';
            text = stream.str();
            return true;
        }
        // Unscaled / Pre / Post
        if ((instruction & 0x3F200000u) == 0x3C000000u) {
            const std::uint32_t mode = (instruction >> 10u) & 0x3u;
            std::int64_t simm9 = (instruction >> 12u) & 0x1FFu;
            if (simm9 & 0x100) simm9 -= 0x200;
            if (mode == 0b00) { // unscaled
                stream << (is_load ? "LDUR " : "STUR ") << fp_reg(prefix, rt) << ", [" << stack_register_name(rn);
                if (simm9 != 0) stream << ", #" << simm9;
                stream << ']';
            } else if (mode == 0b01) { // post
                stream << mnemonic << ' ' << fp_reg(prefix, rt) << ", [" << stack_register_name(rn) << "], #" << simm9;
            } else if (mode == 0b11) { // pre
                stream << mnemonic << ' ' << fp_reg(prefix, rt) << ", [" << stack_register_name(rn) << ", #" << simm9 << "]!";
            }
            text = stream.str();
            return true;
        }
    }

    // 2. Scalar FP 2-source (FADD, FSUB, FMUL, FDIV)
    if ((instruction & 0xFF200C00u) == 0x1E200800u) {
        const std::uint32_t ftype = (instruction >> 22u) & 0x3u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t opcode = (instruction >> 12u) & 0xFu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        if (ftype > 1) return false;
        char p = ftype == 0 ? 'S' : 'D';
        const char* mnemonics[] = {"FMUL", "FDIV", "FADD", "FSUB"};
        if (opcode < 4) {
            std::ostringstream s;
            s << mnemonics[opcode] << ' ' << fp_reg(p, rd) << ", " << fp_reg(p, rn) << ", " << fp_reg(p, rm);
            text = s.str();
            return true;
        }
    }

    // 3. Scalar FP 1-source
    if ((instruction & 0xFF207C00u) == 0x1E204000u) {
        const std::uint32_t ftype = (instruction >> 22u) & 0x3u;
        const std::uint32_t opcode = (instruction >> 15u) & 0x3Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        if (opcode == 4 && ftype == 1) {
            text = "FCVT " + fp_reg('S', rd) + ", " + fp_reg('D', rn);
            return true;
        }
        if (opcode == 5 && ftype == 0) {
            text = "FCVT " + fp_reg('D', rd) + ", " + fp_reg('S', rn);
            return true;
        }
        if (ftype > 1) return false;
        char p = ftype == 0 ? 'S' : 'D';
        const char* m = nullptr;
        if (opcode == 0) m = "FMOV";
        else if (opcode == 1) m = "FABS";
        else if (opcode == 2) m = "FNEG";
        else if (opcode == 3) m = "FSQRT";
        else if (opcode == 8) m = "FRINTN";
        else if (opcode == 9) m = "FRINTP";
        else if (opcode == 10) m = "FRINTM";
        else if (opcode == 11) m = "FRINTZ";
        else if (opcode == 12) m = "FRINTA";
        if (m) {
            text = std::string(m) + " " + fp_reg(p, rd) + ", " + fp_reg(p, rn);
            return true;
        }
    }

    // 4. Scalar FP compare
    if ((instruction & 0xFF20FC07u) == 0x1E202000u) {
        const std::uint32_t ftype = (instruction >> 22u) & 0x3u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const bool zero = (instruction & (1u << 3u)) != 0;
        const bool e = (instruction & (1u << 4u)) != 0;
        if (ftype > 1) return false;
        char p = ftype == 0 ? 'S' : 'D';
        std::ostringstream s;
        s << (e ? "FCMPE " : "FCMP ") << fp_reg(p, rn) << ", ";
        if (zero) s << "#0.0";
        else s << fp_reg(p, rm);
        text = s.str();
        return true;
    }

    // 5. Scalar FP immediate
    if ((instruction & 0xFF201FE0u) == 0x1E201000u) {
        const std::uint32_t ftype = (instruction >> 22u) & 0x3u;
        const std::uint8_t imm8 = static_cast<std::uint8_t>((instruction >> 13u) & 0xFFu);
        const std::uint32_t rd = instruction & 0x1Fu;
        if (ftype > 1) return false;
        char p = ftype == 0 ? 'S' : 'D';
        std::ostringstream s;
        s << "FMOV " << fp_reg(p, rd) << ", #" << static_cast<unsigned>(imm8);
        text = s.str();
        return true;
    }

    // 6. FP/Integer Conversion & FMOV GP
    if ((instruction & 0x7F20FC00u) == 0x1E200000u) {
        const bool sf = (instruction & 0x80000000u) != 0;
        const std::uint32_t ftype = (instruction >> 22u) & 0x3u;
        const std::uint32_t rmode = (instruction >> 19u) & 0x3u;
        const std::uint32_t opcode = (instruction >> 16u) & 0x7u;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        if (ftype > 1) return false;
        char p = ftype == 0 ? 'S' : 'D';
        if (rmode == 0b00) {
            if (opcode == 0b010) {
                text = "SCVTF " + fp_reg(p, rd) + ", " + transfer_register_name(rn, !sf);
                return true;
            }
            if (opcode == 0b011) {
                text = "UCVTF " + fp_reg(p, rd) + ", " + transfer_register_name(rn, !sf);
                return true;
            }
            if (opcode == 0b110) {
                text = "FMOV " + transfer_register_name(rd, !sf) + ", " + fp_reg(p, rn);
                return true;
            }
            if (opcode == 0b111) {
                text = "FMOV " + fp_reg(p, rd) + ", " + transfer_register_name(rn, !sf);
                return true;
            }
        } else if (rmode == 0b11) {
            if (opcode == 0b000) {
                text = "FCVTZS " + transfer_register_name(rd, !sf) + ", " + fp_reg(p, rn);
                return true;
            }
            if (opcode == 0b001) {
                text = "FCVTZU " + transfer_register_name(rd, !sf) + ", " + fp_reg(p, rn);
                return true;
            }
        }
    }

    // 7. System registers (Phase 35: extended from FPCR/FPSR to full sysreg set)
    // Also covers SVC and ERET disassembly.
    if (instruction == kEretEncoding) {
        text = "ERET";
        return true;
    }
    if ((instruction & kSvcMask) == kSvcPattern) {
        const std::uint32_t imm16 = (instruction >> 5u) & 0xFFFFu;
        text = "SVC #" + std::to_string(imm16);
        return true;
    }
    constexpr std::uint32_t kHvcMask = 0xFFE0001Fu;
    constexpr std::uint32_t kHvcPattern = 0xD4000002u;
    if ((instruction & kHvcMask) == kHvcPattern) {
        const std::uint32_t imm16 = (instruction >> 5u) & 0xFFFFu;
        text = "HVC #" + std::to_string(imm16);
        return true;
    }
    // MSR immediate (PSTATE fields): DAIFSet/DAIFClr/SPSel.
    // Encoding: 1101 0101 0000 0[op1:3][CRm:4][op2:3] 1111 1111 1111 1111
    // bits[31:12]=0xD500 4, bits[4:0]=0x1F (fixed Rt=11111).
    if ((instruction & kMsrImmMask) == kMsrImmBase) {
        const std::uint32_t op1 = (instruction >> 16u) & 0x7u;
        const std::uint32_t crm = (instruction >>  8u) & 0xFu;
        const std::uint32_t op2 = (instruction >>  5u) & 0x7u;
        if (op1 == 0u && op2 == 5u) {
            text = "MSR SPSel, #" + std::to_string(crm & 1u);
            return true;
        }
        if (op1 == 3u && op2 == 6u) {
            text = "MSR DAIFSet, #" + std::to_string(crm);
            return true;
        }
        if (op1 == 3u && op2 == 7u) {
            text = "MSR DAIFClr, #" + std::to_string(crm);
            return true;
        }
    }
    // Generic MRS/MSR register (op0=3, system registers).
    if (is_sysreg_instr(instruction)) {
        const bool is_read = (instruction & (1u << 21u)) != 0u;
        const std::uint32_t rt = instruction & 0x1Fu;
        const std::uint16_t key = sysreg_key(instruction);
        const char* name = sysreg_name(key);
        if (name) {
            if (is_read) {
                text = "MRS " + transfer_register_name(rt, false) + ", " + name;
            } else {
                text = "MSR " + std::string(name) + ", " + transfer_register_name(rt, false);
            }
            return true;
        }
    }

    // Phase 38: Barriers, Hints, and Cache instructions disassembly
    if ((instruction & 0xFFFF0000u) == 0xD5030000u) {
        const std::uint32_t crn = (instruction >> 12u) & 0xFu;
        const std::uint32_t crm = (instruction >> 8u) & 0xFu;
        const std::uint32_t op2 = (instruction >> 5u) & 0x7u;
        const std::uint32_t rt  = instruction & 0x1Fu;
        if (crn == 2u && rt == 31u) {
            const std::uint32_t imm7 = (crm << 3u) | op2;
            switch (imm7) {
            case 0: text = "NOP"; return true;
            case 1: text = "YIELD"; return true;
            case 2: text = "WFE"; return true;
            case 3: text = "WFI"; return true;
            case 4: text = "SEV"; return true;
            case 5: text = "SEVL"; return true;
            default: text = "HINT #" + std::to_string(imm7); return true;
            }
        }
        if (crn == 3u) {
            if (op2 == 2u && crm == 15u && rt == 31u) {
                text = "CLREX";
                return true;
            }
            if (op2 == 4u && rt == 31u) {
                text = "DSB #" + std::to_string(crm);
                return true;
            }
            if (op2 == 5u && rt == 31u) {
                text = "DMB #" + std::to_string(crm);
                return true;
            }
            if (op2 == 6u && rt == 31u) {
                text = "ISB #" + std::to_string(crm);
                return true;
            }
        }
    }
    if ((instruction & 0xFF800000u) == 0xD5000000u && ((instruction >> 20u) & 0x3u) == 0) {
        const std::uint32_t op1 = (instruction >> 16u) & 0x7u;
        const std::uint32_t crn = (instruction >> 12u) & 0xFu;
        const std::uint32_t crm = (instruction >> 8u) & 0xFu;
        const std::uint32_t op2 = (instruction >> 5u) & 0x7u;
        const std::uint32_t rt  = instruction & 0x1Fu;
        if (crn == 7u) {
            if (op1 == 3u && crm == 4u && op2 == 1u) {
                text = "DC ZVA, " + transfer_register_name(rt, false);
                return true;
            }
            if (op1 == 3u && crm == 10u && op2 == 1u) {
                text = "DC CVAC, " + transfer_register_name(rt, false);
                return true;
            }
            if (op1 == 3u && crm == 11u && op2 == 1u) {
                text = "DC CVAU, " + transfer_register_name(rt, false);
                return true;
            }
            if (op1 == 3u && crm == 14u && op2 == 1u) {
                text = "DC CIVAC, " + transfer_register_name(rt, false);
                return true;
            }
            if (op1 == 0u && crm == 6u && op2 == 1u) {
                text = "DC IVAC, " + transfer_register_name(rt, false);
                return true;
            }
            if (op1 == 3u && crm == 5u && op2 == 1u) {
                text = "IC IVAU, " + transfer_register_name(rt, false);
                return true;
            }
            if (op1 == 0u && crm == 5u && op2 == 0u && rt == 31u) {
                text = "IC IALLU";
                return true;
            }
            if (op1 == 0u && crm == 1u && op2 == 0u && rt == 31u) {
                text = "IC IALLUIS";
                return true;
            }
        }
    }

    // 8. Advanced SIMD Logicals

    if ((instruction & 0xBF20FC00u) == 0x0E201C00u
        || (instruction & 0xBF20FC00u) == 0x2E201C00u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool bit29 = (instruction & 0x20000000u) != 0;
        const std::uint32_t opc = (instruction >> 22u) & 0x3u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        const char* m = nullptr;
        if (!bit29) {
            const char* ops[] = {"AND", "BIC", "ORR", "ORN"};
            m = ops[opc];
        } else if (opc == 0b00) {
            m = "EOR";
        }
        if (m) {
            const std::string arr = q ? ".16B" : ".8B";
            text = std::string(m) + " V" + std::to_string(rd) + arr + ", V" + std::to_string(rn) + arr + ", V" + std::to_string(rm) + arr;
            return true;
        }
    }
    if ((instruction & 0xBF3FFC00u) == 0x2E205800u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        const std::string arr = q ? ".16B" : ".8B";
        text = "NOT V" + std::to_string(rd) + arr + ", V" + std::to_string(rn) + arr;
        return true;
    }

    // 8b. Advanced SIMD 3-Same Floating-Point
    if ((instruction & 0x9F200400u) == 0x0E200400u && (((instruction >> 11u) & 0x18u) == 0x18u)) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool u = (instruction & 0x20000000u) != 0;
        const bool bit23 = (instruction & (1u << 23u)) != 0;
        const std::uint32_t sz = (instruction >> 22u) & 0x1u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t opcode = (instruction >> 11u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        if (sz == 1 && !q) return false;
        const std::string arr = (sz == 0) ? (q ? ".4S" : ".2S") : ".2D";
        const char* m = nullptr;
        if (opcode == 0b11010) {
            if (!u) m = bit23 ? "FSUB" : "FADD";
            else if (!bit23) m = "FADDP";
        } else if (opcode == 0b11011 && u && !bit23) {
            m = "FMUL";
        } else if (opcode == 0b11111 && u && !bit23) {
            m = "FDIV";
        } else if (opcode == 0b11110) {
            if (!u) m = bit23 ? "FMIN" : "FMAX";
            else m = bit23 ? "FMINP" : "FMAXP";
        } else if (opcode == 0b11000) {
            if (!u) m = bit23 ? "FMINNM" : "FMAXNM";
            else m = bit23 ? "FMINNMP" : "FMAXNMP";
        } else if (opcode == 0b11100) {
            if (!u && !bit23) m = "FCMEQ";
            else if (u && !bit23) m = "FCMGE";
            else if (u && bit23) m = "FCMGT";
        }
        if (m) {
            text = std::string(m) + " V" + std::to_string(rd) + arr + ", V" + std::to_string(rn) + arr + ", V" + std::to_string(rm) + arr;
            return true;
        }
    }

    // 9. Advanced SIMD 3-Same Integer
    if ((instruction & 0x9F200400u) == 0x0E200400u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool u = (instruction & 0x20000000u) != 0;
        const std::uint32_t size = (instruction >> 22u) & 0x3u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t opcode = (instruction >> 11u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        const char* const suffixes[2][4] = {
            {".8B", ".4H", ".2S", ".1D"},
            {".16B", ".8H", ".4S", ".2D"}
        };
        const std::string arr = suffixes[q ? 1 : 0][size];
        const char* m = nullptr;
        if (opcode == 0b00001) m = u ? "UQADD" : "SQADD";
        else if (opcode == 0b00101) m = u ? "UQSUB" : "SQSUB";
        else if (opcode == 0b00110) m = u ? "CMHI" : "CMGT";
        else if (opcode == 0b00111) m = u ? "CMHS" : "CMGE";
        else if (opcode == 0b01000) m = u ? "USHL" : "SSHL";
        else if (opcode == 0b01100 && size < 3) m = u ? "UMAX" : "SMAX";
        else if (opcode == 0b01101 && size < 3) m = u ? "UMIN" : "SMIN";
        else if (opcode == 0b10000) m = u ? "SUB" : "ADD";
        else if (opcode == 0b10001) m = u ? "CMEQ" : "CMTST";
        else if (opcode == 0b10011 && !u && size < 3) m = "MUL";
        else if (opcode == 0b10111 && !u) m = "ADDP";

        if (m) {
            text = std::string(m) + " V" + std::to_string(rd) + arr + ", V" + std::to_string(rn) + arr + ", V" + std::to_string(rm) + arr;
            return true;
        }
    }

    // 10. Advanced SIMD Across-Lanes Reductions
    if ((instruction & 0x9F3E0C00u) == 0x0E300800u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool u = (instruction & 0x20000000u) != 0;
        const std::uint32_t size = (instruction >> 22u) & 0x3u;
        const std::uint32_t opcode = (instruction >> 12u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        const char* const suffixes[2][4] = {
            {".8B", ".4H", ".2S", ".1D"},
            {".16B", ".8H", ".4S", ".2D"}
        };
        const std::string arr = suffixes[q ? 1 : 0][size];
        const char* m = nullptr;
        char d_char = "BHSD"[size];
        if (opcode == 0b11011 && !u) m = "ADDV";
        else if (opcode == 0b01010) m = u ? "UMAXV" : "SMAXV";
        else if (opcode == 0b11010) m = u ? "UMINV" : "SMINV";
        else if (opcode == 0b00011) {
            m = u ? "UADDLV" : "SADDLV";
            if (size < 3) d_char = "BHSD"[size + 1];
        }
        if (m) {
            text = std::string(m) + " " + d_char + std::to_string(rd) + ", V" + std::to_string(rn) + arr;
            return true;
        }
    }

    // 11. Advanced SIMD 2-Reg Misc (ABS, NEG, Zero comparisons, Narrowing)
    if ((instruction & 0x9F200C00u) == 0x0E200800u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool u = (instruction & 0x20000000u) != 0;
        const std::uint32_t size = (instruction >> 22u) & 0x3u;
        const std::uint32_t op_hi = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t op_lo = (instruction >> 11u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        const char* const suffixes[2][4] = {
            {".8B", ".4H", ".2S", ".1D"},
            {".16B", ".8H", ".4S", ".2D"}
        };
        const std::string arr = suffixes[q ? 1 : 0][size];
        if (op_hi == 0b00000) {
            if ((op_lo & 0x8u) == 0) { // Integer
                const char* m = nullptr;
                if (op_lo == 0b10001) m = u ? "CMGE" : "CMGT";
                else if (op_lo == 0b10011) m = u ? "CMLE" : "CMEQ";
                else if (op_lo == 0b10101 && !u) m = "CMLT";
                else if (op_lo == 0b10111) m = u ? "NEG" : "ABS";
                if (m) {
                    if (op_lo == 0b10111) {
                        text = std::string(m) + " V" + std::to_string(rd) + arr + ", V" + std::to_string(rn) + arr;
                    } else {
                        text = std::string(m) + " V" + std::to_string(rd) + arr + ", V" + std::to_string(rn) + arr + ", #0";
                    }
                    return true;
                }
            } else { // Floating-Point
                const bool bit23 = (size & 2u) != 0;
                const std::uint32_t sz = size & 1u;
                if (bit23 && (sz == 0 || q)) {
                    const std::string fp_arr = (sz == 0) ? (q ? ".4S" : ".2S") : ".2D";
                    const char* m = nullptr;
                    if (op_lo == 0b11001) m = u ? "FCMGE" : "FCMGT";
                    else if (op_lo == 0b11011) m = u ? "FCMLE" : "FCMEQ";
                    else if (op_lo == 0b11101 && !u) m = "FCMLT";
                    else if (op_lo == 0b11111) m = u ? "FNEG" : "FABS";
                    if (m) {
                        if (op_lo == 0b11111) {
                            text = std::string(m) + " V" + std::to_string(rd) + fp_arr + ", V" + std::to_string(rn) + fp_arr;
                        } else {
                            text = std::string(m) + " V" + std::to_string(rd) + fp_arr + ", V" + std::to_string(rn) + fp_arr + ", #0";
                        }
                        return true;
                    }
                }
            }
        } else if (op_hi == 0b00001) {
            if ((op_lo == 0b00101 || op_lo == 0b01001) && size < 3) {
                const char* const narr_dst[2][3] = {
                    {".8B", ".4H", ".2S"},
                    {".16B", ".8H", ".4S"}
                };
                const char* const narr_src[3] = {".8H", ".4S", ".2D"};
                const std::string d_arr = narr_dst[q ? 1 : 0][size];
                const std::string s_arr = narr_src[size];
                const char* m = nullptr;
                if (op_lo == 0b00101 && !u) m = q ? "XTN2" : "XTN";
                else if (op_lo == 0b01001) {
                    if (!u) m = q ? "SQXTN2" : "SQXTN";
                    else m = q ? "UQXTN2" : "UQXTN";
                }
                if (m) {
                    text = std::string(m) + " V" + std::to_string(rd) + d_arr + ", V" + std::to_string(rn) + s_arr;
                    return true;
                }
            } else {
                const bool bit23 = (size & 2u) != 0;
                const std::uint32_t sz = size & 1u;
                if (sz == 0 || q) {
                    const std::string fp_arr = (sz == 0) ? (q ? ".4S" : ".2S") : ".2D";
                    const char* m = nullptr;
                    if (op_lo == 0b11111 && u && bit23) m = "FSQRT";
                    else if (op_lo == 0b11011 && !bit23) m = u ? "UCVTF" : "SCVTF";
                    else if (op_lo == 0b10111 && bit23) m = u ? "FCVTZU" : "FCVTZS";
                    if (m) {
                        text = std::string(m) + " V" + std::to_string(rd) + fp_arr + ", V" + std::to_string(rn) + fp_arr;
                        return true;
                    }
                }
            }
        }
    }

    // 12. Advanced SIMD 3-Different (Widening)
    if ((instruction & 0x9F200C00u) == 0x0E200000u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool u = (instruction & 0x20000000u) != 0;
        const std::uint32_t size = (instruction >> 22u) & 0x3u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t opcode = (instruction >> 11u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        if (size < 3) {
            const char* const wide_arr[3] = {".8H", ".4S", ".2D"};
            const char* const narr_arr[3] = {".8B", ".4H", ".2S"};
            const std::string d_arr = wide_arr[size];
            const std::string m_arr = narr_arr[size];
            const char* m = nullptr;
            if (opcode == 0b00000) m = u ? (q ? "UADDL2" : "UADDL") : (q ? "SADDL2" : "SADDL");
            else if (opcode == 0b00100) m = u ? (q ? "USUBL2" : "USUBL") : (q ? "SSUBL2" : "SSUBL");
            else if (opcode == 0b00010) m = u ? (q ? "UADDW2" : "UADDW") : (q ? "SADDW2" : "SADDW");
            else if (opcode == 0b00110) m = u ? (q ? "USUBW2" : "USUBW") : (q ? "SSUBW2" : "SSUBW");
            if (m) {
                const std::string n_arr = (opcode == 0b00010 || opcode == 0b00110) ? d_arr : m_arr;
                text = std::string(m) + " V" + std::to_string(rd) + d_arr + ", V" + std::to_string(rn) + n_arr + ", V" + std::to_string(rm) + m_arr;
                return true;
            }
        }
    }

    // 13. Advanced SIMD Shift by Immediate
    if ((instruction & 0x9F800400u) == 0x0F000400u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool u = (instruction & 0x20000000u) != 0;
        const std::uint32_t immhb = (instruction >> 16u) & 0x7Fu;
        const std::uint32_t opcode = (instruction >> 11u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        unsigned esz = 0;
        unsigned shift = 0;
        if (immhb & 0x40u) {
            esz = 3;
            shift = (opcode == 0b01010) ? (immhb & 0x3Fu) : (64u - (immhb & 0x3Fu));
        } else if (immhb & 0x20u) {
            esz = 2;
            shift = (opcode == 0b01010) ? (immhb & 0x1Fu) : (32u - (immhb & 0x1Fu));
        } else if (immhb & 0x10u) {
            esz = 1;
            shift = (opcode == 0b01010) ? (immhb & 0x0Fu) : (16u - (immhb & 0x0Fu));
        } else if (immhb & 0x08u) {
            esz = 0;
            shift = (opcode == 0b01010) ? (immhb & 0x07u) : (8u - (immhb & 0x07u));
        } else {
            return false;
        }
        const char* const suffixes[2][4] = {
            {".8B", ".4H", ".2S", ".1D"},
            {".16B", ".8H", ".4S", ".2D"}
        };
        const std::string arr = suffixes[q ? 1 : 0][esz];
        const char* m = nullptr;
        if (opcode == 0b00000) m = u ? "USHR" : "SSHR";
        else if (opcode == 0b00100) m = u ? "URSHR" : "SRSHR";
        else if (opcode == 0b01010 && !u) m = "SHL";
        if (m) {
            text = std::string(m) + " V" + std::to_string(rd) + arr + ", V" + std::to_string(rn) + arr + ", #" + std::to_string(shift);
            return true;
        }
    }

    // 10. Advanced SIMD DUP, INS, UMOV
    if ((instruction & 0xBFE08400u) == 0x0E000400u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool bit29 = (instruction & 0x20000000u) != 0;
        const std::uint32_t imm5 = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t op2 = (instruction >> 11u) & 0xFu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        unsigned esz = 0, idx = 0;
        if (!decode_esz_idx(imm5, esz, idx) || bit29) return false;
        const char* const arrs[2][4] = {
            {".8B", ".4H", ".2S", ".1D"},
            {".16B", ".8H", ".4S", ".2D"}
        };
        char elem = "BHSD"[esz];
        if (op2 == 0b0001) { // DUP
            text = "DUP V" + std::to_string(rd) + arrs[q ? 1 : 0][esz] + ", " + transfer_register_name(rn, esz != 3);
            return true;
        }
        if (op2 == 0b0011 && q) { // INS
            text = "INS V" + std::to_string(rd) + "." + elem + "[" + std::to_string(idx) + "], " + transfer_register_name(rn, esz != 3);
            return true;
        }
        if (op2 == 0b0111) { // UMOV
            text = "UMOV " + transfer_register_name(rd, esz != 3) + ", V" + std::to_string(rn) + "." + elem + "[" + std::to_string(idx) + "]";
            return true;
        }
    }

    return false;
}
std::string decode_disassembly(std::uint32_t instruction) {
    std::string fp_simd_text;
    if (disassemble_fp_simd(instruction, fp_simd_text)) {
        return fp_simd_text;
    }
    std::string exclusive_or_atomic_text;
    if (disassemble_exclusive_or_atomic(instruction, exclusive_or_atomic_text)) {
        return exclusive_or_atomic_text;
    }
    const std::uint32_t wide_opcode = instruction & 0x7F800000u;
    if (wide_opcode == 0x12800000u
        || wide_opcode == 0x72800000u
        || wide_opcode == 0x52800000u) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const auto destination = instruction & 0x1Fu;
        const auto halfword = (instruction >> 21u) & 0x3u;
        const auto immediate = (instruction >> 5u) & 0xFFFFu;
        const char* mnemonic = wide_opcode == 0x12800000u
            ? "MOVN" : wide_opcode == 0x72800000u ? "MOVK" : "MOVZ";
        const std::string destination_name =
            destination == CPU::kRegisterCount
                ? (is64 ? "XZR" : "WZR")
                : (std::string(is64 ? "X" : "W") + std::to_string(destination));
        std::ostringstream stream;
        stream << mnemonic << ' ' << destination_name << ", #" << immediate;
        if (halfword != 0) stream << ", LSL #" << (halfword * 16u);
        return stream.str();
    }
    if ((instruction & kMovzMask) == kMovn64
        || (instruction & kMovzMask) == kMovk64) {
        const auto destination = instruction & 0x1Fu;
        const auto halfword = (instruction >> 21u) & 0x3u;
        const auto immediate = (instruction >> 5u) & 0xFFFFu;
        const bool keep = (instruction & kMovzMask) == kMovk64;
        std::ostringstream stream;
        stream << (keep ? "MOVK " : "MOVN ")
               << register_name(destination, false) << ", #" << immediate;
        if (halfword != 0) {
            stream << ", LSL #" << (halfword * 16u);
        }
        return stream.str();
    }
    if ((instruction & kMovzMask) == kMovz64) {
        const auto destination = instruction & 0x1Fu;
        const auto halfword = (instruction >> 21u) & 0x3u;
        const auto immediate = (instruction >> 5u) & 0xFFFFu;
        std::ostringstream stream;
        stream << "MOVZ " << register_name(destination, false) << ", #"
               << immediate;
        if (halfword != 0) {
            stream << ", LSL #" << (halfword * 16u);
        }
        return stream.str();
    }
    if ((instruction & kLogicalShiftedRegisterMask) == kLogicalShiftedRegister) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const auto opc = (instruction >> 29u) & 0x3u;
        const auto shift_type = (instruction >> 22u) & 0x3u;
        const bool invert = (instruction & 0x00200000u) != 0;
        const auto rm = (instruction >> 16u) & 0x1Fu;
        const auto amount = (instruction >> 10u) & 0x3Fu;
        const auto rn = (instruction >> 5u) & 0x1Fu;
        const auto rd = instruction & 0x1Fu;
        auto reg_name = [is64](std::uint32_t idx) {
            if (idx == 31u) return std::string(is64 ? "XZR" : "WZR");
            return (is64 ? "X" : "W") + std::to_string(idx);
        };
        static constexpr const char* shift_names[] = {"LSL", "LSR", "ASR", "ROR"};
        std::ostringstream stream;
        if (opc == 1u && !invert && rn == 31u && shift_type == 0u && amount == 0u) {
            stream << "MOV " << reg_name(rd) << ", " << reg_name(rm);
            return stream.str();
        }
        if (opc == 1u && invert && rn == 31u && shift_type == 0u && amount == 0u) {
            stream << "MVN " << reg_name(rd) << ", " << reg_name(rm);
            return stream.str();
        }
        if (opc == 3u && !invert && rd == 31u) {
            stream << "TST " << reg_name(rn) << ", " << reg_name(rm);
            if (amount != 0u || shift_type != 0u) {
                stream << ", " << shift_names[shift_type] << " #" << amount;
            }
            return stream.str();
        }
        const char* mnemonic = nullptr;
        if (opc == 0u) mnemonic = invert ? "BIC" : "AND";
        else if (opc == 1u) mnemonic = invert ? "ORN" : "ORR";
        else if (opc == 2u) mnemonic = invert ? "EON" : "EOR";
        else if (opc == 3u) mnemonic = invert ? "BICS" : "ANDS";
        stream << mnemonic << ' ' << reg_name(rd) << ", " << reg_name(rn) << ", " << reg_name(rm);
        if (amount != 0u || shift_type != 0u) {
            stream << ", " << shift_names[shift_type] << " #" << amount;
        }
        return stream.str();
    }
    if ((instruction & kLogicalImmediateMask) == kLogicalImmediate) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const auto opc = (instruction >> 29u) & 0x3u;
        const auto n = (instruction >> 22u) & 0x1u;
        const auto immr = (instruction >> 16u) & 0x3Fu;
        const auto imms = (instruction >> 10u) & 0x3Fu;
        const auto rn = (instruction >> 5u) & 0x1Fu;
        const auto rd = instruction & 0x1Fu;
        auto reg_name = [is64](std::uint32_t idx) {
            if (idx == 31u) return std::string(is64 ? "XZR" : "WZR");
            return (is64 ? "X" : "W") + std::to_string(idx);
        };
        std::uint64_t imm_val = 0;
        const bool valid = decode_logical_immediate(is64 ? 1u : 0u, n, imms, immr, imm_val);
        std::ostringstream stream;
        if (valid && opc == 1u && rn == 31u) {
            stream << "MOV " << reg_name(rd) << ", #0x" << std::hex << imm_val;
            return stream.str();
        }
        if (valid && opc == 3u && rd == 31u) {
            stream << "TST " << reg_name(rn) << ", #0x" << std::hex << imm_val;
            return stream.str();
        }
        const char* mnemonic = nullptr;
        if (opc == 0u) mnemonic = "AND";
        else if (opc == 1u) mnemonic = "ORR";
        else if (opc == 2u) mnemonic = "EOR";
        else if (opc == 3u) mnemonic = "ANDS";
        stream << mnemonic << ' ' << reg_name(rd) << ", " << reg_name(rn) << ", ";
        if (valid) {
            stream << "#0x" << std::hex << imm_val;
        } else {
            stream << "#invalid";
        }
        return stream.str();
    }
    if ((instruction & kAddSubRegisterMask) == kAddSubRegister) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool subtract = (instruction & 0x40000000u) != 0;
        const bool set_flags = (instruction & 0x20000000u) != 0;
        const auto destination = instruction & 0x1Fu;
        const auto source = (instruction >> 5u) & 0x1Fu;
        const auto shifted = (instruction >> 16u) & 0x1Fu;
        const auto amount = (instruction >> 10u) & 0x3Fu;
        std::ostringstream stream;
        stream << (set_flags ? (subtract ? "SUBS " : "ADDS ")
                             : (subtract ? "SUB " : "ADD "))
               << (is64 ? "X" : "W") << destination << ", "
               << (is64 ? "X" : "W") << source << ", "
               << (is64 ? "X" : "W") << shifted;
        if (amount != 0) stream << ", LSL #" << amount;
        return stream.str();
    }
    if ((instruction & kConditionalBranchMask) == kConditionalBranch) {
        static constexpr const char* conditions[] = {
            "EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC",
            "HI", "LS", "GE", "LT", "GT", "LE", "AL", "NV"};
        const auto encoded = (instruction >> 5u) & 0x7FFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded);
        if ((encoded & 0x40000u) != 0) offset -= (std::int64_t{1} << 19u);
        std::ostringstream stream;
        stream << "B." << conditions[instruction & 0xFu] << " " << offset * 4;
        return stream.str();
    }
    if ((instruction & kCompareBranchMask) == kCbz) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool nonzero = (instruction & 0x01000000u) != 0;
        const auto source = instruction & 0x1Fu;
        const auto encoded = (instruction >> 5u) & 0x7FFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded);
        if ((encoded & 0x40000u) != 0) offset -= (std::int64_t{1} << 19u);
        std::ostringstream stream;
        stream << (nonzero ? "CBNZ " : "CBZ ");
        if (source == 31u) {
            stream << (is64 ? "XZR" : "WZR");
        } else {
            stream << (is64 ? "X" : "W") << source;
        }
        stream << ", " << offset * 4;
        return stream.str();
    }
    if ((instruction & kTestBitBranchMask) == kTbz) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool nonzero = (instruction & 0x01000000u) != 0;
        const auto rt = instruction & 0x1Fu;
        const auto bitpos = ((instruction >> 26u) & 0x20u) | ((instruction >> 19u) & 0x1Fu);
        const auto encoded = (instruction >> 5u) & 0x3FFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded);
        if ((encoded & 0x2000u) != 0) offset -= (std::int64_t{1} << 14u);
        std::ostringstream stream;
        stream << (nonzero ? "TBNZ " : "TBZ ");
        if (rt == 31u) {
            stream << (is64 ? "XZR" : "WZR");
        } else {
            stream << (is64 ? "X" : "W") << rt;
        }
        stream << ", #" << bitpos << ", " << offset * 4;
        return stream.str();
    }
    if ((instruction & kLoadLiteralFamilyMask) == kLoadLiteralPattern) {
        const std::uint32_t opc = (instruction >> 30u) & 0x3u;
        const auto target = instruction & 0x1Fu;
        const std::int64_t displacement =
            decode_simm19((instruction >> 5u) & 0x7FFFFu) * 4;
        std::ostringstream stream;
        if (opc == 0u) {
            stream << "LDR " << transfer_register_name(target, true) << ", #" << displacement;
            return stream.str();
        }
        if (opc == 1u) {
            stream << "LDR " << transfer_register_name(target, false) << ", #" << displacement;
            return stream.str();
        }
        if (opc == 2u) {
            stream << "LDRSW " << transfer_register_name(target, false) << ", #" << displacement;
            return stream.str();
        }
        stream << "PRFM #" << target << ", #" << displacement;
        return stream.str();
    }
    const std::uint32_t indexed_opcode = instruction & kLoadStoreIndexedMask;
    const bool indexed_load = indexed_opcode == kLdr64Pre
                              || indexed_opcode == kLdr64Post
                              || indexed_opcode == kLdr32Pre
                              || indexed_opcode == kLdr32Post;
    const bool indexed_store = indexed_opcode == kStr64Pre
                               || indexed_opcode == kStr64Post
                               || indexed_opcode == kStr32Pre
                               || indexed_opcode == kStr32Post;
    if (indexed_load || indexed_store) {
        const bool is32 = indexed_opcode == kLdr32Pre
                          || indexed_opcode == kLdr32Post
                          || indexed_opcode == kStr32Pre
                          || indexed_opcode == kStr32Post;
        const bool pre_index = indexed_opcode == kLdr64Pre
                               || indexed_opcode == kStr64Pre
                               || indexed_opcode == kLdr32Pre
                               || indexed_opcode == kStr32Pre;
        const auto target = instruction & 0x1Fu;
        const auto base = (instruction >> 5u) & 0x1Fu;
        const std::int64_t offset =
            decode_simm9((instruction >> 12u) & 0x1FFu);
        std::ostringstream stream;
        stream << (indexed_load ? "LDR " : "STR ");
        if (is32) {
            stream << (target == CPU::kRegisterCount
                           ? std::string("WZR")
                           : "W" + std::to_string(target));
        } else {
            stream << register_name(target, false);
        }
        if (pre_index) {
            stream << ", [" << register_name(base, true) << ", #"
                   << offset << "]!";
        } else {
            stream << ", [" << register_name(base, true) << "], #"
                   << offset;
        }
        return stream.str();
    }
    const bool is_ldrsb_w_idx = indexed_opcode == kLdrsbWPre || indexed_opcode == kLdrsbWPost;
    const bool is_ldrsb_x_idx = indexed_opcode == kLdrsbXPre || indexed_opcode == kLdrsbXPost;
    const bool is_ldrsh_w_idx = indexed_opcode == kLdrshWPre || indexed_opcode == kLdrshWPost;
    const bool is_ldrsh_x_idx = indexed_opcode == kLdrshXPre || indexed_opcode == kLdrshXPost;
    const bool is_ldrsw_x_idx = indexed_opcode == kLdrswXPre || indexed_opcode == kLdrswXPost;
    if (is_ldrsb_w_idx || is_ldrsb_x_idx || is_ldrsh_w_idx || is_ldrsh_x_idx || is_ldrsw_x_idx) {
        const bool pre_index = indexed_opcode == kLdrsbWPre
                               || indexed_opcode == kLdrsbXPre
                               || indexed_opcode == kLdrshWPre
                               || indexed_opcode == kLdrshXPre
                               || indexed_opcode == kLdrswXPre;
        const auto target = instruction & 0x1Fu;
        const auto base = (instruction >> 5u) & 0x1Fu;
        const std::int64_t offset =
            decode_simm9((instruction >> 12u) & 0x1FFu);
        const char* mnemonic = (is_ldrsb_w_idx || is_ldrsb_x_idx) ? "LDRSB "
                             : (is_ldrsh_w_idx || is_ldrsh_x_idx) ? "LDRSH " : "LDRSW ";
        const bool target_is32 = (is_ldrsb_w_idx || is_ldrsh_w_idx);
        std::ostringstream stream;
        stream << mnemonic << transfer_register_name(target, target_is32);
        if (pre_index) {
            stream << ", [" << register_name(base, true) << ", #" << offset << "]!";
        } else {
            stream << ", [" << register_name(base, true) << "], #" << offset;
        }
        return stream.str();
    }
    const bool is_strb_idx = indexed_opcode == kStrbPre || indexed_opcode == kStrbPost;
    const bool is_ldrb_idx = indexed_opcode == kLdrbPre || indexed_opcode == kLdrbPost;
    const bool is_strh_idx = indexed_opcode == kStrhPre || indexed_opcode == kStrhPost;
    const bool is_ldrh_idx = indexed_opcode == kLdrhPre || indexed_opcode == kLdrhPost;
    if (is_strb_idx || is_ldrb_idx || is_strh_idx || is_ldrh_idx) {
        const bool pre_index = indexed_opcode == kStrbPre || indexed_opcode == kLdrbPre
                               || indexed_opcode == kStrhPre || indexed_opcode == kLdrhPre;
        const auto target = instruction & 0x1Fu;
        const auto base = (instruction >> 5u) & 0x1Fu;
        const std::int64_t offset =
            decode_simm9((instruction >> 12u) & 0x1FFu);
        const char* mnemonic = is_strb_idx ? "STRB "
                             : is_ldrb_idx ? "LDRB "
                             : is_strh_idx ? "STRH " : "LDRH ";
        std::ostringstream stream;
        stream << mnemonic << transfer_register_name(target, true);
        if (pre_index) {
            stream << ", [" << register_name(base, true) << ", #" << offset << "]!";
        } else {
            stream << ", [" << register_name(base, true) << "], #" << offset;
        }
        return stream.str();
    }
    const std::uint32_t unscaled_opcode =
        instruction & kLoadStoreUnscaledMask;
    const bool unscaled_load = unscaled_opcode == kLdur64
                               || unscaled_opcode == kLdur32;
    const bool unscaled_store = unscaled_opcode == kStur64
                                || unscaled_opcode == kStur32;
    if (unscaled_load || unscaled_store) {
        const bool is32 =
            unscaled_opcode == kLdur32 || unscaled_opcode == kStur32;
        const auto target = instruction & 0x1Fu;
        const auto base = (instruction >> 5u) & 0x1Fu;
        const std::int64_t offset =
            decode_simm9((instruction >> 12u) & 0x1FFu);
        std::ostringstream stream;
        stream << (unscaled_load ? "LDUR " : "STUR ");
        if (is32) {
            stream << (target == CPU::kRegisterCount
                           ? std::string("WZR")
                           : "W" + std::to_string(target));
        } else {
            stream << register_name(target, false);
        }
        stream << ", [" << register_name(base, true) << ", #"
               << offset << "]";
        return stream.str();
    }
    const bool is_ldursb_w = unscaled_opcode == kLdursbW;
    const bool is_ldursb_x = unscaled_opcode == kLdursbX;
    const bool is_ldursh_w = unscaled_opcode == kLdurshW;
    const bool is_ldursh_x = unscaled_opcode == kLdurshX;
    const bool is_ldursw_x = unscaled_opcode == kLdurswX;
    if (is_ldursb_w || is_ldursb_x || is_ldursh_w || is_ldursh_x || is_ldursw_x) {
        const auto target = instruction & 0x1Fu;
        const auto base = (instruction >> 5u) & 0x1Fu;
        const std::int64_t offset =
            decode_simm9((instruction >> 12u) & 0x1FFu);
        const char* mnemonic = (is_ldursb_w || is_ldursb_x) ? "LDURSB "
                             : (is_ldursh_w || is_ldursh_x) ? "LDURSH " : "LDURSW ";
        const bool target_is32 = (is_ldursb_w || is_ldursh_w);
        std::ostringstream stream;
        stream << mnemonic << transfer_register_name(target, target_is32)
               << ", [" << register_name(base, true) << ", #" << offset << "]";
        return stream.str();
    }
    if (unscaled_opcode == kPrfmUnscaled) {
        const auto target = instruction & 0x1Fu;
        const auto base = (instruction >> 5u) & 0x1Fu;
        const std::int64_t offset =
            decode_simm9((instruction >> 12u) & 0x1FFu);
        std::ostringstream stream;
        stream << "PRFM #" << target << ", [" << register_name(base, true) << ", #" << offset << "]";
        return stream.str();
    }
    const bool is_sturb = unscaled_opcode == kSturb;
    const bool is_ldurb = unscaled_opcode == kLdurb;
    const bool is_sturh = unscaled_opcode == kSturh;
    const bool is_ldurh = unscaled_opcode == kLdurh;
    if (is_sturb || is_ldurb || is_sturh || is_ldurh) {
        const auto target = instruction & 0x1Fu;
        const auto base = (instruction >> 5u) & 0x1Fu;
        const std::int64_t offset =
            decode_simm9((instruction >> 12u) & 0x1FFu);
        const char* mnemonic = is_sturb ? "STURB "
                             : is_ldurb ? "LDURB "
                             : is_sturh ? "STURH " : "LDURH ";
        std::ostringstream stream;
        stream << mnemonic << transfer_register_name(target, true)
               << ", [" << register_name(base, true) << ", #" << offset << "]";
        return stream.str();
    }
    const std::uint32_t regoff_opcode = instruction & kLoadStoreRegOffMask;
    const bool regoff_load = regoff_opcode == kLdr64RegOff
                             || regoff_opcode == kLdr32RegOff;
    const bool regoff_store = regoff_opcode == kStr64RegOff
                              || regoff_opcode == kStr32RegOff;
    if (regoff_load || regoff_store) {
        const std::uint32_t option = (instruction >> 13u) & 0x7u;
        if ((option & 0x2u) != 0) {
            const bool is32 = regoff_opcode == kLdr32RegOff
                              || regoff_opcode == kStr32RegOff;
            const auto target = instruction & 0x1Fu;
            const auto base = (instruction >> 5u) & 0x1Fu;
            const auto index = (instruction >> 16u) & 0x1Fu;
            const bool scaled = ((instruction >> 12u) & 0x1u) != 0;
            const std::uint32_t shift = scaled ? (is32 ? 2u : 3u) : 0u;
            const bool index32 = (option & 0x1u) == 0;
            std::ostringstream stream;
            stream << (regoff_load ? "LDR " : "STR ")
                   << transfer_register_name(target, is32) << ", ["
                   << register_name(base, true) << ", "
                   << index_register_name(index, index32);
            if (option == 2u) {
                stream << ", UXTW";
                if (shift != 0) {
                    stream << " #" << shift;
                }
            } else if (option == 6u) {
                stream << ", SXTW";
                if (shift != 0) {
                    stream << " #" << shift;
                }
            } else if (option == 7u) {
                stream << ", SXTX";
                if (shift != 0) {
                    stream << " #" << shift;
                }
            } else if (shift != 0) {
                stream << ", LSL #" << shift;
            }
            stream << "]";
            return stream.str();
        }
    }
    const bool is_ldrsb_w_reg = regoff_opcode == kLdrsbWRegOff;
    const bool is_ldrsb_x_reg = regoff_opcode == kLdrsbXRegOff;
    const bool is_ldrsh_w_reg = regoff_opcode == kLdrshWRegOff;
    const bool is_ldrsh_x_reg = regoff_opcode == kLdrshXRegOff;
    const bool is_ldrsw_x_reg = regoff_opcode == kLdrswXRegOff;
    if (is_ldrsb_w_reg || is_ldrsb_x_reg || is_ldrsh_w_reg || is_ldrsh_x_reg || is_ldrsw_x_reg) {
        const std::uint32_t option = (instruction >> 13u) & 0x7u;
        if ((option & 0x2u) != 0) {
            const bool scaled = ((instruction >> 12u) & 0x1u) != 0;
            const bool is_byte = is_ldrsb_w_reg || is_ldrsb_x_reg;
            const bool is_half = is_ldrsh_w_reg || is_ldrsh_x_reg;
            if (is_byte && scaled) {
                return "<unallocated>";
            }
            const std::uint32_t scale = is_byte ? 0u : is_half ? 1u : 2u;
            const std::uint32_t shift = scaled ? scale : 0u;
            const auto target = instruction & 0x1Fu;
            const auto base = (instruction >> 5u) & 0x1Fu;
            const auto index = (instruction >> 16u) & 0x1Fu;
            const bool target_is32 = is_ldrsb_w_reg || is_ldrsh_w_reg;
            const bool index32 = (option & 0x1u) == 0;
            const char* mnemonic = is_byte ? "LDRSB " : is_half ? "LDRSH " : "LDRSW ";
            std::ostringstream stream;
            stream << mnemonic << transfer_register_name(target, target_is32) << ", ["
                   << register_name(base, true) << ", "
                   << index_register_name(index, index32);
            if (option == 2u) {
                stream << ", UXTW";
                if (shift != 0) stream << " #" << shift;
            } else if (option == 6u) {
                stream << ", SXTW";
                if (shift != 0) stream << " #" << shift;
            } else if (option == 7u) {
                stream << ", SXTX";
                if (shift != 0) stream << " #" << shift;
            } else if (shift != 0) {
                stream << ", LSL #" << shift;
            }
            stream << "]";
            return stream.str();
        }
    }
    if (regoff_opcode == kPrfmRegOff) {
        const std::uint32_t option = (instruction >> 13u) & 0x7u;
        if ((option & 0x2u) != 0) {
            const bool scaled = ((instruction >> 12u) & 0x1u) != 0;
            const std::uint32_t shift = scaled ? 3u : 0u;
            const auto target = instruction & 0x1Fu;
            const auto base = (instruction >> 5u) & 0x1Fu;
            const auto index = (instruction >> 16u) & 0x1Fu;
            const bool index32 = (option & 0x1u) == 0;
            std::ostringstream stream;
            stream << "PRFM #" << target << ", [" << register_name(base, true) << ", "
                   << index_register_name(index, index32);
            if (option == 2u) {
                stream << ", UXTW";
                if (shift != 0) stream << " #" << shift;
            } else if (option == 6u) {
                stream << ", SXTW";
                if (shift != 0) stream << " #" << shift;
            } else if (option == 7u) {
                stream << ", SXTX";
                if (shift != 0) stream << " #" << shift;
            } else if (shift != 0) {
                stream << ", LSL #" << shift;
            }
            stream << "]";
            return stream.str();
        }
    }
    const bool is_strb_reg = regoff_opcode == kStrbRegOff;
    const bool is_ldrb_reg = regoff_opcode == kLdrbRegOff;
    const bool is_strh_reg = regoff_opcode == kStrhRegOff;
    const bool is_ldrh_reg = regoff_opcode == kLdrhRegOff;
    if (is_strb_reg || is_ldrb_reg || is_strh_reg || is_ldrh_reg) {
        const std::uint32_t option = (instruction >> 13u) & 0x7u;
        if ((option & 0x2u) != 0) {
            const bool scaled = ((instruction >> 12u) & 0x1u) != 0;
            const bool is_byte = is_strb_reg || is_ldrb_reg;
            if (is_byte && scaled) {
                return "<unallocated>";
            }
            const std::uint32_t shift = (!is_byte && scaled) ? 1u : 0u;
            const auto target = instruction & 0x1Fu;
            const auto base = (instruction >> 5u) & 0x1Fu;
            const auto index = (instruction >> 16u) & 0x1Fu;
            const bool index32 = (option & 0x1u) == 0;
            const char* mnemonic = is_strb_reg ? "STRB "
                                 : is_ldrb_reg ? "LDRB "
                                 : is_strh_reg ? "STRH " : "LDRH ";
            std::ostringstream stream;
            stream << mnemonic << transfer_register_name(target, true) << ", ["
                   << register_name(base, true) << ", "
                   << index_register_name(index, index32);
            if (option == 2u) {
                stream << ", UXTW";
                if (shift != 0) stream << " #" << shift;
            } else if (option == 6u) {
                stream << ", SXTW";
                if (shift != 0) stream << " #" << shift;
            } else if (option == 7u) {
                stream << ", SXTX";
                if (shift != 0) stream << " #" << shift;
            } else if (shift != 0) {
                stream << ", LSL #" << shift;
            }
            stream << "]";
            return stream.str();
        }
    }
    const std::uint32_t pair_opcode = instruction & kLoadStorePairMask;
    const bool pair_32 = pair_opcode == kStp32Off || pair_opcode == kLdp32Off
                         || pair_opcode == kStp32Pre || pair_opcode == kLdp32Pre
                         || pair_opcode == kStp32Post || pair_opcode == kLdp32Post;
    const bool pair_64 = pair_opcode == kStp64Off || pair_opcode == kLdp64Off
                         || pair_opcode == kStp64Pre || pair_opcode == kLdp64Pre
                         || pair_opcode == kStp64Post || pair_opcode == kLdp64Post;
    const bool is_ldpsw = pair_opcode == kLdpswOff || pair_opcode == kLdpswPre
                          || pair_opcode == kLdpswPost;
    if (pair_32 || pair_64 || is_ldpsw) {
        const bool is_load = is_ldpsw || ((instruction & (1u << 22u)) != 0);
        const auto target1 = instruction & 0x1Fu;
        const auto base = (instruction >> 5u) & 0x1Fu;
        const auto target2 = (instruction >> 10u) & 0x1Fu;
        const auto mode = (instruction >> 23u) & 0x7u;
        const std::int64_t offset =
            decode_simm7((instruction >> 15u) & 0x7Fu) * (pair_64 ? 8 : 4);
        std::ostringstream stream;
        const char* mnemonic = is_ldpsw ? "LDPSW " : (is_load ? "LDP " : "STP ");
        const bool reg_is32 = pair_32;
        stream << mnemonic
               << transfer_register_name(target1, reg_is32) << ", "
               << transfer_register_name(target2, reg_is32) << ", [";
        if (mode == 2u) {
            stream << register_name(base, true) << ", #" << offset << "]";
        } else if (mode == 3u) {
            stream << register_name(base, true) << ", #" << offset << "]!";
        } else if (mode == 1u) {
            stream << register_name(base, true) << "], #" << offset;
        }
        return stream.str();
    }
    const std::uint32_t load_store_opcode = instruction & kLoadStoreUnsignedMask;
    const bool is_load =
        load_store_opcode == kLdr64 || load_store_opcode == kLdr32;
    const bool is_store =
        load_store_opcode == kStr64 || load_store_opcode == kStr32;
    if (is_load || is_store) {
        const auto target = instruction & 0x1Fu;
        const auto base = (instruction >> 5u) & 0x1Fu;
        const auto offset = (instruction >> 10u) & 0xFFFu;
        const bool is32 =
            load_store_opcode == kLdr32 || load_store_opcode == kStr32;
        std::ostringstream stream;
        stream << (is_load ? "LDR " : "STR ");
        if (is32) {
            stream << (target == CPU::kRegisterCount
                           ? std::string("WZR")
                           : "W" + std::to_string(target));
        } else {
            stream << register_name(target, false);
        }
        stream << ", [" << register_name(base, true) << ", #"
               << (offset * (is32 ? 4u : 8u)) << "]";
        return stream.str();
    }
    const bool is_ldrsb_w_off = load_store_opcode == kLdrsbWOff;
    const bool is_ldrsb_x_off = load_store_opcode == kLdrsbXOff;
    const bool is_ldrsh_w_off = load_store_opcode == kLdrshWOff;
    const bool is_ldrsh_x_off = load_store_opcode == kLdrshXOff;
    const bool is_ldrsw_x_off = load_store_opcode == kLdrswXOff;
    if (is_ldrsb_w_off || is_ldrsb_x_off || is_ldrsh_w_off || is_ldrsh_x_off || is_ldrsw_x_off) {
        const auto target = instruction & 0x1Fu;
        const auto base = (instruction >> 5u) & 0x1Fu;
        const auto offset = (instruction >> 10u) & 0xFFFu;
        const bool is_byte = is_ldrsb_w_off || is_ldrsb_x_off;
        const bool is_half = is_ldrsh_w_off || is_ldrsh_x_off;
        const std::uint32_t scale = is_byte ? 1u : is_half ? 2u : 4u;
        const bool target_is32 = is_ldrsb_w_off || is_ldrsh_w_off;
        const char* mnemonic = is_byte ? "LDRSB " : is_half ? "LDRSH " : "LDRSW ";
        std::ostringstream stream;
        stream << mnemonic << transfer_register_name(target, target_is32) << ", ["
               << register_name(base, true) << ", #" << (offset * scale) << "]";
        return stream.str();
    }
    if (load_store_opcode == kPrfmImmOff) {
        const auto target = instruction & 0x1Fu;
        const auto base = (instruction >> 5u) & 0x1Fu;
        const auto offset = (instruction >> 10u) & 0xFFFu;
        std::ostringstream stream;
        stream << "PRFM #" << target << ", [" << register_name(base, true) << ", #"
               << (offset * 8u) << "]";
        return stream.str();
    }
    const bool is_strb_off = load_store_opcode == kStrbOff;
    const bool is_ldrb_off = load_store_opcode == kLdrbOff;
    const bool is_strh_off = load_store_opcode == kStrhOff;
    const bool is_ldrh_off = load_store_opcode == kLdrhOff;
    if (is_strb_off || is_ldrb_off || is_strh_off || is_ldrh_off) {
        const auto target = instruction & 0x1Fu;
        const auto base = (instruction >> 5u) & 0x1Fu;
        const auto offset = (instruction >> 10u) & 0xFFFu;
        const bool is_byte = is_strb_off || is_ldrb_off;
        const std::uint32_t scale = is_byte ? 1u : 2u;
        const char* mnemonic = is_strb_off ? "STRB "
                             : is_ldrb_off ? "LDRB "
                             : is_strh_off ? "STRH " : "LDRH ";
        std::ostringstream stream;
        stream << mnemonic << transfer_register_name(target, true) << ", ["
               << register_name(base, true) << ", #" << (offset * scale) << "]";
        return stream.str();
    }
    if ((instruction & kAddImmediateMask) == kAddImmediate64) {
        const auto destination = instruction & 0x1Fu;
        const auto source = (instruction >> 5u) & 0x1Fu;
        const auto shift = (instruction >> 22u) & 0x1u;
        const auto immediate = (instruction >> 10u) & 0xFFFu;
        std::ostringstream stream;
        stream << "ADD " << register_name(destination, true) << ", "
               << register_name(source, true) << ", #" << immediate;
        if (shift != 0) {
            stream << ", LSL #12";
        }
        return stream.str();
    }
    if ((instruction & kAddImmediateMask) == kSubImmediate64) {
        const auto destination = instruction & 0x1Fu;
        const auto source = (instruction >> 5u) & 0x1Fu;
        const auto shift = (instruction >> 22u) & 0x1u;
        const auto immediate = (instruction >> 10u) & 0xFFFu;
        std::ostringstream stream;
        stream << "SUB " << register_name(destination, true) << ", "
               << register_name(source, true) << ", #" << immediate;
        if (shift != 0) {
            stream << ", LSL #12";
        }
        return stream.str();
    }
    if ((instruction & kBranchImmediateMask) == kBranch) {
        const bool link = (instruction & 0x80000000u) != 0;
        const auto encoded_offset = instruction & 0x03FFFFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded_offset);
        if ((encoded_offset & 0x02000000u) != 0) {
            offset -= (std::int64_t{1} << 26u);
        }
        std::ostringstream stream;
        stream << (link ? "BL " : "B ") << (offset * 4);
        return stream.str();
    }
    if ((instruction & kBranchRegisterMask) == kBranchRegister) {
        const auto rn = (instruction >> 5u) & 0x1Fu;
        std::ostringstream stream;
        stream << "BR ";
        if (rn == 31u) stream << "XZR";
        else stream << "X" << rn;
        return stream.str();
    }
    if ((instruction & kBranchRegisterMask) == kBranchLinkRegister) {
        const auto rn = (instruction >> 5u) & 0x1Fu;
        std::ostringstream stream;
        stream << "BLR ";
        if (rn == 31u) stream << "XZR";
        else stream << "X" << rn;
        return stream.str();
    }
    if ((instruction & kBranchRegisterMask) == kReturnRegister) {
        const auto rn = (instruction >> 5u) & 0x1Fu;
        std::ostringstream stream;
        if (rn == 30u) {
            stream << "RET";
        } else if (rn == 31u) {
            stream << "RET XZR";
        } else {
            stream << "RET X" << rn;
        }
        return stream.str();
    }
    static constexpr const char* kConditionNames[] = {
        "EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC",
        "HI", "LS", "GE", "LT", "GT", "LE", "AL", "NV"
    };
    const std::uint32_t cc_opcode = instruction & kCcMask;
    if (cc_opcode == kCcmn32Reg || cc_opcode == kCcmp32Reg
        || cc_opcode == kCcmn64Reg || cc_opcode == kCcmp64Reg) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool is_cmp = (instruction & 0x40000000u) != 0;
        const auto rm = (instruction >> 16u) & 0x1Fu;
        const auto cond = (instruction >> 12u) & 0xFu;
        const auto rn = (instruction >> 5u) & 0x1Fu;
        const auto nzcv = instruction & 0xFu;
        std::ostringstream stream;
        stream << (is_cmp ? "CCMP " : "CCMN ")
               << (is64 ? "X" : "W") << rn << ", "
               << (is64 ? "X" : "W") << rm << ", #" << nzcv
               << ", " << kConditionNames[cond];
        return stream.str();
    }
    if (cc_opcode == kCcmn32Imm || cc_opcode == kCcmp32Imm
        || cc_opcode == kCcmn64Imm || cc_opcode == kCcmp64Imm) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool is_cmp = (instruction & 0x40000000u) != 0;
        const auto imm5 = (instruction >> 16u) & 0x1Fu;
        const auto cond = (instruction >> 12u) & 0xFu;
        const auto rn = (instruction >> 5u) & 0x1Fu;
        const auto nzcv = instruction & 0xFu;
        std::ostringstream stream;
        stream << (is_cmp ? "CCMP " : "CCMN ")
               << (is64 ? "X" : "W") << rn << ", #" << imm5
               << ", #" << nzcv << ", " << kConditionNames[cond];
        return stream.str();
    }
    const std::uint32_t csel_opcode = instruction & kCSelMask;
    if (csel_opcode == kCsel32 || csel_opcode == kCsinc32
        || csel_opcode == kCsinv32 || csel_opcode == kCsneg32
        || csel_opcode == kCsel64 || csel_opcode == kCsinc64
        || csel_opcode == kCsinv64 || csel_opcode == kCsneg64) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const auto op = (instruction >> 30u) & 1u;
        const auto rm = (instruction >> 16u) & 0x1Fu;
        const auto cond = (instruction >> 12u) & 0xFu;
        const auto op2 = (instruction >> 10u) & 1u;
        const auto rn = (instruction >> 5u) & 0x1Fu;
        const auto rd = instruction & 0x1Fu;
        const char* mnemonic = (op == 0)
            ? (op2 == 0 ? "CSEL " : "CSINC ")
            : (op2 == 0 ? "CSINV " : "CSNEG ");
        std::ostringstream stream;
        stream << mnemonic << (is64 ? "X" : "W") << rd << ", "
               << (is64 ? "X" : "W") << rn << ", "
               << (is64 ? "X" : "W") << rm << ", "
               << kConditionNames[cond];
        return stream.str();
    }
    const std::uint32_t bitfield_opcode = instruction & kBitfieldMask;
    if (bitfield_opcode == kSbfm32 || bitfield_opcode == kBfm32
        || bitfield_opcode == kUbfm32 || bitfield_opcode == kSbfm64
        || bitfield_opcode == kBfm64 || bitfield_opcode == kUbfm64) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const auto immr = (instruction >> 16u) & 0x3Fu;
        const auto imms = (instruction >> 10u) & 0x3Fu;
        const auto rn = (instruction >> 5u) & 0x1Fu;
        const auto rd = instruction & 0x1Fu;
        const char* mnemonic = (bitfield_opcode == kSbfm32 || bitfield_opcode == kSbfm64) ? "SBFM "
            : (bitfield_opcode == kBfm32 || bitfield_opcode == kBfm64) ? "BFM " : "UBFM ";
        std::ostringstream stream;
        stream << mnemonic << transfer_register_name(rd, !is64) << ", "
               << transfer_register_name(rn, !is64) << ", #" << immr << ", #" << imms;
        return stream.str();
    }
    if ((instruction & kDataProcessing3SourceMask) == kDataProcessing3Source) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t op31 = (instruction >> 21u) & 7u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t o0 = (instruction >> 15u) & 1u;
        const std::uint32_t ra = (instruction >> 10u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        std::ostringstream stream;
        if (!is64 && op31 == 0u) {
            if (o0 == 0u) {
                if (ra == 31u) {
                    stream << "MUL " << transfer_register_name(rd, true) << ", "
                           << transfer_register_name(rn, true) << ", "
                           << transfer_register_name(rm, true);
                } else {
                    stream << "MADD " << transfer_register_name(rd, true) << ", "
                           << transfer_register_name(rn, true) << ", "
                           << transfer_register_name(rm, true) << ", "
                           << transfer_register_name(ra, true);
                }
            } else {
                if (ra == 31u) {
                    stream << "MNEG " << transfer_register_name(rd, true) << ", "
                           << transfer_register_name(rn, true) << ", "
                           << transfer_register_name(rm, true);
                } else {
                    stream << "MSUB " << transfer_register_name(rd, true) << ", "
                           << transfer_register_name(rn, true) << ", "
                           << transfer_register_name(rm, true) << ", "
                           << transfer_register_name(ra, true);
                }
            }
            return stream.str();
        } else if (is64 && op31 == 0u) {
            if (o0 == 0u) {
                if (ra == 31u) {
                    stream << "MUL " << transfer_register_name(rd, false) << ", "
                           << transfer_register_name(rn, false) << ", "
                           << transfer_register_name(rm, false);
                } else {
                    stream << "MADD " << transfer_register_name(rd, false) << ", "
                           << transfer_register_name(rn, false) << ", "
                           << transfer_register_name(rm, false) << ", "
                           << transfer_register_name(ra, false);
                }
            } else {
                if (ra == 31u) {
                    stream << "MNEG " << transfer_register_name(rd, false) << ", "
                           << transfer_register_name(rn, false) << ", "
                           << transfer_register_name(rm, false);
                } else {
                    stream << "MSUB " << transfer_register_name(rd, false) << ", "
                           << transfer_register_name(rn, false) << ", "
                           << transfer_register_name(rm, false) << ", "
                           << transfer_register_name(ra, false);
                }
            }
            return stream.str();
        } else if (is64 && op31 == 1u) {
            if (o0 == 0u) {
                if (ra == 31u) {
                    stream << "SMULL " << transfer_register_name(rd, false) << ", "
                           << transfer_register_name(rn, true) << ", "
                           << transfer_register_name(rm, true);
                } else {
                    stream << "SMADDL " << transfer_register_name(rd, false) << ", "
                           << transfer_register_name(rn, true) << ", "
                           << transfer_register_name(rm, true) << ", "
                           << transfer_register_name(ra, false);
                }
            } else {
                if (ra == 31u) {
                    stream << "SMNEGL " << transfer_register_name(rd, false) << ", "
                           << transfer_register_name(rn, true) << ", "
                           << transfer_register_name(rm, true);
                } else {
                    stream << "SMSUBL " << transfer_register_name(rd, false) << ", "
                           << transfer_register_name(rn, true) << ", "
                           << transfer_register_name(rm, true) << ", "
                           << transfer_register_name(ra, false);
                }
            }
            return stream.str();
        } else if (is64 && op31 == 5u) {
            if (o0 == 0u) {
                if (ra == 31u) {
                    stream << "UMULL " << transfer_register_name(rd, false) << ", "
                           << transfer_register_name(rn, true) << ", "
                           << transfer_register_name(rm, true);
                } else {
                    stream << "UMADDL " << transfer_register_name(rd, false) << ", "
                           << transfer_register_name(rn, true) << ", "
                           << transfer_register_name(rm, true) << ", "
                           << transfer_register_name(ra, false);
                }
            } else {
                if (ra == 31u) {
                    stream << "UMNEGL " << transfer_register_name(rd, false) << ", "
                           << transfer_register_name(rn, true) << ", "
                           << transfer_register_name(rm, true);
                } else {
                    stream << "UMSUBL " << transfer_register_name(rd, false) << ", "
                           << transfer_register_name(rn, true) << ", "
                           << transfer_register_name(rm, true) << ", "
                           << transfer_register_name(ra, false);
                }
            }
            return stream.str();
        } else if (is64 && op31 == 2u && o0 == 0u && ra == 31u) {
            stream << "SMULH " << transfer_register_name(rd, false) << ", "
                   << transfer_register_name(rn, false) << ", "
                   << transfer_register_name(rm, false);
            return stream.str();
        } else if (is64 && op31 == 6u && o0 == 0u && ra == 31u) {
            stream << "UMULH " << transfer_register_name(rd, false) << ", "
                   << transfer_register_name(rn, false) << ", "
                   << transfer_register_name(rm, false);
            return stream.str();
        }
    }
    // Phase 27: Data-processing (2-source): UDIV, SDIV, LSLV, LSRV, ASRV, RORV
    if ((instruction & kDataProcessing2SourceMask) == kDataProcessing2Source) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t opcode = (instruction >> 10u) & 0x3Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        const char* mnemonic = nullptr;
        switch (opcode) {
        case 0x02u: mnemonic = "UDIV"; break;
        case 0x03u: mnemonic = "SDIV"; break;
        case 0x08u: mnemonic = "LSLV"; break;
        case 0x09u: mnemonic = "LSRV"; break;
        case 0x0Au: mnemonic = "ASRV"; break;
        case 0x0Bu: mnemonic = "RORV"; break;
        default: break;
        }
        if (mnemonic != nullptr) {
            std::ostringstream stream;
            stream << mnemonic << ' ' << transfer_register_name(rd, !is64) << ", "
                   << transfer_register_name(rn, !is64) << ", "
                   << transfer_register_name(rm, !is64);
            return stream.str();
        }
    }
    // Phase 27: Data-processing (1-source): RBIT, REV16, REV32, REV, CLZ, CLS
    if ((instruction & kDataProcessing1SourceMask) == kDataProcessing1Source) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t opcode = (instruction >> 10u) & 0x3Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        const char* mnemonic = nullptr;
        switch (opcode) {
        case 0x00u: mnemonic = "RBIT"; break;
        case 0x01u: mnemonic = "REV16"; break;
        case 0x02u: mnemonic = is64 ? "REV32" : "REV"; break;
        case 0x03u: mnemonic = is64 ? "REV" : nullptr; break;
        case 0x04u: mnemonic = "CLZ"; break;
        case 0x05u: mnemonic = "CLS"; break;
        default: break;
        }
        if (mnemonic != nullptr) {
            std::ostringstream stream;
            stream << mnemonic << ' ' << transfer_register_name(rd, !is64) << ", "
                   << transfer_register_name(rn, !is64);
            return stream.str();
        }
    }
    // Phase 27: Extract: EXTR
    if ((instruction & kExtractMask) == kExtract) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t N = (instruction >> 22u) & 1u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t imms = (instruction >> 10u) & 0x3Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        const bool valid = is64 ? (N == 1u) : (N == 0u && (imms & 0x20u) == 0u);
        if (valid) {
            std::ostringstream stream;
            if (rn == rm) {
                stream << "ROR " << transfer_register_name(rd, !is64) << ", "
                       << transfer_register_name(rn, !is64) << ", #" << imms;
            } else {
                stream << "EXTR " << transfer_register_name(rd, !is64) << ", "
                       << transfer_register_name(rn, !is64) << ", "
                       << transfer_register_name(rm, !is64) << ", #" << imms;
            }
            return stream.str();
        }
    }
    return "UNKNOWN " + hex_word(instruction);
}
std::vector<std::string> changed_registers(const CPU& cpu,
                                           const std::uint64_t* before,
                                           std::uint64_t before_sp,
                                           const CPU::VectorRegister* before_v,
                                           std::uint32_t before_fpcr,
                                           std::uint32_t before_fpsr) {
    std::vector<std::string> changed;
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        if (cpu.read_register(i) != before[i]) {
            changed.push_back("X" + std::to_string(i));
        }
    }
    if (cpu.get_stack_pointer() != before_sp) {
        changed.push_back("SP");
    }
    for (std::size_t i = 0; i < CPU::kVectorRegisterCount; ++i) {
        if (cpu.read_vector_register(i) != before_v[i]) {
            changed.push_back("V" + std::to_string(i));
        }
    }
    if (cpu.get_fpcr() != before_fpcr) {
        changed.push_back("FPCR");
    }
    if (cpu.get_fpsr() != before_fpsr) {
        changed.push_back("FPSR");
    }
    return changed;
}
} // namespace
CPU::CPU() {
    reset();
}
void CPU::reset() {
    for (auto& reg : registers_) {
        reg = 0;
    }
    for (auto& v : v_registers_) {
        v = VectorRegister{};
    }
    sp_el0_     = 0;
    sp_el1_     = 0;
    sp_el2_     = 0;
    program_counter_ = 0;
    nzcv_ = 0;
    fpcr_ = 0;
    fpsr_ = 0;
    exclusive_monitor_ = ExclusiveMonitor{};
    // Phase 35: reset exception architecture state to EL1h boot state.
    current_el_ = 1;
    sp_sel_     = true;
    elr_el1_    = 0;
    spsr_el1_   = 0;
    esr_el1_    = 0;
    far_el1_    = 0;
    vbar_el1_   = 0;
    daif_       = 0xFu; // all interrupts masked at reset
    // Phase 40: EL2 reset
    spsr_el2_   = 0;
    elr_el2_    = 0;
    esr_el2_    = 0;
    far_el2_    = 0;
    hpfar_el2_  = 0;
    vbar_el2_   = 0;
    hcr_el2_    = 0;
    cntvoff_el2_= 0;
    cnthctl_el2_= 3;
    cnthp_timer_= TimerState{};
}
void CPU::note_normal_store(std::uint64_t address, std::size_t width) noexcept {
    if (!exclusive_monitor_.valid || width == 0) {
        return;
    }
    const auto end_of = [](std::uint64_t start, std::size_t length) noexcept {
        const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max();
        return static_cast<std::uint64_t>(length) > limit - start
            ? limit
            : start + static_cast<std::uint64_t>(length);
    };
    const std::uint64_t store_end = end_of(address, width);
    const std::uint64_t monitor_end =
        end_of(exclusive_monitor_.address, exclusive_monitor_.size);
    if (address < monitor_end && exclusive_monitor_.address < store_end) {
        exclusive_monitor_ = ExclusiveMonitor{};
    }
}
void CPU::initialize_entry(std::uint64_t entry_address, const Memory& memory) {
    if (entry_address % 4u != 0
        || entry_address > memory.size()
        || memory.size() - static_cast<std::size_t>(entry_address) < 4u) {
        throw std::invalid_argument("Invalid guest entry address");
    }
    reset();
    program_counter_ = entry_address;
}
std::uint64_t CPU::read_register(std::size_t index) const {
    if (index >= kRegisterCount) {
        throw std::out_of_range(register_index_message(index));
    }
    return registers_[index];
}
void CPU::write_register(std::size_t index, std::uint64_t value) {
    if (index >= kRegisterCount) {
        throw std::out_of_range(register_index_message(index));
    }
    registers_[index] = value;
}
CPU::VectorRegister CPU::read_vector_register(std::size_t index) const {
    if (index >= kVectorRegisterCount) {
        throw std::out_of_range("Invalid vector register index: " + std::to_string(index));
    }
    return v_registers_[index];
}
void CPU::write_vector_register(std::size_t index, const VectorRegister& value) {
    if (index >= kVectorRegisterCount) {
        throw std::out_of_range("Invalid vector register index: " + std::to_string(index));
    }
    v_registers_[index] = value;
}
void CPU::write_vector_register(std::size_t index, std::uint64_t low, std::uint64_t high) {
    write_vector_register(index, VectorRegister{low, high});
}
std::uint32_t CPU::read_s_register(std::size_t index) const {
    return static_cast<std::uint32_t>(read_vector_register(index).low & 0xFFFFFFFFu);
}
void CPU::write_s_register(std::size_t index, std::uint32_t value) {
    write_vector_register(index, VectorRegister{value, 0});
}
std::uint64_t CPU::read_d_register(std::size_t index) const {
    return read_vector_register(index).low;
}
void CPU::write_d_register(std::size_t index, std::uint64_t value) {
    write_vector_register(index, VectorRegister{value, 0});
}
std::uint8_t CPU::read_b_register(std::size_t index) const {
    return static_cast<std::uint8_t>(read_vector_register(index).low & 0xFFu);
}
void CPU::write_b_register(std::size_t index, std::uint8_t value) {
    write_vector_register(index, VectorRegister{value, 0});
}
std::uint16_t CPU::read_h_register(std::size_t index) const {
    return static_cast<std::uint16_t>(read_vector_register(index).low & 0xFFFFu);
}
void CPU::write_h_register(std::size_t index, std::uint16_t value) {
    write_vector_register(index, VectorRegister{value, 0});
}
void CPU::raise_synchronous_exception(const SynchronousException& exception) {
    if (exception.target_el == 2 || current_el_ == 2 || (current_el_ == 0 && (hcr_el2_ & (1ULL << 27u)))) {
        raise_el2_exception(exception, current_el_ < 2);
        return;
    }
    spsr_el1_ = encode_spsr(nzcv_, daif_, current_el_, sp_sel_, il_);
    std::uint32_t vector_offset;
    if (current_el_ == 0) {
        vector_offset = 0x400u;                       // lower EL, AArch64
    } else {
        vector_offset = sp_sel_ ? 0x200u : 0x000u;    // EL1h / EL1t
    }
    elr_el1_ = exception.preferred_return;
    if (exception.has_fault_address) {
        far_el1_ = exception.fault_address;
    }
    esr_el1_ = exception.esr();
    daif_ = 0xFu;
    il_ = false;
    current_el_ = 1;
    sp_sel_ = true;
    program_counter_ = vbar_el1_ + vector_offset;
}

void CPU::raise_el2_exception(const SynchronousException& exception, bool from_lower_el) {
    spsr_el2_ = encode_spsr(nzcv_, daif_, current_el_, sp_sel_, il_);
    std::uint32_t vector_offset;
    if (from_lower_el) {
        vector_offset = 0x400u; // lower EL, AArch64
    } else {
        vector_offset = sp_sel_ ? 0x200u : 0x000u; // EL2h / EL2t
    }
    elr_el2_ = exception.preferred_return;
    if (exception.has_fault_address) {
        far_el2_ = exception.fault_address;
    }
    if (exception.has_fault_ipa) {
        hpfar_el2_ = (exception.fault_ipa >> 8u) & 0x0000FFFFFFFFFFF0ULL;
    }
    esr_el2_ = exception.esr();
    daif_ = 0xFu;
    il_ = false;
    current_el_ = 2;
    sp_sel_ = true;
    program_counter_ = vbar_el2_ + vector_offset;
}

void CPU::check_timer_status() noexcept {
    phys_timer_.istatus = phys_timer_.enable && (guest_counter_ >= phys_timer_.cval);
    virt_timer_.istatus = virt_timer_.enable && ((guest_counter_ - cntvoff_el2_) >= virt_timer_.cval);
    cnthp_timer_.istatus = cnthp_timer_.enable && (guest_counter_ >= cnthp_timer_.cval);
}

void CPU::check_pending_interrupts() {
    check_timer_status();
    const bool phys_irq = phys_timer_.enable && phys_timer_.istatus && !phys_timer_.imask;
    const bool virt_irq = virt_timer_.enable && virt_timer_.istatus && !virt_timer_.imask;
    const bool hp_irq   = cnthp_timer_.enable && cnthp_timer_.istatus && !cnthp_timer_.imask;
    const bool any_irq  = phys_irq || virt_irq || hp_irq;
    if (!any_irq) return;

    const bool imo = (hcr_el2_ & (1ULL << 4u)) != 0;
    const bool fmo = (hcr_el2_ & (1ULL << 3u)) != 0;
    if (imo || fmo) {
        const bool is_fiq = fmo && !imo;
        const std::uint8_t mask = is_fiq ? 0x1u : 0x2u;
        if (current_el_ < 2 || !(daif_ & mask)) {
            raise_el2_asynchronous_exception(is_fiq, current_el_ < 2);
            return;
        }
    } else {
        if (current_el_ <= 1 && !(daif_ & 0x2u)) {
            raise_asynchronous_exception(false);
            return;
        }
    }
}

void CPU::raise_asynchronous_exception(bool is_fiq) {
    spsr_el1_ = encode_spsr(nzcv_, daif_, current_el_, sp_sel_, il_);
    std::uint32_t vector_offset;
    if (current_el_ == 0) {
        vector_offset = is_fiq ? 0x500u : 0x480u; // lower EL, AArch64
    } else {
        if (sp_sel_) {
            vector_offset = is_fiq ? 0x300u : 0x280u; // EL1h (SPx)
        } else {
            vector_offset = is_fiq ? 0x100u : 0x080u; // EL1t (SP0)
        }
    }
    elr_el1_ = program_counter_;
    esr_el1_ = 0; // EC = 0 for IRQ/FIQ
    daif_ = 0xFu;
    il_ = false;
    current_el_ = 1;
    sp_sel_ = true;
    program_counter_ = vbar_el1_ + vector_offset;
}

void CPU::raise_el2_asynchronous_exception(bool is_fiq, bool from_lower_el) {
    spsr_el2_ = encode_spsr(nzcv_, daif_, current_el_, sp_sel_, il_);
    std::uint32_t vector_offset;
    if (from_lower_el) {
        vector_offset = is_fiq ? 0x500u : 0x480u;
    } else {
        if (sp_sel_) {
            vector_offset = is_fiq ? 0x300u : 0x280u;
        } else {
            vector_offset = is_fiq ? 0x100u : 0x080u;
        }
    }
    elr_el2_ = program_counter_;
    esr_el2_ = 0;
    daif_ = 0xFu;
    il_ = false;
    current_el_ = 2;
    sp_sel_ = true;
    program_counter_ = vbar_el2_ + vector_offset;
}

void CPU::step(Memory& guest_memory) {
    struct StepCounterGuard {
        std::uint64_t& counter;
        ~StepCounterGuard() {
            ++counter;
        }
    } counter_guard{guest_counter_};

    if (architectural_exceptions_) {
        check_timer_status();
        const bool phys_irq = phys_timer_.enable && phys_timer_.istatus && !phys_timer_.imask;
        const bool virt_irq = virt_timer_.enable && virt_timer_.istatus && !virt_timer_.imask;
        const bool hp_irq   = cnthp_timer_.enable && cnthp_timer_.istatus && !cnthp_timer_.imask;
        const bool any_irq  = phys_irq || virt_irq || hp_irq;
        if (any_irq) {
            const bool imo = (hcr_el2_ & (1ULL << 4u)) != 0;
            const bool fmo = (hcr_el2_ & (1ULL << 3u)) != 0;
            if (imo || fmo) {
                const bool is_fiq = fmo && !imo;
                const std::uint8_t mask = is_fiq ? 0x1u : 0x2u;
                if (current_el_ < 2 || !(daif_ & mask)) {
                    raise_el2_asynchronous_exception(is_fiq, current_el_ < 2);
                    return;
                }
            } else {
                if (current_el_ <= 1 && !(daif_ & 0x2u)) {
                    raise_asynchronous_exception(false);
                    return;
                }
            }
        }
    }
    const std::uint64_t instruction_address = program_counter_;
    const ScopedArchitecturalAddressWrap address_wrap(architectural_exceptions_);
    const bool s1_en = mmu_.is_stage1_enabled();
    const bool s2_en = mmu_.is_stage2_enabled(static_cast<unsigned>(current_el_));

    if (architectural_exceptions_) {
        if ((instruction_address & 0x3u) != 0u) {
            raise_synchronous_exception(make_pc_alignment(instruction_address));
            return;
        }
        if (!s1_en && !s2_en) {
            if (instruction_address >= kPhysicalAddressLimit) {
                raise_synchronous_exception(make_instruction_abort(
                    current_el_ != 0, FaultStatus::AddressSizeLevel0,
                    instruction_address, 4u));
                return;
            }
            const std::uint64_t ram_size =
                static_cast<std::uint64_t>(guest_memory.size());
            if (instruction_address > ram_size
                || ram_size - instruction_address < 4u) {
                raise_synchronous_exception(make_instruction_abort(
                    current_el_ != 0, FaultStatus::SynchronousExternal,
                    instruction_address, 4u));
                return;
            }
        }
    }

    std::uint64_t phys_pc = instruction_address;
    if (architectural_exceptions_ && (s1_en || s2_en)) {
        mmu::TranslationFault fault;
        auto trans = mmu_.translate(guest_memory, instruction_address,
                                    mmu::AccessKind::Execute,
                                    static_cast<unsigned>(current_el_),
                                    &fault);
        if (!trans.has_value()) {
            if (fault.is_stage2) {
                SynchronousException e = make_instruction_abort(
                    false /* from lower EL */,
                    static_cast<FaultStatus>(fault.fsc()),
                    instruction_address, 4u);
                e.has_fault_ipa = true;
                e.fault_ipa = fault.fault_ipa;
                e.target_el = 2;
                raise_el2_exception(e, true);
                return;
            }
            raise_synchronous_exception(make_instruction_abort(
                current_el_ != 0,
                static_cast<FaultStatus>(fault.fsc()),
                instruction_address, 4u));
            return;
        }
        phys_pc = trans.value();
        const std::uint64_t ram_size =
            static_cast<std::uint64_t>(guest_memory.size());
        if (phys_pc > ram_size || ram_size - phys_pc < 4u) {
            raise_synchronous_exception(make_instruction_abort(
                current_el_ != 0, FaultStatus::SynchronousExternal,
                instruction_address, 4u));
            return;
        }
    }
    const std::uint32_t instruction =
        guest_memory.read_u32(pc_as_memory_offset(phys_pc));
    // Phase 36: PSTATE.IL (set by an architecturally illegal ERET) turns the
    // next fetch into an Illegal Execution State exception. Instruction abort
    // and PC-alignment faults have priority, exactly as in the reference.
    if (architectural_exceptions_ && il_) {
        raise_synchronous_exception(
            make_illegal_state(instruction_address));
        return;
    }
    // Phase 36: all data accesses made by the instruction go through this
    // checked view. In legacy mode it is a pure pass-through.
    CheckedGuestMemory memory(guest_memory, *this, architectural_exceptions_,
                              instruction_address);
    std::uint64_t before[kRegisterCount]{};
    for (std::size_t i = 0; i < kRegisterCount; ++i) {
        before[i] = registers_[i];
    }
    const std::uint64_t before_sp = get_stack_pointer();
    CPU::VectorRegister before_v[kVectorRegisterCount]{};
    for (std::size_t i = 0; i < kVectorRegisterCount; ++i) {
        before_v[i] = v_registers_[i];
    }
    const std::uint32_t before_fpcr = fpcr_;
    const std::uint32_t before_fpsr = fpsr_;
    const auto emit_trace = [&] {
        if (trace_sink_) {
            trace_sink_(TraceRecord{instruction_address, instruction,
                                    decode_disassembly(instruction),
                                    program_counter_,
                                    nzcv_,
                                    changed_registers(*this, before, before_sp,
                                                      before_v, before_fpcr, before_fpsr)});
        }
    };
    const std::uint32_t wide_opcode = instruction & 0x7F800000u;
    try {
    if (wide_opcode == 0x12800000u
        || wide_opcode == 0x72800000u
        || wide_opcode == 0x52800000u) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t destination = instruction & 0x1Fu;
        const std::uint32_t halfword = (instruction >> 21u) & 0x3u;
        const std::uint64_t immediate = (instruction >> 5u) & 0xFFFFu;
        if (!is64 && halfword >= 2u) {
            throw std::runtime_error("Unsupported 32-bit wide-move shift");
        }
        const std::uint64_t mask = std::uint64_t{0xFFFF} << (halfword * 16u);
        const std::uint64_t shifted = immediate << (halfword * 16u);
        if (wide_opcode == 0x12800000u) {
            write_width_result(*this, destination, ~shifted, is64, false);
        } else if (wide_opcode == 0x72800000u) {
            if (destination != CPU::kRegisterCount) {
                const std::uint64_t current = read_register(destination)
                    & width_mask(is64);
                write_width_result(*this, destination,
                                   (current & ~mask) | shifted, is64, false);
            }
        } else {
            write_width_result(*this, destination, shifted, is64, false);
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    if ((instruction & kAddSubImmediateMask) == kAddImmediate) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool subtract = (instruction & 0x40000000u) != 0;
        const bool set_flags = (instruction & 0x20000000u) != 0;
        const std::uint32_t destination = instruction & 0x1Fu;
        const std::uint32_t source = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t shift = (instruction >> 22u) & 0x1u;
        const std::uint64_t immediate = (instruction >> 10u) & 0xFFFu;
        if (shift > 1u) {
            throw std::runtime_error("Unsupported ADD (immediate) shift encoding");
        }
        const std::uint64_t shifted_immediate = immediate << (shift * 12u);
        const std::uint64_t left = read_width_operand(
            *this, source, is64, true);
        const std::uint64_t right = shifted_immediate & width_mask(is64);
        const std::uint64_t result = subtract ? left - right : left + right;
        if (set_flags) {
            nzcv_ = arithmetic_flags(left, right, result, subtract, is64);
        }
        // Register 31 is SP for the destination only in the non-flag-setting
        // forms. For ADDS/SUBS (immediate) Rd=31 is XZR: the subtraction is
        // performed for its flags and the result is discarded (CMP/CMN).
        write_width_result(*this, destination, result, is64, !set_flags);
        program_counter_ = checked_add_address(instruction_address, 4u,
                                                "Instruction PC increment");
        emit_trace();
        return;
    }
    if ((instruction & kAddSubRegisterMask) == kAddSubRegister) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool subtract = (instruction & 0x40000000u) != 0;
        const bool set_flags = (instruction & 0x20000000u) != 0;
        const std::uint32_t destination = instruction & 0x1Fu;
        const std::uint32_t source = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t shifted = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t shift_type = (instruction >> 22u) & 0x3u;
        const std::uint32_t amount = (instruction >> 10u) & 0x3Fu;
        if (shift_type == 3u || amount >= (is64 ? 64u : 32u)) {
            throw std::runtime_error("Unsupported ADD/SUB register shift");
        }
        const std::uint64_t right = shift_register_value(
            read_width_operand(*this, shifted, is64, false),
            is64, shift_type, amount);
        const std::uint64_t left = read_width_operand(
            *this, source, is64, false);
        const std::uint64_t result = subtract ? left - right : left + right;
        if (set_flags) {
            nzcv_ = arithmetic_flags(left, right, result, subtract, is64);
        }
        write_width_result(*this, destination, result, is64, false);
        program_counter_ = checked_add_address(instruction_address, 4u,
                                                "Instruction PC increment");
        emit_trace();
        return;
    }
    if ((instruction & kLogicalShiftedRegisterMask) == kLogicalShiftedRegister) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t opc = (instruction >> 29u) & 0x3u;
        const std::uint32_t shift_type = (instruction >> 22u) & 0x3u;
        const bool invert = (instruction & 0x00200000u) != 0;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t amount = (instruction >> 10u) & 0x3Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        if (!is64 && (amount & 0x20u) != 0) {
            throw std::runtime_error("Unsupported 32-bit logical shift amount");
        }
        const std::uint64_t rm_val = read_width_operand(*this, rm, is64, false);
        std::uint64_t shifted = shift_register_value(rm_val, is64, shift_type, amount);
        if (invert) {
            shifted = (~shifted) & width_mask(is64);
        }
        const std::uint64_t rn_val = read_width_operand(*this, rn, is64, false);
        std::uint64_t result = 0;
        if (opc == 0u || opc == 3u) {
            result = rn_val & shifted;
        } else if (opc == 1u) {
            result = rn_val | shifted;
        } else if (opc == 2u) {
            result = rn_val ^ shifted;
        }
        if (opc == 3u) {
            const std::uint64_t sign = is64 ? (1ull << 63u) : (1ull << 31u);
            nzcv_ = ((result & sign) != 0) ? kNegative : 0;
            if ((result & width_mask(is64)) == 0) {
                nzcv_ |= kZero;
            }
        }
        write_width_result(*this, rd, result, is64, false);
        program_counter_ = checked_add_address(instruction_address, 4u,
                                                "Instruction PC increment");
        emit_trace();
        return;
    }
    if ((instruction & kLogicalImmediateMask) == kLogicalImmediate) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t opc = (instruction >> 29u) & 0x3u;
        const std::uint32_t n = (instruction >> 22u) & 0x1u;
        const std::uint32_t immr = (instruction >> 16u) & 0x3Fu;
        const std::uint32_t imms = (instruction >> 10u) & 0x3Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        std::uint64_t imm_val = 0;
        if (!decode_logical_immediate(is64 ? 1u : 0u, n, imms, immr, imm_val)) {
            throw std::runtime_error("Unsupported logical immediate encoding");
        }
        const std::uint64_t rn_val = read_width_operand(*this, rn, is64, false);
        std::uint64_t result = 0;
        if (opc == 0u || opc == 3u) {
            result = rn_val & imm_val;
        } else if (opc == 1u) {
            result = rn_val | imm_val;
        } else if (opc == 2u) {
            result = rn_val ^ imm_val;
        }
        if (opc == 3u) {
            const std::uint64_t sign = is64 ? (1ull << 63u) : (1ull << 31u);
            nzcv_ = ((result & sign) != 0) ? kNegative : 0;
            if ((result & width_mask(is64)) == 0) {
                nzcv_ |= kZero;
            }
        }
        write_width_result(*this, rd, result, is64, false);
        program_counter_ = checked_add_address(instruction_address, 4u,
                                                "Instruction PC increment");
        emit_trace();
        return;
    }
    // Phase 26: Data-processing (3-source): MADD, MSUB, SMADDL, SMSUBL, UMADDL, UMSUBL, SMULH, UMULH
    if ((instruction & kDataProcessing3SourceMask) == kDataProcessing3Source) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t op31 = (instruction >> 21u) & 7u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t o0 = (instruction >> 15u) & 1u;
        const std::uint32_t ra = (instruction >> 10u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        std::uint64_t result = 0;
        if (!is64) {
            // 32-bit: op31 must be 000
            if (op31 != 0u) {
                throw std::runtime_error("Unsupported AArch64 instruction encoding");
            }
            const std::uint64_t rn_val = read_width_operand(*this, rn, false, false);
            const std::uint64_t rm_val = read_width_operand(*this, rm, false, false);
            const std::uint64_t ra_val = read_width_operand(*this, ra, false, false);
            const std::uint64_t prod = (rn_val * rm_val) & 0xFFFFFFFFu;
            result = (o0 == 0u) ? (ra_val + prod) : (ra_val - prod);
            result &= 0xFFFFFFFFu;
            write_width_result(*this, rd, result, false, false);
        } else {
            // 64-bit forms
            if (op31 == 0u) {
                // MADD / MSUB 64-bit
                const std::uint64_t rn_val = read_width_operand(*this, rn, true, false);
                const std::uint64_t rm_val = read_width_operand(*this, rm, true, false);
                const std::uint64_t ra_val = read_width_operand(*this, ra, true, false);
                const std::uint64_t prod = rn_val * rm_val;
                result = (o0 == 0u) ? (ra_val + prod) : (ra_val - prod);
                write_width_result(*this, rd, result, true, false);
            } else if (op31 == 1u) {
                // SMADDL / SMSUBL (signed 32x32 -> 64 + 64)
                const std::int64_t rn_s = static_cast<std::int32_t>(read_width_operand(*this, rn, false, false));
                const std::int64_t rm_s = static_cast<std::int32_t>(read_width_operand(*this, rm, false, false));
                const std::int64_t prod = rn_s * rm_s;
                const std::uint64_t ra_val = read_width_operand(*this, ra, true, false);
                result = (o0 == 0u) ? (ra_val + static_cast<std::uint64_t>(prod))
                                    : (ra_val - static_cast<std::uint64_t>(prod));
                write_width_result(*this, rd, result, true, false);
            } else if (op31 == 5u) {
                // UMADDL / UMSUBL (unsigned 32x32 -> 64 + 64)
                const std::uint64_t rn_u = static_cast<std::uint32_t>(read_width_operand(*this, rn, false, false));
                const std::uint64_t rm_u = static_cast<std::uint32_t>(read_width_operand(*this, rm, false, false));
                const std::uint64_t prod = rn_u * rm_u;
                const std::uint64_t ra_val = read_width_operand(*this, ra, true, false);
                result = (o0 == 0u) ? (ra_val + prod) : (ra_val - prod);
                write_width_result(*this, rd, result, true, false);
            } else if (op31 == 2u) {
                // SMULH (o0 must be 0, ra must be 31)
                if (o0 != 0u || ra != 31u) {
                    throw std::runtime_error("Unsupported AArch64 instruction encoding");
                }
                const std::int64_t rn_s = static_cast<std::int64_t>(read_width_operand(*this, rn, true, false));
                const std::int64_t rm_s = static_cast<std::int64_t>(read_width_operand(*this, rm, true, false));
                result = compute_smulh(rn_s, rm_s);
                write_width_result(*this, rd, result, true, false);
            } else if (op31 == 6u) {
                // UMULH (o0 must be 0, ra must be 31)
                if (o0 != 0u || ra != 31u) {
                    throw std::runtime_error("Unsupported AArch64 instruction encoding");
                }
                const std::uint64_t rn_u = read_width_operand(*this, rn, true, false);
                const std::uint64_t rm_u = read_width_operand(*this, rm, true, false);
                result = compute_umulh(rn_u, rm_u);
                write_width_result(*this, rd, result, true, false);
            } else {
                // op31 == 3, 4, 7 unallocated
                throw std::runtime_error("Unsupported AArch64 instruction encoding");
            }
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    // Phase 27: Data-processing (2-source): UDIV, SDIV, LSLV, LSRV, ASRV, RORV
    if ((instruction & kDataProcessing2SourceMask) == kDataProcessing2Source) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t opcode = (instruction >> 10u) & 0x3Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        std::uint64_t result = 0;
        if (opcode == 0x02u) {
            // UDIV
            if (!is64) {
                const std::uint32_t n = static_cast<std::uint32_t>(read_width_operand(*this, rn, false, false));
                const std::uint32_t m = static_cast<std::uint32_t>(read_width_operand(*this, rm, false, false));
                result = (m == 0u) ? 0u : (n / m);
            } else {
                const std::uint64_t n = read_width_operand(*this, rn, true, false);
                const std::uint64_t m = read_width_operand(*this, rm, true, false);
                result = (m == 0ull) ? 0ull : (n / m);
            }
        } else if (opcode == 0x03u) {
            // SDIV
            if (!is64) {
                const std::int32_t n = static_cast<std::int32_t>(read_width_operand(*this, rn, false, false));
                const std::int32_t m = static_cast<std::int32_t>(read_width_operand(*this, rm, false, false));
                if (m == 0) {
                    result = 0;
                } else if (n == std::numeric_limits<std::int32_t>::min() && m == -1) {
                    result = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::min());
                } else {
                    result = static_cast<std::uint32_t>(n / m);
                }
            } else {
                const std::int64_t n = static_cast<std::int64_t>(read_width_operand(*this, rn, true, false));
                const std::int64_t m = static_cast<std::int64_t>(read_width_operand(*this, rm, true, false));
                if (m == 0) {
                    result = 0;
                } else if (n == std::numeric_limits<std::int64_t>::min() && m == -1) {
                    result = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::min());
                } else {
                    result = static_cast<std::uint64_t>(n / m);
                }
            }
        } else if (opcode >= 0x08u && opcode <= 0x0Bu) {
            // LSLV (0x08), LSRV (0x09), ASRV (0x0A), RORV (0x0B)
            const std::uint32_t shift_type = opcode - 0x08u;
            const std::uint64_t rn_val = read_width_operand(*this, rn, is64, false);
            const std::uint64_t rm_val = read_width_operand(*this, rm, is64, false);
            const std::uint32_t amount = static_cast<std::uint32_t>(rm_val & (is64 ? 0x3Fu : 0x1Fu));
            result = shift_register_value(rn_val, is64, shift_type, amount);
        } else {
            throw std::runtime_error("Unsupported AArch64 instruction encoding");
        }
        write_width_result(*this, rd, result, is64, false);
        program_counter_ = checked_add_address(instruction_address, 4u,
                                                "Instruction PC increment");
        emit_trace();
        return;
    }
    // Phase 27: Data-processing (1-source): RBIT, REV16, REV32, REV, CLZ, CLS
    if ((instruction & kDataProcessing1SourceMask) == kDataProcessing1Source) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t opcode = (instruction >> 10u) & 0x3Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        const std::uint64_t rn_val = read_width_operand(*this, rn, is64, false);
        std::uint64_t result = 0;
        if (opcode == 0x00u) {
            // RBIT
            result = is64 ? reverse_bits64(rn_val)
                          : reverse_bits32(static_cast<std::uint32_t>(rn_val));
        } else if (opcode == 0x01u) {
            // REV16
            result = is64 ? reverse_bytes16_64(rn_val)
                          : reverse_bytes16_32(static_cast<std::uint32_t>(rn_val));
        } else if (opcode == 0x02u) {
            // REV (32-bit) / REV32 (64-bit)
            result = is64 ? reverse_bytes32_in_64(rn_val)
                          : reverse_bytes32(static_cast<std::uint32_t>(rn_val));
        } else if (opcode == 0x03u) {
            // REV (64-bit). 32-bit is unallocated!
            if (!is64) {
                throw std::runtime_error("Unsupported AArch64 instruction encoding");
            }
            result = reverse_bytes64(rn_val);
        } else if (opcode == 0x04u) {
            // CLZ
            result = is64 ? count_leading_zeros64(rn_val)
                          : count_leading_zeros32(static_cast<std::uint32_t>(rn_val));
        } else if (opcode == 0x05u) {
            // CLS
            result = is64 ? count_leading_sign_bits64(rn_val)
                          : count_leading_sign_bits32(static_cast<std::uint32_t>(rn_val));
        } else {
            throw std::runtime_error("Unsupported AArch64 instruction encoding");
        }
        write_width_result(*this, rd, result, is64, false);
        program_counter_ = checked_add_address(instruction_address, 4u,
                                                "Instruction PC increment");
        emit_trace();
        return;
    }
    // Phase 27: Extract: EXTR
    if ((instruction & kExtractMask) == kExtract) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t N = (instruction >> 22u) & 1u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t imms = (instruction >> 10u) & 0x3Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        // Architectural validation:
        // For 32-bit: N must be 0 and imms bit 5 must be 0 (imms < 32).
        // For 64-bit: N must be 1 (imms can be 0..63).
        if (!is64 && (N != 0u || (imms & 0x20u) != 0u)) {
            throw std::runtime_error("Unsupported AArch64 instruction encoding");
        }
        if (is64 && N != 1u) {
            throw std::runtime_error("Unsupported AArch64 instruction encoding");
        }
        const std::uint64_t rn_val = read_width_operand(*this, rn, is64, false);
        const std::uint64_t rm_val = read_width_operand(*this, rm, is64, false);
        const std::uint32_t lsb = imms;
        std::uint64_t result = 0;
        if (!is64) {
            const std::uint64_t concat = (rn_val << 32u) | rm_val;
            result = (concat >> lsb) & 0xFFFFFFFFu;
        } else {
            if (lsb == 0u) {
                result = rm_val;
            } else {
                result = (rm_val >> lsb) | (rn_val << (64u - lsb));
            }
        }
        write_width_result(*this, rd, result, is64, false);
        program_counter_ = checked_add_address(instruction_address, 4u,
                                                "Instruction PC increment");
        emit_trace();
        return;
    }
    if ((instruction & kLoadLiteralFamilyMask) == kLoadLiteralPattern) {
        const std::uint32_t opc = (instruction >> 30u) & 0x3u;
        if (opc == 3u) {
            throw std::runtime_error("Unsupported AArch64 instruction encoding: PRFM literal 0x"
                                     + hex_word(instruction));
        }
        const std::uint32_t target = instruction & 0x1Fu;
        const std::int64_t displacement =
            decode_simm19((instruction >> 5u) & 0x7FFFFu) * 4;
        const std::uint64_t address = add_signed_address(
            instruction_address, displacement, "Literal load address");
        const std::size_t memory_address = pc_as_memory_offset(address);
        if (opc == 0u) {
            const std::uint32_t value = memory.read_u32(memory_address);
            write_zero_register_result(*this, target, static_cast<std::uint64_t>(value));
        } else if (opc == 1u) {
            const std::uint64_t value = memory.read_u64(memory_address);
            write_zero_register_result(*this, target, value);
        } else {
            const std::int32_t sval = static_cast<std::int32_t>(memory.read_u32(memory_address));
            write_zero_register_result(*this, target, static_cast<std::uint64_t>(static_cast<std::int64_t>(sval)));
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    const std::uint32_t indexed_opcode = instruction & kLoadStoreIndexedMask;
    if (indexed_opcode == kLdr64Pre || indexed_opcode == kLdr64Post
        || indexed_opcode == kStr64Pre || indexed_opcode == kStr64Post
        || indexed_opcode == kLdr32Pre || indexed_opcode == kLdr32Post
        || indexed_opcode == kStr32Pre || indexed_opcode == kStr32Post) {
        const bool load = indexed_opcode == kLdr64Pre
                          || indexed_opcode == kLdr64Post
                          || indexed_opcode == kLdr32Pre
                          || indexed_opcode == kLdr32Post;
        const bool is32 = indexed_opcode == kLdr32Pre
                          || indexed_opcode == kLdr32Post
                          || indexed_opcode == kStr32Pre
                          || indexed_opcode == kStr32Post;
        const bool pre_index = indexed_opcode == kLdr64Pre
                               || indexed_opcode == kStr64Pre
                               || indexed_opcode == kLdr32Pre
                               || indexed_opcode == kStr32Pre;
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::int64_t offset =
            decode_simm9((instruction >> 12u) & 0x1FFu);
        const std::uint64_t base_value =
            read_stack_pointer_operand(*this, base);
        // Compute the write-back address before touching memory so an overflow
        // or underflow leaves both registers and memory unchanged. Pre-index
        // accesses that address; post-index accesses the original base and
        // writes the address back afterwards, exactly once.
        const std::uint64_t writeback_address = add_signed_address(
            base_value, offset, "Load/store write-back address");
        const std::uint64_t address = pre_index ? writeback_address : base_value;
        const std::size_t memory_address = pc_as_memory_offset(address);
        if (load) {
            if (is32) {
                const std::uint32_t value = memory.read_u32(memory_address);
                write_zero_register_result(*this, target, value);
            } else {
                const std::uint64_t value = memory.read_u64(memory_address);
                write_zero_register_result(*this, target, value);
            }
        } else if (is32) {
            const std::uint32_t value = static_cast<std::uint32_t>(
                read_zero_register_operand(*this, target));
            memory.write_u32(memory_address, value);
            note_normal_store(memory_address, 4);
        } else {
            const std::uint64_t value =
                read_zero_register_operand(*this, target);
            memory.write_u64(memory_address, value);
            note_normal_store(memory_address, 8);
        }
        if (base == CPU::kRegisterCount) {
            set_stack_pointer(writeback_address);
        } else {
            write_register(base, writeback_address);
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    const bool is_ldrsb_w_idx_exec = (indexed_opcode == kLdrsbWPre || indexed_opcode == kLdrsbWPost);
    const bool is_ldrsb_x_idx_exec = (indexed_opcode == kLdrsbXPre || indexed_opcode == kLdrsbXPost);
    const bool is_ldrsh_w_idx_exec = (indexed_opcode == kLdrshWPre || indexed_opcode == kLdrshWPost);
    const bool is_ldrsh_x_idx_exec = (indexed_opcode == kLdrshXPre || indexed_opcode == kLdrshXPost);
    const bool is_ldrsw_x_idx_exec = (indexed_opcode == kLdrswXPre || indexed_opcode == kLdrswXPost);
    if (is_ldrsb_w_idx_exec || is_ldrsb_x_idx_exec || is_ldrsh_w_idx_exec || is_ldrsh_x_idx_exec || is_ldrsw_x_idx_exec) {
        const bool pre_index = (indexed_opcode == kLdrsbWPre || indexed_opcode == kLdrsbXPre
                                || indexed_opcode == kLdrshWPre || indexed_opcode == kLdrshXPre
                                || indexed_opcode == kLdrswXPre);
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::int64_t offset =
            decode_simm9((instruction >> 12u) & 0x1FFu);
        const std::uint64_t base_value =
            read_stack_pointer_operand(*this, base);
        const std::uint64_t writeback_address = add_signed_address(
            base_value, offset, "Load/store write-back address");
        const std::uint64_t address = pre_index ? writeback_address : base_value;
        const std::size_t memory_address = pc_as_memory_offset(address);

        std::uint64_t value = 0;
        if (is_ldrsb_w_idx_exec) {
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(memory.read_u8(memory_address))));
        } else if (is_ldrsb_x_idx_exec) {
            value = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int8_t>(memory.read_u8(memory_address))));
        } else if (is_ldrsh_w_idx_exec) {
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(memory.read_u16(memory_address))));
        } else if (is_ldrsh_x_idx_exec) {
            value = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(memory.read_u16(memory_address))));
        } else {
            value = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(memory.read_u32(memory_address))));
        }
        write_zero_register_result(*this, target, value);
        if (base == CPU::kRegisterCount) {
            set_stack_pointer(writeback_address);
        } else {
            write_register(base, writeback_address);
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    const bool is_strb_idx_exec = indexed_opcode == kStrbPre || indexed_opcode == kStrbPost;
    const bool is_ldrb_idx_exec = indexed_opcode == kLdrbPre || indexed_opcode == kLdrbPost;
    const bool is_strh_idx_exec = indexed_opcode == kStrhPre || indexed_opcode == kStrhPost;
    const bool is_ldrh_idx_exec = indexed_opcode == kLdrhPre || indexed_opcode == kLdrhPost;
    if (is_strb_idx_exec || is_ldrb_idx_exec || is_strh_idx_exec || is_ldrh_idx_exec) {
        const bool load = is_ldrb_idx_exec || is_ldrh_idx_exec;
        const bool is_byte = is_strb_idx_exec || is_ldrb_idx_exec;
        const bool pre_index = indexed_opcode == kStrbPre || indexed_opcode == kLdrbPre
                               || indexed_opcode == kStrhPre || indexed_opcode == kLdrhPre;
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::int64_t offset =
            decode_simm9((instruction >> 12u) & 0x1FFu);
        const std::uint64_t base_value =
            read_stack_pointer_operand(*this, base);
        const std::uint64_t writeback_address = add_signed_address(
            base_value, offset, "Load/store write-back address");
        const std::uint64_t address = pre_index ? writeback_address : base_value;
        const std::size_t memory_address = pc_as_memory_offset(address);
        if (load) {
            const std::uint64_t value = is_byte ? static_cast<std::uint64_t>(memory.read_u8(memory_address))
                                                : static_cast<std::uint64_t>(memory.read_u16(memory_address));
            write_zero_register_result(*this, target, value);
        } else {
            if (is_byte) {
                const std::uint8_t value = static_cast<std::uint8_t>(read_zero_register_operand(*this, target));
                memory.write_u8(memory_address, value);
                note_normal_store(memory_address, 1);
            } else {
                const std::uint16_t value = static_cast<std::uint16_t>(read_zero_register_operand(*this, target));
                memory.write_u16(memory_address, value);
                note_normal_store(memory_address, 2);
            }
        }
        if (base == CPU::kRegisterCount) {
            set_stack_pointer(writeback_address);
        } else {
            write_register(base, writeback_address);
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    const std::uint32_t unscaled_opcode =
        instruction & kLoadStoreUnscaledMask;
    if (unscaled_opcode == kLdur64 || unscaled_opcode == kStur64
        || unscaled_opcode == kLdur32 || unscaled_opcode == kStur32) {
        const bool load =
            unscaled_opcode == kLdur64 || unscaled_opcode == kLdur32;
        const bool is32 =
            unscaled_opcode == kLdur32 || unscaled_opcode == kStur32;
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::int64_t offset =
            decode_simm9((instruction >> 12u) & 0x1FFu);
        const std::uint64_t base_value =
            read_stack_pointer_operand(*this, base);
        // Unscaled has no write-back: the address is formed from the base and
        // the base register is never modified. Compute the address before the
        // access so overflow/underflow leaves registers and memory unchanged.
        const std::uint64_t address = add_signed_address(
            base_value, offset, "Unscaled load/store address");
        const std::size_t memory_address = pc_as_memory_offset(address);
        if (load) {
            if (is32) {
                const std::uint32_t value = memory.read_u32(memory_address);
                write_zero_register_result(*this, target, value);
            } else {
                const std::uint64_t value = memory.read_u64(memory_address);
                write_zero_register_result(*this, target, value);
            }
        } else if (is32) {
            const std::uint32_t value = static_cast<std::uint32_t>(
                read_zero_register_operand(*this, target));
            memory.write_u32(memory_address, value);
            note_normal_store(memory_address, 4);
        } else {
            const std::uint64_t value =
                read_zero_register_operand(*this, target);
            memory.write_u64(memory_address, value);
            note_normal_store(memory_address, 8);
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    const bool is_ldursb_w_exec = unscaled_opcode == kLdursbW;
    const bool is_ldursb_x_exec = unscaled_opcode == kLdursbX;
    const bool is_ldursh_w_exec = unscaled_opcode == kLdurshW;
    const bool is_ldursh_x_exec = unscaled_opcode == kLdurshX;
    const bool is_ldursw_x_exec = unscaled_opcode == kLdurswX;
    if (is_ldursb_w_exec || is_ldursb_x_exec || is_ldursh_w_exec || is_ldursh_x_exec || is_ldursw_x_exec) {
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::int64_t offset =
            decode_simm9((instruction >> 12u) & 0x1FFu);
        const std::uint64_t base_value =
            read_stack_pointer_operand(*this, base);
        const std::uint64_t address = add_signed_address(
            base_value, offset, "Unscaled load/store address");
        const std::size_t memory_address = pc_as_memory_offset(address);

        std::uint64_t value = 0;
        if (is_ldursb_w_exec) {
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(memory.read_u8(memory_address))));
        } else if (is_ldursb_x_exec) {
            value = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int8_t>(memory.read_u8(memory_address))));
        } else if (is_ldursh_w_exec) {
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(memory.read_u16(memory_address))));
        } else if (is_ldursh_x_exec) {
            value = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(memory.read_u16(memory_address))));
        } else {
            value = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(memory.read_u32(memory_address))));
        }
        write_zero_register_result(*this, target, value);
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    if (unscaled_opcode == kPrfmUnscaled) {
        throw std::runtime_error("Unsupported AArch64 instruction encoding: PRFM unscaled 0x"
                                 + hex_word(instruction));
    }
    const bool is_sturb_exec = unscaled_opcode == kSturb;
    const bool is_ldurb_exec = unscaled_opcode == kLdurb;
    const bool is_sturh_exec = unscaled_opcode == kSturh;
    const bool is_ldurh_exec = unscaled_opcode == kLdurh;
    if (is_sturb_exec || is_ldurb_exec || is_sturh_exec || is_ldurh_exec) {
        const bool load = is_ldurb_exec || is_ldurh_exec;
        const bool is_byte = is_sturb_exec || is_ldurb_exec;
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::int64_t offset =
            decode_simm9((instruction >> 12u) & 0x1FFu);
        const std::uint64_t base_value =
            read_stack_pointer_operand(*this, base);
        const std::uint64_t address = add_signed_address(
            base_value, offset, "Unscaled load/store address");
        const std::size_t memory_address = pc_as_memory_offset(address);
        if (load) {
            const std::uint64_t value = is_byte ? static_cast<std::uint64_t>(memory.read_u8(memory_address))
                                                : static_cast<std::uint64_t>(memory.read_u16(memory_address));
            write_zero_register_result(*this, target, value);
        } else {
            if (is_byte) {
                const std::uint8_t value = static_cast<std::uint8_t>(read_zero_register_operand(*this, target));
                memory.write_u8(memory_address, value);
                note_normal_store(memory_address, 1);
            } else {
                const std::uint16_t value = static_cast<std::uint16_t>(read_zero_register_operand(*this, target));
                memory.write_u16(memory_address, value);
                note_normal_store(memory_address, 2);
            }
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    const std::uint32_t regoff_opcode = instruction & kLoadStoreRegOffMask;
    if (regoff_opcode == kLdr64RegOff || regoff_opcode == kStr64RegOff
        || regoff_opcode == kLdr32RegOff || regoff_opcode == kStr32RegOff) {
        const std::uint32_t option = (instruction >> 13u) & 0x7u;
        // Arm ARM: if option<1> == '0' the encoding is unallocated.
        if ((option & 0x2u) == 0) {
            throw std::runtime_error("Unsupported AArch64 instruction encoding: 0x"
                                     + std::to_string(instruction));
        }
        const bool load =
            regoff_opcode == kLdr64RegOff || regoff_opcode == kLdr32RegOff;
        const bool is32 =
            regoff_opcode == kLdr32RegOff || regoff_opcode == kStr32RegOff;
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t index = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t scale = is32 ? 2u : 3u;
        const std::uint32_t shift =
            ((instruction >> 12u) & 0x1u) != 0 ? scale : 0u;
        const std::uint64_t base_value =
            read_stack_pointer_operand(*this, base);
        const std::uint64_t rm_value =
            read_zero_register_operand(*this, index);
        const std::uint64_t offset =
            extend_register_offset(rm_value, option, shift);
        // No write-back. Overflow/underflow is checked before the access so a
        // failed address leaves registers, memory and PC unchanged.
        const std::uint64_t address = add_extended_offset_address(
            base_value, offset, option == 6u || option == 7u,
            "Register-offset load/store address");
        const std::size_t memory_address = pc_as_memory_offset(address);
        if (load) {
            if (is32) {
                const std::uint32_t value = memory.read_u32(memory_address);
                write_zero_register_result(*this, target, value);
            } else {
                const std::uint64_t value = memory.read_u64(memory_address);
                write_zero_register_result(*this, target, value);
            }
        } else if (is32) {
            const std::uint32_t value = static_cast<std::uint32_t>(
                read_zero_register_operand(*this, target));
            memory.write_u32(memory_address, value);
            note_normal_store(memory_address, 4);
        } else {
            const std::uint64_t value =
                read_zero_register_operand(*this, target);
            memory.write_u64(memory_address, value);
            note_normal_store(memory_address, 8);
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    const bool is_ldrsb_w_reg_exec = regoff_opcode == kLdrsbWRegOff;
    const bool is_ldrsb_x_reg_exec = regoff_opcode == kLdrsbXRegOff;
    const bool is_ldrsh_w_reg_exec = regoff_opcode == kLdrshWRegOff;
    const bool is_ldrsh_x_reg_exec = regoff_opcode == kLdrshXRegOff;
    const bool is_ldrsw_x_reg_exec = regoff_opcode == kLdrswXRegOff;
    if (is_ldrsb_w_reg_exec || is_ldrsb_x_reg_exec || is_ldrsh_w_reg_exec || is_ldrsh_x_reg_exec || is_ldrsw_x_reg_exec) {
        const std::uint32_t option = (instruction >> 13u) & 0x7u;
        if ((option & 0x2u) == 0) {
            throw std::runtime_error("Unsupported AArch64 instruction encoding: 0x"
                                     + hex_word(instruction));
        }
        const bool scaled = ((instruction >> 12u) & 0x1u) != 0;
        const bool is_byte = is_ldrsb_w_reg_exec || is_ldrsb_x_reg_exec;
        const bool is_half = is_ldrsh_w_reg_exec || is_ldrsh_x_reg_exec;
        if (is_byte && scaled) {
            throw std::runtime_error("Unsupported AArch64 instruction encoding: S bit set in byte register offset: 0x"
                                     + hex_word(instruction));
        }
        const std::uint32_t scale = is_byte ? 0u : is_half ? 1u : 2u;
        const std::uint32_t shift = scaled ? scale : 0u;
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t index = (instruction >> 16u) & 0x1Fu;
        const std::uint64_t base_value =
            read_stack_pointer_operand(*this, base);
        const std::uint64_t rm_value =
            read_zero_register_operand(*this, index);
        const std::uint64_t offset =
            extend_register_offset(rm_value, option, shift);
        const std::uint64_t address = add_extended_offset_address(
            base_value, offset, option == 6u || option == 7u,
            "Register-offset load/store address");
        const std::size_t memory_address = pc_as_memory_offset(address);

        std::uint64_t value = 0;
        if (is_ldrsb_w_reg_exec) {
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(memory.read_u8(memory_address))));
        } else if (is_ldrsb_x_reg_exec) {
            value = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int8_t>(memory.read_u8(memory_address))));
        } else if (is_ldrsh_w_reg_exec) {
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(memory.read_u16(memory_address))));
        } else if (is_ldrsh_x_reg_exec) {
            value = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(memory.read_u16(memory_address))));
        } else {
            value = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(memory.read_u32(memory_address))));
        }
        write_zero_register_result(*this, target, value);
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    if (regoff_opcode == kPrfmRegOff) {
        const std::uint32_t option = (instruction >> 13u) & 0x7u;
        if ((option & 0x2u) == 0) {
            throw std::runtime_error("Unsupported AArch64 instruction encoding: 0x"
                                     + hex_word(instruction));
        }
        throw std::runtime_error("Unsupported AArch64 instruction encoding: PRFM register offset 0x"
                                 + hex_word(instruction));
    }
    const bool is_strb_reg_exec = regoff_opcode == kStrbRegOff;
    const bool is_ldrb_reg_exec = regoff_opcode == kLdrbRegOff;
    const bool is_strh_reg_exec = regoff_opcode == kStrhRegOff;
    const bool is_ldrh_reg_exec = regoff_opcode == kLdrhRegOff;
    if (is_strb_reg_exec || is_ldrb_reg_exec || is_strh_reg_exec || is_ldrh_reg_exec) {
        const std::uint32_t option = (instruction >> 13u) & 0x7u;
        if ((option & 0x2u) == 0) {
            throw std::runtime_error("Unsupported AArch64 instruction encoding: 0x"
                                     + hex_word(instruction));
        }
        const bool scaled = ((instruction >> 12u) & 0x1u) != 0;
        const bool is_byte = is_strb_reg_exec || is_ldrb_reg_exec;
        if (is_byte && scaled) {
            throw std::runtime_error("Unsupported AArch64 instruction encoding: S bit set in byte register offset: 0x"
                                     + hex_word(instruction));
        }
        const std::uint32_t shift = (!is_byte && scaled) ? 1u : 0u;
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t index = (instruction >> 16u) & 0x1Fu;
        const std::uint64_t base_value =
            read_stack_pointer_operand(*this, base);
        const std::uint64_t rm_value =
            read_zero_register_operand(*this, index);
        const std::uint64_t offset =
            extend_register_offset(rm_value, option, shift);
        const std::uint64_t address = add_extended_offset_address(
            base_value, offset, option == 6u || option == 7u,
            "Register-offset load/store address");
        const std::size_t memory_address = pc_as_memory_offset(address);
        const bool load = is_ldrb_reg_exec || is_ldrh_reg_exec;
        if (load) {
            const std::uint64_t value = is_byte ? static_cast<std::uint64_t>(memory.read_u8(memory_address))
                                                : static_cast<std::uint64_t>(memory.read_u16(memory_address));
            write_zero_register_result(*this, target, value);
        } else {
            if (is_byte) {
                const std::uint8_t value = static_cast<std::uint8_t>(read_zero_register_operand(*this, target));
                memory.write_u8(memory_address, value);
                note_normal_store(memory_address, 1);
            } else {
                const std::uint16_t value = static_cast<std::uint16_t>(read_zero_register_operand(*this, target));
                memory.write_u16(memory_address, value);
                note_normal_store(memory_address, 2);
            }
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    const std::uint32_t exclusive_size_field = (instruction >> 30u) & 0x3u;
    if ((instruction & kExclusiveGroupMask) == kExclusiveGroup) {
        const std::uint32_t op = (instruction >> 21u) & 0x7u;
        const bool ordered = (instruction & (1u << 15u)) != 0;
        const std::uint32_t status = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t target = instruction & 0x1Fu;
        if (op == 0u || op == 2u) {
            const bool load = op == 2u;
            // Non-pair encodings fix the op2 (Rs) field to 11111 for loads and
            // the Rt2 field to 11111 for both directions. Words that violate
            // this are not valid LDXR/LDAXR/STXR/STLXR encodings.
            if ((load && (((instruction >> 16u) & 0x1Fu) != 31u))
                || (((instruction >> 10u) & 0x1Fu) != 31u)) {
                throw std::runtime_error(
                    "Unsupported AArch64 instruction encoding");
            }
            const std::size_t width = access_width_bytes(exclusive_size_field);
            const std::uint64_t address =
                read_stack_pointer_operand(*this, base);
            const std::size_t memory_address = pc_as_memory_offset(address);
            // Phase 36: exclusive accesses follow the reference's alignment
            // rule, and the reference reports their alignment faults with
            // WnR = 1 even for the load form.
            memory.check_access(address, static_cast<unsigned>(width),
                                AccessType::Write);
            require_memory_range(memory, memory_address, width);
            if (load) {
                const std::uint64_t value =
                    read_access_value(memory, memory_address,
                                      exclusive_size_field);
                if (ordered) {
                    std::atomic_thread_fence(std::memory_order_acquire);
                }
                exclusive_monitor_ =
                    ExclusiveMonitor{true, address, width, value, 0};
                write_zero_register_result(*this, target, value);
            } else {
                const std::uint64_t value =
                    read_zero_register_operand(*this, target)
                    & access_value_mask(exclusive_size_field);
                const bool success =
                    exclusive_monitor_.valid
                    && exclusive_monitor_.address == address
                    && exclusive_monitor_.size == width
                    && exclusive_monitor_.low
                           == read_access_value(memory, memory_address,
                                                exclusive_size_field);
                if (success) {
                    if (ordered) {
                        std::atomic_thread_fence(std::memory_order_release);
                    }
                    write_access_value(memory, memory_address,
                                       exclusive_size_field, value);
                    note_normal_store(address, width);
                    write_zero_register_result(*this, status, 0);
                } else {
                    write_zero_register_result(*this, status, 1);
                }
                clear_exclusive_monitor();
            }
            program_counter_ = checked_add_address(
                instruction_address, 4u, "Instruction PC increment");
            emit_trace();
            return;
        }
        if (op == 1u || op == 3u) {
            if ((exclusive_size_field & 2u) == 0u) {
                throw std::runtime_error(
                    "Unsupported AArch64 instruction encoding");
            }
            const bool load = op == 3u;
            // LDXP/LDAXP fix the op2 (Rs) field to 11111.
            if (load && (((instruction >> 16u) & 0x1Fu) != 31u)) {
                throw std::runtime_error(
                    "Unsupported AArch64 instruction encoding");
            }
            const std::uint32_t element_size_field =
                exclusive_size_field == 3u ? 3u : 2u;
            const std::size_t element_width =
                access_width_bytes(element_size_field);
            const std::size_t pair_width = element_width * 2u;
            const std::uint32_t target2 = (instruction >> 10u) & 0x1Fu;
            const std::uint64_t address =
                read_stack_pointer_operand(*this, base);
            const std::size_t memory_address = pc_as_memory_offset(address);
            // Phase 36: an exclusive pair is single-copy atomic per doubleword,
            // so the reference requires the pair to be doubleword aligned and
            // reports the second doubleword in FAR when it is not.
            memory.check_range(address, static_cast<unsigned>(pair_width),
                               load ? AccessType::Read : AccessType::Write);
            if (architectural_exceptions_ && (address & 0x7u) != 0u) {
                memory.raise_alignment_fault(
                    address + 8u, static_cast<unsigned>(pair_width),
                    load ? AccessType::Read : AccessType::Write);
            }
            require_memory_range(memory, memory_address, pair_width);
            const std::size_t second_address = memory_address + element_width;
            if (load) {
                const std::uint64_t first = read_access_value(
                    memory, memory_address, element_size_field);
                const std::uint64_t second = read_access_value(
                    memory, second_address, element_size_field);
                if (ordered) {
                    std::atomic_thread_fence(std::memory_order_acquire);
                }
                exclusive_monitor_ =
                    ExclusiveMonitor{true, address, pair_width, first, second};
                write_zero_register_result(*this, target, first);
                write_zero_register_result(*this, target2, second);
            } else {
                const std::uint64_t element_mask =
                    access_value_mask(element_size_field);
                const std::uint64_t first =
                    read_zero_register_operand(*this, target) & element_mask;
                const std::uint64_t second =
                    read_zero_register_operand(*this, target2) & element_mask;
                const bool success =
                    exclusive_monitor_.valid
                    && exclusive_monitor_.address == address
                    && exclusive_monitor_.size == pair_width
                    && exclusive_monitor_.low
                           == read_access_value(memory, memory_address,
                                                element_size_field)
                    && exclusive_monitor_.high
                           == read_access_value(memory, second_address,
                                                element_size_field);
                if (success) {
                    if (ordered) {
                        std::atomic_thread_fence(std::memory_order_release);
                    }
                    write_access_value(memory, memory_address,
                                       element_size_field, first);
                    write_access_value(memory, second_address,
                                       element_size_field, second);
                    note_normal_store(address, pair_width);
                    write_zero_register_result(*this, status, 0);
                } else {
                    write_zero_register_result(*this, status, 1);
                }
                clear_exclusive_monitor();
            }
            program_counter_ = checked_add_address(
                instruction_address, 4u, "Instruction PC increment");
            emit_trace();
            return;
        }
        if (op == 4u || op == 6u) {
            const bool load = (op == 6u);
            const std::uint32_t target2 = (instruction >> 10u) & 0x1Fu;
            if (status != 31u || target2 != 31u || !ordered) {
                if (architectural_exceptions_) {
                    raise_synchronous_exception(
                        make_undefined_instruction(instruction_address));
                    return;
                }
                throw std::runtime_error("Unsupported AArch64 instruction encoding");
            }
            const std::size_t width = access_width_bytes(exclusive_size_field);
            const std::uint64_t address = read_stack_pointer_operand(*this, base);
            if (width > 1u && (address & (std::uint64_t{width} - 1u)) != 0u) {
                if (architectural_exceptions_) {
                    memory.raise_alignment_fault(address, static_cast<unsigned>(width),
                                                 load ? AccessType::Read : AccessType::Write);
                }
                throw std::runtime_error("Alignment fault on ordered memory access");
            }
            const std::size_t memory_address = pc_as_memory_offset(address);
            memory.check_access(address, static_cast<unsigned>(width),
                                load ? AccessType::Read : AccessType::Write);
            require_memory_range(memory, memory_address, width);
            if (load) {
                const std::uint64_t value =
                    read_access_value(memory, memory_address, exclusive_size_field);
                std::atomic_thread_fence(std::memory_order_acquire);
                write_zero_register_result(*this, target, value);
            } else {
                const std::uint64_t value =
                    read_zero_register_operand(*this, target)
                    & access_value_mask(exclusive_size_field);
                std::atomic_thread_fence(std::memory_order_release);
                write_access_value(memory, memory_address, exclusive_size_field, value);
                note_normal_store(address, width);
            }
            program_counter_ = checked_add_address(
                instruction_address, 4u, "Instruction PC increment");
            emit_trace();
            return;
        }
        throw std::runtime_error("Unsupported AArch64 instruction encoding");
    }
    if ((instruction & kLseAtomicMask) == kLseAtomicPattern) {
        const std::uint32_t opc = (instruction >> 12u) & 0xFu;
        if (opc > 8u) {
            throw std::runtime_error("Unsupported AArch64 instruction encoding");
        }
        const bool acquire = (instruction & (1u << 23u)) != 0;
        const bool release = (instruction & (1u << 22u)) != 0;
        const std::uint32_t source = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t target = instruction & 0x1Fu;
        const std::size_t width = access_width_bytes(exclusive_size_field);
        const std::uint64_t address = read_stack_pointer_operand(*this, base);
        const std::size_t memory_address = pc_as_memory_offset(address);
        // Phase 36: LSE atomics are read-modify-write, so the reference reports
        // alignment faults with WnR = 1.
        memory.check_access(address, static_cast<unsigned>(width),
                            AccessType::Write);
        require_memory_range(memory, memory_address, width);
        const std::uint64_t value_mask =
            access_value_mask(exclusive_size_field);
        const std::uint64_t old_value =
            read_access_value(memory, memory_address, exclusive_size_field)
            & value_mask;
        const std::uint64_t source_value =
            read_zero_register_operand(*this, source) & value_mask;
        if (release) {
            std::atomic_thread_fence(std::memory_order_release);
        }
        std::uint64_t new_value = 0;
        switch (opc) {
        case 0u: // LDADD
            new_value = old_value + source_value;
            break;
        case 1u: // LDCLR
            new_value = old_value & ~source_value;
            break;
        case 2u: // LDEOR
            new_value = old_value ^ source_value;
            break;
        case 3u: // LDSET
            new_value = old_value | source_value;
            break;
        case 4u: // LDSMAX
            new_value = signed_access_value(old_value, exclusive_size_field)
                            >= signed_access_value(source_value,
                                                   exclusive_size_field)
                ? old_value : source_value;
            break;
        case 5u: // LDSMIN
            new_value = signed_access_value(old_value, exclusive_size_field)
                            <= signed_access_value(source_value,
                                                   exclusive_size_field)
                ? old_value : source_value;
            break;
        case 6u: // LDUMAX
            new_value = old_value >= source_value ? old_value : source_value;
            break;
        case 7u: // LDUMIN
            new_value = old_value <= source_value ? old_value : source_value;
            break;
        default: // SWP
            new_value = source_value;
            break;
        }
        new_value &= value_mask;
        write_access_value(memory, memory_address, exclusive_size_field,
                           new_value);
        note_normal_store(address, width);
        if (acquire) {
            std::atomic_thread_fence(std::memory_order_acquire);
        }
        write_zero_register_result(*this, target, old_value);
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    const std::uint32_t pair_opcode = instruction & kLoadStorePairMask;
    const bool pair_32 = pair_opcode == kStp32Off || pair_opcode == kLdp32Off
                         || pair_opcode == kStp32Pre || pair_opcode == kLdp32Pre
                         || pair_opcode == kStp32Post || pair_opcode == kLdp32Post;
    const bool pair_64 = pair_opcode == kStp64Off || pair_opcode == kLdp64Off
                         || pair_opcode == kStp64Pre || pair_opcode == kLdp64Pre
                         || pair_opcode == kStp64Post || pair_opcode == kLdp64Post;
    const bool is_ldpsw = pair_opcode == kLdpswOff || pair_opcode == kLdpswPre
                          || pair_opcode == kLdpswPost;
    if (pair_32 || pair_64 || is_ldpsw) {
        const bool load = is_ldpsw || ((instruction & (1u << 22u)) != 0);
        const bool is32 = pair_32;
        const std::uint32_t target1 = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t target2 = (instruction >> 10u) & 0x1Fu;
        const std::uint32_t mode = (instruction >> 23u) & 0x7u;
        const std::size_t scale = (pair_64 ? 8u : 4u);
        const std::int64_t offset =
            decode_simm7((instruction >> 15u) & 0x7Fu) * static_cast<std::int64_t>(scale);
        const std::uint64_t base_value = read_stack_pointer_operand(*this, base);
        std::uint64_t ea = 0;
        std::uint64_t writeback = 0;
        const bool has_writeback = (mode == 3u || mode == 1u);
        if (mode == 2u) {
            // Signed scaled offset
            ea = add_signed_address(base_value, offset, "Load/store pair address");
        } else if (mode == 3u) {
            // Pre-index: writeback = base + offset, ea = writeback
            writeback = add_signed_address(base_value, offset, "Load/store pair address");
            ea = writeback;
        } else if (mode == 1u) {
            // Post-index: ea = base, writeback = base + offset
            ea = base_value;
            writeback = add_signed_address(base_value, offset, "Load/store pair address");
        } else {
            throw std::runtime_error("Unsupported AArch64 instruction encoding");
        }
        const std::uint64_t ea2 = checked_add_address(ea, scale, "Load/store pair second element address");
        const std::size_t mem_offset1 = pc_as_memory_offset(ea);
        const std::size_t mem_offset2 = pc_as_memory_offset(ea2);
        // Fault atomicity: check memory bounds for the entire 2*scale byte range before
        // modifying any architectural registers or writing any bytes to memory.
        // Phase 36: each element must be naturally aligned; both checks happen
        // before any store so a faulting second element cannot commit the first.
        memory.check_access(ea, static_cast<unsigned>(scale),
                            load ? AccessType::Read : AccessType::Write);
        memory.check_access(ea2, static_cast<unsigned>(scale),
                            load ? AccessType::Read : AccessType::Write);
        if (mem_offset1 > memory.size() || (scale * 2u) > memory.size() - mem_offset1) {
            std::ostringstream message;
            message << "Memory access out of range: offset " << mem_offset1
                    << ", width " << (scale * 2u)
                    << ", size " << memory.size();
            throw std::out_of_range(message.str());
        }
        if (load) {
            // Read both elements into temporaries before updating registers
            std::uint64_t val1 = 0;
            std::uint64_t val2 = 0;
            if (is_ldpsw) {
                const std::int32_t sval1 = static_cast<std::int32_t>(memory.read_u32(mem_offset1));
                const std::int32_t sval2 = static_cast<std::int32_t>(memory.read_u32(mem_offset2));
                val1 = static_cast<std::uint64_t>(static_cast<std::int64_t>(sval1));
                val2 = static_cast<std::uint64_t>(static_cast<std::int64_t>(sval2));
            } else if (is32) {
                val1 = memory.read_u32(mem_offset1);
                val2 = memory.read_u32(mem_offset2);
            } else {
                val1 = memory.read_u64(mem_offset1);
                val2 = memory.read_u64(mem_offset2);
            }
            // Transfer register writes: Rt then Rt2. Register 31 is WZR/XZR (discarded).
            // When Rt == Rt2, Rt2 is written second so Rt receives val2 (deterministic policy).
            write_zero_register_result(*this, target1, val1);
            write_zero_register_result(*this, target2, val2);
        } else {
            // Store: read transfer registers (register 31 is WZR/XZR -> 0)
            if (is32) {
                const std::uint32_t val1 = static_cast<std::uint32_t>(read_zero_register_operand(*this, target1));
                const std::uint32_t val2 = static_cast<std::uint32_t>(read_zero_register_operand(*this, target2));
                memory.write_u32(mem_offset1, val1);
                memory.write_u32(mem_offset2, val2);
                note_normal_store(mem_offset1, 8);
            } else {
                const std::uint64_t val1 = read_zero_register_operand(*this, target1);
                const std::uint64_t val2 = read_zero_register_operand(*this, target2);
                memory.write_u64(mem_offset1, val1);
                memory.write_u64(mem_offset2, val2);
                note_normal_store(mem_offset1, 16);
            }
        }
        // Base register write-back occurs after successful memory access.
        // If base overlaps with Rt or Rt2 on load, write-back occurs last,
        // matching Phase 19 precedent and Inferno QEMU behavior.
        if (has_writeback) {
            if (base == CPU::kRegisterCount) {
                set_stack_pointer(writeback);
            } else {
                write_register(base, writeback);
            }
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    const std::uint32_t load_store_opcode = instruction & kLoadStoreUnsignedMask;
    if (load_store_opcode == kLdr64 || load_store_opcode == kStr64
        || load_store_opcode == kLdr32 || load_store_opcode == kStr32) {
        const bool load = load_store_opcode == kLdr64 || load_store_opcode == kLdr32;
        const bool is32 = load_store_opcode == kLdr32 || load_store_opcode == kStr32;
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::uint64_t offset =
            static_cast<std::uint64_t>((instruction >> 10u) & 0xFFFu)
            * (is32 ? 4u : 8u);
        const std::uint64_t address = checked_add_address(
            read_stack_pointer_operand(*this, base), offset,
            "Load/store effective address");
        const std::size_t memory_address = pc_as_memory_offset(address);
        if (load) {
            if (is32) {
                const std::uint32_t value = memory.read_u32(memory_address);
                write_zero_register_result(*this, target, value);
            } else {
                const std::uint64_t value = memory.read_u64(memory_address);
                write_zero_register_result(*this, target, value);
            }
        } else if (is32) {
            const std::uint32_t value = static_cast<std::uint32_t>(
                read_zero_register_operand(*this, target));
            memory.write_u32(memory_address, value);
            note_normal_store(memory_address, 4);
        } else {
            const std::uint64_t value = read_zero_register_operand(*this, target);
            memory.write_u64(memory_address, value);
            note_normal_store(memory_address, 8);
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    const bool is_ldrsb_w_off_exec = load_store_opcode == kLdrsbWOff;
    const bool is_ldrsb_x_off_exec = load_store_opcode == kLdrsbXOff;
    const bool is_ldrsh_w_off_exec = load_store_opcode == kLdrshWOff;
    const bool is_ldrsh_x_off_exec = load_store_opcode == kLdrshXOff;
    const bool is_ldrsw_x_off_exec = load_store_opcode == kLdrswXOff;
    if (is_ldrsb_w_off_exec || is_ldrsb_x_off_exec || is_ldrsh_w_off_exec || is_ldrsh_x_off_exec || is_ldrsw_x_off_exec) {
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const auto imm12 = (instruction >> 10u) & 0xFFFu;
        const bool is_byte = is_ldrsb_w_off_exec || is_ldrsb_x_off_exec;
        const bool is_half = is_ldrsh_w_off_exec || is_ldrsh_x_off_exec;
        const std::uint64_t scale = is_byte ? 1u : is_half ? 2u : 4u;
        const std::uint64_t offset = static_cast<std::uint64_t>(imm12) * scale;
        const std::uint64_t base_value =
            read_stack_pointer_operand(*this, base);
        const std::uint64_t address = checked_add_address(
            base_value, offset, "Unsigned offset load address");
        const std::size_t memory_address = pc_as_memory_offset(address);

        std::uint64_t value = 0;
        if (is_ldrsb_w_off_exec) {
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(memory.read_u8(memory_address))));
        } else if (is_ldrsb_x_off_exec) {
            value = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int8_t>(memory.read_u8(memory_address))));
        } else if (is_ldrsh_w_off_exec) {
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(memory.read_u16(memory_address))));
        } else if (is_ldrsh_x_off_exec) {
            value = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(memory.read_u16(memory_address))));
        } else {
            value = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(memory.read_u32(memory_address))));
        }
        write_zero_register_result(*this, target, value);
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    if (load_store_opcode == kPrfmImmOff) {
        throw std::runtime_error("Unsupported AArch64 instruction encoding: PRFM immediate 0x"
                                 + hex_word(instruction));
    }
    const bool is_strb_off_exec = load_store_opcode == kStrbOff;
    const bool is_ldrb_off_exec = load_store_opcode == kLdrbOff;
    const bool is_strh_off_exec = load_store_opcode == kStrhOff;
    const bool is_ldrh_off_exec = load_store_opcode == kLdrhOff;
    if (is_strb_off_exec || is_ldrb_off_exec || is_strh_off_exec || is_ldrh_off_exec) {
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const auto imm12 = (instruction >> 10u) & 0xFFFu;
        const bool is_byte = is_strb_off_exec || is_ldrb_off_exec;
        const std::uint64_t scale = is_byte ? 1u : 2u;
        const std::uint64_t offset = static_cast<std::uint64_t>(imm12) * scale;
        const std::uint64_t base_value =
            read_stack_pointer_operand(*this, base);
        const std::uint64_t address = checked_add_address(
            base_value, offset, "Unsigned offset load/store address");
        const std::size_t memory_address = pc_as_memory_offset(address);
        const bool load = is_ldrb_off_exec || is_ldrh_off_exec;
        if (load) {
            const std::uint64_t value = is_byte ? static_cast<std::uint64_t>(memory.read_u8(memory_address))
                                                : static_cast<std::uint64_t>(memory.read_u16(memory_address));
            write_zero_register_result(*this, target, value);
        } else {
            if (is_byte) {
                const std::uint8_t value = static_cast<std::uint8_t>(read_zero_register_operand(*this, target));
                memory.write_u8(memory_address, value);
                note_normal_store(memory_address, 1);
            } else {
                const std::uint16_t value = static_cast<std::uint16_t>(read_zero_register_operand(*this, target));
                memory.write_u16(memory_address, value);
                note_normal_store(memory_address, 2);
            }
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    const std::uint32_t cc_opcode = instruction & kCcMask;
    if (cc_opcode == kCcmn32Reg || cc_opcode == kCcmp32Reg
        || cc_opcode == kCcmn64Reg || cc_opcode == kCcmp64Reg) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool is_cmp = (instruction & 0x40000000u) != 0;
        const auto rm = (instruction >> 16u) & 0x1Fu;
        const auto condition = (instruction >> 12u) & 0xFu;
        const auto rn = (instruction >> 5u) & 0x1Fu;
        const auto nzcv_imm = instruction & 0xFu;
        if (condition_holds(condition, nzcv_)) {
            const std::uint64_t left = read_width_operand(*this, rn, is64, false);
            const std::uint64_t right = read_width_operand(*this, rm, is64, false);
            const std::uint64_t result = is_cmp ? (left - right) : (left + right);
            nzcv_ = arithmetic_flags(left, right, result, is_cmp, is64);
        } else {
            nzcv_ = static_cast<std::uint8_t>(nzcv_imm);
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    if (cc_opcode == kCcmn32Imm || cc_opcode == kCcmp32Imm
        || cc_opcode == kCcmn64Imm || cc_opcode == kCcmp64Imm) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool is_cmp = (instruction & 0x40000000u) != 0;
        const auto imm5 = (instruction >> 16u) & 0x1Fu;
        const auto condition = (instruction >> 12u) & 0xFu;
        const auto rn = (instruction >> 5u) & 0x1Fu;
        const auto nzcv_imm = instruction & 0xFu;
        if (condition_holds(condition, nzcv_)) {
            const std::uint64_t left = read_width_operand(*this, rn, is64, false);
            const std::uint64_t right = imm5;
            const std::uint64_t result = is_cmp ? (left - right) : (left + right);
            nzcv_ = arithmetic_flags(left, right, result, is_cmp, is64);
        } else {
            nzcv_ = static_cast<std::uint8_t>(nzcv_imm);
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    const std::uint32_t csel_opcode = instruction & kCSelMask;
    if (csel_opcode == kCsel32 || csel_opcode == kCsinc32
        || csel_opcode == kCsinv32 || csel_opcode == kCsneg32
        || csel_opcode == kCsel64 || csel_opcode == kCsinc64
        || csel_opcode == kCsinv64 || csel_opcode == kCsneg64) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const auto op = (instruction >> 30u) & 1u;
        const auto rm = (instruction >> 16u) & 0x1Fu;
        const auto condition = (instruction >> 12u) & 0xFu;
        const auto op2 = (instruction >> 10u) & 1u;
        const auto rn = (instruction >> 5u) & 0x1Fu;
        const auto rd = instruction & 0x1Fu;
        std::uint64_t result = 0;
        if (condition_holds(condition, nzcv_)) {
            result = read_width_operand(*this, rn, is64, false);
        } else {
            const std::uint64_t m_val = read_width_operand(*this, rm, is64, false);
            if (op == 0 && op2 == 0) {
                // CSEL
                result = m_val;
            } else if (op == 0 && op2 == 1) {
                // CSINC
                result = m_val + 1u;
            } else if (op == 1 && op2 == 0) {
                // CSINV
                result = ~m_val;
            } else {
                // CSNEG
                result = 0ull - m_val;
            }
        }
        write_width_result(*this, rd, result, is64, false);
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    // Phase 24: Bitfield move instructions: SBFM, BFM, UBFM (32-bit and 64-bit)
    const std::uint32_t bitfield_opcode = instruction & kBitfieldMask;
    if (bitfield_opcode == kSbfm32 || bitfield_opcode == kBfm32
        || bitfield_opcode == kUbfm32 || bitfield_opcode == kSbfm64
        || bitfield_opcode == kBfm64 || bitfield_opcode == kUbfm64) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        if (!is64 && (instruction & 0x00208000u) != 0) {
            throw std::runtime_error("Unsupported AArch64 instruction encoding");
        }
        const std::uint32_t datasize = is64 ? 64u : 32u;
        const std::uint32_t immr = (instruction >> 16u) & 0x3Fu;
        const std::uint32_t imms = (instruction >> 10u) & 0x3Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;
        const std::uint64_t src = read_width_operand(*this, rn, is64, false);
        const std::uint64_t old_dst = read_width_operand(*this, rd, is64, false);
        std::uint64_t result = 0;
        const bool is_bfm  = (bitfield_opcode == kBfm32  || bitfield_opcode == kBfm64);
        const bool is_ubfm = (bitfield_opcode == kUbfm32 || bitfield_opcode == kUbfm64);
        if (imms >= immr) {
            // Non-wrapping: extract bits [imms:immr] of src to bits [imms-immr:0]
            const std::uint32_t len = (imms - immr) + 1u;
            const std::uint64_t mask = (len == 64u) ? ~0ull : ((1ull << len) - 1ull);
            const std::uint64_t extracted = (src >> immr) & mask;
            if (is_ubfm) {
                result = extracted;
            } else if (is_bfm) {
                result = (old_dst & ~mask) | extracted;
            } else { // SBFM
                const std::uint64_t sign_bit = 1ull << (len - 1u);
                if ((extracted & sign_bit) != 0) {
                    result = (extracted | ~mask) & width_mask(is64);
                } else {
                    result = extracted;
                }
            }
        } else {
            // Wrapping: insert bits [imms:0] of src at position (datasize - immr)
            const std::uint32_t len = imms + 1u;
            const std::uint32_t pos = datasize - immr;
            const std::uint64_t mask = (len == 64u) ? ~0ull : ((1ull << len) - 1ull);
            const std::uint64_t val = src & mask;
            if (is_ubfm) {
                result = (val << pos) & width_mask(is64);
            } else if (is_bfm) {
                const std::uint64_t insert_mask = (mask << pos) & width_mask(is64);
                result = (old_dst & ~insert_mask) | ((val << pos) & width_mask(is64));
            } else { // SBFM
                result = (val << pos) & width_mask(is64);
                const std::uint64_t sign_bit = 1ull << (len - 1u);
                if ((val & sign_bit) != 0) {
                    const std::uint32_t top = pos + len;
                    if (top < datasize) {
                        const std::uint64_t ext_mask = (width_mask(is64) >> top) << top;
                        result |= ext_mask;
                    }
                }
            }
        }
        write_width_result(*this, rd, result, is64, false);
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    if ((instruction & 0x1F800000u) == 0x13000000u) {
        // Unallocated bitfield instruction encoding (e.g. opc=11, sf!=N)
        throw std::runtime_error("Unsupported AArch64 instruction encoding");
    }
    if ((instruction & kConditionalBranchMask) == kConditionalBranch) {
        const std::uint32_t condition = instruction & 0xFu;
        if (condition == 0xFu) {
            throw std::runtime_error("Reserved B.cond condition NV");
        }
        const std::uint32_t encoded = (instruction >> 5u) & 0x7FFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded);
        if ((encoded & 0x40000u) != 0) {
            offset -= (std::int64_t{1} << 19u);
        }
        const std::int64_t displacement = offset * 4;
        if (condition_holds(condition, nzcv_)) {
            if (displacement >= 0) {
                program_counter_ = checked_add_address(
                    instruction_address, static_cast<std::uint64_t>(displacement),
                    "Conditional branch target");
            } else {
                program_counter_ = checked_sub_address(
                    instruction_address, static_cast<std::uint64_t>(-displacement),
                    "Conditional branch target");
            }
        } else {
            program_counter_ = checked_add_address(
                instruction_address, 4u, "Instruction PC increment");
        }
        emit_trace();
        return;
    }
    if ((instruction & kCompareBranchMask) == kCbz) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool nonzero = (instruction & 0x01000000u) != 0;
        const std::uint32_t source = instruction & 0x1Fu;
        const std::uint32_t encoded = (instruction >> 5u) & 0x7FFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded);
        if ((encoded & 0x40000u) != 0) {
            offset -= (std::int64_t{1} << 19u);
        }
        const std::uint64_t value = read_width_operand(
            *this, source, is64, false);
        const bool take = nonzero ? value != 0 : value == 0;
        if (take) {
            const std::int64_t displacement = offset * 4;
            if (displacement >= 0) {
                program_counter_ = checked_add_address(
                    instruction_address, static_cast<std::uint64_t>(displacement),
                    "Compare branch target");
            } else {
                program_counter_ = checked_sub_address(
                    instruction_address, static_cast<std::uint64_t>(-displacement),
                    "Compare branch target");
            }
        } else {
            program_counter_ = checked_add_address(
                instruction_address, 4u, "Instruction PC increment");
        }
        emit_trace();
        return;
    }
    if ((instruction & kTestBitBranchMask) == kTbz) {
        const bool nonzero = (instruction & 0x01000000u) != 0;
        const std::uint32_t rt = instruction & 0x1Fu;
        const std::uint32_t bitpos = ((instruction >> 26u) & 0x20u) | ((instruction >> 19u) & 0x1Fu);
        const std::uint32_t encoded = (instruction >> 5u) & 0x3FFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded);
        if ((encoded & 0x2000u) != 0) {
            offset -= (std::int64_t{1} << 14u);
        }
        const std::uint64_t val = (rt == 31u) ? 0ull : read_register(rt);
        const bool bit_set = ((val >> bitpos) & 1ull) != 0;
        const bool take = nonzero ? bit_set : !bit_set;
        if (take) {
            const std::int64_t displacement = offset * 4;
            if (displacement >= 0) {
                program_counter_ = checked_add_address(
                    instruction_address, static_cast<std::uint64_t>(displacement),
                    "Test bit and branch target");
            } else {
                program_counter_ = checked_sub_address(
                    instruction_address, static_cast<std::uint64_t>(-displacement),
                    "Test bit and branch target");
            }
        } else {
            program_counter_ = checked_add_address(
                instruction_address, 4u, "Instruction PC increment");
        }
        emit_trace();
        return;
    }
    if ((instruction & kBranchRegisterMask) == kBranchRegister) {
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint64_t target = (rn == 31u) ? 0ull : read_register(rn);
        if (!architectural_exceptions_ && (target & 0x3u) != 0) {
            throw std::runtime_error("Misaligned branch target address: " + hex_word64(target));
        }
        program_counter_ = target;
        emit_trace();
        return;
    }
    if ((instruction & kBranchRegisterMask) == kBranchLinkRegister) {
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint64_t target = (rn == 31u) ? 0ull : read_register(rn);
        if (!architectural_exceptions_ && (target & 0x3u) != 0) {
            throw std::runtime_error("Misaligned branch target address: " + hex_word64(target));
        }
        const std::uint64_t return_address = checked_add_address(
            instruction_address, 4u, "Branch with link return address");
        write_register(30, return_address);
        program_counter_ = target;
        emit_trace();
        return;
    }
    if ((instruction & kBranchRegisterMask) == kReturnRegister) {
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint64_t target = (rn == 31u) ? 0ull : read_register(rn);
        if (!architectural_exceptions_ && (target & 0x3u) != 0) {
            throw std::runtime_error("Misaligned branch target address: " + hex_word64(target));
        }
        program_counter_ = target;
        emit_trace();
        return;
    }
    if ((instruction & kBranchImmediateMask) == kBranch) {
        const bool link = (instruction & 0x80000000u) != 0;
        const std::uint32_t encoded_offset = instruction & 0x03FFFFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded_offset);
        if ((encoded_offset & 0x02000000u) != 0) {
            offset -= (std::int64_t{1} << 26u);
        }
        offset *= 4;
        std::uint64_t target = 0;
        if (offset >= 0) {
            target = checked_add_address(
                instruction_address, static_cast<std::uint64_t>(offset),
                "Branch target");
        } else {
            target = checked_sub_address(
                instruction_address, static_cast<std::uint64_t>(-offset),
                "Branch target");
        }
        if (link) {
            const std::uint64_t return_address = checked_add_address(
                instruction_address, 4u, "Branch with link return address");
            write_register(30, return_address);
        }
        program_counter_ = target;
        emit_trace();
        return;
    }
    // Phase 32B: SIMD & Floating-Point Load/Store
    // Bits 29:27 = 111, Bit 26 = 1 (V=1).
    if ((instruction & 0x3E000000u) == 0x3C000000u) {
        const std::uint32_t size = (instruction >> 30u) & 0x3u;
        const std::uint32_t opc = (instruction >> 22u) & 0x3u;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rt = instruction & 0x1Fu;

        std::size_t access_bytes = 0;
        bool is_load = false;
        bool valid_size = false;

        if (opc == 0b00) { // STR B/H/S/D
            access_bytes = std::size_t{1} << size;
            is_load = false;
            valid_size = true;
        } else if (opc == 0b01) { // LDR B/H/S/D
            access_bytes = std::size_t{1} << size;
            is_load = true;
            valid_size = true;
        } else if (opc == 0b10 && size == 0) { // STR Q
            access_bytes = 16;
            is_load = false;
            valid_size = true;
        } else if (opc == 0b11 && size == 0) { // LDR Q
            access_bytes = 16;
            is_load = true;
            valid_size = true;
        }

        if (valid_size) {
            std::uint64_t target_address = 0;
            std::uint64_t updated_base = 0;
            bool writeback = false;

            // Addressing mode 1: Unsigned scaled immediate
            // Bits 25:24 = 01
            if ((instruction & 0x3F000000u) == 0x3D000000u) {
                const std::uint64_t imm12 = (instruction >> 10u) & 0xFFFu;
                const std::uint64_t offset = imm12 * access_bytes;
                target_address = checked_add_address(
                    read_stack_pointer_operand(*this, rn), offset,
                    "SIMD/FP unsigned offset address");
            }
            // Addressing mode 2: Register offset
            // Bits 25:24 = 00, bit 21 = 1, bits 11:10 = 10
            else if ((instruction & 0x3F200C00u) == 0x3C200800u) {
                const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
                const std::uint32_t option = (instruction >> 13u) & 0x7u;
                const bool scale = (instruction & (1u << 12u)) != 0;
                if ((option & 2u) == 0) {
                    throw std::runtime_error("Unsupported register offset option");
                }
                const std::uint32_t shift_amount = scale ? static_cast<std::uint32_t>(std::countr_zero(access_bytes)) : 0u;
                const std::uint64_t offset = extend_register_offset(
                    read_zero_register_operand(*this, rm), option, shift_amount);
                target_address = add_extended_offset_address(
                    read_stack_pointer_operand(*this, rn), offset,
                    option == 6u || option == 7u,
                    "SIMD/FP reg offset address");
            }
            // Addressing mode 3: Unscaled immediate / Pre-indexed / Post-indexed
            // Bits 25:24 = 00, bit 21 = 0
            else if ((instruction & 0x3F200000u) == 0x3C000000u) {
                const std::uint32_t mode = (instruction >> 10u) & 0x3u;
                if (mode == 0b10) {
                    throw std::runtime_error("Unsupported SIMD/FP immediate mode");
                }
                std::int64_t simm9 = (instruction >> 12u) & 0x1FFu;
                if (simm9 & 0x100) simm9 -= 0x200;

                const std::uint64_t base_val = read_stack_pointer_operand(*this, rn);
                std::uint64_t offset_addr = 0;
                if (simm9 >= 0) {
                    offset_addr = checked_add_address(
                        base_val, static_cast<std::uint64_t>(simm9),
                        "SIMD/FP imm9 address");
                } else {
                    offset_addr = checked_sub_address(
                        base_val, static_cast<std::uint64_t>(-simm9),
                        "SIMD/FP imm9 address");
                }

                if (mode == 0b00) { // Unscaled (LDUR / STUR)
                    target_address = offset_addr;
                } else if (mode == 0b01) { // Post-indexed
                    target_address = base_val;
                    updated_base = offset_addr;
                    writeback = true;
                } else if (mode == 0b11) { // Pre-indexed
                    target_address = offset_addr;
                    updated_base = offset_addr;
                    writeback = true;
                }
            } else {
                throw std::runtime_error("Unsupported SIMD/FP load/store encoding");
            }

            const std::size_t mem_offset = pc_as_memory_offset(target_address);
            // Phase 36: a 128-bit SIMD/FP access requires 16-byte alignment
            // (checked here because the transfer is issued as two 8-byte
            // element accesses).
            if (access_bytes == 16) {
                memory.check_access(target_address, 16u,
                                    is_load ? AccessType::Read
                                            : AccessType::Write);
            }
            if (is_load) {
                if (access_bytes == 1) {
                    write_b_register(rt, memory.read_u8(mem_offset));
                } else if (access_bytes == 2) {
                    write_h_register(rt, memory.read_u16(mem_offset));
                } else if (access_bytes == 4) {
                    write_s_register(rt, memory.read_u32(mem_offset));
                } else if (access_bytes == 8) {
                    write_d_register(rt, memory.read_u64(mem_offset));
                } else if (access_bytes == 16) {
                    if (mem_offset + 16 > memory.size() || mem_offset + 16 < mem_offset) {
                        throw std::out_of_range("Memory access out of range");
                    }
                    const std::uint64_t low = memory.read_u64(mem_offset);
                    const std::uint64_t high = memory.read_u64(mem_offset + 8);
                    write_vector_register(rt, VectorRegister{low, high});
                }
            } else {
                if (access_bytes == 1) {
                    memory.write_u8(mem_offset, read_b_register(rt));
                    note_normal_store(mem_offset, 1);
                } else if (access_bytes == 2) {
                    memory.write_u16(mem_offset, read_h_register(rt));
                    note_normal_store(mem_offset, 2);
                } else if (access_bytes == 4) {
                    memory.write_u32(mem_offset, read_s_register(rt));
                    note_normal_store(mem_offset, 4);
                } else if (access_bytes == 8) {
                    memory.write_u64(mem_offset, read_d_register(rt));
                    note_normal_store(mem_offset, 8);
                } else if (access_bytes == 16) {
                    if (mem_offset + 16 > memory.size() || mem_offset + 16 < mem_offset) {
                        throw std::out_of_range("Memory access out of range");
                    }
                    const VectorRegister v = read_vector_register(rt);
                    memory.write_u64(mem_offset, v.low);
                    memory.write_u64(mem_offset + 8, v.high);
                    note_normal_store(mem_offset, 16);
                }
            }

            if (writeback) {
                if (rn == CPU::kRegisterCount) {
                    set_stack_pointer(updated_base);
                } else {
                    write_register(rn, updated_base);
                }
            }

            program_counter_ = checked_add_address(
                instruction_address, 4u, "Instruction PC increment");
            emit_trace();
            return;
        }
    }

    // Phase 32C/32D: Scalar Floating-Point Instructions
    // 1. Floating-point 2-source (FADD, FSUB, FMUL, FDIV)
    if ((instruction & 0xFF200C00u) == 0x1E200800u) {
        const std::uint32_t ftype = (instruction >> 22u) & 0x3u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t opcode = (instruction >> 12u) & 0xFu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;

        if (ftype == 0) { // Single precision
            const float a = std::bit_cast<float>(read_s_register(rn));
            const float b = std::bit_cast<float>(read_s_register(rm));
            float res = 0.0f;
            if (opcode == 0) res = a * b;
            else if (opcode == 1) res = a / b;
            else if (opcode == 2) res = a + b;
            else if (opcode == 3) res = a - b;
            else throw std::runtime_error("Unsupported FP 2-source opcode");
            write_s_register(rd, std::bit_cast<std::uint32_t>(res));
        } else if (ftype == 1) { // Double precision
            const double a = std::bit_cast<double>(read_d_register(rn));
            const double b = std::bit_cast<double>(read_d_register(rm));
            double res = 0.0;
            if (opcode == 0) res = a * b;
            else if (opcode == 1) res = a / b;
            else if (opcode == 2) res = a + b;
            else if (opcode == 3) res = a - b;
            else throw std::runtime_error("Unsupported FP 2-source opcode");
            write_d_register(rd, std::bit_cast<std::uint64_t>(res));
        } else {
            throw std::runtime_error("Unsupported FP ftype");
        }

        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    // 2. Floating-point 1-source (FMOV, FABS, FNEG, FSQRT, FCVT, FRINT*)
    if ((instruction & 0xFF207C00u) == 0x1E204000u) {
        const std::uint32_t ftype = (instruction >> 22u) & 0x3u;
        const std::uint32_t opcode = (instruction >> 15u) & 0x3Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;

        if (opcode == 4 && ftype == 1) { // FCVT S, D (Double to Single)
            const double src = std::bit_cast<double>(read_d_register(rn));
            const float res = static_cast<float>(src);
            write_s_register(rd, std::bit_cast<std::uint32_t>(res));
        } else if (opcode == 5 && ftype == 0) { // FCVT D, S (Single to Double)
            const float src = std::bit_cast<float>(read_s_register(rn));
            const double res = static_cast<double>(src);
            write_d_register(rd, std::bit_cast<std::uint64_t>(res));
        } else if (ftype == 0) { // Single precision 1-source
            const float src = std::bit_cast<float>(read_s_register(rn));
            float res = 0.0f;
            if (opcode == 0) res = src; // FMOV
            else if (opcode == 1) res = std::fabs(src); // FABS
            else if (opcode == 2) res = -src; // FNEG
            else if (opcode == 3) res = std::sqrt(src); // FSQRT
            else if (opcode == 8) res = fp_round_ties_even(src); // FRINTN
            else if (opcode == 9) res = std::ceil(src); // FRINTP
            else if (opcode == 10) res = std::floor(src); // FRINTM
            else if (opcode == 11) res = std::trunc(src); // FRINTZ
            else if (opcode == 12) res = std::round(src); // FRINTA
            else throw std::runtime_error("Unsupported FP 1-source opcode");
            write_s_register(rd, std::bit_cast<std::uint32_t>(res));
        } else if (ftype == 1) { // Double precision 1-source
            const double src = std::bit_cast<double>(read_d_register(rn));
            double res = 0.0;
            if (opcode == 0) res = src; // FMOV
            else if (opcode == 1) res = std::fabs(src); // FABS
            else if (opcode == 2) res = -src; // FNEG
            else if (opcode == 3) res = std::sqrt(src); // FSQRT
            else if (opcode == 8) res = fp_round_ties_even(src); // FRINTN
            else if (opcode == 9) res = std::ceil(src); // FRINTP
            else if (opcode == 10) res = std::floor(src); // FRINTM
            else if (opcode == 11) res = std::trunc(src); // FRINTZ
            else if (opcode == 12) res = std::round(src); // FRINTA
            else throw std::runtime_error("Unsupported FP 1-source opcode");
            write_d_register(rd, std::bit_cast<std::uint64_t>(res));
        } else {
            throw std::runtime_error("Unsupported FP ftype");
        }

        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    // 3. Floating-point Compare (FCMP, FCMPE)
    if ((instruction & 0xFF20FC07u) == 0x1E202000u) {
        const std::uint32_t ftype = (instruction >> 22u) & 0x3u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const bool zero = (instruction & (1u << 3u)) != 0;

        if (ftype == 0) { // Single precision
            const float a = std::bit_cast<float>(read_s_register(rn));
            const float b = zero ? 0.0f : std::bit_cast<float>(read_s_register(rm));
            nzcv_ = fp_compare_flags(a, b);
        } else if (ftype == 1) { // Double precision
            const double a = std::bit_cast<double>(read_d_register(rn));
            const double b = zero ? 0.0 : std::bit_cast<double>(read_d_register(rm));
            nzcv_ = fp_compare_flags(a, b);
        } else {
            throw std::runtime_error("Unsupported FP compare ftype");
        }

        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    // 4. Floating-point Immediate (FMOV S/D, #imm8)
    if ((instruction & 0xFF201FE0u) == 0x1E201000u) {
        const std::uint32_t ftype = (instruction >> 22u) & 0x3u;
        const std::uint8_t imm8 = static_cast<std::uint8_t>((instruction >> 13u) & 0xFFu);
        const std::uint32_t rd = instruction & 0x1Fu;

        if (ftype == 0) {
            const std::uint64_t val = vfp_expand_imm(2, imm8);
            write_s_register(rd, static_cast<std::uint32_t>(val));
        } else if (ftype == 1) {
            const std::uint64_t val = vfp_expand_imm(3, imm8);
            write_d_register(rd, val);
        } else {
            throw std::runtime_error("Unsupported FP immediate ftype");
        }

        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    // 5. Conversion between FP and Integer / FMOV with General Register
    if ((instruction & 0x7F20FC00u) == 0x1E200000u) {
        const bool sf = (instruction & 0x80000000u) != 0;
        const std::uint32_t ftype = (instruction >> 22u) & 0x3u;
        const std::uint32_t rmode = (instruction >> 19u) & 0x3u;
        const std::uint32_t opcode = (instruction >> 16u) & 0x7u;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;

        if (rmode == 0b00) {
            if (opcode == 0b010) { // SCVTF
                if (sf) {
                    const std::int64_t int_val = static_cast<std::int64_t>(read_zero_register_operand(*this, rn));
                    if (ftype == 0) write_s_register(rd, std::bit_cast<std::uint32_t>(static_cast<float>(int_val)));
                    else write_d_register(rd, std::bit_cast<std::uint64_t>(static_cast<double>(int_val)));
                } else {
                    const std::int32_t int_val = static_cast<std::int32_t>(read_zero_register_operand(*this, rn));
                    if (ftype == 0) write_s_register(rd, std::bit_cast<std::uint32_t>(static_cast<float>(int_val)));
                    else write_d_register(rd, std::bit_cast<std::uint64_t>(static_cast<double>(int_val)));
                }
            } else if (opcode == 0b011) { // UCVTF
                if (sf) {
                    const std::uint64_t uint_val = read_zero_register_operand(*this, rn);
                    if (ftype == 0) write_s_register(rd, std::bit_cast<std::uint32_t>(static_cast<float>(uint_val)));
                    else write_d_register(rd, std::bit_cast<std::uint64_t>(static_cast<double>(uint_val)));
                } else {
                    const std::uint32_t uint_val = static_cast<std::uint32_t>(read_zero_register_operand(*this, rn));
                    if (ftype == 0) write_s_register(rd, std::bit_cast<std::uint32_t>(static_cast<float>(uint_val)));
                    else write_d_register(rd, std::bit_cast<std::uint64_t>(static_cast<double>(uint_val)));
                }
            } else if (opcode == 0b110) { // FMOV FP to GP
                if (!sf && ftype == 0) { // FMOV Wd, Sn
                    write_zero_register_result(*this, rd, read_s_register(rn));
                } else if (sf && ftype == 1) { // FMOV Xd, Dn
                    write_zero_register_result(*this, rd, read_d_register(rn));
                } else {
                    throw std::runtime_error("Unsupported FMOV FP to GP combination");
                }
            } else if (opcode == 0b111) { // FMOV GP to FP
                if (!sf && ftype == 0) { // FMOV Sd, Wn
                    write_s_register(rd, static_cast<std::uint32_t>(read_zero_register_operand(*this, rn)));
                } else if (sf && ftype == 1) { // FMOV Dd, Xn
                    write_d_register(rd, read_zero_register_operand(*this, rn));
                } else {
                    throw std::runtime_error("Unsupported FMOV GP to FP combination");
                }
            } else {
                throw std::runtime_error("Unsupported FP/integer conversion opcode");
            }
        } else if (rmode == 0b11) {
            if (opcode == 0b000) { // FCVTZS
                if (ftype == 0) {
                    const float src = std::bit_cast<float>(read_s_register(rn));
                    if (sf) write_zero_register_result(*this, rd, static_cast<std::uint64_t>(fcvt_to_signed_sat<std::int64_t>(src, fpsr_)));
                    else write_zero_register_result(*this, rd, static_cast<std::uint32_t>(fcvt_to_signed_sat<std::int32_t>(src, fpsr_)));
                } else if (ftype == 1) {
                    const double src = std::bit_cast<double>(read_d_register(rn));
                    if (sf) write_zero_register_result(*this, rd, static_cast<std::uint64_t>(fcvt_to_signed_sat<std::int64_t>(src, fpsr_)));
                    else write_zero_register_result(*this, rd, static_cast<std::uint32_t>(fcvt_to_signed_sat<std::int32_t>(src, fpsr_)));
                }
            } else if (opcode == 0b001) { // FCVTZU
                if (ftype == 0) {
                    const float src = std::bit_cast<float>(read_s_register(rn));
                    if (sf) write_zero_register_result(*this, rd, fcvt_to_unsigned_sat<std::uint64_t>(src, fpsr_));
                    else write_zero_register_result(*this, rd, fcvt_to_unsigned_sat<std::uint32_t>(src, fpsr_));
                } else if (ftype == 1) {
                    const double src = std::bit_cast<double>(read_d_register(rn));
                    if (sf) write_zero_register_result(*this, rd, fcvt_to_unsigned_sat<std::uint64_t>(src, fpsr_));
                    else write_zero_register_result(*this, rd, fcvt_to_unsigned_sat<std::uint32_t>(src, fpsr_));
                }
            } else {
                throw std::runtime_error("Unsupported FCVT opcode");
            }
        } else {
            throw std::runtime_error("Unsupported FP conversion rmode");
        }

        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    // Phase 35: ERET — exception return.
    // Restores PC from ELR_EL1 and PSTATE from SPSR_EL1.
    // Phase 35 + Phase 40: ERET
    if (instruction == kEretEncoding) {
        if (current_el_ == 0) {
            if (architectural_exceptions_) {
                raise_synchronous_exception(
                    make_undefined_instruction(instruction_address));
                return;
            }
            throw std::runtime_error("Privilege violation: ERET at EL0");
        }
        const std::uint32_t spsr = (current_el_ == 2) ? spsr_el2_ : spsr_el1_;
        const std::uint32_t m    = spsr & 0x1Fu;
        const std::uint64_t new_pc = (current_el_ == 2) ? elr_el2_ : elr_el1_;
        const std::uint32_t target_el = (m >> 2u) & 0x3u;
        const bool valid_m = (current_el_ == 2)
            ? (m == 0x00u || m == 0x04u || m == 0x05u || m == 0x08u || m == 0x09u)
            : (m == 0x00u || m == 0x04u || m == 0x05u);
        if (!valid_m || target_el > current_el_) {
            if (architectural_exceptions_) {
                nzcv_       = static_cast<std::uint8_t>((spsr >> 28u) & 0xFu);
                daif_       = static_cast<std::uint8_t>((spsr >>  6u) & 0xFu);
                il_         = true;
                program_counter_ = new_pc;
                emit_trace();
                return;
            }
            throw std::runtime_error("ERET: invalid SPSR M field: 0x" + std::to_string(m));
        }
        nzcv_       = static_cast<std::uint8_t>((spsr >> 28u) & 0xFu);
        daif_       = static_cast<std::uint8_t>((spsr >>  6u) & 0xFu);
        il_         = ((spsr >> 20u) & 0x1u) != 0u;
        current_el_ = target_el;
        sp_sel_     = (m & 0x1u) != 0u;
        program_counter_ = new_pc;
        emit_trace();
        return;
    }

    // Phase 35/36: SVC
    if ((instruction & kSvcMask) == kSvcPattern) {
        const std::uint32_t imm16 = (instruction >> 5u) & 0xFFFFu;
        SynchronousException exception =
            make_svc(instruction_address, static_cast<std::uint16_t>(imm16));
        exception.instruction_length_4 = architectural_exceptions_;
        raise_synchronous_exception(exception);
        emit_trace();
        return;
    }

    // Phase 40: HVC - hypervisor call.
    constexpr std::uint32_t kHvcMask = 0xFFE0001Fu;
    constexpr std::uint32_t kHvcPattern = 0xD4000002u;
    if ((instruction & kHvcMask) == kHvcPattern) {
        const std::uint32_t imm16 = (instruction >> 5u) & 0xFFFFu;
        if (current_el_ == 0) {
            if (architectural_exceptions_) {
                raise_synchronous_exception(make_undefined_instruction(instruction_address));
                return;
            }
            throw std::runtime_error("Privilege violation: HVC at EL0");
        }
        SynchronousException exception = make_hvc(instruction_address, static_cast<std::uint16_t>(imm16));
        exception.instruction_length_4 = true;
        raise_el2_exception(exception, current_el_ < 2);
        emit_trace();
        return;
    }

    // Phase 35: MSR immediate (PSTATE fields): SPSel, DAIFSet, DAIFClr.
    // Encoding: bits[31:12]=0xD500_4, bits[4:0]=0x1F (Rt=XZR=11111).
    // Bits variable: op1[18:16], CRm[11:8], op2[7:5].
    if ((instruction & kMsrImmMask) == kMsrImmBase) {
        const std::uint32_t op1 = (instruction >> 16u) & 0x7u;
        const std::uint32_t crm = (instruction >>  8u) & 0xFu;
        const std::uint32_t op2 = (instruction >>  5u) & 0x7u;
        if (current_el_ == 0) {
            if (architectural_exceptions_) {
                raise_synchronous_exception(
                    make_undefined_instruction(instruction_address));
                return;
            }
            throw std::runtime_error("Privilege violation: MSR immediate at EL0");
        }
        bool handled = false;
        if (op1 == 0u && op2 == 5u) {
            // MSR SPSel, #imm1: set or clear SPSel bit.
            // Only architectural in EL1+ (trap at EL0 is optional; we allow it).
            sp_sel_ = (crm & 0x1u) != 0u;
            handled = true;
        } else if (op1 == 3u && op2 == 6u) {
            // MSR DAIFSet, #imm4: set (mask) DAIF bits.
            daif_ |= static_cast<std::uint8_t>(crm & 0xFu);
            handled = true;
        } else if (op1 == 3u && op2 == 7u) {
            // MSR DAIFClr, #imm4: clear (unmask) DAIF bits.
            daif_ &= static_cast<std::uint8_t>(~crm & 0xFu);
            handled = true;
        }
        if (handled) {
            program_counter_ = checked_add_address(
                instruction_address, 4u, "Instruction PC increment");
            emit_trace();
            return;
        }
    }

    // Phase 38: System Instructions (op0=1, L=0: Hints, Barriers, Cache, TLBI)
    if ((instruction & 0xFF800000u) == 0xD5000000u && ((instruction >> 20u) & 0x3u) == 0) {
        const std::uint32_t op1 = (instruction >> 16u) & 0x7u;
        const std::uint32_t crn = (instruction >> 12u) & 0xFu;
        const std::uint32_t crm = (instruction >> 8u) & 0xFu;
        const std::uint32_t op2 = (instruction >> 5u) & 0x7u;
        const std::uint32_t rt  = instruction & 0x1Fu;

        // Hints (op1=3, CRn=2, Rt=31): HINT #imm7 where imm7 = (CRm << 3) | op2
        if (op1 == 3u && crn == 2u && rt == 31u) {
            const std::uint32_t imm7 = (crm << 3u) | op2;
            if (imm7 == 2u) { // WFE
                if (event_register_) {
                    event_register_ = false;
                } else if (architectural_exceptions_) {
                    check_timer_status();
                    const bool irq = (phys_timer_.enable && phys_timer_.istatus && !phys_timer_.imask)
                                  || (virt_timer_.enable && virt_timer_.istatus && !virt_timer_.imask);
                    if (irq && !(daif_ & 0x2u)) {
                        program_counter_ = checked_add_address(
                            instruction_address, 4u, "Instruction PC increment");
                        raise_asynchronous_exception(false);
                        emit_trace();
                        return;
                    }
                }
            } else if (imm7 == 3u) { // WFI
                if (architectural_exceptions_) {
                    check_timer_status();
                    const bool irq = (phys_timer_.enable && phys_timer_.istatus && !phys_timer_.imask)
                                  || (virt_timer_.enable && virt_timer_.istatus && !virt_timer_.imask);
                    if (irq && !(daif_ & 0x2u)) {
                        program_counter_ = checked_add_address(
                            instruction_address, 4u, "Instruction PC increment");
                        raise_asynchronous_exception(false);
                        emit_trace();
                        return;
                    }
                }
            } else if (imm7 == 4u) { // SEV
                event_register_ = true;
            } else if (imm7 == 5u) { // SEVL
                event_register_ = true;
            }
            program_counter_ = checked_add_address(
                instruction_address, 4u, "Instruction PC increment");
            emit_trace();
            return;
        }

        // Barriers and CLREX (op1=3, CRn=3)
        if (op1 == 3u && crn == 3u) {
            if (op2 == 2u) {
                // CLREX (op2=2, CRm=15, Rt=31)
                if (crm == 15u && rt == 31u) {
                    clear_exclusive_monitor();
                    program_counter_ = checked_add_address(
                        instruction_address, 4u, "Instruction PC increment");
                    emit_trace();
                    return;
                }
                if (architectural_exceptions_) {
                    raise_synchronous_exception(make_undefined_instruction(instruction_address));
                    return;
                }
                throw std::runtime_error("Unsupported CLREX encoding");
            }
            if (op2 == 4u) {
                // DSB <opt>
                if (rt == 31u) {
                    std::atomic_thread_fence(std::memory_order_seq_cst);
                    program_counter_ = checked_add_address(
                        instruction_address, 4u, "Instruction PC increment");
                    emit_trace();
                    return;
                }
                if (architectural_exceptions_) {
                    raise_synchronous_exception(make_undefined_instruction(instruction_address));
                    return;
                }
                throw std::runtime_error("Unsupported DSB encoding");
            }
            if (op2 == 5u) {
                // DMB <opt>
                if (rt == 31u) {
                    std::atomic_thread_fence(std::memory_order_seq_cst);
                    program_counter_ = checked_add_address(
                        instruction_address, 4u, "Instruction PC increment");
                    emit_trace();
                    return;
                }
                if (architectural_exceptions_) {
                    raise_synchronous_exception(make_undefined_instruction(instruction_address));
                    return;
                }
                throw std::runtime_error("Unsupported DMB encoding");
            }
            if (op2 == 6u) {
                // ISB {<opt>}
                if (rt == 31u) {
                    std::atomic_thread_fence(std::memory_order_seq_cst);
                    program_counter_ = checked_add_address(
                        instruction_address, 4u, "Instruction PC increment");
                    emit_trace();
                    return;
                }
                if (architectural_exceptions_) {
                    raise_synchronous_exception(make_undefined_instruction(instruction_address));
                    return;
                }
                throw std::runtime_error("Unsupported ISB encoding");
            }
        }

        // Cache Maintenance Instructions (CRn=7)
        if (crn == 7u) {
            // DC ZVA, Xt: op1=3, CRn=7, CRm=4, op2=1
            if (op1 == 3u && crm == 4u && op2 == 1u) {
                if (current_el_ == 0 && ((mmu_.get_sctlr_el1() & (1ULL << 14u)) == 0)) {
                    if (architectural_exceptions_) {
                        raise_synchronous_exception(make_undefined_instruction(instruction_address));
                        return;
                    }
                    throw std::runtime_error("Privilege violation: DC ZVA executed at EL0 with SCTLR_EL1.DZE=0");
                }
                const std::uint64_t va = read_zero_register_operand(*this, rt);
                const std::uint64_t block_va = va & ~63ULL;
                memory.check_range(block_va, 64u, AccessType::Write);
                for (std::size_t offset = 0; offset < 64u; offset += 8u) {
                    memory.write_u64(static_cast<std::size_t>(block_va + offset), 0ULL);
                }
                note_normal_store(block_va, 64u);
                program_counter_ = checked_add_address(
                    instruction_address, 4u, "Instruction PC increment");
                emit_trace();
                return;
            }

            if (op1 == 0u && current_el_ == 0) {
                if (architectural_exceptions_) {
                    raise_synchronous_exception(make_undefined_instruction(instruction_address));
                    return;
                }
                throw std::runtime_error("Privilege violation: EL1 cache instruction executed at EL0");
            }

            if ((op1 == 3u && crm == 10u && op2 == 1u) || // DC CVAC
                (op1 == 3u && crm == 11u && op2 == 1u) || // DC CVAU
                (op1 == 3u && crm == 14u && op2 == 1u) || // DC CIVAC
                (op1 == 3u && crm == 5u  && op2 == 1u)) {  // IC IVAU
                const std::uint64_t va = read_zero_register_operand(*this, rt);
                memory.check_range(va & ~63ULL, 64u, AccessType::Read);
                program_counter_ = checked_add_address(
                    instruction_address, 4u, "Instruction PC increment");
                emit_trace();
                return;
            }
            if (op1 == 0u && crm == 6u && op2 == 1u) { // DC IVAC
                const std::uint64_t va = read_zero_register_operand(*this, rt);
                memory.check_range(va & ~63ULL, 64u, AccessType::Write);
                program_counter_ = checked_add_address(
                    instruction_address, 4u, "Instruction PC increment");
                emit_trace();
                return;
            }
            if (op1 == 0u && op2 == 0u && rt == 31u && (crm == 5u || crm == 1u)) { // IC IALLU, IC IALLUIS
                program_counter_ = checked_add_address(
                    instruction_address, 4u, "Instruction PC increment");
                emit_trace();
                return;
            }
        }

        // TLBI Instructions (op1=0, CRn=8)
        if (op1 == 0 && crn == 8) {
            if (current_el_ == 0) {
                if (architectural_exceptions_) {
                    raise_synchronous_exception(
                        make_undefined_instruction(instruction_address));
                    return;
                }
                throw std::runtime_error("Privilege violation: TLBI instruction executed at EL0");
            }
            const std::uint64_t reg_val = read_zero_register_operand(*this, rt);
            if (crm == 7 || crm == 3) {
                switch (op2) {
                case 0: // TLBI VMALLE1 / VMALLE1IS
                    mmu_.invalidate_tlb_all();
                    break;
                case 1: { // TLBI VAE1 / VAE1IS, Xt
                    const std::uint16_t asid = static_cast<std::uint16_t>((reg_val >> 48u) & 0xFFFFu);
                    const std::uint64_t va = (reg_val & 0x0000FFFFFFFFFFFFULL) << 12u;
                    mmu_.invalidate_tlb_va(va, asid, false);
                    break;
                }
                case 2: { // TLBI ASIDE1 / ASIDE1IS, Xt
                    const std::uint16_t asid = static_cast<std::uint16_t>((reg_val >> 48u) & 0xFFFFu);
                    mmu_.invalidate_tlb_asid(asid);
                    break;
                }
                case 3: { // TLBI VAAE1 / VAAE1IS, Xt
                    const std::uint64_t va = (reg_val & 0x0000FFFFFFFFFFFFULL) << 12u;
                    mmu_.invalidate_tlb_vaa(va, false);
                    break;
                }
                case 5: { // TLBI VALE1 / VALE1IS, Xt
                    const std::uint16_t asid = static_cast<std::uint16_t>((reg_val >> 48u) & 0xFFFFu);
                    const std::uint64_t va = (reg_val & 0x0000FFFFFFFFFFFFULL) << 12u;
                    mmu_.invalidate_tlb_va(va, asid, true);
                    break;
                }
                case 7: { // TLBI VAALE1 / VAALE1IS, Xt
                    const std::uint64_t va = (reg_val & 0x0000FFFFFFFFFFFFULL) << 12u;
                    mmu_.invalidate_tlb_vaa(va, true);
                    break;
                }
                default:
                    if (architectural_exceptions_) {
                        raise_synchronous_exception(
                            make_undefined_instruction(instruction_address));
                        return;
                    }
                    throw std::runtime_error("Unsupported TLBI encoding");
                }
            } else {
                if (architectural_exceptions_) {
                    raise_synchronous_exception(
                        make_undefined_instruction(instruction_address));
                    return;
                }
                throw std::runtime_error("Unsupported SYS encoding");
            }
            program_counter_ = checked_add_address(
                instruction_address, 4u, "Instruction PC increment");
            emit_trace();
            return;
        }

        if (architectural_exceptions_) {
            raise_synchronous_exception(
                make_undefined_instruction(instruction_address));
            return;
        }
        throw std::runtime_error("Unsupported SYS encoding");
    }
    // Phase 35 + existing Phase 32: Generic system register MRS/MSR (op0=3).
    // Supersedes the narrow FPCR/FPSR-only handler from Phase 32.
    if (is_sysreg_instr(instruction)) {
        const bool is_read = (instruction & (1u << 21u)) != 0u;
        const std::uint32_t rt = instruction & 0x1Fu;
        const std::uint16_t key = sysreg_key(instruction);
        auto can_access_el0 = [&](std::uint16_t reg_key, bool is_mrs) noexcept -> bool {
            if (reg_key == kSysregNZCV || reg_key == kSysregFPCR || reg_key == kSysregFPSR) {
                return true;
            }
            if (reg_key == kSysregDCZID_EL0) {
                return is_mrs; // read-only
            }
            if (reg_key == kSysregCNTFRQ_EL0) {
                return is_mrs && ((cntkctl_el1_ & 0x3u) != 0u);
            }
            if (reg_key == kSysregCNTPCT_EL0) {
                return is_mrs && ((cntkctl_el1_ & (1u << 0)) != 0u);
            }
            if (reg_key == kSysregCNTVCT_EL0) {
                return is_mrs && ((cntkctl_el1_ & (1u << 1)) != 0u);
            }
            if (reg_key == kSysregCNTP_CTL || reg_key == kSysregCNTP_CVAL || reg_key == kSysregCNTP_TVAL) {
                return (cntkctl_el1_ & (1u << 9)) != 0u;
            }
            if (reg_key == kSysregCNTV_CTL || reg_key == kSysregCNTV_CVAL || reg_key == kSysregCNTV_TVAL) {
                return (cntkctl_el1_ & (1u << 8)) != 0u;
            }
            return false;
        };
        const std::uint32_t op1 = (instruction >> 16u) & 0x7u;
        const bool is_el2_reg = (op1 == 4u || op1 == 5u) || (key == kSysregSP_EL2);
        if (is_el2_reg && current_el_ < 2) {
            if (architectural_exceptions_) {
                raise_synchronous_exception(
                    make_undefined_instruction(instruction_address));
                return;
            }
            throw std::runtime_error("Privilege violation: EL2 system register accessed below EL2");
        }

        if (current_el_ == 0 && !can_access_el0(key, is_read)) {
            if (architectural_exceptions_) {
                raise_synchronous_exception(
                    make_undefined_instruction(instruction_address));
                return;
            }
            throw std::runtime_error("Privilege violation: system register accessed at EL0");
        }

        if (is_read) {
            // Phase 40: Timer traps at EL1 via CNTHCTL_EL2 (reads only)
            if (current_el_ == 1 && architectural_exceptions_) {
                if (key == kSysregCNTPCT_EL0 && !(cnthctl_el2_ & 1ULL)) {
                    SynchronousException ex = make_trapped_sysreg(instruction_address, 0x1800000u | (instruction & 0x1FFFFFFu));
                    raise_el2_exception(ex, true);
                    return;
                }
                if ((key == kSysregCNTP_CTL || key == kSysregCNTP_CVAL || key == kSysregCNTP_TVAL) && !(cnthctl_el2_ & 2ULL)) {
                    SynchronousException ex = make_trapped_sysreg(instruction_address, 0x1800000u | (instruction & 0x1FFFFFFu));
                    raise_el2_exception(ex, true);
                    return;
                }
            }
            // MRS: read system register → Xt
            std::uint64_t value = 0;
            switch (key) {
            case kSysregNZCV:
                value = static_cast<std::uint64_t>(nzcv_) << 28u;
                break;
            case kSysregDAIF:
                value = static_cast<std::uint64_t>(daif_) << 6u;
                break;
            case kSysregCurrentEL:
                // CurrentEL[3:2] = EL, rest = 0.
                value = (current_el_ & 0x3u) << 2u;
                break;
            case kSysregSPSR_EL1:
                value = spsr_el1_;
                break;
            case kSysregELR_EL1:
                value = elr_el1_;
                break;
            case kSysregSP_EL0:
                value = sp_el0_;
                break;
            case kSysregSPSel:
                value = sp_sel_ ? 1u : 0u;
                break;
            case kSysregESR_EL1:
                value = esr_el1_;
                break;
            case kSysregFAR_EL1:
                value = far_el1_;
                break;
            case kSysregVBAR_EL1:
                value = vbar_el1_;
                break;
            case kSysregFPCR:
                value = fpcr_;
                break;
            case kSysregFPSR:
                value = fpsr_;
                break;
            case kSysregSCTLR_EL1:
                value = mmu_.get_sctlr_el1();
                break;
            case kSysregTCR_EL1:
                value = mmu_.get_tcr_el1();
                break;
            case kSysregTTBR0_EL1:
                value = mmu_.get_ttbr0_el1();
                break;
            case kSysregTTBR1_EL1:
                value = mmu_.get_ttbr1_el1();
                break;
            case kSysregMAIR_EL1:
                value = mmu_.get_mair_el1();
                break;
            case kSysregID_AA64MMFR0:
                value = mmu_.get_id_aa64mmfr0_el1();
                break;
            case kSysregPAR_EL1:
                value = mmu_.get_par_el1();
                break;
            case kSysregDCZID_EL0:
                value = 0x00000004ULL;
                break;
            case kSysregCNTFRQ_EL0:
                value = cntfrq_el0_;
                break;
            case kSysregCNTPCT_EL0:
                value = guest_counter_;
                break;
            case kSysregCNTVCT_EL0:
                value = guest_counter_ - cntvoff_el2_;
                break;
            case kSysregCNTKCTL_EL1:
                value = cntkctl_el1_;
                break;
            case kSysregCNTP_CTL:
                check_timer_status();
                value = (phys_timer_.enable ? 1ULL : 0ULL)
                      | (phys_timer_.imask ? 2ULL : 0ULL)
                      | (phys_timer_.istatus ? 4ULL : 0ULL);
                break;
            case kSysregCNTP_CVAL:
                value = phys_timer_.cval;
                break;
            case kSysregCNTP_TVAL: {
                std::int64_t diff = static_cast<std::int64_t>(phys_timer_.cval) - static_cast<std::int64_t>(guest_counter_);
                value = static_cast<std::uint32_t>(diff & 0xFFFFFFFFLL);
                break;
            }
            case kSysregCNTV_CTL:
                check_timer_status();
                value = (virt_timer_.enable ? 1ULL : 0ULL)
                      | (virt_timer_.imask ? 2ULL : 0ULL)
                      | (virt_timer_.istatus ? 4ULL : 0ULL);
                break;
            case kSysregCNTV_CVAL:
                value = virt_timer_.cval;
                break;
            case kSysregSP_EL1:
                value = (current_el_ == 1 && sp_sel_) ? get_stack_pointer() : sp_el1_;
                break;
            case kSysregSP_EL2:
                value = (current_el_ == 2 && sp_sel_) ? get_stack_pointer() : sp_el2_;
                break;
            case kSysregSPSR_EL2:
                value = spsr_el2_;
                break;
            case kSysregELR_EL2:
                value = elr_el2_;
                break;
            case kSysregESR_EL2:
                value = esr_el2_;
                break;
            case kSysregFAR_EL2:
                value = far_el2_;
                break;
            case kSysregHPFAR_EL2:
                value = hpfar_el2_;
                break;
            case kSysregVBAR_EL2:
                value = vbar_el2_;
                break;
            case kSysregHCR_EL2:
                value = hcr_el2_;
                break;
            case kSysregVTTBR_EL2:
                value = mmu_.get_vttbr_el2();
                break;
            case kSysregVTCR_EL2:
                value = mmu_.get_vtcr_el2();
                break;
            case kSysregCNTVOFF_EL2:
                value = cntvoff_el2_;
                break;
            case kSysregCNTHCTL_EL2:
                value = cnthctl_el2_;
                break;
            case kSysregCNTHP_CTL:
                check_timer_status();
                value = (cnthp_timer_.enable ? 1ULL : 0ULL)
                      | (cnthp_timer_.imask ? 2ULL : 0ULL)
                      | (cnthp_timer_.istatus ? 4ULL : 0ULL);
                break;
            case kSysregCNTHP_CVAL:
                value = cnthp_timer_.cval;
                break;
            case kSysregCNTHP_TVAL: {
                std::int64_t diff = static_cast<std::int64_t>(cnthp_timer_.cval) - static_cast<std::int64_t>(guest_counter_);
                value = static_cast<std::uint32_t>(diff & 0xFFFFFFFFLL);
                break;
            }
            case kSysregCNTV_TVAL: {
                std::int64_t diff = static_cast<std::int64_t>(virt_timer_.cval) - static_cast<std::int64_t>(guest_counter_);
                value = static_cast<std::uint32_t>(diff & 0xFFFFFFFFLL);
                break;
            }
            default:
                throw std::runtime_error(
                    "Unsupported MRS system register encoding: 0x"
                    + std::to_string(instruction));
            }
            write_zero_register_result(*this, rt, value);
        } else {
            // MSR: write system register ← Xt
            const std::uint64_t val = read_zero_register_operand(*this, rt);
            switch (key) {
            case kSysregNZCV:
                // Only NZCV [31:28] bits are architecturally significant.
                nzcv_ = static_cast<std::uint8_t>((val >> 28u) & 0xFu);
                break;
            case kSysregDAIF:
                // DAIF [9:6] bits.
                daif_ = static_cast<std::uint8_t>((val >> 6u) & 0xFu);
                break;
            case kSysregSPSR_EL1:
                spsr_el1_ = static_cast<std::uint32_t>(val & 0xFFFFFFFFu);
                break;
            case kSysregELR_EL1:
                elr_el1_ = val;
                break;
            case kSysregSP_EL0:
                sp_el0_ = val;
                break;
            case kSysregSPSel:
                sp_sel_ = (val & 0x1u) != 0u;
                break;
            case kSysregESR_EL1:
                esr_el1_ = static_cast<std::uint32_t>(val & 0xFFFFFFFFu);
                break;
            case kSysregFAR_EL1:
                far_el1_ = val;
                break;
            case kSysregVBAR_EL1:
                vbar_el1_ = val;
                break;
            case kSysregFPCR:
                fpcr_ = static_cast<std::uint32_t>(val & 0xFFFFFFFFu);
                break;
            case kSysregFPSR:
                fpsr_ = static_cast<std::uint32_t>(val & 0xFFFFFFFFu);
                break;
            case kSysregSCTLR_EL1:
                mmu_.set_sctlr_el1(val);
                break;
            case kSysregTCR_EL1:
                mmu_.set_tcr_el1(val);
                break;
            case kSysregTTBR0_EL1:
                mmu_.set_ttbr0_el1(val);
                break;
            case kSysregTTBR1_EL1:
                mmu_.set_ttbr1_el1(val);
                break;
            case kSysregMAIR_EL1:
                mmu_.set_mair_el1(val);
                break;
            case kSysregPAR_EL1:
                mmu_.set_par_el1(val);
                break;
            case kSysregDCZID_EL0:
                if (architectural_exceptions_) {
                    raise_synchronous_exception(
                        make_undefined_instruction(instruction_address));
                    return;
                }
                throw std::runtime_error("MSR to read-only DCZID_EL0 is unallocated");
            case kSysregCNTFRQ_EL0:
                cntfrq_el0_ = val;
                break;
            case kSysregCNTPCT_EL0:
                if (architectural_exceptions_) {
                    raise_synchronous_exception(
                        make_undefined_instruction(instruction_address));
                    return;
                }
                throw std::runtime_error("MSR to read-only CNTPCT_EL0 is unallocated");
            case kSysregCNTVCT_EL0:
                if (architectural_exceptions_) {
                    raise_synchronous_exception(
                        make_undefined_instruction(instruction_address));
                    return;
                }
                throw std::runtime_error("MSR to read-only CNTVCT_EL0 is unallocated");
            case kSysregCNTKCTL_EL1:
                cntkctl_el1_ = val;
                break;
            case kSysregCNTP_CTL:
                phys_timer_.enable = (val & 1u) != 0;
                phys_timer_.imask  = (val & 2u) != 0;
                check_timer_status();
                break;
            case kSysregCNTP_CVAL:
                phys_timer_.cval = val;
                check_timer_status();
                break;
            case kSysregCNTP_TVAL: {
                std::int64_t offset = static_cast<std::int64_t>(static_cast<std::int32_t>(val & 0xFFFFFFFFu));
                phys_timer_.cval = static_cast<std::uint64_t>(static_cast<std::int64_t>(guest_counter_) + offset);
                check_timer_status();
                break;
            }
            case kSysregCNTV_CTL:
                virt_timer_.enable = (val & 1u) != 0;
                virt_timer_.imask  = (val & 2u) != 0;
                check_timer_status();
                break;
            case kSysregCNTV_CVAL:
                virt_timer_.cval = val;
                check_timer_status();
                break;
            case kSysregCNTV_TVAL: {
                std::int64_t offset = static_cast<std::int64_t>(static_cast<std::int32_t>(val & 0xFFFFFFFFu));
                virt_timer_.cval = static_cast<std::uint64_t>(static_cast<std::int64_t>(guest_counter_ - cntvoff_el2_) + offset);
                check_timer_status();
                break;
            }
            case kSysregSP_EL1:
                if (current_el_ == 1 && sp_sel_) {
                    set_stack_pointer(val);
                } else {
                    sp_el1_ = val;
                }
                break;
            case kSysregSP_EL2:
                if (current_el_ == 2 && sp_sel_) {
                    set_stack_pointer(val);
                } else {
                    sp_el2_ = val;
                }
                break;
            case kSysregSPSR_EL2:
                spsr_el2_ = static_cast<std::uint32_t>(val & 0xFFFFFFFFu);
                break;
            case kSysregELR_EL2:
                elr_el2_ = val;
                break;
            case kSysregESR_EL2:
                esr_el2_ = static_cast<std::uint32_t>(val & 0xFFFFFFFFu);
                break;
            case kSysregFAR_EL2:
                far_el2_ = val;
                break;
            case kSysregHPFAR_EL2:
                hpfar_el2_ = val;
                break;
            case kSysregVBAR_EL2:
                vbar_el2_ = val;
                break;
            case kSysregHCR_EL2:
                hcr_el2_ = val;
                mmu_.set_hcr_el2(val);
                break;
            case kSysregVTTBR_EL2:
                mmu_.set_vttbr_el2(val);
                break;
            case kSysregVTCR_EL2:
                mmu_.set_vtcr_el2(val);
                break;
            case kSysregCNTVOFF_EL2:
                cntvoff_el2_ = val;
                break;
            case kSysregCNTHCTL_EL2:
                cnthctl_el2_ = val;
                break;
            case kSysregCNTHP_CTL:
                cnthp_timer_.enable = (val & 1u) != 0;
                cnthp_timer_.imask  = (val & 2u) != 0;
                check_timer_status();
                break;
            case kSysregCNTHP_CVAL:
                cnthp_timer_.cval = val;
                check_timer_status();
                break;
            case kSysregCNTHP_TVAL: {
                std::int64_t offset = static_cast<std::int64_t>(static_cast<std::int32_t>(val & 0xFFFFFFFFu));
                cnthp_timer_.cval = static_cast<std::uint64_t>(static_cast<std::int64_t>(guest_counter_) + offset);
                check_timer_status();
                break;
            }
            case kSysregID_AA64MMFR0:
                if (architectural_exceptions_) {
                    raise_synchronous_exception(
                        make_undefined_instruction(instruction_address));
                    return;
                }
                throw std::runtime_error("MSR to read-only ID_AA64MMFR0_EL1 is unallocated");
            case kSysregCurrentEL:
                // CurrentEL is architecturally read-only.
                if (architectural_exceptions_) {
                    raise_synchronous_exception(
                        make_undefined_instruction(instruction_address));
                    return;
                }
                throw std::runtime_error(
                    "MSR to read-only CurrentEL is unallocated");
            default:
                throw std::runtime_error(
                    "Unsupported MSR system register encoding: 0x"
                    + std::to_string(instruction));
            }
        }

        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    // Phase 32E: Advanced SIMD Foundation
    // 1. Vector logicals: AND, ORR, EOR, BIC, ORN
    if ((instruction & 0xBF20FC00u) == 0x0E201C00u  // AND, BIC, ORR, ORN
        || (instruction & 0xBF20FC00u) == 0x2E201C00u) { // EOR
        const bool q = (instruction & 0x40000000u) != 0;
        const bool bit29 = (instruction & 0x20000000u) != 0;
        const std::uint32_t opc = (instruction >> 22u) & 0x3u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;

        const VectorRegister n_val = read_vector_register(rn);
        const VectorRegister m_val = read_vector_register(rm);
        VectorRegister out{};

        if (!bit29 && opc == 0b00) { // AND
            out.low = n_val.low & m_val.low;
            out.high = q ? (n_val.high & m_val.high) : 0;
        } else if (!bit29 && opc == 0b01) { // BIC
            out.low = n_val.low & ~m_val.low;
            out.high = q ? (n_val.high & ~m_val.high) : 0;
        } else if (!bit29 && opc == 0b10) { // ORR
            out.low = n_val.low | m_val.low;
            out.high = q ? (n_val.high | m_val.high) : 0;
        } else if (!bit29 && opc == 0b11) { // ORN
            out.low = n_val.low | ~m_val.low;
            out.high = q ? (n_val.high | ~m_val.high) : 0;
        } else if (bit29 && opc == 0b00) { // EOR
            out.low = n_val.low ^ m_val.low;
            out.high = q ? (n_val.high ^ m_val.high) : 0;
        } else {
            throw std::runtime_error("Unsupported vector logical instruction");
        }

        write_vector_register(rd, out);
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    // Vector NOT (MVN)
    if ((instruction & 0xBF3FFC00u) == 0x2E205800u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;

        const VectorRegister n_val = read_vector_register(rn);
        VectorRegister out{};
        out.low = ~n_val.low;
        out.high = q ? ~n_val.high : 0;

        write_vector_register(rd, out);
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    // 2. Advanced SIMD Shift by Immediate
    if ((instruction & 0x9F800400u) == 0x0F000400u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool u = (instruction & 0x20000000u) != 0;
        const std::uint32_t immhb = (instruction >> 16u) & 0x7Fu;
        const std::uint32_t opcode = (instruction >> 11u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;

        unsigned esz = 0, shift = 0;
        if (immhb & 0x40u) {
            esz = 3;
            if (!q) throw std::runtime_error("1D shift immediate not supported");
            shift = (opcode == 0b01010) ? (immhb & 0x3Fu) : (64u - (immhb & 0x3Fu));
        } else if (immhb & 0x20u) {
            esz = 2;
            shift = (opcode == 0b01010) ? (immhb & 0x1Fu) : (32u - (immhb & 0x1Fu));
        } else if (immhb & 0x10u) {
            esz = 1;
            shift = (opcode == 0b01010) ? (immhb & 0x0Fu) : (16u - (immhb & 0x0Fu));
        } else if (immhb & 0x08u) {
            esz = 0;
            shift = (opcode == 0b01010) ? (immhb & 0x07u) : (8u - (immhb & 0x07u));
        } else {
            throw std::runtime_error("Reserved immh in shift immediate");
        }

        const VectorRegister n_val = read_vector_register(rn);
        VectorRegister out{};
        const unsigned lane_count = q ? (16u >> esz) : (8u >> esz);
        for (unsigned i = 0; i < lane_count; ++i) {
            const std::uint64_t val = get_vec_lane(n_val, i, esz);
            std::uint64_t res = 0;
            if (opcode == 0b01010 && !u) { // SHL
                res = (val << shift) & vec_cmp_mask(true, esz);
            } else if (opcode == 0b00000) { // SSHR (u=0), USHR (u=1)
                res = shift_right_imm_op(val, shift, esz, !u, false);
            } else if (opcode == 0b00100) { // SRSHR (u=0), URSHR (u=1)
                res = shift_right_imm_op(val, shift, esz, !u, true);
            } else {
                throw std::runtime_error("Unsupported shift immediate opcode");
            }
            set_vec_lane(out, i, esz, res);
        }
        if (!q) out.high = 0;
        write_vector_register(rd, out);
        program_counter_ = checked_add_address(instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    // 3. Advanced SIMD Across-Lanes Reductions
    if ((instruction & 0x9F3E0C00u) == 0x0E300800u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool u = (instruction & 0x20000000u) != 0;
        const std::uint32_t size = (instruction >> 22u) & 0x3u;
        const std::uint32_t opcode = (instruction >> 12u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;

        const unsigned elements = (q ? 16u : 8u) >> size;
        if (elements < 4) {
            throw std::runtime_error("Across-lanes reduction requires elements >= 4");
        }
        const VectorRegister n_val = read_vector_register(rn);
        std::uint64_t res = 0;
        if (opcode == 0b11011 && !u) { // ADDV
            std::uint64_t sum = 0;
            for (unsigned i = 0; i < elements; ++i) {
                sum += get_vec_lane(n_val, i, size);
            }
            res = sum & vec_cmp_mask(true, size);
        } else if (opcode == 0b00011) { // SADDLV (u=0), UADDLV (u=1)
            if (size >= 3) throw std::runtime_error("Invalid size for SADDLV/UADDLV");
            if (!u) { // SADDLV
                std::int64_t sum = 0;
                for (unsigned i = 0; i < elements; ++i) {
                    sum += sign_extend(get_vec_lane(n_val, i, size), size);
                }
                res = static_cast<std::uint64_t>(sum) & vec_cmp_mask(true, size + 1);
            } else { // UADDLV
                std::uint64_t sum = 0;
                for (unsigned i = 0; i < elements; ++i) {
                    sum += get_vec_lane(n_val, i, size);
                }
                res = sum & vec_cmp_mask(true, size + 1);
            }
        } else if (opcode == 0b01010) { // SMAXV (u=0), UMAXV (u=1)
            if (!u) {
                std::int64_t max_val = sign_extend(get_vec_lane(n_val, 0, size), size);
                for (unsigned i = 1; i < elements; ++i) {
                    const std::int64_t val = sign_extend(get_vec_lane(n_val, i, size), size);
                    if (val > max_val) max_val = val;
                }
                res = static_cast<std::uint64_t>(max_val) & vec_cmp_mask(true, size);
            } else {
                std::uint64_t max_val = get_vec_lane(n_val, 0, size);
                for (unsigned i = 1; i < elements; ++i) {
                    const std::uint64_t val = get_vec_lane(n_val, i, size);
                    if (val > max_val) max_val = val;
                }
                res = max_val & vec_cmp_mask(true, size);
            }
        } else if (opcode == 0b11010) { // SMINV (u=0), UMINV (u=1)
            if (!u) {
                std::int64_t min_val = sign_extend(get_vec_lane(n_val, 0, size), size);
                for (unsigned i = 1; i < elements; ++i) {
                    const std::int64_t val = sign_extend(get_vec_lane(n_val, i, size), size);
                    if (val < min_val) min_val = val;
                }
                res = static_cast<std::uint64_t>(min_val) & vec_cmp_mask(true, size);
            } else {
                std::uint64_t min_val = get_vec_lane(n_val, 0, size);
                for (unsigned i = 1; i < elements; ++i) {
                    const std::uint64_t val = get_vec_lane(n_val, i, size);
                    if (val < min_val) min_val = val;
                }
                res = min_val & vec_cmp_mask(true, size);
            }
        } else {
            throw std::runtime_error("Unsupported across-lanes opcode");
        }

        write_vector_register(rd, VectorRegister{res, 0});
        program_counter_ = checked_add_address(instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    // 4. Advanced SIMD 2-Reg Misc (ABS, NEG, Zero comparisons, Narrowing)
    if ((instruction & 0x9F200C00u) == 0x0E200800u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool u = (instruction & 0x20000000u) != 0;
        const std::uint32_t size = (instruction >> 22u) & 0x3u;
        const std::uint32_t op_hi = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t op_lo = (instruction >> 11u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;

        if (op_hi == 0b00000) {
            if ((op_lo & 0x8u) == 0) { // Integer 2-reg misc
                if (size == 3 && !q) throw std::runtime_error("1D 2-reg misc not supported");
                const VectorRegister n_val = read_vector_register(rn);
                VectorRegister out{};
                const unsigned lane_count = q ? (16u >> size) : (8u >> size);
                for (unsigned i = 0; i < lane_count; ++i) {
                    const std::uint64_t val = get_vec_lane(n_val, i, size);
                    const std::int64_t sval = sign_extend(val, size);
                    std::uint64_t res = 0;
                    if (op_lo == 0b10001) { // CMGT0 (u=0), CMGE0 (u=1)
                        res = vec_cmp_mask(u ? (sval >= 0) : (sval > 0), size);
                    } else if (op_lo == 0b10011) { // CMEQ0 (u=0), CMLE0 (u=1)
                        res = vec_cmp_mask(u ? (sval <= 0) : (sval == 0), size);
                    } else if (op_lo == 0b10101 && !u) { // CMLT0
                        res = vec_cmp_mask(sval < 0, size);
                    } else if (op_lo == 0b10111) { // ABS (u=0), NEG (u=1)
                        if (!u) { // ABS
                            res = (sval < 0) ? static_cast<std::uint64_t>(0 - val) : val;
                            res &= vec_cmp_mask(true, size);
                        } else { // NEG
                            res = static_cast<std::uint64_t>(0 - val) & vec_cmp_mask(true, size);
                        }
                    } else {
                        throw std::runtime_error("Unsupported 2-reg misc opcode");
                    }
                    set_vec_lane(out, i, size, res);
                }
                if (!q) out.high = 0;
                write_vector_register(rd, out);
                program_counter_ = checked_add_address(instruction_address, 4u, "Instruction PC increment");
                emit_trace();
                return;
            } else { // Floating-point 2-reg misc (FABS, FNEG, FCMEQ0, FCMGE0, FCMGT0, FCMLE0, FCMLT0)
                const bool bit23 = (size & 2u) != 0;
                const std::uint32_t sz = size & 1u;
                if (!bit23 || (sz == 1 && !q)) {
                    throw std::runtime_error("Invalid 2-reg misc float encoding");
                }
                const unsigned esz = (sz == 0) ? 2u : 3u;
                const unsigned lane_count = q ? (16u >> esz) : (8u >> esz);
                const VectorRegister n_val = read_vector_register(rn);
                VectorRegister out{};
                for (unsigned i = 0; i < lane_count; ++i) {
                    std::uint64_t res = 0;
                    if (sz == 0) {
                        const std::uint64_t val_raw = get_vec_lane(n_val, i, 2);
                        const float val = std::bit_cast<float>(static_cast<std::uint32_t>(val_raw));
                        if (op_lo == 0b11001) { // FCMGT0 (u=0), FCMGE0 (u=1)
                            res = vec_cmp_mask(u ? (val >= 0.0f) : (val > 0.0f), 2);
                        } else if (op_lo == 0b11011) { // FCMEQ0 (u=0), FCMLE0 (u=1)
                            res = vec_cmp_mask(u ? (val <= 0.0f) : (val == 0.0f), 2);
                        } else if (op_lo == 0b11101 && !u) { // FCMLT0
                            res = vec_cmp_mask(val < 0.0f, 2);
                        } else if (op_lo == 0b11111) { // FABS (u=0), FNEG (u=1)
                            res = u ? (val_raw ^ 0x80000000u) : (val_raw & 0x7FFFFFFFu);
                        } else {
                            throw std::runtime_error("Unsupported 2-reg misc float opcode");
                        }
                    } else {
                        const std::uint64_t val_raw = get_vec_lane(n_val, i, 3);
                        const double val = std::bit_cast<double>(val_raw);
                        if (op_lo == 0b11001) { // FCMGT0 (u=0), FCMGE0 (u=1)
                            res = vec_cmp_mask(u ? (val >= 0.0) : (val > 0.0), 3);
                        } else if (op_lo == 0b11011) { // FCMEQ0 (u=0), FCMLE0 (u=1)
                            res = vec_cmp_mask(u ? (val <= 0.0) : (val == 0.0), 3);
                        } else if (op_lo == 0b11101 && !u) { // FCMLT0
                            res = vec_cmp_mask(val < 0.0, 3);
                        } else if (op_lo == 0b11111) { // FABS (u=0), FNEG (u=1)
                            res = u ? (val_raw ^ 0x8000000000000000ull) : (val_raw & 0x7FFFFFFFFFFFFFFFull);
                        } else {
                            throw std::runtime_error("Unsupported 2-reg misc float opcode");
                        }
                    }
                    set_vec_lane(out, i, esz, res);
                }
                if (!q) out.high = 0;
                write_vector_register(rd, out);
                program_counter_ = checked_add_address(instruction_address, 4u, "Instruction PC increment");
                emit_trace();
                return;
            }
        } else if (op_hi == 0b00001) {
            if ((op_lo == 0b00101 || op_lo == 0b01001) && size < 3) { // Narrowing (XTN, SQXTN, UQXTN)
                int op = 0;
                if (op_lo == 0b00101 && !u) op = 0; // XTN
                else if (op_lo == 0b01001 && !u) op = 1; // SQXTN
                else if (op_lo == 0b01001 && u) op = 2; // UQXTN
                else throw std::runtime_error("Unsupported narrowing opcode");

                const VectorRegister n_val = read_vector_register(rn);
                VectorRegister out = read_vector_register(rd);
                bool sat = false;
                const unsigned lane_count = 8u >> size;
                std::uint64_t narrowed_half = 0;
                for (unsigned i = 0; i < lane_count; ++i) {
                    const std::uint64_t src = get_vec_lane(n_val, i, size + 1);
                    const std::uint64_t res = narrow_op(src, size, op, sat);
                    const unsigned shift = i * (8u << size);
                    narrowed_half |= (res & vec_cmp_mask(true, size)) << shift;
                }
                if (sat) {
                    fpsr_ |= (1u << 27);
                }
                if (!q) {
                    out.low = narrowed_half;
                    out.high = 0;
                } else {
                    out.high = narrowed_half;
                }
                write_vector_register(rd, out);
                program_counter_ = checked_add_address(instruction_address, 4u, "Instruction PC increment");
                emit_trace();
                return;
            } else if (op_lo == 0b11111 || op_lo == 0b11011 || op_lo == 0b10111) { // Floating-point conversions & FSQRT
                const bool bit23 = (size & 2u) != 0;
                const std::uint32_t sz = size & 1u;
                if (sz == 1 && !q) {
                    throw std::runtime_error("1D 2-reg misc float is unallocated");
                }
                const unsigned esz = (sz == 0) ? 2u : 3u;
                const unsigned lane_count = q ? (16u >> esz) : (8u >> esz);
                const VectorRegister n_val = read_vector_register(rn);
                VectorRegister out{};
                for (unsigned i = 0; i < lane_count; ++i) {
                    std::uint64_t res = 0;
                    if (sz == 0) {
                        if (op_lo == 0b11111 && u && bit23) { // FSQRT
                            const float src = std::bit_cast<float>(static_cast<std::uint32_t>(get_vec_lane(n_val, i, 2)));
                            res = std::bit_cast<std::uint32_t>(fp_sqrt_op(src, fpsr_));
                        } else if (op_lo == 0b11011 && !bit23) { // SCVTF (u=0), UCVTF (u=1)
                            const std::uint64_t raw = get_vec_lane(n_val, i, 2);
                            if (!u) res = std::bit_cast<std::uint32_t>(static_cast<float>(static_cast<std::int32_t>(raw)));
                            else res = std::bit_cast<std::uint32_t>(static_cast<float>(static_cast<std::uint32_t>(raw)));
                        } else if (op_lo == 0b10111 && bit23) { // FCVTZS (u=0), FCVTZU (u=1)
                            const float src = std::bit_cast<float>(static_cast<std::uint32_t>(get_vec_lane(n_val, i, 2)));
                            if (!u) res = static_cast<std::uint32_t>(fcvt_to_signed_sat<std::int32_t>(src, fpsr_));
                            else res = fcvt_to_unsigned_sat<std::uint32_t>(src, fpsr_);
                        } else {
                            throw std::runtime_error("Unsupported 2-reg misc float conversion opcode");
                        }
                    } else {
                        if (op_lo == 0b11111 && u && bit23) { // FSQRT
                            const double src = std::bit_cast<double>(get_vec_lane(n_val, i, 3));
                            res = std::bit_cast<std::uint64_t>(fp_sqrt_op(src, fpsr_));
                        } else if (op_lo == 0b11011 && !bit23) { // SCVTF (u=0), UCVTF (u=1)
                            const std::uint64_t raw = get_vec_lane(n_val, i, 3);
                            if (!u) res = std::bit_cast<std::uint64_t>(static_cast<double>(static_cast<std::int64_t>(raw)));
                            else res = std::bit_cast<std::uint64_t>(static_cast<double>(raw));
                        } else if (op_lo == 0b10111 && bit23) { // FCVTZS (u=0), FCVTZU (u=1)
                            const double src = std::bit_cast<double>(get_vec_lane(n_val, i, 3));
                            if (!u) res = static_cast<std::uint64_t>(fcvt_to_signed_sat<std::int64_t>(src, fpsr_));
                            else res = fcvt_to_unsigned_sat<std::uint64_t>(src, fpsr_);
                        } else {
                            throw std::runtime_error("Unsupported 2-reg misc float conversion opcode");
                        }
                    }
                    set_vec_lane(out, i, esz, res);
                }
                if (!q) out.high = 0;
                write_vector_register(rd, out);
                program_counter_ = checked_add_address(instruction_address, 4u, "Instruction PC increment");
                emit_trace();
                return;
            }
        }
    }

    // 5. Advanced SIMD 3-Different (Widening)
    if ((instruction & 0x9F200C00u) == 0x0E200000u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool u = (instruction & 0x20000000u) != 0;
        const std::uint32_t size = (instruction >> 22u) & 0x3u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t opcode = (instruction >> 11u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;

        if (size >= 3) throw std::runtime_error("Invalid size for 3-different widening");
        const unsigned desz = size + 1;
        const unsigned lane_count = 8u >> size;
        const unsigned src_offset = q ? lane_count : 0;

        const VectorRegister n_val = read_vector_register(rn);
        const VectorRegister m_val = read_vector_register(rm);
        VectorRegister out{};

        for (unsigned i = 0; i < lane_count; ++i) {
            std::uint64_t res = 0;
            if (opcode == 0b00000 || opcode == 0b00100) { // SADDL/UADDL or SSUBL/USUBL
                const std::uint64_t a_raw = get_vec_lane(n_val, src_offset + i, size);
                const std::uint64_t b_raw = get_vec_lane(m_val, src_offset + i, size);
                const std::uint64_t a = !u ? static_cast<std::uint64_t>(sign_extend(a_raw, size)) : a_raw;
                const std::uint64_t b = !u ? static_cast<std::uint64_t>(sign_extend(b_raw, size)) : b_raw;
                res = (opcode == 0b00000) ? (a + b) : (a - b);
            } else if (opcode == 0b00010 || opcode == 0b00110) { // SADDW/UADDW or SSUBW/USUBW
                const std::uint64_t a = get_vec_lane(n_val, i, desz);
                const std::uint64_t b_raw = get_vec_lane(m_val, src_offset + i, size);
                const std::uint64_t b = !u ? static_cast<std::uint64_t>(sign_extend(b_raw, size)) : b_raw;
                res = (opcode == 0b00010) ? (a + b) : (a - b);
            } else {
                throw std::runtime_error("Unsupported 3-different opcode");
            }
            set_vec_lane(out, i, desz, res);
        }
        write_vector_register(rd, out);
        program_counter_ = checked_add_address(instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    // 5b. Advanced SIMD 3-Same Floating-Point
    if ((instruction & 0x9F200400u) == 0x0E200400u && (((instruction >> 11u) & 0x18u) == 0x18u)) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool u = (instruction & 0x20000000u) != 0;
        const bool bit23 = (instruction & (1u << 23u)) != 0;
        const std::uint32_t sz = (instruction >> 22u) & 0x1u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t opcode = (instruction >> 11u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;

        if (sz == 1 && !q) {
            throw std::runtime_error("1D SIMD 3-same float is unallocated");
        }

        const VectorRegister n_val = read_vector_register(rn);
        const VectorRegister m_val = read_vector_register(rm);
        VectorRegister out{};

        const unsigned esz = (sz == 0) ? 2u : 3u;
        const unsigned lane_count = q ? (16u >> esz) : (8u >> esz);

        const bool is_pairwise = u && (opcode == 0b11010 || opcode == 0b11110 || opcode == 0b11000);
        if (is_pairwise) {
            const unsigned half = lane_count / 2;
            for (unsigned i = 0; i < half; ++i) {
                if (sz == 0) {
                    const float a = std::bit_cast<float>(static_cast<std::uint32_t>(get_vec_lane(n_val, 2 * i, 2)));
                    const float b = std::bit_cast<float>(static_cast<std::uint32_t>(get_vec_lane(n_val, 2 * i + 1, 2)));
                    float r = 0.0f;
                    if (opcode == 0b11010 && !bit23) r = a + b; // FADDP
                    else if (opcode == 0b11110) r = bit23 ? fp_min_op(a, b) : fp_max_op(a, b); // FMINP / FMAXP
                    else if (opcode == 0b11000) r = bit23 ? fp_minnm_op(a, b) : fp_maxnm_op(a, b); // FMINNMP / FMAXNMP
                    else throw std::runtime_error("Unsupported 3-same float pairwise opcode");
                    set_vec_lane(out, i, 2, std::bit_cast<std::uint32_t>(r));
                } else {
                    const double a = std::bit_cast<double>(get_vec_lane(n_val, 2 * i, 3));
                    const double b = std::bit_cast<double>(get_vec_lane(n_val, 2 * i + 1, 3));
                    double r = 0.0;
                    if (opcode == 0b11010 && !bit23) r = a + b; // FADDP
                    else if (opcode == 0b11110) r = bit23 ? fp_min_op(a, b) : fp_max_op(a, b); // FMINP / FMAXP
                    else if (opcode == 0b11000) r = bit23 ? fp_minnm_op(a, b) : fp_maxnm_op(a, b); // FMINNMP / FMAXNMP
                    else throw std::runtime_error("Unsupported 3-same float pairwise opcode");
                    set_vec_lane(out, i, 3, std::bit_cast<std::uint64_t>(r));
                }
            }
            for (unsigned i = 0; i < half; ++i) {
                if (sz == 0) {
                    const float a = std::bit_cast<float>(static_cast<std::uint32_t>(get_vec_lane(m_val, 2 * i, 2)));
                    const float b = std::bit_cast<float>(static_cast<std::uint32_t>(get_vec_lane(m_val, 2 * i + 1, 2)));
                    float r = 0.0f;
                    if (opcode == 0b11010 && !bit23) r = a + b; // FADDP
                    else if (opcode == 0b11110) r = bit23 ? fp_min_op(a, b) : fp_max_op(a, b); // FMINP / FMAXP
                    else if (opcode == 0b11000) r = bit23 ? fp_minnm_op(a, b) : fp_maxnm_op(a, b); // FMINNMP / FMAXNMP
                    else throw std::runtime_error("Unsupported 3-same float pairwise opcode");
                    set_vec_lane(out, half + i, 2, std::bit_cast<std::uint32_t>(r));
                } else {
                    const double a = std::bit_cast<double>(get_vec_lane(m_val, 2 * i, 3));
                    const double b = std::bit_cast<double>(get_vec_lane(m_val, 2 * i + 1, 3));
                    double r = 0.0;
                    if (opcode == 0b11010 && !bit23) r = a + b; // FADDP
                    else if (opcode == 0b11110) r = bit23 ? fp_min_op(a, b) : fp_max_op(a, b); // FMINP / FMAXP
                    else if (opcode == 0b11000) r = bit23 ? fp_minnm_op(a, b) : fp_maxnm_op(a, b); // FMINNMP / FMAXNMP
                    else throw std::runtime_error("Unsupported 3-same float pairwise opcode");
                    set_vec_lane(out, half + i, 3, std::bit_cast<std::uint64_t>(r));
                }
            }
        } else {
            for (unsigned i = 0; i < lane_count; ++i) {
                std::uint64_t res = 0;
                if (sz == 0) {
                    const float a = std::bit_cast<float>(static_cast<std::uint32_t>(get_vec_lane(n_val, i, 2)));
                    const float b = std::bit_cast<float>(static_cast<std::uint32_t>(get_vec_lane(m_val, i, 2)));
                    if (opcode == 0b11010) {
                        res = std::bit_cast<std::uint32_t>(bit23 ? (a - b) : (a + b)); // FSUB / FADD
                    } else if (opcode == 0b11011 && u && !bit23) { // FMUL
                        res = std::bit_cast<std::uint32_t>(a * b);
                    } else if (opcode == 0b11111 && u && !bit23) { // FDIV
                        res = std::bit_cast<std::uint32_t>(fp_div_op(a, b, fpsr_));
                    } else if (opcode == 0b11110 && !u) { // FMAX / FMIN
                        res = std::bit_cast<std::uint32_t>(bit23 ? fp_min_op(a, b) : fp_max_op(a, b));
                    } else if (opcode == 0b11000 && !u) { // FMAXNM / FMINNM
                        res = std::bit_cast<std::uint32_t>(bit23 ? fp_minnm_op(a, b) : fp_maxnm_op(a, b));
                    } else if (opcode == 0b11100) { // FCMEQ, FCMGE, FCMGT
                        if (!u && !bit23) res = vec_cmp_mask(a == b, 2);
                        else if (u && !bit23) res = vec_cmp_mask(a >= b, 2);
                        else if (u && bit23) res = vec_cmp_mask(a > b, 2);
                        else throw std::runtime_error("Unsupported 3-same float cmp opcode");
                    } else {
                        throw std::runtime_error("Unsupported 3-same float opcode");
                    }
                } else {
                    const double a = std::bit_cast<double>(get_vec_lane(n_val, i, 3));
                    const double b = std::bit_cast<double>(get_vec_lane(m_val, i, 3));
                    if (opcode == 0b11010) {
                        res = std::bit_cast<std::uint64_t>(bit23 ? (a - b) : (a + b)); // FSUB / FADD
                    } else if (opcode == 0b11011 && u && !bit23) { // FMUL
                        res = std::bit_cast<std::uint64_t>(a * b);
                    } else if (opcode == 0b11111 && u && !bit23) { // FDIV
                        res = std::bit_cast<std::uint64_t>(fp_div_op(a, b, fpsr_));
                    } else if (opcode == 0b11110 && !u) { // FMAX / FMIN
                        res = std::bit_cast<std::uint64_t>(bit23 ? fp_min_op(a, b) : fp_max_op(a, b));
                    } else if (opcode == 0b11000 && !u) { // FMAXNM / FMINNM
                        res = std::bit_cast<std::uint64_t>(bit23 ? fp_minnm_op(a, b) : fp_maxnm_op(a, b));
                    } else if (opcode == 0b11100) { // FCMEQ, FCMGE, FCMGT
                        if (!u && !bit23) res = vec_cmp_mask(a == b, 3);
                        else if (u && !bit23) res = vec_cmp_mask(a >= b, 3);
                        else if (u && bit23) res = vec_cmp_mask(a > b, 3);
                        else throw std::runtime_error("Unsupported 3-same float cmp opcode");
                    } else {
                        throw std::runtime_error("Unsupported 3-same float opcode");
                    }
                }
                set_vec_lane(out, i, esz, res);
            }
        }
        if (!q) out.high = 0;
        write_vector_register(rd, out);
        program_counter_ = checked_add_address(instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    // 6. Advanced SIMD 3-Same Integer
    if ((instruction & 0x9F200400u) == 0x0E200400u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool u = (instruction & 0x20000000u) != 0;
        const std::uint32_t size = (instruction >> 22u) & 0x3u;
        const std::uint32_t rm = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t opcode = (instruction >> 11u) & 0x1Fu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;

        if (size == 3 && !q) {
            throw std::runtime_error("1D SIMD 3-same is unallocated");
        }

        const VectorRegister n_val = read_vector_register(rn);
        const VectorRegister m_val = read_vector_register(rm);
        VectorRegister out{};
        bool sat = false;

        const unsigned lane_count = q ? (16u >> size) : (8u >> size);
        if (opcode == 0b10111 && !u) { // ADDP
            const unsigned half = lane_count / 2;
            for (unsigned i = 0; i < half; ++i) {
                const std::uint64_t a = get_vec_lane(n_val, 2 * i, size);
                const std::uint64_t b = get_vec_lane(n_val, 2 * i + 1, size);
                set_vec_lane(out, i, size, a + b);
            }
            for (unsigned i = 0; i < half; ++i) {
                const std::uint64_t a = get_vec_lane(m_val, 2 * i, size);
                const std::uint64_t b = get_vec_lane(m_val, 2 * i + 1, size);
                set_vec_lane(out, half + i, size, a + b);
            }
        } else {
            for (unsigned i = 0; i < lane_count; ++i) {
                const std::uint64_t left = get_vec_lane(n_val, i, size);
                const std::uint64_t right = get_vec_lane(m_val, i, size);
                std::uint64_t res = 0;
                if (opcode == 0b10000) { // ADD (u=0), SUB (u=1)
                    res = u ? (left - right) : (left + right);
                } else if (opcode == 0b00001) { // SQADD (u=0), UQADD (u=1)
                    res = u ? uqadd(left, right, size, sat) : sqadd(left, right, size, sat);
                } else if (opcode == 0b00101) { // SQSUB (u=0), UQSUB (u=1)
                    res = u ? uqsub(left, right, size, sat) : sqsub(left, right, size, sat);
                } else if (opcode == 0b01100 && size < 3) { // SMAX (u=0), UMAX (u=1)
                    res = u ? umax_op(left, right, size) : smax_op(left, right, size);
                } else if (opcode == 0b01101 && size < 3) { // SMIN (u=0), UMIN (u=1)
                    res = u ? umin_op(left, right, size) : smin_op(left, right, size);
                } else if (opcode == 0b10011 && !u && size < 3) { // MUL
                    res = left * right;
                } else if (opcode == 0b10001) { // CMTST (u=0), CMEQ (u=1)
                    res = u ? vec_cmp_mask(left == right, size) : vec_cmp_mask((left & right) != 0, size);
                } else if (opcode == 0b00110) { // CMGT (u=0), CMHI (u=1)
                    res = u ? vec_cmp_mask(left > right, size) : vec_cmp_mask(sign_extend(left, size) > sign_extend(right, size), size);
                } else if (opcode == 0b00111) { // CMGE (u=0), CMHS (u=1)
                    res = u ? vec_cmp_mask(left >= right, size) : vec_cmp_mask(sign_extend(left, size) >= sign_extend(right, size), size);
                } else if (opcode == 0b01000) { // SSHL (u=0), USHL (u=1)
                    res = u ? ushl_op(left, right, size) : sshl_op(left, right, size);
                } else {
                    throw std::runtime_error("Unsupported 3-same integer opcode");
                }
                set_vec_lane(out, i, size, res);
            }
        }
        if (sat) {
            fpsr_ |= (1u << 27);
        }
        if (!q) out.high = 0;
        write_vector_register(rd, out);
        program_counter_ = checked_add_address(instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    // 3. Vector DUP, INS, UMOV (Advanced SIMD copy)
    if ((instruction & 0xBFE08400u) == 0x0E000400u) {
        const bool q = (instruction & 0x40000000u) != 0;
        const bool bit29 = (instruction & 0x20000000u) != 0;
        const std::uint32_t imm5 = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t op2 = (instruction >> 11u) & 0xFu;
        const std::uint32_t rn = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t rd = instruction & 0x1Fu;

        unsigned esz = 0, idx = 0;
        if (!decode_esz_idx(imm5, esz, idx) || bit29) {
            throw std::runtime_error("Invalid imm5 in SIMD copy instruction");
        }

        if (op2 == 0b0001) { // DUP general
            if (esz == 3 && !q) {
                throw std::runtime_error("1D DUP general is unallocated");
            }
            const std::uint64_t val = read_zero_register_operand(*this, rn);
            VectorRegister out{};
            const unsigned lane_count = q ? (16u >> esz) : (8u >> esz);
            for (unsigned i = 0; i < lane_count; ++i) {
                set_vec_lane(out, i, esz, val);
            }
            if (!q) out.high = 0;
            write_vector_register(rd, out);
        } else if (op2 == 0b0011 && q) { // INS general
            const std::uint64_t val = read_zero_register_operand(*this, rn);
            VectorRegister out = read_vector_register(rd);
            set_vec_lane(out, idx, esz, val);
            write_vector_register(rd, out);
        } else if (op2 == 0b0111) { // UMOV
            if (esz == 3 ? !q : q) {
                throw std::runtime_error("Invalid q for UMOV esz");
            }
            const std::uint64_t val = get_vec_lane(read_vector_register(rn), idx, esz);
            if (esz == 3) {
                write_zero_register_result(*this, rd, val);
            } else {
                write_zero_register_result(*this, rd, static_cast<std::uint32_t>(val));
            }
        } else {
            throw std::runtime_error("Unsupported SIMD copy instruction");
        }

        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }
    // Phase 36: only a genuinely unallocated/reserved encoding becomes a guest
    // Undefined Instruction. An allocated encoding that this interpreter does
    // not implement stays a host limitation and keeps throwing.
    if (architectural_exceptions_ && is_unallocated_encoding(instruction)) {
        raise_synchronous_exception(
            make_undefined_instruction(instruction_address));
        return;
    }
    throw std::runtime_error("Unsupported AArch64 instruction encoding: 0x"
                             + std::to_string(instruction));
    } catch (const SynchronousExceptionSignal& signal) {
        // A guest data access faulted. The faulting instruction has been
        // unwound without committing register, PC or memory effects, so
        // architectural entry can now capture the pre-exception state.
        raise_synchronous_exception(signal.exception);
    }
}
std::size_t CPU::run(Memory& memory, std::size_t max_instructions) {
    std::size_t executed = 0;
    while (executed < max_instructions) {
        step(memory);
        ++executed;
    }
    return executed;
}
void CPU::run_demo() {
    write_register(0, 10);
    write_register(1, 20);
    write_register(2, read_register(0) + read_register(1));
    std::cout << "Demo calculation: X2 = "
              << read_register(2) << '\n';
    program_counter_ = 0;
    std::cout << "CPU state demonstration complete.\n";
}
