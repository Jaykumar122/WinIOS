#include "runtime/apple_kernel_fault_analyzer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace winios::runtime {

namespace {

std::string to_hex(std::uint64_t val, int width = 16) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(width) << val;
    return ss.str();
}

constexpr std::uint32_t kFaultRecordMagic = 0x31464E57u; // 'W', 'N', 'F', '1'
constexpr std::uint32_t kFaultRecordVersion = 1u;
constexpr std::size_t kHeaderSize = 16; // 4 magic + 4 version + 4 payload_len + 4 checksum

} // namespace

// -----------------------------------------------------------------------------
// Adler-32 Checksum Implementation (Data Integrity Verification)
// -----------------------------------------------------------------------------
std::uint32_t compute_adler32(const std::uint8_t* data, std::size_t len) noexcept {
    constexpr std::uint32_t kModAdler = 65521u;
    std::uint32_t a = 1u;
    std::uint32_t b = 0u;

    for (std::size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % kModAdler;
        b = (b + a) % kModAdler;
    }
    return (b << 16u) | a;
}

// -----------------------------------------------------------------------------
// ESR_EL1 Syndrome Decoding
// -----------------------------------------------------------------------------
AppleGuestExceptionSyndrome AppleGuestExceptionSyndrome::decode(std::uint32_t esr) noexcept {
    AppleGuestExceptionSyndrome syn;
    syn.raw_esr = esr;
    syn.ec = (esr >> 26u) & 0x3Fu;
    syn.il = ((esr >> 25u) & 0x01u) != 0;
    syn.iss = esr & 0x00FFFFFFu;
    syn.fsc = syn.iss & 0x3Fu;
    syn.wnr = ((syn.iss >> 6u) & 0x01u) != 0;

    switch (syn.ec) {
        case 0x00: syn.ec_name = "Unknown / Undefined Instruction"; break;
        case 0x07: syn.ec_name = "Trapped FP/SIMD Access (CPACR_EL1.FPEN)"; break;
        case 0x0E: syn.ec_name = "Illegal Execution State"; break;
        case 0x15: syn.ec_name = "SVC Instruction Execution (AArch64)"; break;
        case 0x16: syn.ec_name = "HVC Instruction Execution (AArch64)"; break;
        case 0x17: syn.ec_name = "SMC Instruction Execution (AArch64)"; break;
        case 0x18: syn.ec_name = "Trapped MSR/MRS System Register Access"; break;
        case 0x20: syn.ec_name = "Instruction Abort (Lower EL)"; break;
        case 0x21: syn.ec_name = "Instruction Abort (Same EL)"; break;
        case 0x22: syn.ec_name = "PC Alignment Fault"; break;
        case 0x24: syn.ec_name = "Data Abort (Lower EL)"; break;
        case 0x25: syn.ec_name = "Data Abort (Same EL)"; break;
        case 0x30: syn.ec_name = "Breakpoint (Lower EL)"; break;
        case 0x31: syn.ec_name = "Breakpoint (Same EL)"; break;
        case 0x32: syn.ec_name = "Software Step (Lower EL)"; break;
        case 0x33: syn.ec_name = "Software Step (Same EL)"; break;
        case 0x34: syn.ec_name = "Watchpoint (Lower EL)"; break;
        case 0x35: syn.ec_name = "Watchpoint (Same EL)"; break;
        default:   syn.ec_name = "Reserved / Unallocated EC 0x" + to_hex(syn.ec, 2); break;
    }

    if (syn.ec == 0x20 || syn.ec == 0x21 || syn.ec == 0x24 || syn.ec == 0x25) {
        switch (syn.fsc & 0x3Cu) {
            case 0x00: syn.fsc_name = "Address Size Fault Level " + std::to_string(syn.fsc & 3u); break;
            case 0x04: syn.fsc_name = "Translation Fault Level " + std::to_string(syn.fsc & 3u); break;
            case 0x08: syn.fsc_name = "Access Flag Fault Level " + std::to_string(syn.fsc & 3u); break;
            case 0x0C: syn.fsc_name = "Permission Fault Level " + std::to_string(syn.fsc & 3u); break;
            default:
                if (syn.fsc == 0x10) {
                    syn.fsc_name = "Synchronous External Abort";
                } else if ((syn.fsc & 0x3Cu) == 0x14) {
                    syn.fsc_name = "Synchronous External Abort on Page Table Walk Level " + std::to_string(syn.fsc & 3u);
                } else if (syn.fsc == 0x21) {
                    syn.fsc_name = "Alignment Fault";
                } else {
                    syn.fsc_name = "Unknown Fault Status Code 0x" + to_hex(syn.fsc, 2);
                }
                break;
        }
    } else {
        syn.fsc_name = "N/A";
    }

    return syn;
}

