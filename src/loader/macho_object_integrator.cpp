#include "loader/macho_object_integrator.h"

#include "loader/linux_image_loader.h"
#include "loader/macho_loader.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>

namespace winios::loader {

namespace {

constexpr std::uint32_t kMachHeaderMagic64 = 0xFEEDFACFu;
constexpr std::uint32_t kCpuTypeArm64      = 0x0100000Cu;
constexpr std::uint32_t kMhObject          = 0x1u;
constexpr std::uint32_t kMhExecute         = 0x2u;

constexpr std::uint32_t kLcSymtab          = 0x2u;
constexpr std::uint32_t kLcSegment64       = 0x19u;
constexpr std::uint64_t kLcReqDylink       = 0x80000000ULL;
constexpr std::uint32_t kLcMain            = 0x80000028u;

constexpr std::uint32_t kSectionTypeMask   = 0x000000FFu;
constexpr std::uint32_t kSectionZerofill   = 0x1u;
constexpr std::uint32_t kSectionGbZerofill = 0xCu;
constexpr std::uint32_t kSecAttrDebug      = 0x02000000u;
constexpr std::uint32_t kSecAttrPureInsn   = 0x80000000u;
constexpr std::uint32_t kSecAttrSomeInsn   = 0x00000400u;

constexpr std::uint8_t kNlistStabMask      = 0xE0u;
constexpr std::uint8_t kNlistTypeMask      = 0x0Eu;
constexpr std::uint8_t kNlistExtMask       = 0x01u;
constexpr std::uint8_t kNlistUndf          = 0x00u;
constexpr std::uint8_t kNlistAbs           = 0x02u;
constexpr std::uint8_t kNlistSect          = 0x0Eu;

constexpr std::uint8_t kNlistPrivateExt     = 0x10u;
constexpr std::uint16_t kNDescWeakDef     = 0x0080u;

constexpr std::uint8_t kArm64RelocUnsigned       = 0u;
constexpr std::uint8_t kArm64RelocBranch26       = 2u;
constexpr std::uint8_t kArm64RelocPage21         = 3u;
constexpr std::uint8_t kArm64RelocPageoff12      = 4u;
constexpr std::uint8_t kArm64RelocGotLoadPage21   = 5u;
constexpr std::uint8_t kArm64RelocGotLoadPageoff12= 6u;
constexpr std::uint8_t kArm64RelocAddend         = 10u;

#pragma pack(push, 1)
struct RawSymtabCommand {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    std::uint32_t symoff;
    std::uint32_t nsyms;
    std::uint32_t stroff;
    std::uint32_t strsize;
};

struct RawNlist64 {
    std::uint32_t n_strx;
    std::uint8_t  n_type;
    std::uint8_t  n_sect;
    std::uint16_t n_desc;
    std::uint64_t n_value;
};

struct RawRelocationInfo {
    std::int32_t  r_address;
    std::uint32_t r_info;
};
#pragma pack(pop)

template <typename T>
bool read_pod_at(const std::vector<std::uint8_t>& buf, std::size_t off, T& out) noexcept {
    if (off > buf.size() || sizeof(T) > buf.size() - off) {
        return false;
    }
    std::memcpy(&out, buf.data() + off, sizeof(T));
    return true;
}

std::string read_fixed_cstr(const char* data, std::size_t max_len) {
    std::size_t len = 0;
    while (len < max_len && data[len] != '\0') {
        ++len;
    }
    return std::string(data, len);
}

bool checked_add_u64(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
    if (a > (std::numeric_limits<std::uint64_t>::max)() - b) {
        return false;
    }
    out = a + b;
    return true;
}

std::uint64_t align_up_u64(std::uint64_t val, std::uint64_t align) noexcept {
    if (align <= 1u) {
        return val;
    }
    const std::uint64_t rem = val % align;
    return rem == 0u ? val : val + (align - rem);
}

std::uint32_t read_u32_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8u)
         | (static_cast<std::uint32_t>(p[2]) << 16u)
         | (static_cast<std::uint32_t>(p[3]) << 24u);
}

void write_u32_le(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8u) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16u) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24u) & 0xFFu);
}

void write_u64_le(std::uint8_t* p, std::uint64_t v) noexcept {
    for (unsigned i = 0; i < 8u; ++i) {
        p[i] = static_cast<std::uint8_t>((v >> (i * 8u)) & 0xFFu);
    }
}

bool checked_apply_signed_addend(std::uint64_t base, std::int64_t addend, std::uint64_t& out) noexcept {
    if (addend >= 0) {
        return checked_add_u64(base, static_cast<std::uint64_t>(addend), out);
    }
    const std::uint64_t neg = static_cast<std::uint64_t>(-(addend + 1)) + 1ULL;
    if (base < neg) {
        return false;
    }
    out = base - neg;
    return true;
}

bool regions_overlap(std::uint64_t a_start, std::uint64_t a_span,
                     std::uint64_t b_start, std::uint64_t b_span) noexcept {
    if (a_span == 0u || b_span == 0u) {
        return false;
    }
    std::uint64_t a_end = 0;
    std::uint64_t b_end = 0;
    if (!checked_add_u64(a_start, a_span, a_end) || !checked_add_u64(b_start, b_span, b_end)) {
        return true;
    }
    return a_start < b_end && b_start < a_end;
}

bool validate_binding_list(const std::vector<BoundedExternalBinding>& bindings,
                           std::map<std::string, BoundedExternalBinding>& out_map,
                           ObjectIntegrationStatus& out_status,
                           std::string& out_error) {
    out_map.clear();
    for (const auto& b : bindings) {
        if (b.symbol_name.empty()) {
            out_status = ObjectIntegrationStatus::MalformedSymbolTable;
            out_error = "External binding has empty symbol_name";
            return false;
        }
        if (b.classification == SymbolProvenanceClassification::UnresolvedExternal ||
            b.classification == SymbolProvenanceClassification::UnsupportedSymbol) {
            out_status = ObjectIntegrationStatus::UnresolvedExternal;
            out_error = "External binding cannot specify UnresolvedExternal/UnsupportedSymbol: " + b.symbol_name;
            return false;
        }
        if (b.classification == SymbolProvenanceClassification::GenuineUpstream &&
            !b.stub_instructions.empty()) {
            out_status = ObjectIntegrationStatus::MalformedSymbolTable;
            out_error = "Synthetic stub instructions cannot be classified GenuineUpstream: " + b.symbol_name;
            return false;
        }
        out_map[b.symbol_name] = b;
    }
    return true;
}

bool write_staged_bytes(Memory& guest_mem,
                        GuestMemoryMap* memory_map,
                        std::uint64_t virtual_base_bias,
                        std::uint64_t va,
                        const std::vector<std::uint8_t>& data,
                        std::uint32_t vm_prot,
                        const std::string& region_name,
                        std::uint64_t align_granule,
                        ObjectIntegrationStatus& out_status,
                        std::string& out_error) {
    if ((vm_prot & 0x2u) != 0u && (vm_prot & 0x4u) != 0u) {
        out_status = ObjectIntegrationStatus::WxPermissionViolation;
        out_error = "W^X violation on staged region: " + region_name;
        return false;
    }
    if (va < virtual_base_bias) {
        out_status = ObjectIntegrationStatus::MemoryBoundsExceeded;
        out_error = "Staged VA precedes virtual_base_bias: " + region_name;
        return false;
    }
    const std::uint64_t phys_off = va - virtual_base_bias;
    std::uint64_t phys_end = 0;
    if (!checked_add_u64(phys_off, data.size(), phys_end) || phys_end > guest_mem.size()) {
        out_status = ObjectIntegrationStatus::MemoryBoundsExceeded;
        out_error = "Staged region exceeds Memory capacity: " + region_name;
        return false;
    }
    for (std::size_t i = 0; i < data.size(); ++i) {
        guest_mem.write_u8(static_cast<std::size_t>(phys_off) + i, data[i]);
    }
    if (memory_map != nullptr && !data.empty()) {
        GuestMemoryRegion reg;
        reg.name = region_name;
        reg.type = GuestMemoryRegionType::KernelImage;
        reg.base_address = va;
        reg.size = align_up_u64(data.size(), align_granule);
        reg.is_reserved = true;
        if (!memory_map->add_region(reg)) {
            out_status = ObjectIntegrationStatus::MemoryBoundsExceeded;
            out_error = "GuestMemoryMap rejected overlapping region: " + region_name;
            return false;
        }
    }
    return true;
}

void append_bytes(std::vector<std::uint8_t>& out, const void* ptr, std::size_t len) {
    const auto* p = static_cast<const std::uint8_t*>(ptr);
    out.insert(out.end(), p, p + len);
}

} // namespace

const char* symbol_provenance_string(SymbolProvenanceClassification c) noexcept {
    switch (c) {
    case SymbolProvenanceClassification::GenuineUpstream:     return "GenuineUpstream";
    case SymbolProvenanceClassification::WinIOSArchitectural: return "WinIOSArchitectural";
    case SymbolProvenanceClassification::TestHarnessStub:     return "TestHarnessStub";
    case SymbolProvenanceClassification::UnresolvedExternal:  return "UnresolvedExternal";
    case SymbolProvenanceClassification::UnsupportedSymbol:   return "UnsupportedSymbol";
    }
    return "Unknown";
}

const char* object_integration_status_string(ObjectIntegrationStatus s) noexcept {
    switch (s) {
    case ObjectIntegrationStatus::Success:                    return "Success";
    case ObjectIntegrationStatus::InvalidMagic:               return "InvalidMagic";
    case ObjectIntegrationStatus::UnsupportedCpuType:         return "UnsupportedCpuType";
    case ObjectIntegrationStatus::NotObjectFile:              return "NotObjectFile";
    case ObjectIntegrationStatus::TruncatedHeader:            return "TruncatedHeader";
    case ObjectIntegrationStatus::CorruptedLoadCommands:      return "CorruptedLoadCommands";
    case ObjectIntegrationStatus::InvalidSectionBounds:       return "InvalidSectionBounds";
    case ObjectIntegrationStatus::MalformedSymbolTable:       return "MalformedSymbolTable";
    case ObjectIntegrationStatus::UnresolvedExternal:         return "UnresolvedExternal";
    case ObjectIntegrationStatus::UnsupportedRelocationType:  return "UnsupportedRelocationType";
    case ObjectIntegrationStatus::RelocationOutOfBounds:      return "RelocationOutOfBounds";
    case ObjectIntegrationStatus::RelocationRangeOverflow:    return "RelocationRangeOverflow";
    case ObjectIntegrationStatus::RelocationOpcodeMismatch:   return "RelocationOpcodeMismatch";
    case ObjectIntegrationStatus::WxPermissionViolation:      return "WxPermissionViolation";
    case ObjectIntegrationStatus::MemoryBoundsExceeded:       return "MemoryBoundsExceeded";
    case ObjectIntegrationStatus::IntegerOverflow:            return "IntegerOverflow";
    case ObjectIntegrationStatus::FileReadError:              return "FileReadError";
    }
    return "Unknown";
}

