#include "loader/macho_user_loader.h"
#include "loader/embedded_user_apps_211.inl"

#include <cstring>
#include <iostream>

namespace winios::loader {

std::vector<std::uint8_t> MachoUserLoader::get_embedded_first_app() {
    return std::vector<std::uint8_t>(kUserFirstAppMacho_data, kUserFirstAppMacho_data + kUserFirstAppMacho_len);
}

std::vector<std::uint8_t> MachoUserLoader::get_embedded_init_app() {
    return std::vector<std::uint8_t>(kUserInitAppMacho_data, kUserInitAppMacho_data + kUserInitAppMacho_len);
}

bool MachoUserLoader::validate_header(const std::uint8_t* data, std::size_t size, std::string* out_err) {
    if (size < 32) {
        if (out_err) *out_err = "Binary size smaller than mach_header_64 (32 bytes)";
        return false;
    }

    std::uint32_t magic = 0;
    std::memcpy(&magic, data, sizeof(magic));
    if (magic != kMhMagic64) {
        if (out_err) *out_err = "Invalid Mach-O 64-bit magic (expected 0xFEEDFACF)";
        return false;
    }

    std::int32_t cputype = 0;
    std::memcpy(&cputype, data + 4, sizeof(cputype));
    if (cputype != kCpuTypeArm64) {
        if (out_err) *out_err = "Unsupported architecture (expected CPU_TYPE_ARM64)";
        return false;
    }

    return true;
}

LoadedUserImage MachoUserLoader::load_image(const std::vector<std::uint8_t>& binary,
                                           memory::UserAddressSpace& uas,
                                           memory::PhysicalPageManager& ppm,
                                           Memory& mem) {
    LoadedUserImage res{};

    if (!validate_header(binary.data(), binary.size(), &res.error_message)) {
        res.valid = false;
        return res;
    }

    std::uint32_t ncmds = 0;
    std::uint32_t sizeofcmds = 0;
    std::memcpy(&ncmds, binary.data() + 16, sizeof(ncmds));
    std::memcpy(&sizeofcmds, binary.data() + 20, sizeof(sizeofcmds));

    if (32 + sizeofcmds > binary.size()) {
        res.valid = false;
        res.error_message = "Corrupt Mach-O load command stream exceeds file boundary";
        return res;
    }

    std::size_t offset = 32;

    for (std::uint32_t i = 0; i < ncmds; ++i) {
        if (offset + 8 > binary.size()) {
            res.valid = false;
            res.error_message = "Truncated load command header";
            return res;
        }

        std::uint32_t cmd = 0;
        std::uint32_t cmdsize = 0;
        std::memcpy(&cmd, binary.data() + offset, sizeof(cmd));
        std::memcpy(&cmdsize, binary.data() + offset + 4, sizeof(cmdsize));

        if (cmdsize < 8 || offset + cmdsize > binary.size()) {
            res.valid = false;
            res.error_message = "Invalid load command size";
            return res;
        }

        if (cmd == kLcSegment64) {
            if (cmdsize < 72) {
                res.valid = false;
                res.error_message = "Malformed LC_SEGMENT_64 command size";
                return res;
            }

            char segname[17]{0};
            std::memcpy(segname, binary.data() + offset + 8, 16);

            std::uint64_t vmaddr = 0;
            std::uint64_t vmsize = 0;
            std::uint64_t fileoff = 0;
            std::uint64_t filesize = 0;
            std::int32_t maxprot = 0;
            std::int32_t initprot = 0;

            std::memcpy(&vmaddr, binary.data() + offset + 24, sizeof(vmaddr));
            std::memcpy(&vmsize, binary.data() + offset + 32, sizeof(vmsize));
            std::memcpy(&fileoff, binary.data() + offset + 40, sizeof(fileoff));
            std::memcpy(&filesize, binary.data() + offset + 48, sizeof(filesize));
            std::memcpy(&maxprot, binary.data() + offset + 56, sizeof(maxprot));
            std::memcpy(&initprot, binary.data() + offset + 60, sizeof(initprot));

            // W^X Policy check: no segment simultaneously writable and executable
            if ((initprot & 0x2) != 0 && (initprot & 0x4) != 0) {
                res.valid = false;
                res.error_message = "W^X violation: Segment marked simultaneously Writable and Executable";
                return res;
            }

            if (std::strcmp(segname, "__PAGEZERO") == 0) {
                // PAGEZERO: unmapped guard region to catch NULL pointer dereferences
                // Enforce that it has no permissions
                if (initprot != 0 || maxprot != 0) {
                    res.valid = false;
                    res.error_message = "__PAGEZERO segment must have VM_PROT_NONE";
                    return res;
                }
            } else if (vmsize > 0) {
                // Determine page permissions
                memory::PagePermissions perms = memory::PagePermissions::UserRead;
                if ((initprot & 0x4) != 0) {
                    perms = memory::PagePermissions::UserExecute;
                } else if ((initprot & 0x2) != 0) {
                    perms = memory::PagePermissions::UserReadWrite;
                }

                // Allocate physical pages and map into UserAddressSpace
                const std::size_t pages = (vmsize + memory::kPhysicalPageSize16KB - 1) / memory::kPhysicalPageSize16KB;
                for (std::size_t p = 0; p < pages; ++p) {
                    const std::uint64_t page_va = vmaddr + (p * memory::kPhysicalPageSize16KB);
                    const std::uint64_t pa = ppm.allocate_page();
                    if (pa == 0) {
                        res.valid = false;
                        res.error_message = "Out of physical DRAM pages while loading Mach-O segment";
                        return res;
                    }

                    // Zero-initialize page
                    for (std::size_t b = 0; b < memory::kPhysicalPageSize16KB; ++b) {
                        if (pa + b < mem.size()) mem.write_u8(static_cast<std::size_t>(pa + b), 0);
                    }

                    // Copy payload if within filesize
                    const std::uint64_t page_file_offset = p * memory::kPhysicalPageSize16KB;
                    if (page_file_offset < filesize && fileoff + page_file_offset < binary.size()) {
                        const std::size_t copy_bytes = std::min(
                            static_cast<std::size_t>(memory::kPhysicalPageSize16KB),
                            static_cast<std::size_t>(filesize - page_file_offset));
                        for (std::size_t b = 0; b < copy_bytes; ++b) {
                            if (pa + b < mem.size()) {
                                mem.write_u8(static_cast<std::size_t>(pa + b),
                                             binary[fileoff + page_file_offset + b]);
                            }
                        }
                    }

                    if (!uas.map_user_page(page_va, pa, perms)) {
                        res.valid = false;
                        res.error_message = "Failed to map user page into UserAddressSpace";
                        return res;
                    }
                }

                if (std::strcmp(segname, "__TEXT") == 0) {
                    res.text_vmaddr = vmaddr;
                    res.text_vmsize = vmsize;
                } else if (std::strcmp(segname, "__DATA") == 0) {
                    res.data_vmaddr = vmaddr;
                    res.data_vmsize = vmsize;
                }
                ++res.segments_loaded;
            }
        } else if (cmd == kLcMain) {
            if (cmdsize < 24) {
                res.valid = false;
                res.error_message = "Malformed LC_MAIN command";
                return res;
            }
            std::uint64_t entryoff = 0;
            std::uint64_t stacksize = 0;
            std::memcpy(&entryoff, binary.data() + offset + 8, sizeof(entryoff));
            std::memcpy(&stacksize, binary.data() + offset + 16, sizeof(stacksize));

            res.entry_point = res.text_vmaddr + entryoff;
            if (stacksize != 0) res.stack_size = std::min(stacksize, 0x10000ULL);
        } else if (cmd == kLcUnixThread) {
            // ARM_THREAD_STATE64 flavor: entry point PC is at offset 8 + 16 + (32 * 8) = 280
            if (cmdsize >= 288) {
                std::uint64_t thread_pc = 0;
                std::memcpy(&thread_pc, binary.data() + offset + 16 + 256, sizeof(thread_pc));
                if (thread_pc != 0) {
                    res.entry_point = thread_pc;
                }
            }
        }

        offset += cmdsize;
    }

    if (res.entry_point == 0 && res.text_vmaddr != 0) {
        // Fallback: default to start of __TEXT segment
        res.entry_point = res.text_vmaddr;
    }

    res.valid = (res.segments_loaded > 0 && res.entry_point != 0);
    return res;
}

} // namespace winios::loader