// -----------------------------------------------------------------------------
// System Register Diagnostic Analysis
// -----------------------------------------------------------------------------
AppleGuestSysRegFaultInfo AppleKernelFaultAnalyzer::check_sysreg_access(
    std::uint8_t op0, std::uint8_t op1, std::uint8_t crn, std::uint8_t crm, std::uint8_t op2,
    unsigned current_el, bool is_read, const CPU& cpu) const {
    
    AppleGuestSysRegFaultInfo info;
    info.op0 = op0;
    info.op1 = op1;
    info.crn = crn;
    info.crm = crm;
    info.op2 = op2;
    info.key = sysreg::make_sysreg_key(op0, op1, crn, crm, op2);
    info.is_read = is_read;
    info.current_el = current_el;

    const auto* desc = sysreg::find_descriptor_by_key(info.key);
    if (!desc) {
        info.reg_name = "Unknown_SysReg_Key_0x" + to_hex(info.key, 4);
        info.required_el = 1;
        info.fault_subtype = "UnknownRegister";
        return info;
    }

    info.reg_name = desc->name;
    info.required_el = desc->min_el;

    const auto priv = sysreg::check_privilege(*desc, current_el, is_read, cpu);
    if (priv == sysreg::PrivilegeResult::UndefinedException || current_el < desc->min_el) {
        info.fault_subtype = "PrivilegeViolation";
    } else if (priv == sysreg::PrivilegeResult::TrapToEl2) {
        info.fault_subtype = "TrapToEl2";
    } else if (priv == sysreg::PrivilegeResult::TrapToEl3) {
        info.fault_subtype = "TrapToEl3";
    } else if (!is_read && !desc->is_writable) {
        info.fault_subtype = "ReadOnlyWrite";
    } else if (is_read && !desc->is_readable) {
        info.fault_subtype = "WriteOnlyRead";
    } else {
        info.fault_subtype = "Allowed";
    }

    return info;
}