const MachOObjectSectionInfo* MachOObjectParseResult::find_section(
    const std::string& segname,
    const std::string& sectname) const noexcept {
    for (const auto& s : sections) {
        if (s.segname == segname && s.sectname == sectname) {
            return &s;
        }
    }
    return nullptr;
}

const MachOSymbolEntry* MachOObjectParseResult::find_symbol(const std::string& name) const noexcept {
    for (const auto& sym : symbols) {
        if (sym.name == name) {
            return &sym;
        }
    }
    return nullptr;
}

bool MachOIntegrationResult::find_symbol_va(const std::string& name, std::uint64_t& out_va) const noexcept {
    for (const auto& sym : symbols) {
        if (sym.name == name &&
            sym.classification != SymbolProvenanceClassification::UnresolvedExternal &&
            sym.classification != SymbolProvenanceClassification::UnsupportedSymbol &&
            sym.resolved_va != 0u) {
            out_va = sym.resolved_va;
            return true;
        }
    }
    return false;
}

SymbolProvenanceClassification MachOIntegrationResult::classify_va(std::uint64_t va) const noexcept {
    for (const auto& r : provenance_regions) {
        std::uint64_t end_va = 0;
        if (!checked_add_u64(r.va_start, r.size, end_va)) {
            continue;
        }
        if (va >= r.va_start && va < end_va) {
            return r.classification;
        }
    }
    return SymbolProvenanceClassification::UnresolvedExternal;
}

bool MachOObjectIntegrator::encode_branch26(
    std::uint32_t original_insn,
    std::uint64_t pc_va,
    std::uint64_t target_va,
    std::uint32_t& out_patched_insn,
    std::string& out_error) {
    const std::uint32_t top6 = original_insn & 0xFC000000u;
    if (top6 != 0x14000000u && top6 != 0x94000000u) {
        out_error = "Instruction is neither ARM64 B nor BL";
        return false;
    }
    if ((pc_va & 0x3u) != 0u || (target_va & 0x3u) != 0u) {
        out_error = "BRANCH26 PC or target VA is not 4-byte aligned";
        return false;
    }

    // Compute signed byte displacement without UB signed overflow.
    std::int64_t delta = 0;
    if (target_va >= pc_va) {
        const std::uint64_t diff = target_va - pc_va;
        if (diff > 0x07FFFFFCULL) {
            out_error = "BRANCH26 forward displacement exceeds +128 MiB range";
            return false;
        }
        delta = static_cast<std::int64_t>(diff);
    } else {
        const std::uint64_t diff = pc_va - target_va;
        if (diff > 0x08000000ULL) {
            out_error = "BRANCH26 backward displacement exceeds -128 MiB range";
            return false;
        }
        delta = -static_cast<std::int64_t>(diff);
    }

    const std::int64_t imm26_signed = delta >> 2;
    const std::uint32_t imm26 = static_cast<std::uint32_t>(imm26_signed) & 0x03FFFFFFu;
    out_patched_insn = top6 | imm26;
    return true;
}

bool MachOObjectIntegrator::encode_page21(
    std::uint32_t original_insn,
    std::uint64_t pc_va,
    std::uint64_t target_va,
    std::uint32_t& out_patched_insn,
    std::string& out_error) {
    if ((original_insn & 0x9F000000u) != 0x90000000u) {
        out_error = "Instruction is not ARM64 ADRP for PAGE21 relocation";
        return false;
    }
    if ((pc_va & 0x3u) != 0u) {
        out_error = "PAGE21 PC VA is not 4-byte aligned";
        return false;
    }

    const std::uint64_t target_page = target_va & ~0xFFFULL;
    const std::uint64_t pc_page     = pc_va & ~0xFFFULL;
    std::int64_t delta_pages = 0;
    if (target_page >= pc_page) {
        const std::uint64_t diff_pages = (target_page - pc_page) >> 12u;
        if (diff_pages > 0xFFFFFULL) {
            out_error = "PAGE21 forward displacement exceeds +4 GiB ADRP range";
            return false;
        }
        delta_pages = static_cast<std::int64_t>(diff_pages);
    } else {
        const std::uint64_t diff_pages = (pc_page - target_page) >> 12u;
        if (diff_pages > 0x100000ULL) {
            out_error = "PAGE21 backward displacement exceeds -4 GiB ADRP range";
            return false;
        }
        delta_pages = -static_cast<std::int64_t>(diff_pages);
    }

    const std::uint32_t imm21 = static_cast<std::uint32_t>(delta_pages) & 0x1FFFFFu;
    const std::uint32_t immlo = imm21 & 0x3u;
    const std::uint32_t immhi = (imm21 >> 2u) & 0x7FFFFu;
    out_patched_insn = (original_insn & 0x9F00001Fu) | (immlo << 29u) | (immhi << 5u);
    return true;
}

bool MachOObjectIntegrator::encode_pageoff12(
    std::uint32_t original_insn,
    std::uint64_t target_va,
    std::uint32_t& out_patched_insn,
    std::string& out_error) {
    const std::uint64_t page_off = target_va & 0xFFFULL;
    std::uint32_t scale = 0;

    if ((original_insn & 0x7FC00000u) == 0x11000000u) {
        // ADD (immediate) 32-bit or 64-bit, unshifted (sh == 0)
        scale = 0u;
    } else if ((original_insn & 0x3B000000u) == 0x39000000u) {
        // Load/Store register (unsigned immediate)
        const std::uint32_t size_bits = (original_insn >> 30u) & 0x3u;
        const bool is_simd = ((original_insn >> 26u) & 0x1u) != 0u;
        const std::uint32_t opc = (original_insn >> 22u) & 0x3u;
        scale = (is_simd && size_bits == 0u && (opc & 0x2u) != 0u) ? 4u : size_bits;
    } else {
        out_error = "Instruction is neither ADD(imm) nor Load/Store(uimm) for PAGEOFF12 relocation";
        return false;
    }

    if ((page_off & ((1ULL << scale) - 1ULL)) != 0ULL) {
        out_error = "PAGEOFF12 offset is unaligned for instruction access width";
        return false;
    }

    const std::uint32_t imm12 = static_cast<std::uint32_t>((page_off >> scale) & 0xFFFu);
    out_patched_insn = (original_insn & 0xFFC003FFu) | (imm12 << 10u);
    return true;
}

bool MachOMultiObjectLinkResult::find_symbol_va(const std::string& name, std::uint64_t& out_va) const noexcept {
    MachOSymbolEntry sym;
    if (find_symbol(name, sym)) {
        out_va = sym.resolved_va;
        return true;
    }
    return false;
}

bool MachOMultiObjectLinkResult::find_symbol(const std::string& name, MachOSymbolEntry& out_sym) const noexcept {
    for (const auto& sym : resolved_symbols) {
        if (sym.name == name &&
            sym.classification != SymbolProvenanceClassification::UnresolvedExternal &&
            sym.classification != SymbolProvenanceClassification::UnsupportedSymbol &&
            sym.resolved_va != 0u) {
            out_sym = sym;
            return true;
        }
    }
    return false;
}

SymbolProvenanceClassification MachOMultiObjectLinkResult::classify_va(std::uint64_t va) const noexcept {
    for (const auto& r : provenance_regions) {
        std::uint64_t end_va = 0;
        if (!checked_add_u64(r.va_start, r.size, end_va)) {
            continue;
        }
        if (va >= r.va_start && va < end_va) {
            return r.classification;
        }
    }
    return SymbolProvenanceClassification::UnresolvedExternal;
}

MachOObjectParseResult MachOObjectIntegrator::parse_object_file(const std::string& path) {
    MachOObjectParseResult res;
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        res.status = ObjectIntegrationStatus::FileReadError;
        res.error_message = "Failed to open Mach-O object file: " + path;
        return res;
    }
    const std::streamoff sz = ifs.tellg();
    if (sz < 0) {
        res.status = ObjectIntegrationStatus::FileReadError;
        res.error_message = "Failed to determine Mach-O object file size: " + path;
        return res;
    }
    ifs.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sz));
    if (sz > 0 && !ifs.read(reinterpret_cast<char*>(bytes.data()), sz)) {
        res.status = ObjectIntegrationStatus::FileReadError;
        res.error_message = "Failed to read Mach-O object file bytes: " + path;
        return res;
    }
    return parse_object(bytes);
}

