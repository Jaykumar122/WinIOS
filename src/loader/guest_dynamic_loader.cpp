#include "loader/guest_dynamic_loader.h"
#include "loader/macho_user_loader.h"
#include "loader/embedded_runtime_lib_212.inl"

#include <algorithm>
#include <cstring>

namespace winios::loader {

inline constexpr std::uint32_t kLcSymtab    = 0x00000002u;
inline constexpr std::uint32_t kLcDysymtab  = 0x0000000Bu;
inline constexpr std::uint32_t kLcIdDylib   = 0x0000000Du;

inline constexpr std::uint8_t kNTypeMask = 0x0Eu;
inline constexpr std::uint8_t kNSect     = 0x0Eu;
inline constexpr std::uint8_t kNExt      = 0x01u;
inline constexpr std::uint8_t kNUndf     = 0x00u;

GuestDynamicLoader::GuestDynamicLoader(
    memory::UserAddressSpace& uas,
    memory::PhysicalPageManager& ppm,
    Memory& mem)
    : uas_(uas), ppm_(ppm), mem_(mem) {}

bool GuestDynamicLoader::load_library(
    const std::string& name,
    const std::vector<std::uint8_t>& binary,
    LoadedDylibInfo* out_info,
    std::string* out_err) {
    if (loaded_libs_.find(name) != loaded_libs_.end()) {
        if (out_info) *out_info = loaded_libs_[name];
        return true; // Already loaded
    }

    if (!MachoUserLoader::validate_header(binary.data(), binary.size(), out_err)) {
        return false;
    }

    std::uint32_t filetype = 0;
    std::uint32_t ncmds = 0;
    std::uint32_t sizeofcmds = 0;
    std::memcpy(&filetype, binary.data() + 12, sizeof(filetype));
    std::memcpy(&ncmds, binary.data() + 16, sizeof(ncmds));
    std::memcpy(&sizeofcmds, binary.data() + 20, sizeof(sizeofcmds));

    if (filetype != 6u /* MH_DYLIB */ && filetype != 2u /* MH_EXECUTE */) {
        if (out_err) *out_err = "Unsupported Mach-O filetype for dylib loading (expected MH_DYLIB)";
        return false;
    }

    if (32 + sizeofcmds > binary.size()) {
        if (out_err) *out_err = "Load commands exceed binary size";
        return false;
    }

    // Determine preferred VM address and total VM size
    std::uint64_t preferred_base = 0;
    bool has_preferred_base = false;
    std::uint64_t total_vmsize = 0;

    std::size_t offset = 32;
    std::string dylib_id = name;

    for (std::uint32_t i = 0; i < ncmds; ++i) {
        if (offset + 8 > binary.size()) break;
        std::uint32_t cmd = 0;
        std::uint32_t cmdsize = 0;
        std::memcpy(&cmd, binary.data() + offset, sizeof(cmd));
        std::memcpy(&cmdsize, binary.data() + offset + 4, sizeof(cmdsize));

        if (cmd == kLcIdDylib && cmdsize >= 24) {
            std::uint32_t stroff = 0;
            std::memcpy(&stroff, binary.data() + offset + 8, sizeof(stroff));
            if (stroff < cmdsize && offset + stroff < binary.size()) {
                const char* str = reinterpret_cast<const char*>(binary.data() + offset + stroff);
                dylib_id = std::string(str, strnlen(str, cmdsize - stroff));
            }
        } else if (cmd == kLcSegment64 && cmdsize >= 72) {
            char segname[17]{0};
            std::memcpy(segname, binary.data() + offset + 8, 16);
            if (std::strcmp(segname, "__PAGEZERO") != 0) {
                std::uint64_t vmaddr = 0;
                std::uint64_t vmsize = 0;
                std::memcpy(&vmaddr, binary.data() + offset + 24, sizeof(vmaddr));
                std::memcpy(&vmsize, binary.data() + offset + 32, sizeof(vmsize));
                if (!has_preferred_base) {
                    preferred_base = vmaddr;
                    has_preferred_base = true;
                }
                total_vmsize += vmsize;
            }
        }

        offset += cmdsize;
    }

    // Round total size up to 16KB pages
    const std::uint64_t aligned_size = (total_vmsize + memory::kPhysicalPageSize16KB - 1) &
                                       ~(memory::kPhysicalPageSize16KB - 1);

    // Assign non-overlapping base VA
    const std::uint64_t base_va = next_dylib_va_;
    const std::int64_t slide = static_cast<std::int64_t>(base_va) - static_cast<std::int64_t>(preferred_base);

    // Advance next_dylib_va_ by aligned size + 64KB guard
    next_dylib_va_ += aligned_size + (4 * memory::kPhysicalPageSize16KB);

    LoadedDylibInfo info{};
    info.name = name;
    info.dylib_id = dylib_id;
    info.base_va = base_va;
    info.size = aligned_size;
    info.slide = slide;

    // Load segments and map into guest address space
    offset = 32;
    for (std::uint32_t i = 0; i < ncmds; ++i) {
        if (offset + 8 > binary.size()) break;
        std::uint32_t cmd = 0;
        std::uint32_t cmdsize = 0;
        std::memcpy(&cmd, binary.data() + offset, sizeof(cmd));
        std::memcpy(&cmdsize, binary.data() + offset + 4, sizeof(cmdsize));

        if (cmd == kLcSegment64 && cmdsize >= 72) {
            char segname[17]{0};
            std::memcpy(segname, binary.data() + offset + 8, 16);

            if (std::strcmp(segname, "__PAGEZERO") != 0) {
                std::uint64_t vmaddr = 0;
                std::uint64_t vmsize = 0;
                std::uint64_t fileoff = 0;
                std::uint64_t filesize = 0;
                std::int32_t initprot = 0;

                std::memcpy(&vmaddr, binary.data() + offset + 24, sizeof(vmaddr));
                std::memcpy(&vmsize, binary.data() + offset + 32, sizeof(vmsize));
                std::memcpy(&fileoff, binary.data() + offset + 40, sizeof(fileoff));
                std::memcpy(&filesize, binary.data() + offset + 48, sizeof(filesize));
                std::memcpy(&initprot, binary.data() + offset + 60, sizeof(initprot));

                // Enforce W^X
                if ((initprot & 0x2) != 0 && (initprot & 0x4) != 0) {
                    if (out_err) *out_err = "W^X violation in dylib segment '" + std::string(segname) + "'";
                    return false;
                }

                memory::PagePermissions perms = memory::PagePermissions::UserRead;
                if ((initprot & 0x4) != 0) {
                    perms = memory::PagePermissions::UserExecute;
                } else if ((initprot & 0x2) != 0) {
                    perms = memory::PagePermissions::UserReadWrite;
                }

                const std::uint64_t mapped_va = vmaddr + static_cast<std::uint64_t>(slide);
                const std::size_t num_pages = (vmsize + memory::kPhysicalPageSize16KB - 1) / memory::kPhysicalPageSize16KB;

                for (std::size_t p = 0; p < num_pages; ++p) {
                    const std::uint64_t page_va = mapped_va + (p * memory::kPhysicalPageSize16KB);
                    const std::uint64_t pa = ppm_.allocate_page();
                    if (pa == 0) {
                        if (out_err) *out_err = "DRAM exhaustion during dylib mapping";
                        for (std::uint64_t allocated_pa : info.allocated_pas) {
                            ppm_.free_page(allocated_pa);
                        }
                        return false;
                    }

                    info.allocated_pas.push_back(pa);
                    uas_.record_allocated_page(pa);

                    // Zero page
                    for (std::size_t b = 0; b < memory::kPhysicalPageSize16KB; ++b) {
                        if (pa + b < mem_.size()) mem_.write_u8(static_cast<std::size_t>(pa + b), 0);
                    }

                    // Copy file content if present
                    const std::uint64_t p_off = p * memory::kPhysicalPageSize16KB;
                    if (p_off < filesize && fileoff + p_off < binary.size()) {
                        const std::size_t copy_bytes = std::min(
                            static_cast<std::size_t>(memory::kPhysicalPageSize16KB),
                            static_cast<std::size_t>(filesize - p_off));
                        for (std::size_t b = 0; b < copy_bytes; ++b) {
                            if (pa + b < mem_.size()) {
                                mem_.write_u8(static_cast<std::size_t>(pa + b), binary[fileoff + p_off + b]);
                            }
                        }
                    }

                    if (!uas_.map_user_page(page_va, pa, perms)) {
                        if (out_err) *out_err = "Failed to map page into UserAddressSpace";
                        for (std::uint64_t allocated_pa : info.allocated_pas) {
                            ppm_.free_page(allocated_pa);
                        }
                        return false;
                    }
                }
            }
        }

        offset += cmdsize;
    }

    // Parse LC_SYMTAB to index exported symbols
    offset = 32;
    for (std::uint32_t i = 0; i < ncmds; ++i) {
        if (offset + 8 > binary.size()) break;
        std::uint32_t cmd = 0;
        std::uint32_t cmdsize = 0;
        std::memcpy(&cmd, binary.data() + offset, sizeof(cmd));
        std::memcpy(&cmdsize, binary.data() + offset + 4, sizeof(cmdsize));

        if (cmd == kLcSymtab && cmdsize >= 24) {
            std::uint32_t symoff = 0;
            std::uint32_t nsyms = 0;
            std::uint32_t stroff = 0;
            std::uint32_t strsize = 0;

            std::memcpy(&symoff, binary.data() + offset + 8, sizeof(symoff));
            std::memcpy(&nsyms, binary.data() + offset + 12, sizeof(nsyms));
            std::memcpy(&stroff, binary.data() + offset + 16, sizeof(stroff));
            std::memcpy(&strsize, binary.data() + offset + 20, sizeof(strsize));

            if (symoff + (nsyms * 16ULL) <= binary.size() && stroff + strsize <= binary.size()) {
                for (std::uint32_t s = 0; s < nsyms; ++s) {
                    const std::size_t nlist_off = symoff + (s * 16ULL);
                    std::uint32_t strx = 0;
                    std::uint8_t type = 0;
                    // std::uint8_t sect = 0;
                    // std::uint16_t desc = 0;
                    std::uint64_t val = 0;

                    std::memcpy(&strx, binary.data() + nlist_off + 0, sizeof(strx));
                    std::memcpy(&type, binary.data() + nlist_off + 4, sizeof(type));
                    // std::memcpy(&sect, binary.data() + nlist_off + 5, sizeof(sect));
                    // std::memcpy(&desc, binary.data() + nlist_off + 6, sizeof(desc));
                    std::memcpy(&val, binary.data() + nlist_off + 8, sizeof(val));

                    // If symbol is externally defined (N_SECT and N_EXT)
                    if ((type & kNTypeMask) == kNSect && (type & kNExt) != 0 && strx < strsize) {
                        const char* sym_name = reinterpret_cast<const char*>(binary.data() + stroff + strx);
                        const std::string name_str(sym_name);
                        const std::uint64_t resolved_va = val + static_cast<std::uint64_t>(slide);
                        info.exported_symbols[name_str] = resolved_va;
                        global_exports_[name_str] = resolved_va;
                    }
                }
            }
        }

        offset += cmdsize;
    }

    if (out_info) *out_info = info;
    loaded_libs_[name] = std::move(info);
    return true;
}

std::uint64_t GuestDynamicLoader::find_exported_symbol(const std::string& name) const {
    auto it = global_exports_.find(name);
    return (it != global_exports_.end()) ? it->second : 0ULL;
}

std::vector<std::uint8_t> GuestDynamicLoader::get_embedded_runtime_library() {
    return std::vector<std::uint8_t>(kLibWiniosRuntime_data, kLibWiniosRuntime_data + kLibWiniosRuntime_len);
}

bool GuestDynamicLoader::resolve_and_bind_symbols(
    const std::vector<std::uint8_t>& binary,
    std::uint64_t image_slide,
    std::string* out_err) {
    std::uint32_t ncmds = 0;
    std::memcpy(&ncmds, binary.data() + 16, sizeof(ncmds));

    std::size_t offset = 32;

    std::uint32_t symoff = 0;
    std::uint32_t nsyms = 0;
    std::uint32_t stroff = 0;
    std::uint32_t strsize = 0;

    std::uint32_t indirect_off = 0;
    std::uint32_t nindirect = 0;

    // First pass: locate LC_SYMTAB and LC_DYSYMTAB
    for (std::uint32_t i = 0; i < ncmds; ++i) {
        if (offset + 8 > binary.size()) break;
        std::uint32_t cmd = 0;
        std::uint32_t cmdsize = 0;
        std::memcpy(&cmd, binary.data() + offset, sizeof(cmd));
        std::memcpy(&cmdsize, binary.data() + offset + 4, sizeof(cmdsize));

        if (cmd == kLcSymtab && cmdsize >= 24) {
            std::memcpy(&symoff, binary.data() + offset + 8, sizeof(symoff));
            std::memcpy(&nsyms, binary.data() + offset + 12, sizeof(nsyms));
            std::memcpy(&stroff, binary.data() + offset + 16, sizeof(stroff));
            std::memcpy(&strsize, binary.data() + offset + 20, sizeof(strsize));
        } else if (cmd == kLcDysymtab && cmdsize >= 80) {
            std::memcpy(&indirect_off, binary.data() + offset + 64, sizeof(indirect_off));
            std::memcpy(&nindirect, binary.data() + offset + 68, sizeof(nindirect));
        }

        offset += cmdsize;
    }

    if (nsyms == 0 || strsize == 0) {
        return true; // No symbols to resolve
    }

    // Second pass: scan sections in LC_SEGMENT_64 for indirect symbol pointers
    offset = 32;
    for (std::uint32_t i = 0; i < ncmds; ++i) {
        if (offset + 8 > binary.size()) break;
        std::uint32_t cmd = 0;
        std::uint32_t cmdsize = 0;
        std::memcpy(&cmd, binary.data() + offset, sizeof(cmd));
        std::memcpy(&cmdsize, binary.data() + offset + 4, sizeof(cmdsize));

        if (cmd == kLcSegment64 && cmdsize >= 72) {
            std::uint32_t nsects = 0;
            std::memcpy(&nsects, binary.data() + offset + 64, sizeof(nsects));

            std::size_t sect_off = offset + 72;
            for (std::uint32_t s = 0; s < nsects; ++s) {
                if (sect_off + 80 > binary.size()) break;

                char sectname[17]{0};
                std::memcpy(sectname, binary.data() + sect_off, 16);
                std::uint64_t addr = 0;
                std::uint64_t size = 0;
                std::uint32_t flags = 0;
                std::uint32_t reserved1 = 0; // indirect symbol table index

                std::memcpy(&addr, binary.data() + sect_off + 32, sizeof(addr));
                std::memcpy(&size, binary.data() + sect_off + 40, sizeof(size));
                std::memcpy(&flags, binary.data() + sect_off + 56, sizeof(flags));
                std::memcpy(&reserved1, binary.data() + sect_off + 68, sizeof(reserved1));

                const std::uint32_t type = flags & 0x000000FFu;

                // Check for S_NON_LAZY_SYMBOL_POINTERS (0x6) or S_LAZY_SYMBOL_POINTERS (0x7) or __got
                if (type == 0x6u || type == 0x7u || std::strcmp(sectname, "__got") == 0 ||
                    std::strcmp(sectname, "__la_symbol_ptr") == 0 || std::strcmp(sectname, "__nl_symbol_ptr") == 0) {
                    const std::size_t pointer_count = size / 8;
                    for (std::size_t p = 0; p < pointer_count; ++p) {
                        const std::uint32_t indirect_index = reserved1 + static_cast<std::uint32_t>(p);
                        if (indirect_off + (indirect_index * 4ULL) + 4 <= binary.size()) {
                            std::uint32_t sym_idx = 0;
                            std::memcpy(&sym_idx, binary.data() + indirect_off + (indirect_index * 4ULL), sizeof(sym_idx));

                            // If not INDIRECT_SYMBOL_LOCAL (0x80000000) or INDIRECT_SYMBOL_ABS (0x40000000)
                            if (sym_idx < nsyms && (sym_idx & 0xC0000000u) == 0) {
                                const std::size_t nlist_off = symoff + (sym_idx * 16ULL);
                                std::uint32_t strx = 0;
                                std::memcpy(&strx, binary.data() + nlist_off + 0, sizeof(strx));

                                if (strx < strsize) {
                                    const char* sym_name = reinterpret_cast<const char*>(binary.data() + stroff + strx);
                                    const std::string name_str(sym_name);

                                    const std::uint64_t target_va = find_exported_symbol(name_str);
                                    if (target_va != 0) {
                                        // Write target VA to pointer in guest memory
                                        const std::uint64_t ptr_va = addr + image_slide + (p * 8ULL);
                                        if (!uas_.copyin(ptr_va, &target_va, sizeof(target_va))) {
                                            if (out_err) *out_err = "Failed to bind pointer for symbol '" + name_str + "' at 0x" + std::to_string(ptr_va);
                                            return false;
                                        }
                                    } else {
                                        // Check if symbol is weak
                                        std::uint16_t desc = 0;
                                        std::memcpy(&desc, binary.data() + nlist_off + 6, sizeof(desc));
                                        if ((desc & 0x0080u) != 0) { // N_WEAK_REF
                                            const std::uint64_t null_va = 0;
                                            const std::uint64_t ptr_va = addr + image_slide + (p * 8ULL);
                                            uas_.copyin(ptr_va, &null_va, sizeof(null_va));
                                        } else {
                                            if (out_err) *out_err = "Undefined symbol: '" + name_str + "'";
                                            return false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                sect_off += 80;
            }
        }

        offset += cmdsize;
    }

    return true;
}

} // namespace winios::loader