// -----------------------------------------------------------------------------
// CPU Fault State Analysis
// -----------------------------------------------------------------------------
AppleGuestFaultContext AppleKernelFaultAnalyzer::analyze_cpu_fault(
    const CPU& cpu,
    const Memory& memory,
    std::uint64_t pc_at_fault,
    TerminationReason reason,
    const std::string& error_message,
    KernelClassification classification,
    const std::string& session_id) const {

    AppleGuestFaultContext ctx;
    ctx.session_id = session_id;
    ctx.artifact_classification = classification;
    ctx.guest_pc = pc_at_fault;
    ctx.exception_level = static_cast<unsigned>(cpu.get_current_el());
    ctx.retired_instructions_at_fault = cpu.get_instruction_retirement_count();

    // Check if PC is within memory bounds to read instruction opcode
    if (pc_at_fault + 4u <= memory.size() && (pc_at_fault & 3u) == 0u) {
        ctx.instruction_word = memory.read_u32_untraced(pc_at_fault);
        ctx.instruction_length = 4;
    } else {
        ctx.instruction_word = 0;
        ctx.instruction_length = 0;
    }

    // Capture architectural exception registers
    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint64_t far = cpu.get_far_el1();
    ctx.syndrome = AppleGuestExceptionSyndrome::decode(esr);

    if (cpu.exception_taken_in_step()) {
        ctx.has_fault_va = true;
        ctx.fault_va = far;
        ctx.has_fault_pa = (far < memory.size());
        ctx.fault_pa = ctx.has_fault_pa ? far : 0;
        ctx.access_kind = ctx.syndrome.wnr ? MemoryAccessKind::Write : MemoryAccessKind::Read;
        ctx.access_width = 4;

        if (ctx.syndrome.ec == 0x22) { // PC Alignment
            ctx.category = AppleGuestExceptionClass::AlignmentFault;
            ctx.description = "PC Alignment Fault at 0x" + to_hex(far);
        } else if (ctx.syndrome.ec == 0x20 || ctx.syndrome.ec == 0x21) { // Instruction Abort
            ctx.access_kind = MemoryAccessKind::Execute;
            const auto fsc_type = ctx.syndrome.fsc & 0x3Cu;
            if (fsc_type == 0x04) {
                ctx.category = AppleGuestExceptionClass::TranslationFault;
                ctx.description = "Instruction Translation Fault (Level " + std::to_string(ctx.syndrome.fsc & 3u) + ") at 0x" + to_hex(far);
            } else if (fsc_type == 0x0C) {
                ctx.category = AppleGuestExceptionClass::PermissionFault;
                ctx.description = "Instruction Permission Fault (Level " + std::to_string(ctx.syndrome.fsc & 3u) + ") at 0x" + to_hex(far);
            } else if (fsc_type == 0x00) {
                ctx.category = AppleGuestExceptionClass::AddressSizeFault;
                ctx.description = "Instruction Address Size Fault at 0x" + to_hex(far);
            } else if (ctx.syndrome.fsc == 0x21) {
                ctx.category = AppleGuestExceptionClass::AlignmentFault;
                ctx.description = "Instruction Alignment Fault at 0x" + to_hex(far);
            } else {
                ctx.category = AppleGuestExceptionClass::InstructionAbort;
                ctx.description = "Instruction Abort (" + ctx.syndrome.fsc_name + ") at 0x" + to_hex(far);
            }
        } else if (ctx.syndrome.ec == 0x24 || ctx.syndrome.ec == 0x25) { // Data Abort
            const auto fsc_type = ctx.syndrome.fsc & 0x3Cu;
            if (fsc_type == 0x04) {
                ctx.category = AppleGuestExceptionClass::TranslationFault;
                ctx.description = "Data Translation Fault (Level " + std::to_string(ctx.syndrome.fsc & 3u) + ") at 0x" + to_hex(far);
            } else if (fsc_type == 0x0C) {
                ctx.category = AppleGuestExceptionClass::PermissionFault;
                ctx.description = "Data Permission Fault (Level " + std::to_string(ctx.syndrome.fsc & 3u) + ") at 0x" + to_hex(far);
            } else if (fsc_type == 0x00) {
                ctx.category = AppleGuestExceptionClass::AddressSizeFault;
                ctx.description = "Data Address Size Fault at 0x" + to_hex(far);
            } else if (ctx.syndrome.fsc == 0x21) {
                ctx.category = AppleGuestExceptionClass::AlignmentFault;
                ctx.description = "Data Alignment Fault at 0x" + to_hex(far);
            } else {
                ctx.category = AppleGuestExceptionClass::DataAbort;
                ctx.description = "Data Abort (" + ctx.syndrome.fsc_name + ") at 0x" + to_hex(far);
            }
        } else if (ctx.syndrome.ec == 0x18) { // Trapped MSR/MRS
            ctx.category = AppleGuestExceptionClass::SystemRegisterAccess;
            ctx.description = "System register access trapped: ISS=0x" + to_hex(ctx.syndrome.iss, 6);
        } else if (ctx.syndrome.ec == 0x00) { // Unknown / Undefined
            ctx.category = AppleGuestExceptionClass::UndefinedInstruction;
            ctx.description = "Undefined instruction opcode 0x" + to_hex(ctx.instruction_word, 8) + " at 0x" + to_hex(pc_at_fault);
        } else {
            ctx.category = AppleGuestExceptionClass::SynchronousException;
            ctx.description = "Synchronous exception (" + ctx.syndrome.ec_name + ") taken at 0x" + to_hex(pc_at_fault);
        }
    } else {
        // Exception was caught via C++ exception or execution engine termination reason
        switch (reason) {
            case TerminationReason::UnsupportedInstruction:
                ctx.category = AppleGuestExceptionClass::IllegalInstructionEncoding;
                ctx.description = "Unsupported instruction encoding: " + error_message;
                break;
            case TerminationReason::TranslationFault:
                ctx.category = AppleGuestExceptionClass::TranslationFault;
                ctx.description = "Translation fault: " + error_message;
                break;
            case TerminationReason::PermissionFault:
                ctx.category = AppleGuestExceptionClass::PermissionFault;
                ctx.description = "Permission fault: " + error_message;
                break;
            case TerminationReason::AlignmentFault:
                ctx.category = AppleGuestExceptionClass::AlignmentFault;
                ctx.description = "Alignment fault: " + error_message;
                break;
            case TerminationReason::AddressOutOfRange:
                ctx.category = AppleGuestExceptionClass::ExternalAbort;
                ctx.description = "Physical address out of range: " + error_message;
                break;
            case TerminationReason::BudgetReached:
                ctx.category = AppleGuestExceptionClass::BudgetExhaustion;
                ctx.description = "Instruction budget reached: " + error_message;
                break;
            case TerminationReason::WatchdogTimeout:
                ctx.category = AppleGuestExceptionClass::WatchdogTimeout;
                ctx.description = "Watchdog timer expired: " + error_message;
                break;
            default:
                ctx.category = AppleGuestExceptionClass::Unknown;
                ctx.description = error_message.empty() ? "Unspecified fault" : error_message;
                break;
        }
    }

    return ctx;
}