MachOObjectParseResult MachOObjectIntegrator::parse_object(const std::vector<std::uint8_t>& bytes) {
    MachOObjectParseResult res;
    if (bytes.size() < sizeof(MachOHeader64)) {
        res.status = ObjectIntegrationStatus::TruncatedHeader;
        res.error_message = "Buffer smaller than mach_header_64";
        return res;
    }

    MachOHeader64 hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(MachOHeader64));
    res.magic      = hdr.magic;
    res.cputype    = hdr.cputype;
    res.cpusubtype = hdr.cpusubtype;
    res.filetype   = hdr.filetype;
    res.ncmds      = hdr.ncmds;
    res.sizeofcmds = hdr.sizeofcmds;
    res.flags      = hdr.flags;

    if (hdr.magic != kMachHeaderMagic64) {
        res.status = ObjectIntegrationStatus::InvalidMagic;
        res.error_message = "Expected MH_MAGIC_64 (0xFEEDFACF)";
        return res;
    }
    if (hdr.cputype != kCpuTypeArm64) {
        res.status = ObjectIntegrationStatus::UnsupportedCpuType;
        res.error_message = "Expected CPU_TYPE_ARM64 (0x0100000C)";
        return res;
    }
    if (hdr.filetype != kMhObject) {
        res.status = ObjectIntegrationStatus::NotObjectFile;
        res.error_message = "Expected MH_OBJECT (filetype == 1)";
        return res;
    }

    std::uint64_t cmds_end = 0;
    if (!checked_add_u64(sizeof(MachOHeader64), hdr.sizeofcmds, cmds_end) ||
        cmds_end > bytes.size()) {
        res.status = ObjectIntegrationStatus::CorruptedLoadCommands;
        res.error_message = "Load commands region exceeds buffer size";
        return res;
    }

    res.object_sha256_hex = LinuxImageLoader::compute_sha256(bytes.data(), bytes.size());

    std::size_t cmd_off = sizeof(MachOHeader64);
    bool has_symtab = false;
    RawSymtabCommand symtab{};

    for (std::uint32_t i = 0; i < hdr.ncmds; ++i) {
        MachOLoadCommand lc{};
        if (cmd_off + sizeof(MachOLoadCommand) > cmds_end ||
            !read_pod_at(bytes, cmd_off, lc)) {
            res.status = ObjectIntegrationStatus::CorruptedLoadCommands;
            res.error_message = "Load command header truncated";
            return res;
        }
        if (lc.cmdsize < sizeof(MachOLoadCommand)) {
            res.status = ObjectIntegrationStatus::CorruptedLoadCommands;
            res.error_message = "Load command cmdsize smaller than load_command";
            return res;
        }
        std::uint64_t next_cmd = 0;
        if (!checked_add_u64(cmd_off, lc.cmdsize, next_cmd) || next_cmd > cmds_end) {
            res.status = ObjectIntegrationStatus::CorruptedLoadCommands;
            res.error_message = "Load command extends past sizeofcmds";
            return res;
        }

        if (lc.cmd == kLcSegment64) {
            MachOSegmentCommand64 seg{};
            if (lc.cmdsize < sizeof(MachOSegmentCommand64) ||
                !read_pod_at(bytes, cmd_off, seg)) {
                res.status = ObjectIntegrationStatus::CorruptedLoadCommands;
                res.error_message = "LC_SEGMENT_64 command truncated";
                return res;
            }
            const std::uint64_t needed_sections_bytes =
                static_cast<std::uint64_t>(seg.nsects) * sizeof(MachOSection64);
            std::uint64_t total_seg_cmd = 0;
            if (!checked_add_u64(sizeof(MachOSegmentCommand64), needed_sections_bytes, total_seg_cmd) ||
                total_seg_cmd > lc.cmdsize) {
                res.status = ObjectIntegrationStatus::CorruptedLoadCommands;
                res.error_message = "LC_SEGMENT_64 nsects exceeds cmdsize";
                return res;
            }

            for (std::uint32_t s = 0; s < seg.nsects; ++s) {
                MachOSection64 raw_sec{};
                const std::size_t sec_off =
                    cmd_off + sizeof(MachOSegmentCommand64) + static_cast<std::size_t>(s) * sizeof(MachOSection64);
                if (!read_pod_at(bytes, sec_off, raw_sec)) {
                    res.status = ObjectIntegrationStatus::InvalidSectionBounds;
                    res.error_message = "Failed reading section_64 header";
                    return res;
                }

                MachOObjectSectionInfo sec;
                sec.sectname   = read_fixed_cstr(raw_sec.sectname, 16);
                sec.segname    = read_fixed_cstr(raw_sec.segname, 16);
                sec.addr       = raw_sec.addr;
                sec.size       = raw_sec.size;
                sec.offset     = raw_sec.offset;
                sec.align_log2 = raw_sec.align;
                sec.reloff     = raw_sec.reloff;
                sec.nreloc     = raw_sec.nreloc;
                sec.flags      = raw_sec.flags;
                sec.is_debug   = (raw_sec.flags & kSecAttrDebug) != 0u;
                const std::uint32_t sec_type = raw_sec.flags & kSectionTypeMask;
                sec.is_zerofill = (sec_type == kSectionZerofill || sec_type == kSectionGbZerofill);
                sec.is_executable = !sec.is_debug &&
                    ((raw_sec.flags & (kSecAttrPureInsn | kSecAttrSomeInsn)) != 0u ||
                     sec.segname == "__TEXT");

                if (!sec.is_zerofill && sec.size > 0u) {
                    std::uint64_t sec_end = 0;
                    if (!checked_add_u64(sec.offset, sec.size, sec_end) || sec_end > bytes.size()) {
                        res.status = ObjectIntegrationStatus::InvalidSectionBounds;
                        res.error_message = "Section payload exceeds file bounds: " + sec.sectname;
                        return res;
                    }
                    sec.bytes.assign(
                        bytes.begin() + static_cast<std::ptrdiff_t>(sec.offset),
                        bytes.begin() + static_cast<std::ptrdiff_t>(sec_end));
                }

                if (sec.nreloc > 0u) {
                    const std::uint64_t rel_bytes =
                        static_cast<std::uint64_t>(sec.nreloc) * sizeof(RawRelocationInfo);
                    std::uint64_t rel_end = 0;
                    if (!checked_add_u64(sec.reloff, rel_bytes, rel_end) || rel_end > bytes.size()) {
                        res.status = ObjectIntegrationStatus::RelocationOutOfBounds;
                        res.error_message = "Section relocations exceed file bounds: " + sec.sectname;
                        return res;
                    }
                    for (std::uint32_t r = 0; r < sec.nreloc; ++r) {
                        RawRelocationInfo rr{};
                        const std::size_t roff =
                            static_cast<std::size_t>(sec.reloff) +
                            static_cast<std::size_t>(r) * sizeof(RawRelocationInfo);
                        if (!read_pod_at(bytes, roff, rr)) {
                            res.status = ObjectIntegrationStatus::RelocationOutOfBounds;
                            res.error_message = "Truncated relocation_info entry";
                            return res;
                        }
                        if ((static_cast<std::uint32_t>(rr.r_address) & 0x80000000u) != 0u) {
                            res.status = ObjectIntegrationStatus::UnsupportedRelocationType;
                            res.error_message = "Scattered relocations are not supported on ARM64 MH_OBJECT";
                            return res;
                        }
                        MachORelocEntry entry;
                        entry.r_address    = rr.r_address;
                        entry.symbol_index = rr.r_info & 0x00FFFFFFu;
                        entry.pcrel        = ((rr.r_info >> 24u) & 0x1u) != 0u;
                        entry.length       = static_cast<std::uint8_t>((rr.r_info >> 25u) & 0x3u);
                        entry.is_extern    = ((rr.r_info >> 27u) & 0x1u) != 0u;
                        entry.type         = static_cast<std::uint8_t>((rr.r_info >> 28u) & 0xFu);
                        sec.relocations.push_back(entry);
                    }
                }

                res.sections.push_back(std::move(sec));
            }
        } else if (lc.cmd == kLcSymtab) {
            if (lc.cmdsize < sizeof(RawSymtabCommand) ||
                !read_pod_at(bytes, cmd_off, symtab)) {
                res.status = ObjectIntegrationStatus::MalformedSymbolTable;
                res.error_message = "LC_SYMTAB truncated";
                return res;
            }
            has_symtab = true;
        }

        cmd_off = static_cast<std::size_t>(next_cmd);
    }

    if (has_symtab) {
        const std::uint64_t sym_bytes =
            static_cast<std::uint64_t>(symtab.nsyms) * sizeof(RawNlist64);
        std::uint64_t sym_end = 0;
        std::uint64_t str_end = 0;
        if (!checked_add_u64(symtab.symoff, sym_bytes, sym_end) || sym_end > bytes.size() ||
            !checked_add_u64(symtab.stroff, symtab.strsize, str_end) || str_end > bytes.size()) {
            res.status = ObjectIntegrationStatus::MalformedSymbolTable;
            res.error_message = "Symbol or string table exceeds file bounds";
            return res;
        }

        for (std::uint32_t i = 0; i < symtab.nsyms; ++i) {
            RawNlist64 nl{};
            const std::size_t noff =
                static_cast<std::size_t>(symtab.symoff) +
                static_cast<std::size_t>(i) * sizeof(RawNlist64);
            if (!read_pod_at(bytes, noff, nl)) {
                res.status = ObjectIntegrationStatus::MalformedSymbolTable;
                res.error_message = "Truncated nlist_64 symbol entry";
                return res;
            }
            if (nl.n_strx >= symtab.strsize) {
                res.status = ObjectIntegrationStatus::MalformedSymbolTable;
                res.error_message = "Symbol n_strx offset exceeds LC_SYMTAB string table";
                return res;
            }
            const char* str_base = reinterpret_cast<const char*>(bytes.data() + symtab.stroff);
            const std::size_t max_chars = static_cast<std::size_t>(symtab.strsize - nl.n_strx);

            MachOSymbolEntry sym;
            sym.index       = i;
            sym.name        = read_fixed_cstr(str_base + nl.n_strx, max_chars);
            sym.n_type      = nl.n_type;
            sym.n_sect      = nl.n_sect;
            sym.n_desc      = nl.n_desc;
            sym.n_value     = nl.n_value;
            sym.is_stab     = (nl.n_type & kNlistStabMask) != 0u;
            sym.is_external = (nl.n_type & kNlistExtMask) != 0u;

            const std::uint8_t n_type_field = nl.n_type & kNlistTypeMask;
            if (!sym.is_stab && n_type_field == kNlistSect) {
                if (nl.n_sect == 0u || nl.n_sect > res.sections.size()) {
                    res.status = ObjectIntegrationStatus::MalformedSymbolTable;
                    res.error_message = "Symbol references invalid section ordinal: " + sym.name;
                    return res;
                }
                sym.is_defined = true;
                sym.classification = SymbolProvenanceClassification::GenuineUpstream;
            } else if (!sym.is_stab && n_type_field == kNlistAbs) {
                sym.is_defined = true;
                sym.classification = SymbolProvenanceClassification::GenuineUpstream;
            } else if (!sym.is_stab && n_type_field == kNlistUndf) {
                sym.is_undefined = true;
                sym.classification = SymbolProvenanceClassification::UnresolvedExternal;
            } else {
                sym.classification = SymbolProvenanceClassification::UnsupportedSymbol;
            }

            res.symbols.push_back(std::move(sym));
        }
    }

    res.status = ObjectIntegrationStatus::Success;
    return res;
}

