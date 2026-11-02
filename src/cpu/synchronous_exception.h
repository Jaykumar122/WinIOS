#pragma once
#include <cstdint>

// Phase 36: typed AArch64 synchronous exception model.
//
// Generation (decoding, address arithmetic, memory-access checks) and entry
// (vector selection, SPSR/ELR/ESR/FAR capture, PSTATE mutation) are deliberately
// separated: an instruction path *generates* a SynchronousException and hands it
// to CPU::raise_synchronous_exception, which performs architectural entry.
//
// The reference configuration is the pinned Inferno build (`-cpu max,sve=off`),
// whose observable encodings are reproduced here.

// Exception class, ESR_EL1.EC (bits [31:26]).
enum class ExceptionClass : std::uint32_t {
    Unknown               = 0x00, // EC 0b000000, "Unknown reason"
    IllegalExecutionState = 0x0E, // EC 0b001110
    SvcAArch64            = 0x15, // EC 0b010101
    HvcAArch64            = 0x16, // EC 0b010110
    TrappedMsrMrs         = 0x18, // EC 0b011000
    InstructionAbortLower = 0x20, // instruction abort from a lower EL
    InstructionAbortSame  = 0x21, // instruction abort from the same EL
    PcAlignment           = 0x22, // PC alignment fault
    DataAbortLower        = 0x24, // data abort from a lower EL
    DataAbortSame         = 0x25, // data abort from the same EL
};

// Fault status code, ESR_EL1.ISS[5:0] for abort exceptions.
enum class FaultStatus : std::uint32_t {
    AddressSizeLevel0   = 0x00, // address exceeds the implemented PA width / TCR bounds
    AddressSizeLevel1   = 0x01,
    AddressSizeLevel2   = 0x02,
    AddressSizeLevel3   = 0x03,
    TranslationLevel0   = 0x04,
    TranslationLevel1   = 0x05,
    TranslationLevel2   = 0x06,
    TranslationLevel3   = 0x07,
    AccessFlagLevel0    = 0x08,
    AccessFlagLevel1    = 0x09,
    AccessFlagLevel2    = 0x0A,
    AccessFlagLevel3    = 0x0B,
    PermissionLevel0    = 0x0C,
    PermissionLevel1    = 0x0D,
    PermissionLevel2    = 0x0E,
    PermissionLevel3    = 0x0F,
    SynchronousExternal = 0x10, // external abort on an unassigned address
    SyncExternalWalk0   = 0x14,
    SyncExternalWalk1   = 0x15,
    SyncExternalWalk2   = 0x16,
    SyncExternalWalk3   = 0x17,
    Alignment           = 0x21, // alignment fault
};

enum class AccessType : std::uint32_t {
    Read  = 0,
    Write = 1,
};

// Implemented physical address width: the pinned reference (`-cpu max`) accepts
// [0, 2^52) and reports an address-size fault at or above 2^52.
constexpr std::uint64_t kPhysicalAddressLimit = std::uint64_t{1} << 52;

// A single synchronous exception, independent of the EL it is taken to.
struct SynchronousException {
    ExceptionClass ec{ExceptionClass::Unknown};
    std::uint32_t iss{0};             // ISS bits [23:0], IL excluded
    bool instruction_length_4{true};  // ESR_ELx.IL
    std::uint64_t preferred_return{0};// written to ELR_ELx
    bool has_fault_address{false};
    std::uint64_t fault_address{0};   // written to FAR_ELx
    bool has_fault_ipa{false};
    std::uint64_t fault_ipa{0};       // written to HPFAR_EL2
    std::uint32_t target_el{1};       // target Exception Level (1 or 2)
    AccessType access{AccessType::Read};
    unsigned access_size{0};          // bytes, 0 when not a memory access
    bool same_el{false};              // abort taken from the same EL

    std::uint32_t esr() const noexcept {
        return (static_cast<std::uint32_t>(ec) << 26u)
             | (instruction_length_4 ? (1u << 25u) : 0u)
             | (iss & 0x00FFFFFFu);
    }
};

// --- generation helpers ----------------------------------------------------

// EC 0x00: unallocated/reserved encoding, or a privilege trap that the
// reference reports through the unallocated-encoding path.
inline SynchronousException make_undefined_instruction(std::uint64_t address) {
    SynchronousException e;
    e.ec = ExceptionClass::Unknown;
    e.preferred_return = address;
    return e;
}