// -----------------------------------------------------------------------------
// Fault Record Serialization & Deserialization
// -----------------------------------------------------------------------------
std::vector<std::uint8_t> AppleGuestFaultRecord::serialize() const {
    std::stringstream ss;

    // Structured textual payload with deterministic key-value lines
    ss << "SESSION_ID=" << session_id << "\n";
    ss << "ARTIFACT_CLASS=" << static_cast<int>(artifact_classification) << "\n";
    ss << "ARTIFACT_ID=" << artifact_id << "\n";
    ss << "ARTIFACT_SHA256=" << artifact_sha256 << "\n";
    ss << "INITIAL_PC=0x" << std::hex << initial_pc << "\n";
    ss << "FAULTING_PC=0x" << faulting_pc << "\n";
    ss << "INSTRUCTIONS_RETIRED=" << std::dec << instructions_retired << "\n";
    ss << "TERM_REASON=" << static_cast<int>(termination_reason) << "\n";
    ss << "TERM_DETAIL=" << termination_detail << "\n";
    ss << "FAULT_CATEGORY=" << static_cast<int>(fault_context.category) << "\n";
    ss << "FAULT_DESC=" << fault_context.description << "\n";
    ss << "INSTRUCTION_WORD=0x" << std::hex << fault_context.instruction_word << "\n";
    ss << "FAULT_VA=0x" << fault_context.fault_va << "\n";
    ss << "FAULT_PA=0x" << fault_context.fault_pa << "\n";
    ss << "HAS_FAULT_VA=" << (fault_context.has_fault_va ? 1 : 0) << "\n";
    ss << "HAS_FAULT_PA=" << (fault_context.has_fault_pa ? 1 : 0) << "\n";
    ss << "ACCESS_KIND=" << static_cast<int>(fault_context.access_kind) << "\n";
    ss << "ACCESS_WIDTH=" << std::dec << fault_context.access_width << "\n";
    ss << "CURRENT_EL=" << fault_context.exception_level << "\n";
    ss << "ESR_EL1=0x" << std::hex << fault_state.esr_el1 << "\n";
    ss << "FAR_EL1=0x" << fault_state.far_el1 << "\n";
    ss << "ELR_EL1=0x" << fault_state.elr_el1 << "\n";
    ss << "SPSR_EL1=0x" << fault_state.spsr_el1 << "\n";
    ss << "SP_EL1=0x" << fault_state.sp_el1 << "\n";
    ss << "SP_EL0=0x" << fault_state.sp_el0 << "\n";
    ss << "SCTLR_EL1=0x" << sctlr_el1 << "\n";
    ss << "TCR_EL1=0x" << tcr_el1 << "\n";
    ss << "TTBR0_EL1=0x" << ttbr0_el1 << "\n";
    ss << "TTBR1_EL1=0x" << ttbr1_el1 << "\n";

    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        ss << "X" << std::dec << i << "=0x" << std::hex << fault_state.x[i] << "\n";
    }

    ss << "HISTORY_COUNT=" << std::dec << instruction_history.size() << "\n";
    for (const auto& step : instruction_history) {
        ss << "H:0x" << std::hex << step.pc << ":0x" << step.instruction_word << ":" << step.disassembly << "\n";
    }

    ss << "MEM_TRACE_COUNT=" << std::dec << memory_history.size() << "\n";
    for (const auto& mem : memory_history) {
        ss << "M:0x" << std::hex << mem.instruction_pc << ":0x" << mem.physical_address << ":" << std::dec << mem.size << ":0x" << std::hex << mem.value << "\n";
    }

    const std::string payload = ss.str();
    const std::uint32_t payload_len = static_cast<std::uint32_t>(payload.size());
    const std::uint32_t checksum = compute_adler32(
        reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());

    std::vector<std::uint8_t> output(kHeaderSize + payload_len);
    // Write 16-byte header
    std::memcpy(output.data(), &kFaultRecordMagic, 4);
    std::memcpy(output.data() + 4, &kFaultRecordVersion, 4);
    std::memcpy(output.data() + 8, &payload_len, 4);
    std::memcpy(output.data() + 12, &checksum, 4);
    // Write payload
    std::memcpy(output.data() + kHeaderSize, payload.data(), payload_len);

    return output;
}