MachOIntegrationResult MachOObjectIntegrator::link_object(
    const MachOObjectParseResult& parsed,
    const MachOIntegrationOptions& opts) {
    MachOIntegrationResult out;
    out.object_sha256_hex = parsed.object_sha256_hex;

    if (!parsed.is_success()) {
        out.status = parsed.status;
        out.error_message = parsed.error_message;
        return out;
    }

    if ((opts.text_base_va & 0x3u) != 0u ||
        (opts.stub_base_va & (kPageGranuleBytes - 1u)) != 0u) {
        out.status = ObjectIntegrationStatus::MemoryBoundsExceeded;
        out.error_message = "text_base_va must be 4-byte aligned and stub_base_va must be 16 KiB aligned";
        return out;
    }

    const MachOObjectSectionInfo* text_sec = parsed.find_section("__TEXT", "__text");
    if (!text_sec || text_sec->bytes.empty()) {
        out.status = ObjectIntegrationStatus::InvalidSectionBounds;
        out.error_message = "MH_OBJECT missing non-empty __TEXT,__text section";
        return out;
    }

    // Track section base VAs per 1-based ordinal (Mach-O n_sect is 1-based).
    std::vector<std::uint64_t> section_vas(parsed.sections.size(), 0u);
    std::size_t text_sec_index = parsed.sections.size();
    for (std::size_t i = 0; i < parsed.sections.size(); ++i) {
        if (&parsed.sections[i] == text_sec) {
            text_sec_index = i;
            section_vas[i] = opts.text_base_va;
            break;
        }
    }

    out.text_va = opts.text_base_va;
    out.text_size = text_sec->bytes.size();
    out.relocated_text_bytes = text_sec->bytes;

    // Validate caller external bindings upfront (no provenance spoofing allowed).
    std::map<std::string, BoundedExternalBinding> binding_map;
    if (!validate_binding_list(opts.external_bindings, binding_map, out.status, out.error_message)) {
        return out;
    }

    // Assign VAs to defined object symbols and bind requested external symbols.
    out.symbols = parsed.symbols;
    std::uint64_t next_stub_offset = 0;
    for (auto& sym : out.symbols) {
        if (sym.is_stab) {
            continue;
        }
        if (sym.is_defined && (sym.n_type & kNlistTypeMask) == kNlistSect) {
            const std::size_t sec_idx = static_cast<std::size_t>(sym.n_sect - 1u);
            if (sec_idx != text_sec_index) {
                // Only non-debug code/data sections staged in bounded integrator.
                continue;
            }
            if (sym.n_value < text_sec->addr) {
                out.status = ObjectIntegrationStatus::InvalidSectionBounds;
                out.error_message = "Symbol n_value precedes section base: " + sym.name;
                return out;
            }
            const std::uint64_t rel_off = sym.n_value - text_sec->addr;
            if (rel_off > text_sec->size) {
                out.status = ObjectIntegrationStatus::InvalidSectionBounds;
                out.error_message = "Symbol n_value exceeds __TEXT,__text size: " + sym.name;
                return out;
            }
            if (!checked_add_u64(opts.text_base_va, rel_off, sym.resolved_va)) {
                out.status = ObjectIntegrationStatus::IntegerOverflow;
                out.error_message = "Overflow computing resolved_va for " + sym.name;
                return out;
            }
            sym.classification = SymbolProvenanceClassification::GenuineUpstream;
        } else if (sym.is_undefined) {
            auto it = binding_map.find(sym.name);
            if (it == binding_map.end()) {
                sym.classification = SymbolProvenanceClassification::UnresolvedExternal;
                sym.resolved_va = 0;
                continue;
            }
            const auto& binding = it->second;
            sym.classification = binding.classification;
            if (binding.prebound_va != 0u) {
                sym.resolved_va = binding.prebound_va;
            } else {
                std::vector<std::uint32_t> insns = binding.stub_instructions;
                if (insns.empty()) {
                    insns.push_back(0xD65F03C0u); // ARM64 RET
                }
                if (!checked_add_u64(opts.stub_base_va, next_stub_offset, sym.resolved_va)) {
                    out.status = ObjectIntegrationStatus::IntegerOverflow;
                    out.error_message = "Overflow placing stub for " + sym.name;
                    return out;
                }
                for (std::uint32_t insn : insns) {
                    std::uint8_t buf[4];
                    write_u32_le(buf, insn);
                    append_bytes(out.stub_bytes, buf, sizeof(buf));
                }
                next_stub_offset += static_cast<std::uint64_t>(insns.size()) * 4u;
            }
            if (sym.classification == SymbolProvenanceClassification::TestHarnessStub) {
                out.has_test_harness_stubs = true;
                out.stub_symbols.push_back(sym.name);
            } else if (sym.classification == SymbolProvenanceClassification::WinIOSArchitectural) {
                out.has_winios_architectural_bindings = true;
            }
        }
    }

    out.stub_va = out.stub_bytes.empty() ? 0u : opts.stub_base_va;
    out.stub_size = out.stub_bytes.size();

    // Verify stub region and text region do not overlap when stubs exist.
    if (!out.stub_bytes.empty()) {
        std::uint64_t text_end = 0;
        std::uint64_t stub_end = 0;
        const std::uint64_t aligned_text_span = align_up_u64(out.text_size, kPageGranuleBytes);
        const std::uint64_t aligned_stub_span = align_up_u64(out.stub_size, kPageGranuleBytes);
        if (!checked_add_u64(out.text_va, aligned_text_span, text_end) ||
            !checked_add_u64(out.stub_va, aligned_stub_span, stub_end)) {
            out.status = ObjectIntegrationStatus::IntegerOverflow;
            out.error_message = "Overflow checking text/stub overlap";
            return out;
        }
        if (out.text_va < stub_end && out.stub_va < text_end) {
            out.status = ObjectIntegrationStatus::MemoryBoundsExceeded;
            out.error_message = "Stub RX page overlaps genuine upstream __TEXT RX page";
            return out;
        }
    }

    // Validate non-debug section relocations across all non-debug sections.
    for (std::size_t si = 0; si < parsed.sections.size(); ++si) {
        const auto& sec = parsed.sections[si];
        if (sec.is_debug) {
            continue;
        }
        if (si != text_sec_index && !sec.relocations.empty()) {
            out.status = ObjectIntegrationStatus::UnsupportedRelocationType;
            out.error_message = "Unsupported relocations in non-text section: " + sec.sectname;
            return out;
        }
    }

    // Apply ARM64_RELOC_BRANCH26 relocations to __TEXT,__text.
    for (const auto& r : text_sec->relocations) {
        MachORelocEntry entry = r;
        if (!entry.is_extern) {
            out.status = ObjectIntegrationStatus::UnsupportedRelocationType;
            out.error_message = "Non-extern ARM64 __TEXT relocation unsupported";
            return out;
        }
        if (entry.type != kArm64RelocBranch26) {
            out.status = ObjectIntegrationStatus::UnsupportedRelocationType;
            out.error_message = "Unsupported relocation type (expected ARM64_RELOC_BRANCH26)";
            return out;
        }
        if (!entry.pcrel || entry.length != 2u) {
            out.status = ObjectIntegrationStatus::UnsupportedRelocationType;
            out.error_message = "ARM64_RELOC_BRANCH26 must have pcrel=1 and length=2";
            return out;
        }
        if (entry.r_address < 0 || (entry.r_address & 0x3) != 0) {
            out.status = ObjectIntegrationStatus::RelocationOutOfBounds;
            out.error_message = "Relocation r_address negative or unaligned";
            return out;
        }
        const std::uint64_t off = static_cast<std::uint64_t>(entry.r_address);
        if (off + 4u > out.relocated_text_bytes.size()) {
            out.status = ObjectIntegrationStatus::RelocationOutOfBounds;
            out.error_message = "Relocation site exceeds __TEXT,__text size";
            return out;
        }
        if (entry.symbol_index >= out.symbols.size()) {
            out.status = ObjectIntegrationStatus::MalformedSymbolTable;
            out.error_message = "Relocation symbol_index out of range";
            return out;
        }

        const auto& target_sym = out.symbols[entry.symbol_index];
        entry.target_symbol = target_sym.name;
        entry.target_classification = target_sym.classification;
        if (!checked_add_u64(out.text_va, off, entry.site_va)) {
            out.status = ObjectIntegrationStatus::IntegerOverflow;
            out.error_message = "Overflow computing relocation site VA";
            return out;
        }

        if (target_sym.classification == SymbolProvenanceClassification::UnresolvedExternal ||
            target_sym.classification == SymbolProvenanceClassification::UnsupportedSymbol ||
            target_sym.resolved_va == 0u) {
            if (std::find(out.unresolved_symbols.begin(), out.unresolved_symbols.end(), target_sym.name) ==
                out.unresolved_symbols.end()) {
                out.unresolved_symbols.push_back(target_sym.name);
            }
            out.relocations.push_back(entry);
            continue;
        }

        entry.target_va = target_sym.resolved_va;
        entry.original_insn = read_u32_le(out.relocated_text_bytes.data() + off);

        std::string enc_err;
        if (!encode_branch26(entry.original_insn, entry.site_va, entry.target_va,
                             entry.patched_insn, enc_err)) {
            out.status = (enc_err.find("neither") != std::string::npos)
                ? ObjectIntegrationStatus::RelocationOpcodeMismatch
                : ObjectIntegrationStatus::RelocationRangeOverflow;
            out.error_message = enc_err + " for symbol " + target_sym.name;
            return out;
        }

        write_u32_le(out.relocated_text_bytes.data() + off, entry.patched_insn);
        entry.patched = true;
        out.relocations.push_back(entry);
    }

    if (opts.require_all_relocations_resolved && !out.unresolved_symbols.empty()) {
        out.status = ObjectIntegrationStatus::UnresolvedExternal;
        out.error_message = "Unresolved external symbol(s): " + out.unresolved_symbols.front();
        return out;
    }

    // Record W^X provenance regions: all executable regions are R|X (0x5), never W|X.
    MachOProvenanceRegion text_region;
    text_region.va_start       = out.text_va;
    text_region.size           = out.text_size;
    text_region.vm_prot        = 0x5u; // R | X
    text_region.classification = SymbolProvenanceClassification::GenuineUpstream;
    text_region.label          = opts.upstream_source_label + ":__TEXT,__text";
    out.provenance_regions.push_back(text_region);

    if (!out.stub_bytes.empty()) {
        MachOProvenanceRegion stub_region;
        stub_region.va_start       = out.stub_va;
        stub_region.size           = out.stub_size;
        stub_region.vm_prot        = 0x5u; // R | X
        stub_region.classification = out.has_test_harness_stubs
            ? SymbolProvenanceClassification::TestHarnessStub
            : SymbolProvenanceClassification::WinIOSArchitectural;
        stub_region.label          = "__STUB_TEXT";
        out.provenance_regions.push_back(stub_region);
    }

    out.status = ObjectIntegrationStatus::Success;
    return out;
}

MachOIntegrationResult MachOObjectIntegrator::stage_in_memory(
    const MachOObjectParseResult& parsed,
    const MachOIntegrationOptions& opts,
    Memory& guest_mem,
    GuestMemoryMap* memory_map) {
    MachOIntegrationResult out = link_object(parsed, opts);
    if (!out.is_success()) {
        return out;
    }

    if (!write_staged_bytes(guest_mem, memory_map, opts.virtual_base_bias, out.text_va,
                            out.relocated_text_bytes, 0x5u, "__TEXT,__text",
                            kPageGranuleBytes, out.status, out.error_message)) {
        return out;
    }
    if (!out.stub_bytes.empty()) {
        if (!write_staged_bytes(guest_mem, memory_map, opts.virtual_base_bias, out.stub_va,
                                out.stub_bytes, 0x5u, "__STUB_TEXT",
                                kPageGranuleBytes, out.status, out.error_message)) {
            return out;
        }
    }

    return out;
}

