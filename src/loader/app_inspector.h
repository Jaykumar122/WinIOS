#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace winios::loader {

struct SegmentInfo {
    std::string segname;
    uint64_t    vmaddr{0};
    uint64_t    vmsize{0};
    uint64_t    fileoff{0};
    uint64_t    filesize{0};
    uint32_t    maxprot{0};
    uint32_t    initprot{0};
    uint32_t    nsects{0};
};

struct AppInspectionReport {
    bool                     is_valid{false};
    std::string              rejection_reason;
    uint32_t                 magic{0};
    uint32_t                 cputype{0};
    uint32_t                 cpusubtype{0};
    uint32_t                 filetype{0};
    uint32_t                 ncmds{0};
    uint32_t                 sizeofcmds{0};
    uint64_t                 entry_point_offset{0};
    std::vector<std::string> load_commands;
    std::vector<SegmentInfo> segments;
    size_t                   binary_size_bytes{0};
    bool                     wx_clean{true};
    bool                     has_code_signature{false};
    uint32_t                 code_signature_offset{0};
    uint32_t                 code_signature_size{0};
    bool                     page_aligned_16k{true};
};

class AppInspector {
public:
    static constexpr size_t kMinMachOSize = 32;
    static constexpr size_t kMaxMachOSize = 64 * 1024 * 1024; // 64 MB
    static constexpr uint32_t kMachO64Magic = 0xfeedfacf;
    static constexpr uint32_t kCpuTypeArm64 = 0x0100000c;
    static constexpr uint32_t kMhExecute = 2;

    static AppInspectionReport inspect_binary(const uint8_t* data, size_t size);
    static AppInspectionReport inspect_file(const std::string& host_or_vfs_path);
};

} // namespace winios::loader