bool AppleGuestFaultRecord::deserialize(const std::uint8_t* data, std::size_t len,
                                        AppleGuestFaultRecord* out_record,
                                        std::string* out_error) {
    if (!data || len < kHeaderSize) {
        if (out_error) *out_error = "Buffer smaller than fault record header (minimum 16 bytes required)";
        return false;
    }

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t payload_len = 0;
    std::uint32_t checksum = 0;

    std::memcpy(&magic, data, 4);
    std::memcpy(&version, data + 4, 4);
    std::memcpy(&payload_len, data + 8, 4);
    std::memcpy(&checksum, data + 12, 4);

    if (magic != kFaultRecordMagic) {
        if (out_error) *out_error = "Invalid fault record magic header (expected 'WNF1')";
        return false;
    }
    if (version != kFaultRecordVersion) {
        if (out_error) *out_error = "Unsupported fault record format version: " + std::to_string(version);
        return false;
    }
    if (len - kHeaderSize != payload_len) {
        if (out_error) *out_error = "Truncated or malformed fault record: header states " +
                                     std::to_string(payload_len) + " bytes but got " +
                                     std::to_string(len - kHeaderSize);
        return false;
    }

    const std::uint8_t* payload_bytes = data + kHeaderSize;
    const std::uint32_t computed_checksum = compute_adler32(payload_bytes, payload_len);
    if (computed_checksum != checksum) {
        if (out_error) *out_error = "Fault record data integrity checksum mismatch (corrupted record)";
        return false;
    }

    if (!out_record) return true;

    // Parse payload lines
    std::string payload_str(reinterpret_cast<const char*>(payload_bytes), payload_len);
    std::istringstream stream(payload_str);
    std::string line;

    AppleGuestFaultRecord rec;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        try {
            if (key == "SESSION_ID") rec.session_id = val;
            else if (key == "ARTIFACT_CLASS") rec.artifact_classification = static_cast<KernelClassification>(std::stoi(val));
            else if (key == "ARTIFACT_ID") rec.artifact_id = val;
            else if (key == "ARTIFACT_SHA256") rec.artifact_sha256 = val;
            else if (key == "INITIAL_PC") rec.initial_pc = std::stoull(val, nullptr, 16);
            else if (key == "FAULTING_PC") rec.faulting_pc = std::stoull(val, nullptr, 16);
            else if (key == "INSTRUCTIONS_RETIRED") rec.instructions_retired = std::stoull(val);
            else if (key == "TERM_REASON") rec.termination_reason = static_cast<TerminationReason>(std::stoi(val));
            else if (key == "TERM_DETAIL") rec.termination_detail = val;
            else if (key == "FAULT_CATEGORY") rec.fault_context.category = static_cast<AppleGuestExceptionClass>(std::stoi(val));
            else if (key == "FAULT_DESC") rec.fault_context.description = val;
            else if (key == "INSTRUCTION_WORD") rec.fault_context.instruction_word = static_cast<std::uint32_t>(std::stoul(val, nullptr, 16));
            else if (key == "FAULT_VA") rec.fault_context.fault_va = std::stoull(val, nullptr, 16);
            else if (key == "FAULT_PA") rec.fault_context.fault_pa = std::stoull(val, nullptr, 16);
            else if (key == "HAS_FAULT_VA") rec.fault_context.has_fault_va = (val == "1");
            else if (key == "HAS_FAULT_PA") rec.fault_context.has_fault_pa = (val == "1");
            else if (key == "ACCESS_KIND") rec.fault_context.access_kind = static_cast<MemoryAccessKind>(std::stoi(val));
            else if (key == "ACCESS_WIDTH") rec.fault_context.access_width = std::stoul(val);
            else if (key == "CURRENT_EL") rec.fault_context.exception_level = std::stoul(val);
            else if (key == "ESR_EL1") rec.fault_state.esr_el1 = static_cast<std::uint32_t>(std::stoul(val, nullptr, 16));
            else if (key == "FAR_EL1") rec.fault_state.far_el1 = std::stoull(val, nullptr, 16);
            else if (key == "ELR_EL1") rec.fault_state.elr_el1 = std::stoull(val, nullptr, 16);
            else if (key == "SPSR_EL1") rec.fault_state.spsr_el1 = static_cast<std::uint32_t>(std::stoul(val, nullptr, 16));
            else if (key == "SP_EL1") rec.fault_state.sp_el1 = std::stoull(val, nullptr, 16);
            else if (key == "SP_EL0") rec.fault_state.sp_el0 = std::stoull(val, nullptr, 16);
            else if (key == "SCTLR_EL1") rec.sctlr_el1 = std::stoull(val, nullptr, 16);
            else if (key == "TCR_EL1") rec.tcr_el1 = std::stoull(val, nullptr, 16);
            else if (key == "TTBR0_EL1") rec.ttbr0_el1 = std::stoull(val, nullptr, 16);
            else if (key == "TTBR1_EL1") rec.ttbr1_el1 = std::stoull(val, nullptr, 16);
            else if (key.size() >= 2 && key[0] == 'X') {
                const std::size_t reg_idx = std::stoul(key.substr(1));
                if (reg_idx < CPU::kRegisterCount) {
                    rec.fault_state.x[reg_idx] = std::stoull(val, nullptr, 16);
                }
            }
        } catch (...) {
            if (out_error) *out_error = "Malformed field in fault record payload: " + line;
            return false;
        }
    }

    *out_record = std::move(rec);
    return true;
}