MachOBoundedContainerResult MachOObjectIntegrator::build_bounded_macho_execute(
    const MachOObjectParseResult& parsed,
    const MachOIntegrationOptions& opts,
    const std::string& entry_symbol) {
    MachOBoundedContainerResult container;

    if ((opts.text_base_va & (kPageGranuleBytes - 1u)) != 0u) {
        container.status = ObjectIntegrationStatus::MemoryBoundsExceeded;
        container.error_message = "build_bounded_macho_execute requires 16 KiB aligned text_base_va";
        return container;
    }

    constexpr std::uint32_t kTextFileOff = 4096u; // aligned header offset inside __TEXT
    MachOIntegrationOptions link_opts = opts;
    if (!checked_add_u64(opts.text_base_va, kTextFileOff, link_opts.text_base_va)) {
        container.status = ObjectIntegrationStatus::IntegerOverflow;
        container.error_message = "Overflow offsetting __text inside __TEXT segment";
        return container;
    }

    const MachOIntegrationResult link = link_object(parsed, link_opts);
    container.link_audit = link;
    if (!link.is_success()) {
        container.status = link.status;
        container.error_message = link.error_message;
        return container;
    }

    std::uint64_t entry_va = 0;
    if (!link.find_symbol_va(entry_symbol, entry_va) || entry_va < link_opts.text_base_va) {
        container.status = ObjectIntegrationStatus::MalformedSymbolTable;
        container.error_message = "Entry symbol not defined in linked object: " + entry_symbol;
        return container;
    }

    // Build a 16 KiB-aligned MH_EXECUTE image matching MachOLoader::parse_metadata rules:
    // Segment 1: __TEXT (R|X = 5), vmaddr = opts.text_base_va, fileoff = 0,
    // contains mach_header_64 + load commands + padding to 4096 + __TEXT,__text,
    // plus optional __STUB_TEXT segment if stubs were emitted.
    const bool has_stubs = !link.stub_bytes.empty();

    std::uint32_t text_section_count = has_stubs ? 2u : 1u;
    // Wait: if stubs live at opts.stub_base_va (separate segment for clean VA layout!), emit:
    // LC_SEGMENT_64 __TEXT (vmaddr=text_base_va, maxprot=5, initprot=5, 1 section __text)
    // optional LC_SEGMENT_64 __STUB_TEXT (vmaddr=stub_base_va, maxprot=5, initprot=5, 1 section __stubs)
    // LC_MAIN (entryoff relative to __TEXT vmaddr/file layout: here placed right after header offset!)
    (void)text_section_count;

    const std::uint32_t seg_text_size =
        static_cast<std::uint32_t>(sizeof(MachOSegmentCommand64) + sizeof(MachOSection64));
    const std::uint32_t seg_stub_size = has_stubs ? seg_text_size : 0u;
    const std::uint32_t main_cmd_size = static_cast<std::uint32_t>(sizeof(MachOEntrypointCommand));
    const std::uint32_t ncmds = has_stubs ? 3u : 2u;
    const std::uint32_t sizeofcmds = seg_text_size + seg_stub_size + main_cmd_size;

    if (sizeof(MachOHeader64) + sizeofcmds > kTextFileOff) {
        container.status = ObjectIntegrationStatus::MemoryBoundsExceeded;
        container.error_message = "Bounded MH_EXECUTE headers exceed header area";
        return container;
    }

    const std::uint64_t text_vmsize = align_up_u64(kTextFileOff + link.relocated_text_bytes.size(), kPageGranuleBytes);
    const std::uint64_t stub_fileoff = kTextFileOff + align_up_u64(link.relocated_text_bytes.size(), kPageGranuleBytes);
    const std::uint64_t stub_vmsize = has_stubs ? align_up_u64(link.stub_bytes.size(), kPageGranuleBytes) : 0u;

    MachOHeader64 hdr{};
    hdr.magic      = kMachHeaderMagic64;
    hdr.cputype    = kCpuTypeArm64;
    hdr.cpusubtype = parsed.cpusubtype;
    hdr.filetype   = kMhExecute;
    hdr.ncmds      = ncmds;
    hdr.sizeofcmds = sizeofcmds;
    hdr.flags      = 0x00000001u; // MH_NOUNDEFS
    hdr.reserved   = 0u;

    MachOSegmentCommand64 seg_text{};
    seg_text.cmd      = kLcSegment64;
    seg_text.cmdsize  = seg_text_size;
    std::memcpy(seg_text.segname, "__TEXT", 6);
    seg_text.vmaddr   = opts.text_base_va;
    seg_text.vmsize   = text_vmsize;
    seg_text.fileoff  = 0u;
    seg_text.filesize = kTextFileOff + link.relocated_text_bytes.size();
    seg_text.maxprot  = 0x5u; // R | X (strict W^X)
    seg_text.initprot = 0x5u; // R | X
    seg_text.nsects   = 1u;

    MachOSection64 sec_text{};
    std::memcpy(sec_text.sectname, "__text", 6);
    std::memcpy(sec_text.segname, "__TEXT", 6);
    sec_text.addr   = opts.text_base_va + kTextFileOff;
    sec_text.size   = link.relocated_text_bytes.size();
    sec_text.offset = kTextFileOff;
    sec_text.align  = 5u;
    sec_text.flags  = kSecAttrPureInsn | kSecAttrSomeInsn;

    MachOSegmentCommand64 seg_stub{};
    MachOSection64 sec_stub{};
    if (has_stubs) {
        seg_stub.cmd      = kLcSegment64;
        seg_stub.cmdsize  = seg_stub_size;
        std::memcpy(seg_stub.segname, "__STUB_TEXT", 11);
        seg_stub.vmaddr   = opts.stub_base_va;
        seg_stub.vmsize   = stub_vmsize;
        seg_stub.fileoff  = stub_fileoff;
        seg_stub.filesize = link.stub_bytes.size();
        seg_stub.maxprot  = 0x5u; // R | X (strict W^X)
        seg_stub.initprot = 0x5u; // R | X
        seg_stub.nsects   = 1u;

        std::memcpy(sec_stub.sectname, "__stubs", 7);
        std::memcpy(sec_stub.segname, "__STUB_TEXT", 11);
        sec_stub.addr   = opts.stub_base_va;
        sec_stub.size   = link.stub_bytes.size();
        sec_stub.offset = static_cast<std::uint32_t>(stub_fileoff);
        sec_stub.align  = 2u;
        sec_stub.flags  = kSecAttrPureInsn | kSecAttrSomeInsn;
    }

    MachOEntrypointCommand ep{};
    ep.cmd       = kLcMain;
    ep.cmdsize   = main_cmd_size;
    ep.entryoff  = kTextFileOff + (entry_va - link_opts.text_base_va);
    ep.stacksize = kPageGranuleBytes;

    std::vector<std::uint8_t> bin;
    const std::size_t total_size = has_stubs
        ? static_cast<std::size_t>(stub_fileoff + link.stub_bytes.size())
        : static_cast<std::size_t>(kTextFileOff + link.relocated_text_bytes.size());
    bin.assign(total_size, 0u);

    std::size_t woff = 0;
    std::memcpy(bin.data() + woff, &hdr, sizeof(hdr)); woff += sizeof(hdr);
    std::memcpy(bin.data() + woff, &seg_text, sizeof(seg_text)); woff += sizeof(seg_text);
    std::memcpy(bin.data() + woff, &sec_text, sizeof(sec_text)); woff += sizeof(sec_text);
    if (has_stubs) {
        std::memcpy(bin.data() + woff, &seg_stub, sizeof(seg_stub)); woff += sizeof(seg_stub);
        std::memcpy(bin.data() + woff, &sec_stub, sizeof(sec_stub)); woff += sizeof(sec_stub);
    }
    std::memcpy(bin.data() + woff, &ep, sizeof(ep));
    std::memcpy(bin.data() + kTextFileOff, link.relocated_text_bytes.data(), link.relocated_text_bytes.size());
    if (has_stubs) {
        std::memcpy(bin.data() + stub_fileoff, link.stub_bytes.data(), link.stub_bytes.size());
    }

    container.container_sha256_hex = LinuxImageLoader::compute_sha256(bin.data(), bin.size());
    container.macho_execute_bytes  = std::move(bin);
    container.status               = ObjectIntegrationStatus::Success;
    return container;
}

