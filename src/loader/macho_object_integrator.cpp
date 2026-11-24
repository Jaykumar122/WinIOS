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

constexpr std::uint8_t kArm64RelocBranch26 = 2u;

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
    for (const auto& b : opts.external_bindings) {
        if (b.symbol_name.empty()) {
            out.status = ObjectIntegrationStatus::MalformedSymbolTable;
            out.error_message = "External binding has empty symbol_name";
            return out;
        }
        if (b.classification == SymbolProvenanceClassification::UnresolvedExternal ||
            b.classification == SymbolProvenanceClassification::UnsupportedSymbol) {
            out.status = ObjectIntegrationStatus::UnresolvedExternal;
            out.error_message = "External binding cannot specify UnresolvedExternal/UnsupportedSymbol: " + b.symbol_name;
            return out;
        }
        if (b.classification == SymbolProvenanceClassification::GenuineUpstream &&
            !b.stub_instructions.empty()) {
            out.status = ObjectIntegrationStatus::MalformedSymbolTable;
            out.error_message = "Synthetic stub instructions cannot be classified GenuineUpstream: " + b.symbol_name;
            return out;
        }
        binding_map[b.symbol_name] = b;
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

    auto write_region = [&](std::uint64_t va, const std::vector<std::uint8_t>& data,
                            std::uint32_t vm_prot, const std::string& region_name) -> bool {
        if ((vm_prot & 0x2u) != 0u && (vm_prot & 0x4u) != 0u) {
            out.status = ObjectIntegrationStatus::WxPermissionViolation;
            out.error_message = "W^X violation on staged region: " + region_name;
            return false;
        }
        if (va < opts.virtual_base_bias) {
            out.status = ObjectIntegrationStatus::MemoryBoundsExceeded;
            out.error_message = "Staged VA precedes virtual_base_bias: " + region_name;
            return false;
        }
        const std::uint64_t phys_off = va - opts.virtual_base_bias;
        std::uint64_t phys_end = 0;
        if (!checked_add_u64(phys_off, data.size(), phys_end) ||
            phys_end > guest_mem.size()) {
            out.status = ObjectIntegrationStatus::MemoryBoundsExceeded;
            out.error_message = "Staged region exceeds Memory capacity: " + region_name;
            return false;
        }
        for (std::size_t i = 0; i < data.size(); ++i) {
            guest_mem.write_u8(static_cast<std::size_t>(phys_off) + i, data[i]);
        }
        if (memory_map != nullptr) {
            GuestMemoryRegion reg;
            reg.name = region_name;
            reg.type = GuestMemoryRegionType::KernelImage;
            reg.base_address = va;
            reg.size = align_up_u64(data.size(), kPageGranuleBytes);
            reg.is_reserved = true;
            if (!memory_map->add_region(reg)) {
                out.status = ObjectIntegrationStatus::MemoryBoundsExceeded;
                out.error_message = "GuestMemoryMap rejected overlapping region: " + region_name;
                return false;
            }
        }
        return true;
    };

    if (!write_region(out.text_va, out.relocated_text_bytes, 0x5u, "__TEXT,__text")) {
        return out;
    }
    if (!out.stub_bytes.empty()) {
        if (!write_region(out.stub_va, out.stub_bytes, 0x5u, "__STUB_TEXT")) {
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

} // namespace winios::loader