// -----------------------------------------------------------------------------
// Diagnostic String Conversions
// -----------------------------------------------------------------------------
const char* AppleKernelFaultAnalyzer::exception_class_to_string(AppleGuestExceptionClass c) noexcept {
    switch (c) {
        case AppleGuestExceptionClass::UndefinedInstruction:       return "UndefinedInstruction";
        case AppleGuestExceptionClass::IllegalInstructionEncoding: return "IllegalInstructionEncoding";
        case AppleGuestExceptionClass::PrivilegedInstruction:      return "PrivilegedInstruction";
        case AppleGuestExceptionClass::SystemRegisterAccess:       return "SystemRegisterAccess";
        case AppleGuestExceptionClass::InstructionAbort:           return "InstructionAbort";
        case AppleGuestExceptionClass::DataAbort:                  return "DataAbort";
        case AppleGuestExceptionClass::TranslationFault:           return "TranslationFault";
        case AppleGuestExceptionClass::PermissionFault:            return "PermissionFault";
        case AppleGuestExceptionClass::AddressSizeFault:           return "AddressSizeFault";
        case AppleGuestExceptionClass::AlignmentFault:             return "AlignmentFault";
        case AppleGuestExceptionClass::ExternalAbort:              return "ExternalAbort";
        case AppleGuestExceptionClass::StackAccessFault:           return "StackAccessFault";
        case AppleGuestExceptionClass::MmioAccessFault:            return "MmioAccessFault";
        case AppleGuestExceptionClass::SynchronousException:       return "SynchronousException";
        case AppleGuestExceptionClass::InterruptInterruption:      return "InterruptInterruption";
        case AppleGuestExceptionClass::BudgetExhaustion:           return "BudgetExhaustion";
        case AppleGuestExceptionClass::WatchdogTimeout:            return "WatchdogTimeout";
        case AppleGuestExceptionClass::Unknown:                    return "Unknown";
    }
    return "Unknown";
}

const char* AppleKernelFaultAnalyzer::memory_access_kind_to_string(MemoryAccessKind k) noexcept {
    switch (k) {
        case MemoryAccessKind::Read:    return "Read";
        case MemoryAccessKind::Write:   return "Write";
        case MemoryAccessKind::Execute: return "Execute";
    }
    return "Unknown";
}