MachOMultiObjectLinkResult MachOObjectIntegrator::link_objects(
    const std::vector<MachOObjectInput>& inputs,
    const MachOMultiObjectOptions& opts) {
    MachOMultiObjectLinkResult out;

    if (inputs.empty()) {
        out.status = ObjectIntegrationStatus::InvalidSectionBounds;
        out.error_message = "Multi-object linker requires at least one MH_OBJECT input";
        return out;
    }
    for (const auto& inp : inputs) {
        if (!inp.parsed.is_success()) {
            out.status = inp.parsed.status;
            out.error_message = "Input object invalid (" + inp.source_label + "): " + inp.parsed.error_message;
            return out;
        }
    }

    if ((opts.text_base_va & 0x3u) != 0u ||
        (opts.rodata_base_va & (kPageGranuleBytes - 1u)) != 0u ||
        (opts.data_base_va & (kPageGranuleBytes - 1u)) != 0u ||
        (opts.stub_base_va & (kPageGranuleBytes - 1u)) != 0u) {
        out.status = ObjectIntegrationStatus::MemoryBoundsExceeded;
        out.error_message = "text_base_va must be 4-byte aligned and rodata/data/stub_base_va must be 16 KiB aligned";
        return out;
    }

    std::map<std::string, BoundedExternalBinding> binding_map;
    if (!validate_binding_list(opts.external_bindings, binding_map, out.status, out.error_message)) {
        return out;
    }

    // Track placement: each non-debug section maps into RX (__TEXT,__text / insn), RO (__TEXT ro / __DATA_CONST),
    // or RW (__DATA / __BOOTDATA).
    struct SecPlacement {
        bool placed{false};
        std::size_t staged_idx{0};
        std::uint64_t va{0};
    };
    std::vector<std::vector<SecPlacement>> sec_map(inputs.size());

    std::uint64_t text_cursor   = opts.text_base_va;
    std::uint64_t rodata_cursor = opts.rodata_base_va;
    std::uint64_t data_cursor   = opts.data_base_va;

    auto classify_vm_prot = [](const MachOObjectSectionInfo& sec) noexcept -> std::uint32_t {
        if (sec.segname == "__TEXT" &&
            (sec.sectname == "__text" || (sec.flags & (kSecAttrPureInsn | kSecAttrSomeInsn)) != 0u)) {
            return 0x5u; // R | X (strict W^X)
        }
        if (sec.segname == "__TEXT" || sec.segname == "__DATA_CONST") {
            return 0x1u; // R
        }
        return 0x3u; // R | W (__DATA, __BOOTDATA, zerofill)
    };

    for (std::size_t oi = 0; oi < inputs.size(); ++oi) {
        const auto& parsed = inputs[oi].parsed;
        sec_map[oi].resize(parsed.sections.size());
        for (std::size_t si = 0; si < parsed.sections.size(); ++si) {
            const auto& sec = parsed.sections[si];
            if (sec.is_debug || (sec.segname == "__TEXT" && sec.sectname == "__eh_frame") || sec.size == 0u) {
                continue;
            }
            const std::uint32_t prot = classify_vm_prot(sec);
            const std::uint64_t align_shift = (sec.align_log2 > 14u) ? 14u : sec.align_log2;
            const std::uint64_t sec_align = (std::uint64_t{1} << align_shift) < 4u ? 4u : (std::uint64_t{1} << align_shift);

            std::uint64_t& cursor = (prot == 0x5u) ? text_cursor
                                  : (prot == 0x1u) ? rodata_cursor
                                                   : data_cursor;
            const std::uint64_t aligned_va = align_up_u64(cursor, sec_align);
            std::uint64_t next_end = 0;
            if (!checked_add_u64(aligned_va, sec.size, next_end)) {
                out.status = ObjectIntegrationStatus::IntegerOverflow;
                out.error_message = "Section size overflow placing " + sec.segname + "," + sec.sectname;
                return out;
            }
            cursor = next_end;

            MachOStagedSection staged;
            staged.object_label = inputs[oi].source_label;
            staged.segname      = sec.segname;
            staged.sectname     = sec.sectname;
            staged.va           = aligned_va;
            staged.size         = sec.size;
            staged.vm_prot      = prot;
            staged.is_zerofill  = sec.is_zerofill;
            if (sec.is_zerofill) {
                staged.bytes.assign(static_cast<std::size_t>(sec.size), 0u);
            } else {
                staged.bytes = sec.bytes;
            }

            sec_map[oi][si].placed     = true;
            sec_map[oi][si].staged_idx = out.staged_sections.size();
            sec_map[oi][si].va         = aligned_va;
            out.staged_sections.push_back(std::move(staged));
        }
    }

    if (text_cursor == opts.text_base_va) {
        out.status = ObjectIntegrationStatus::InvalidSectionBounds;
        out.error_message = "Multi-object inputs contain no executable code sections";
        return out;
    }

    out.text_va   = opts.text_base_va;
    out.text_size = text_cursor - opts.text_base_va;

    // Build per-object resolved symbols and global external symbol table.
    struct GlobalDef {
        std::size_t owner_obj{0};
        std::uint64_t va{0};
        bool is_weak{false};
        SymbolProvenanceClassification classification{SymbolProvenanceClassification::GenuineUpstream};
    };
    std::map<std::string, GlobalDef> global_defs;
    std::vector<std::vector<MachOSymbolEntry>> obj_symbols(inputs.size());

    for (std::size_t oi = 0; oi < inputs.size(); ++oi) {
        const auto& parsed = inputs[oi].parsed;
        obj_symbols[oi] = parsed.symbols;
        for (auto& sym : obj_symbols[oi]) {
            if (sym.is_stab) {
                continue;
            }
            const std::uint8_t n_type_field = sym.n_type & kNlistTypeMask;
            if (sym.is_defined && n_type_field == kNlistSect) {
                const std::size_t sec_idx = static_cast<std::size_t>(sym.n_sect - 1u);
                if (sec_idx >= sec_map[oi].size() || !sec_map[oi][sec_idx].placed) {
                    continue;
                }
                const auto& sec = parsed.sections[sec_idx];
                if (sym.n_value < sec.addr || (sym.n_value - sec.addr) > sec.size) {
                    out.status = ObjectIntegrationStatus::InvalidSectionBounds;
                    out.error_message = "Symbol n_value out of section bounds: " + sym.name;
                    return out;
                }
                if (!checked_add_u64(sec_map[oi][sec_idx].va, sym.n_value - sec.addr, sym.resolved_va)) {
                    out.status = ObjectIntegrationStatus::IntegerOverflow;
                    out.error_message = "Overflow computing symbol VA: " + sym.name;
                    return out;
                }
                sym.classification = SymbolProvenanceClassification::GenuineUpstream;

                if (sym.is_external) {
                    const bool is_weak = (sym.n_desc & kNDescWeakDef) != 0u;
                    auto it = global_defs.find(sym.name);
                    if (it == global_defs.end()) {
                        global_defs[sym.name] = {oi, sym.resolved_va, is_weak, SymbolProvenanceClassification::GenuineUpstream};
                    } else if (it->second.is_weak && !is_weak) {
                        it->second = {oi, sym.resolved_va, false, SymbolProvenanceClassification::GenuineUpstream};
                    } else if (!it->second.is_weak && !is_weak && opts.reject_duplicate_strong_symbols) {
                        out.status = ObjectIntegrationStatus::MalformedSymbolTable;
                        out.error_message = "Duplicate strong global symbol across objects: " + sym.name;
                        return out;
                    }
                }
            } else if (sym.is_defined && n_type_field == kNlistAbs) {
                sym.resolved_va = sym.n_value;
                sym.classification = SymbolProvenanceClassification::GenuineUpstream;
            }
        }
    }

    // Bind external symbols cleanly; never allow spoofing strong GenuineUpstream symbols.
    std::map<std::string, MachOSymbolEntry> resolved_external_bindings;
    std::vector<std::uint8_t> stub_bytes;
    std::uint64_t next_stub_offset = 0;

    for (const auto& [bname, binding] : binding_map) {
        if (global_defs.find(bname) != global_defs.end()) {
            out.status = ObjectIntegrationStatus::MalformedSymbolTable;
            out.error_message = "External binding cannot override GenuineUpstream symbol: " + bname;
            return out;
        }
        MachOSymbolEntry bsym;
        bsym.name = bname;
        bsym.classification = binding.classification;
        if (binding.prebound_va != 0u) {
            bsym.resolved_va = binding.prebound_va;
        } else {
            std::vector<std::uint32_t> insns = binding.stub_instructions;
            if (insns.empty()) {
                insns.push_back(0xD65F03C0u); // RET
            }
            if (!checked_add_u64(opts.stub_base_va, next_stub_offset, bsym.resolved_va)) {
                out.status = ObjectIntegrationStatus::IntegerOverflow;
                out.error_message = "Overflow placing stub for " + bname;
                return out;
            }
            for (std::uint32_t insn : insns) {
                std::uint8_t buf[4];
                write_u32_le(buf, insn);
                append_bytes(stub_bytes, buf, sizeof(buf));
            }
            next_stub_offset += static_cast<std::uint64_t>(insns.size()) * 4u;
        }
        if (binding.classification == SymbolProvenanceClassification::TestHarnessStub) {
            out.has_test_harness_stubs = true;
            out.stub_symbols.push_back(bname);
        } else if (binding.classification == SymbolProvenanceClassification::WinIOSArchitectural) {
            out.has_winios_architectural_bindings = true;
        }
        resolved_external_bindings[bname] = bsym;
    }

    // Allocate synthetic GOT base immediately after RO sections in __DATA_CONST,__got (8-byte aligned, R only).
    const std::uint64_t got_base_va = align_up_u64(rodata_cursor, 8u);
    std::map<std::string, std::size_t> got_index_by_key;
    std::vector<std::uint8_t> got_bytes;

    auto get_or_create_got_slot = [&](const std::string& sym_name,
                                      std::uint64_t target_va,
                                      SymbolProvenanceClassification cls,
                                      std::uint64_t& out_slot_va) -> bool {
        const std::string key = sym_name + "@" + std::to_string(target_va);
        auto it = got_index_by_key.find(key);
        if (it != got_index_by_key.end()) {
            out_slot_va = out.got_slots[it->second].slot_va;
            return true;
        }
        const std::size_t slot_idx = out.got_slots.size();
        if (!checked_add_u64(got_base_va, static_cast<std::uint64_t>(slot_idx) * 8u, out_slot_va)) {
            return false;
        }
        std::uint8_t slot_buf[8];
        write_u64_le(slot_buf, target_va);
        append_bytes(got_bytes, slot_buf, sizeof(slot_buf));
        out.got_slots.push_back({sym_name, out_slot_va, target_va, cls});
        got_index_by_key[key] = slot_idx;
        return true;
    };

    // Resolve undefined symbols per object and patch relocations.
    for (std::size_t oi = 0; oi < inputs.size(); ++oi) {
        const auto& parsed = inputs[oi].parsed;
        for (auto& sym : obj_symbols[oi]) {
            if (sym.is_stab || sym.is_defined) {
                continue;
            }
            if (sym.is_undefined) {
                auto git = global_defs.find(sym.name);
                if (git != global_defs.end()) {
                    sym.resolved_va = git->second.va;
                    sym.classification = git->second.classification;
                    continue;
                }
                auto bit = resolved_external_bindings.find(sym.name);
                if (bit != resolved_external_bindings.end()) {
                    sym.resolved_va = bit->second.resolved_va;
                    sym.classification = bit->second.classification;
                } else {
                    sym.resolved_va = 0u;
                    sym.classification = SymbolProvenanceClassification::UnresolvedExternal;
                }
            }
        }

        for (std::size_t si = 0; si < parsed.sections.size(); ++si) {
            const auto& sec = parsed.sections[si];
            if (sec.is_debug || !sec_map[oi][si].placed) {
                continue;
            }
            const bool is_code_sec = (out.staged_sections[sec_map[oi][si].staged_idx].vm_prot == 0x5u);
            auto& staged_bytes = out.staged_sections[sec_map[oi][si].staged_idx].bytes;
            const std::uint64_t sec_va = sec_map[oi][si].va;

            bool has_pending_addend = false;
            std::int64_t pending_addend = 0;
            std::int32_t pending_addend_raddr = 0;

            for (const auto& r : sec.relocations) {
                MachORelocEntry entry = r;
                entry.owner_object = inputs[oi].source_label;

                if (entry.type == kArm64RelocAddend) {
                    if (entry.is_extern || entry.pcrel || entry.length != 2u || has_pending_addend) {
                        out.status = ObjectIntegrationStatus::UnsupportedRelocationType;
                        out.error_message = "Malformed ARM64_RELOC_ADDEND entry in " + inputs[oi].source_label;
                        return out;
                    }
                    std::int32_t signed24 = static_cast<std::int32_t>(entry.symbol_index & 0x00FFFFFFu);
                    if ((signed24 & 0x00800000) != 0) {
                        signed24 |= static_cast<std::int32_t>(0xFF000000u);
                    }
                    has_pending_addend = true;
                    pending_addend = static_cast<std::int64_t>(signed24);
                    pending_addend_raddr = entry.r_address;
                    entry.addend = pending_addend;
                    entry.target_classification = SymbolProvenanceClassification::GenuineUpstream;
                    out.relocations.push_back(entry);
                    continue;
                }

                const std::int64_t cur_addend = has_pending_addend ? pending_addend : 0;
                if (has_pending_addend) {
                    if (pending_addend_raddr != entry.r_address ||
                        (entry.type != kArm64RelocPage21 && entry.type != kArm64RelocPageoff12)) {
                        out.status = ObjectIntegrationStatus::UnsupportedRelocationType;
                        out.error_message = "ARM64_RELOC_ADDEND must immediately precede paired PAGE21/PAGEOFF12";
                        return out;
                    }
                    has_pending_addend = false;
                    pending_addend = 0;
                }
                entry.addend = cur_addend;

                const bool supported_text_reloc =
                    entry.type == kArm64RelocBranch26 ||
                    entry.type == kArm64RelocPage21 ||
                    entry.type == kArm64RelocPageoff12 ||
                    entry.type == kArm64RelocGotLoadPage21 ||
                    entry.type == kArm64RelocGotLoadPageoff12;

                if (!is_code_sec) {
                    // Non-code data/init sections may contain UNSIGNED/AUTHENTICATED_POINTER relocs.
                    if (entry.type == kArm64RelocUnsigned && entry.is_extern && !entry.pcrel && entry.length == 3u &&
                        entry.r_address >= 0 && static_cast<std::uint64_t>(entry.r_address) + 8u <= staged_bytes.size() &&
                        entry.symbol_index < obj_symbols[oi].size()) {
                        const auto& target_sym = obj_symbols[oi][entry.symbol_index];
                        entry.target_symbol = target_sym.name;
                        entry.target_classification = target_sym.classification;
                        (void)checked_add_u64(sec_va, static_cast<std::uint64_t>(entry.r_address), entry.site_va);
                        if (target_sym.classification != SymbolProvenanceClassification::UnresolvedExternal &&
                            target_sym.classification != SymbolProvenanceClassification::UnsupportedSymbol &&
                            target_sym.resolved_va != 0u) {
                            entry.target_va = target_sym.resolved_va;
                            write_u64_le(staged_bytes.data() + entry.r_address, target_sym.resolved_va);
                            entry.patched = true;
                        } else if (std::find(out.unresolved_symbols.begin(), out.unresolved_symbols.end(), target_sym.name) ==
                                   out.unresolved_symbols.end()) {
                            out.unresolved_symbols.push_back(target_sym.name);
                        }
                        out.relocations.push_back(entry);
                        continue;
                    }
                    if (!opts.require_all_relocations_resolved) {
                        out.relocations.push_back(entry);
                        continue;
                    }
                    out.status = ObjectIntegrationStatus::UnsupportedRelocationType;
                    out.error_message = "Unsupported non-code relocation in " + sec.segname + "," + sec.sectname;
                    return out;
                }

                if (!supported_text_reloc) {
                    out.status = ObjectIntegrationStatus::UnsupportedRelocationType;
                    out.error_message = "Unsupported ARM64 code relocation type " + std::to_string(entry.type);
                    return out;
                }
                if (!entry.is_extern || entry.length != 2u) {
                    out.status = ObjectIntegrationStatus::UnsupportedRelocationType;
                    out.error_message = "ARM64 code relocation must be extern and 4-byte length";
                    return out;
                }
                const bool expects_pcrel =
                    (entry.type == kArm64RelocBranch26 ||
                     entry.type == kArm64RelocPage21 ||
                     entry.type == kArm64RelocGotLoadPage21);
                if (entry.pcrel != expects_pcrel) {
                    out.status = ObjectIntegrationStatus::UnsupportedRelocationType;
                    out.error_message = "ARM64 relocation pcrel bit mismatch";
                    return out;
                }
                if (entry.r_address < 0 || (entry.r_address & 0x3) != 0) {
                    out.status = ObjectIntegrationStatus::RelocationOutOfBounds;
                    out.error_message = "Code relocation r_address negative or unaligned";
                    return out;
                }
                const std::uint64_t off = static_cast<std::uint64_t>(entry.r_address);
                if (off + 4u > staged_bytes.size()) {
                    out.status = ObjectIntegrationStatus::RelocationOutOfBounds;
                    out.error_message = "Code relocation site exceeds section size";
                    return out;
                }
                if (entry.symbol_index >= obj_symbols[oi].size()) {
                    out.status = ObjectIntegrationStatus::MalformedSymbolTable;
                    out.error_message = "Relocation symbol_index out of range";
                    return out;
                }

                const auto& target_sym = obj_symbols[oi][entry.symbol_index];
                entry.target_symbol = target_sym.name;
                entry.target_classification = target_sym.classification;
                if (!checked_add_u64(sec_va, off, entry.site_va)) {
                    out.status = ObjectIntegrationStatus::IntegerOverflow;
                    out.error_message = "Overflow computing relocation site VA";
                    return out;
                }

                if (target_sym.classification == SymbolProvenanceClassification::UnresolvedExternal ||
                    target_sym.classification == SymbolProvenanceClassification::UnsupportedSymbol ||
                    target_sym.resolved_va == 0u) {
                    if (std::find(out.unresolved_symbols.begin(), out.unresolved_symbols.end(), target_sym.name) ==
                        out.unresolved_symbols.end()) {
                        out.unresolved_symbols.push_back(target_sym.name);
                    }
                    out.relocations.push_back(entry);
                    continue;
                }

                std::uint64_t eff_target_va = 0;
                if (!checked_apply_signed_addend(target_sym.resolved_va, cur_addend, eff_target_va)) {
                    out.status = ObjectIntegrationStatus::IntegerOverflow;
                    out.error_message = "Overflow applying relocation addend for " + target_sym.name;
                    return out;
                }

                entry.target_va = eff_target_va;
                entry.original_insn = read_u32_le(staged_bytes.data() + off);

                std::string enc_err;
                bool enc_ok = false;
                if (entry.type == kArm64RelocBranch26) {
                    enc_ok = encode_branch26(entry.original_insn, entry.site_va, eff_target_va,
                                             entry.patched_insn, enc_err);
                } else if (entry.type == kArm64RelocPage21) {
                    enc_ok = encode_page21(entry.original_insn, entry.site_va, eff_target_va,
                                           entry.patched_insn, enc_err);
                } else if (entry.type == kArm64RelocPageoff12) {
                    enc_ok = encode_pageoff12(entry.original_insn, eff_target_va,
                                              entry.patched_insn, enc_err);
                } else {
                    std::uint64_t slot_va = 0;
                    if (!get_or_create_got_slot(target_sym.name, eff_target_va, target_sym.classification, slot_va)) {
                        out.status = ObjectIntegrationStatus::IntegerOverflow;
                        out.error_message = "Overflow allocating GOT slot for " + target_sym.name;
                        return out;
                    }
                    entry.target_va = slot_va;
                    if (entry.type == kArm64RelocGotLoadPage21) {
                        enc_ok = encode_page21(entry.original_insn, entry.site_va, slot_va,
                                               entry.patched_insn, enc_err);
                    } else {
                        enc_ok = encode_pageoff12(entry.original_insn, slot_va,
                                                  entry.patched_insn, enc_err);
                    }
                }

                if (!enc_ok) {
                    out.status = (enc_err.find("range") != std::string::npos)
                        ? ObjectIntegrationStatus::RelocationRangeOverflow
                        : ObjectIntegrationStatus::RelocationOpcodeMismatch;
                    out.error_message = enc_err + " for symbol " + target_sym.name;
                    return out;
                }

                write_u32_le(staged_bytes.data() + off, entry.patched_insn);
                entry.patched = true;
                out.relocations.push_back(entry);
            }

            if (has_pending_addend) {
                out.status = ObjectIntegrationStatus::UnsupportedRelocationType;
                out.error_message = "Dangling ARM64_RELOC_ADDEND without paired relocation";
                return out;
            }
        }
    }

    if (opts.require_all_relocations_resolved && !out.unresolved_symbols.empty()) {
        out.status = ObjectIntegrationStatus::UnresolvedExternal;
        out.error_message = "Unresolved external symbol(s): " + out.unresolved_symbols.front();
        return out;
    }

    if (!got_bytes.empty()) {
        MachOStagedSection got_sec;
        got_sec.object_label = "winios-synthetic-got";
        got_sec.segname      = "__DATA_CONST";
        got_sec.sectname     = "__got";
        got_sec.va           = got_base_va;
        got_sec.size         = got_bytes.size();
        got_sec.vm_prot      = 0x1u; // R (strict W^X)
        got_sec.bytes        = std::move(got_bytes);
        std::uint64_t new_ro_end = 0;
        if (!checked_add_u64(got_sec.va, got_sec.size, new_ro_end)) {
            out.status = ObjectIntegrationStatus::IntegerOverflow;
            out.error_message = "GOT section size overflow";
            return out;
        }
        rodata_cursor = new_ro_end;
        out.staged_sections.push_back(std::move(got_sec));
    }

    out.rodata_va   = (rodata_cursor > opts.rodata_base_va) ? opts.rodata_base_va : 0u;
    out.rodata_size = (rodata_cursor > opts.rodata_base_va) ? (rodata_cursor - opts.rodata_base_va) : 0u;
    out.data_va     = (data_cursor > opts.data_base_va) ? opts.data_base_va : 0u;
    out.data_size   = (data_cursor > opts.data_base_va) ? (data_cursor - opts.data_base_va) : 0u;
    out.stub_va     = stub_bytes.empty() ? 0u : opts.stub_base_va;
    out.stub_size   = stub_bytes.size();

    if (!stub_bytes.empty()) {
        MachOStagedSection stub_sec;
        stub_sec.object_label = out.has_test_harness_stubs ? "test-harness-stubs" : "winios-architectural-stubs";
        stub_sec.segname      = "__STUB_TEXT";
        stub_sec.sectname     = "__stubs";
        stub_sec.va           = out.stub_va;
        stub_sec.size         = out.stub_size;
        stub_sec.vm_prot      = 0x5u; // R | X
        stub_sec.bytes        = std::move(stub_bytes);
        out.staged_sections.push_back(std::move(stub_sec));
    }

    // Verify pairwise non-overlap of page-granular regions (RX text, RO rodata, RW data, RX stub).
    struct SpanCheck {
        std::uint64_t va;
        std::uint64_t span;
        const char* name;
    };
    const SpanCheck spans[4] = {
        {out.text_va,   align_up_u64(out.text_size, kPageGranuleBytes),   "__TEXT"},
        {out.rodata_va, align_up_u64(out.rodata_size, kPageGranuleBytes), "__RODATA"},
        {out.data_va,   align_up_u64(out.data_size, kPageGranuleBytes),   "__DATA"},
        {out.stub_va,   align_up_u64(out.stub_size, kPageGranuleBytes),   "__STUB_TEXT"},
    };
    for (int i = 0; i < 4; ++i) {
        if (spans[i].span == 0u) continue;
        for (int j = i + 1; j < 4; ++j) {
            if (spans[j].span == 0u) continue;
            if (regions_overlap(spans[i].va, spans[i].span, spans[j].va, spans[j].span)) {
                out.status = ObjectIntegrationStatus::MemoryBoundsExceeded;
                out.error_message = std::string("16 KiB segment overlap between ") + spans[i].name + " and " + spans[j].name;
                return out;
            }
        }
    }

    // Emit unified resolved_symbols list (preferring global_defs + external bindings + per-object locals).
    for (std::size_t oi = 0; oi < obj_symbols.size(); ++oi) {
        for (const auto& sym : obj_symbols[oi]) {
            if (!sym.is_stab && sym.is_defined && sym.resolved_va != 0u) {
                out.resolved_symbols.push_back(sym);
            }
        }
    }
    for (const auto& [_, bsym] : resolved_external_bindings) {
        out.resolved_symbols.push_back(bsym);
    }

    // Populate W^X provenance regions per staged section.
    for (const auto& s : out.staged_sections) {
        if (s.size == 0u) continue;
        MachOProvenanceRegion reg;
        reg.va_start = s.va;
        reg.size     = s.size;
        reg.vm_prot  = s.vm_prot;
        if (s.segname == "__STUB_TEXT") {
            reg.classification = out.has_test_harness_stubs
                ? SymbolProvenanceClassification::TestHarnessStub
                : SymbolProvenanceClassification::WinIOSArchitectural;
        } else {
            reg.classification = SymbolProvenanceClassification::GenuineUpstream;
        }
        reg.label = s.object_label + ":" + s.segname + "," + s.sectname;
        out.provenance_regions.push_back(std::move(reg));
    }

    out.status = ObjectIntegrationStatus::Success;
    return out;
}