inline SynchronousException make_svc(std::uint64_t address, std::uint16_t imm16) {
    SynchronousException e;
    e.ec = ExceptionClass::SvcAArch64;
    e.iss = imm16;
    // SVC's preferred return address is the following instruction.
    e.preferred_return = address + 4u;
    return e;
}

inline SynchronousException make_hvc(std::uint64_t address, std::uint16_t imm16) {
    SynchronousException e;
    e.ec = ExceptionClass::HvcAArch64;
    e.iss = imm16;
    // HVC's preferred return address is the following instruction.
    e.preferred_return = address + 4u;
    e.target_el = 2;
    return e;
}

inline SynchronousException make_trapped_sysreg(std::uint64_t address, std::uint32_t iss) {
    SynchronousException e;
    e.ec = ExceptionClass::TrappedMsrMrs;
    e.iss = iss;
    e.preferred_return = address;
    e.target_el = 2;
    return e;
}

inline SynchronousException make_illegal_state(std::uint64_t address) {
    SynchronousException e;
    e.ec = ExceptionClass::IllegalExecutionState;
    e.preferred_return = address;
    return e;
}

// PC alignment fault: FAR and ELR both hold the misaligned PC.
inline SynchronousException make_pc_alignment(std::uint64_t fault_address) {
    SynchronousException e;
    e.ec = ExceptionClass::PcAlignment;
    e.preferred_return = fault_address;
    e.has_fault_address = true;
    e.fault_address = fault_address;
    return e;
}

// Instruction abort: FAR and ELR both hold the faulting fetch address.
inline SynchronousException make_instruction_abort(bool same_el, FaultStatus fsc,
                                                   std::uint64_t fault_address,
                                                   unsigned access_size) {
    SynchronousException e;
    e.ec = same_el ? ExceptionClass::InstructionAbortSame
                   : ExceptionClass::InstructionAbortLower;
    e.iss = static_cast<std::uint32_t>(fsc) & 0x3Fu;
    e.preferred_return = fault_address;
    e.has_fault_address = true;
    e.fault_address = fault_address;
    e.access = AccessType::Read;
    e.access_size = access_size;
    e.same_el = same_el;
    return e;
}

// Data abort ISS: WnR is ISS[6]; FSC is ISS[5:0]. FnV/EA/CM/S1PTW and the
// ISV payload are all zero, which is what the reference reports for the
// translation/alignment/external faults modelled here.
inline SynchronousException make_data_abort(bool same_el, AccessType access,
                                            FaultStatus fsc,
                                            std::uint64_t fault_address,
                                            std::uint64_t instruction_address,
                                            unsigned access_size) {
    SynchronousException e;
    e.ec = same_el ? ExceptionClass::DataAbortSame : ExceptionClass::DataAbortLower;
    e.iss = ((access == AccessType::Write ? 1u : 0u) << 6u)
          | (static_cast<std::uint32_t>(fsc) & 0x3Fu);
    e.preferred_return = instruction_address;
    e.has_fault_address = true;
    e.fault_address = fault_address;
    e.access = access;
    e.access_size = access_size;
    e.same_el = same_el;
    return e;
}

// True when the reference decodes this word through the "unallocated encoding"
// path. The top-level encoding index is bits [28:25]:
//   0000 = reserved (the reference reserves it except for internal opcodes),
//   0001, 0011, 1111 = unallocated,
// and every other value names an allocated encoding group (which must never be
// turned into a guest Undefined Instruction just because this interpreter does
// not implement it).
inline bool is_unallocated_encoding(std::uint32_t instruction) noexcept {
    const std::uint32_t op0 = (instruction >> 25u) & 0xFu;
    if (op0 == 0x0u) {
        // The reserved group hosts a small set of internal opcodes that the
        // reference does decode; anything else in it is unallocated.
        const std::uint32_t reserved_opcode = (instruction >> 10u) & 0x3Fu;
        return reserved_opcode != 2u && reserved_opcode != 3u
            && reserved_opcode != 5u;
    }
    return op0 == 0x1u || op0 == 0x3u || op0 == 0xFu;
}

// Internal control-flow signal: a guest instruction discovered a synchronous
// fault and must stop without committing further architectural effects. It is
// deliberately not derived from std::exception so that host errors can never
// be mistaken for guest exceptions (only this exact type is caught).
struct SynchronousExceptionSignal {
    SynchronousException exception;
};