// -----------------------------------------------------------------------------
// Markdown Report Generator
// -----------------------------------------------------------------------------
std::string AppleKernelFaultAnalyzer::generate_diagnostic_markdown(const AppleGuestFaultRecord& rec) {
    std::stringstream ss;
    ss << "# Stage 103 — Kernel Execution Fault Analysis & Exception Diagnostic Report\n\n";

    ss << "## 1. Execution Fault Summary\n\n";
    ss << "| Parameter | Diagnostic Value |\n";
    ss << "| :--- | :--- |\n";
    ss << "| **Session Identifier** | `" << rec.session_id << "` |\n";
    ss << "| **Artifact Classification** | `" << AppleKernelExecutionEngine::classification_to_string(rec.artifact_classification) << "` |\n";
    ss << "| **Artifact Identifier** | `" << rec.artifact_id << "` |\n";
    ss << "| **Termination Reason** | `" << AppleKernelExecutionEngine::termination_reason_to_string(rec.termination_reason) << "` |\n";
    ss << "| **Termination Detail** | " << rec.termination_detail << " |\n";
    ss << "| **Fault Category** | `" << exception_class_to_string(rec.fault_context.category) << "` |\n";
    ss << "| **Faulting PC** | `0x" << to_hex(rec.faulting_pc) << "` |\n";
    ss << "| **Retired Instructions** | **" << rec.instructions_retired << "** |\n\n";

    ss << "## 2. AArch64 Architectural Exception Syndrome (ESR_EL1 & FAR_EL1)\n\n";
    ss << "| Register / Syndrome Field | Value | Architectural Interpretation |\n";
    ss << "| :--- | :--- | :--- |\n";
    ss << "| **`ESR_EL1` Raw** | `0x" << to_hex(rec.fault_state.esr_el1, 8) << "` | Complete exception syndrome register |\n";
    ss << "| **`ESR_EL1.EC` (Class)** | `0x" << to_hex(rec.fault_context.syndrome.ec, 2) << "` | " << rec.fault_context.syndrome.ec_name << " |\n";
    ss << "| **`ESR_EL1.IL` (Length)** | `" << (rec.fault_context.syndrome.il ? "1 (32-bit)" : "0 (16-bit)") << "` | Instruction length bit |\n";
    ss << "| **`ESR_EL1.ISS`** | `0x" << to_hex(rec.fault_context.syndrome.iss, 6) << "` | Instruction specific syndrome payload |\n";
    ss << "| **`ESR_EL1.FSC` (Status)** | `0x" << to_hex(rec.fault_context.syndrome.fsc, 2) << "` | " << rec.fault_context.syndrome.fsc_name << " |\n";
    ss << "| **`ESR_EL1.WnR` (Write)** | `" << (rec.fault_context.syndrome.wnr ? "1 (Write/Store)" : "0 (Read/Load)") << "` | Memory access direction |\n";
    ss << "| **`FAR_EL1` (Fault Address)**| `0x" << to_hex(rec.fault_state.far_el1) << "` | Faulting virtual/physical address |\n";
    ss << "| **`ELR_EL1` (Return PC)** | `0x" << to_hex(rec.fault_state.elr_el1) << "` | Exception preferred return address |\n";
    ss << "| **`SPSR_EL1` (Pre-fault PSTATE)**| `0x" << to_hex(rec.fault_state.spsr_el1, 8) << "` | Saved Program Status Register |\n\n";

    ss << "## 3. Fault-Time Register Forensics\n\n";
    ss << "| Register | Value | Register | Value |\n";
    ss << "| :--- | :--- | :--- | :--- |\n";
    ss << "| `PC` | `0x" << to_hex(rec.fault_state.pc) << "` | `SP_EL1` | `0x" << to_hex(rec.fault_state.sp_el1) << "` |\n";
    ss << "| `SP_EL0` | `0x" << to_hex(rec.fault_state.sp_el0) << "` | `CurrentEL` | EL" << rec.fault_state.current_el << " |\n";
    for (std::size_t i = 0; i < 16; ++i) {
        ss << "| `X" << i << "` | `0x" << to_hex(rec.fault_state.x[i]) << "` | `X" << (i + 16) << "` | `0x"
           << to_hex(rec.fault_state.x[i + 16]) << "` |\n";
    }

    return ss.str();
}

} // namespace winios::runtime