MachOMultiObjectLinkResult MachOObjectIntegrator::stage_objects_in_memory(
    const std::vector<MachOObjectInput>& inputs,
    const MachOMultiObjectOptions& opts,
    Memory& guest_mem,
    GuestMemoryMap* memory_map) {
    if ((opts.text_base_va & (kPageGranuleBytes - 1u)) != 0u) {
        MachOMultiObjectLinkResult err;
        err.status = ObjectIntegrationStatus::MemoryBoundsExceeded;
        err.error_message = "stage_objects_in_memory requires 16 KiB aligned text_base_va";
        return err;
    }

    MachOMultiObjectLinkResult out = link_objects(inputs, opts);
    if (!out.is_success()) {
        return out;
    }

    // Aggregate per-segment spans to register coarse W^X regions cleanly in GuestMemoryMap if provided.
    for (const auto& s : out.staged_sections) {
        if (s.bytes.empty()) continue;
        if (!write_staged_bytes(guest_mem, nullptr, opts.virtual_base_bias, s.va, s.bytes,
                                s.vm_prot, s.object_label + ":" + s.sectname, kPageGranuleBytes,
                                out.status, out.error_message)) {
            return out;
        }
    }

    if (memory_map != nullptr) {
        auto add_seg = [&](const std::string& name, GuestMemoryRegionType type, std::uint64_t va, std::uint64_t sz) -> bool {
            if (sz == 0u) return true;
            GuestMemoryRegion reg;
            reg.name = name;
            reg.type = type;
            reg.base_address = va;
            reg.size = align_up_u64(sz, kPageGranuleBytes);
            reg.is_reserved = true;
            if (!memory_map->add_region(reg)) {
                out.status = ObjectIntegrationStatus::MemoryBoundsExceeded;
                out.error_message = "GuestMemoryMap rejected overlapping region: " + name;
                return false;
            }
            return true;
        };
        if (!add_seg("__TEXT", GuestMemoryRegionType::KernelImage, out.text_va, out.text_size) ||
            !add_seg("__RODATA", GuestMemoryRegionType::KernelImage, out.rodata_va, out.rodata_size) ||
            !add_seg("__DATA", GuestMemoryRegionType::KernelImage, out.data_va, out.data_size) ||
            !add_seg("__STUB_TEXT", GuestMemoryRegionType::KernelImage, out.stub_va, out.stub_size)) {
            return out;
        }
    }

    return out;
}

MachOMultiObjectContainerResult MachOObjectIntegrator::build_bounded_multi_object_execute(
    const std::vector<MachOObjectInput>& inputs,
    const MachOMultiObjectOptions& opts,
    const std::string& entry_symbol) {
    MachOMultiObjectContainerResult container;
    if ((opts.text_base_va & (kPageGranuleBytes - 1u)) != 0u) {
        container.status = ObjectIntegrationStatus::MemoryBoundsExceeded;
        container.error_message = "build_bounded_multi_object_execute requires 16 KiB aligned text_base_va";
        return container;
    }

    constexpr std::uint32_t kTextHeaderPad = 4096u;
    MachOMultiObjectOptions link_opts = opts;
    if (!checked_add_u64(opts.text_base_va, kTextHeaderPad, link_opts.text_base_va)) {
        container.status = ObjectIntegrationStatus::IntegerOverflow;
        container.error_message = "Overflow offsetting __TEXT sections inside MH_EXECUTE";
        return container;
    }

    const auto link = link_objects(inputs, link_opts);
    container.link_audit = link;
    if (!link.is_success()) {
        container.status = link.status;
        container.error_message = link.error_message;
        return container;
    }

    std::uint64_t entry_va = 0;
    if (!link.find_symbol_va(entry_symbol, entry_va) || entry_va < link_opts.text_base_va ||
        entry_va >= link_opts.text_base_va + link.text_size) {
        container.status = ObjectIntegrationStatus::MalformedSymbolTable;
        container.error_message = "Entry symbol not in linked executable __TEXT: " + entry_symbol;
        return container;
    }

    // Build contiguous byte blobs for __TEXT, optional __RODATA, optional __DATA, optional __STUB_TEXT.
    auto build_region_blob = [&](std::uint32_t prot, bool is_stub_only, std::uint64_t base_va, std::uint64_t total_sz) {
        std::vector<std::uint8_t> blob(static_cast<std::size_t>(total_sz), 0u);
        for (const auto& s : link.staged_sections) {
            const bool is_stub = (s.segname == "__STUB_TEXT");
            if (s.vm_prot != prot || is_stub != is_stub_only || s.bytes.empty()) {
                continue;
            }
            const std::size_t rel = static_cast<std::size_t>(s.va - base_va);
            if (rel + s.bytes.size() <= blob.size()) {
                std::memcpy(blob.data() + rel, s.bytes.data(), s.bytes.size());
            }
        }
        return blob;
    };

    const auto text_blob   = build_region_blob(0x5u, false, link.text_va, link.text_size);
    const auto rodata_blob = link.rodata_size > 0u ? build_region_blob(0x1u, false, link.rodata_va, link.rodata_size) : std::vector<std::uint8_t>{};
    const auto data_blob   = link.data_size > 0u   ? build_region_blob(0x3u, false, link.data_va, link.data_size)     : std::vector<std::uint8_t>{};
    const auto stub_blob   = link.stub_size > 0u   ? build_region_blob(0x5u, true,  link.stub_va, link.stub_size)     : std::vector<std::uint8_t>{};

    const std::uint32_t seg1_size = static_cast<std::uint32_t>(sizeof(MachOSegmentCommand64) + sizeof(MachOSection64));
    const std::uint32_t main_size = static_cast<std::uint32_t>(sizeof(MachOEntrypointCommand));
    std::uint32_t ncmds = 2u; // __TEXT + LC_MAIN
    if (!rodata_blob.empty()) ++ncmds;
    if (!data_blob.empty())   ++ncmds;
    if (!stub_blob.empty())   ++ncmds;
    const std::uint32_t sizeofcmds = (ncmds - 1u) * seg1_size + main_size;

    if (sizeof(MachOHeader64) + sizeofcmds > kTextHeaderPad) {
        container.status = ObjectIntegrationStatus::MemoryBoundsExceeded;
        container.error_message = "Multi-object MH_EXECUTE load commands exceed header pad";
        return container;
    }

    std::uint64_t cur_fileoff = align_up_u64(kTextHeaderPad + text_blob.size(), kPageGranuleBytes);
    const std::uint64_t ro_fileoff = cur_fileoff;
    if (!rodata_blob.empty()) cur_fileoff = align_up_u64(cur_fileoff + rodata_blob.size(), kPageGranuleBytes);
    const std::uint64_t da_fileoff = cur_fileoff;
    if (!data_blob.empty())   cur_fileoff = align_up_u64(cur_fileoff + data_blob.size(), kPageGranuleBytes);
    const std::uint64_t st_fileoff = cur_fileoff;
    if (!stub_blob.empty())   cur_fileoff += stub_blob.size();

    MachOHeader64 hdr{};
    hdr.magic      = kMachHeaderMagic64;
    hdr.cputype    = kCpuTypeArm64;
    hdr.cpusubtype = inputs.front().parsed.cpusubtype;
    hdr.filetype   = kMhExecute;
    hdr.ncmds      = ncmds;
    hdr.sizeofcmds = sizeofcmds;
    hdr.flags      = 0x00000001u;

    std::vector<std::uint8_t> bin(static_cast<std::size_t>(cur_fileoff), 0u);
    std::size_t woff = 0;
    std::memcpy(bin.data() + woff, &hdr, sizeof(hdr));
    woff += sizeof(hdr);

    auto emit_seg = [&](const char* seg_nm, const char* sec_nm, std::uint64_t vmaddr,
                        std::uint64_t sec_addr, std::uint64_t vmsize, std::uint64_t foff,
                        std::uint64_t fsz, std::uint64_t payload_sz, std::uint32_t prot,
                        std::uint32_t sec_flags, const std::vector<std::uint8_t>& payload,
                        std::uint64_t payload_file_off) {
        MachOSegmentCommand64 seg{};
        seg.cmd      = kLcSegment64;
        seg.cmdsize  = seg1_size;
        std::memcpy(seg.segname, seg_nm, std::min<std::size_t>(16u, std::strlen(seg_nm)));
        seg.vmaddr   = vmaddr;
        seg.vmsize   = align_up_u64(vmsize, kPageGranuleBytes);
        seg.fileoff  = foff;
        seg.filesize = fsz;
        seg.maxprot  = prot;
        seg.initprot = prot;
        seg.nsects   = 1u;

        MachOSection64 sec{};
        std::memcpy(sec.sectname, sec_nm, std::min<std::size_t>(16u, std::strlen(sec_nm)));
        std::memcpy(sec.segname, seg_nm, std::min<std::size_t>(16u, std::strlen(seg_nm)));
        sec.addr   = sec_addr;
        sec.size   = payload_sz;
        sec.offset = static_cast<std::uint32_t>(payload_file_off);
        sec.align  = 4u;
        sec.flags  = sec_flags;

        std::memcpy(bin.data() + woff, &seg, sizeof(seg)); woff += sizeof(seg);
        std::memcpy(bin.data() + woff, &sec, sizeof(sec)); woff += sizeof(sec);
        if (!payload.empty()) {
            std::memcpy(bin.data() + payload_file_off, payload.data(), payload.size());
        }
    };

    emit_seg("__TEXT", "__text", opts.text_base_va, link.text_va, kTextHeaderPad + text_blob.size(),
             0u, kTextHeaderPad + text_blob.size(), text_blob.size(), 0x5u,
             kSecAttrPureInsn | kSecAttrSomeInsn, text_blob, kTextHeaderPad);
    if (!rodata_blob.empty()) {
        emit_seg("__RODATA", "__rodata", link.rodata_va, link.rodata_va, rodata_blob.size(),
                 ro_fileoff, rodata_blob.size(), rodata_blob.size(), 0x1u, 0u, rodata_blob, ro_fileoff);
    }
    if (!data_blob.empty()) {
        emit_seg("__DATA", "__data", link.data_va, link.data_va, data_blob.size(),
                 da_fileoff, data_blob.size(), data_blob.size(), 0x3u, 0u, data_blob, da_fileoff);
    }
    if (!stub_blob.empty()) {
        emit_seg("__STUB_TEXT", "__stubs", link.stub_va, link.stub_va, stub_blob.size(),
                 st_fileoff, stub_blob.size(), stub_blob.size(), 0x5u,
                 kSecAttrPureInsn | kSecAttrSomeInsn, stub_blob, st_fileoff);
    }

    MachOEntrypointCommand ep{};
    ep.cmd       = kLcMain;
    ep.cmdsize   = main_size;
    ep.entryoff  = kTextHeaderPad + (entry_va - link.text_va);
    ep.stacksize = kPageGranuleBytes;
    std::memcpy(bin.data() + woff, &ep, sizeof(ep));

    container.container_sha256_hex = LinuxImageLoader::compute_sha256(bin.data(), bin.size());
    container.macho_execute_bytes  = std::move(bin);
    container.status               = ObjectIntegrationStatus::Success;
    return container;
}

} // namespace winios::loader
