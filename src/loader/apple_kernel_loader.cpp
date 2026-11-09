#include "loader/apple_kernel_loader.h"

#include <iomanip>
#include <sstream>

namespace winios::loader {

AppleKernelLoadResult AppleKernelLoader::load_kernelcache(
    const std::uint8_t* data,
    std::size_t size,
    Memory& guest_memory,
    const memory::ApplePhysicalMemoryMap& memory_map,
    std::uint64_t kaslr_slide,
    const AdmissionPolicy& policy) {
    AppleKernelLoadResult res;

    // 1. Admission Policy Check
    AppleArtifactAdmissionController controller(policy);
    AppleBootArtifact candidate;
    candidate.artifact_id = "kernelcache.release.iphone15";
    candidate.kind = AppleArtifactKind::KernelCollection;
    candidate.target_device = "iPhone15,4";

    AdmissionResult admission = controller.evaluate_artifact(candidate, data, size);
    if (!admission.admitted) {
        res.success = false;
        res.error_message = "Admission evaluation rejected kernelcache: " + admission.reason;
        if (!admission.remediation.empty()) {
            res.error_message += " (" + admission.remediation + ")";
        }
        return res;
    }

    // 2. Resolve Physical Staging Base
    res.phys_load_base = memory_map.ram_base();
    for (const auto& r : memory_map.all_regions()) {
        if (r.kind == memory::AppleMemoryRegionKind::DramKernel) {
            res.phys_load_base = r.phys_base;
            break;
        }
    }

    // 3. Load Mach-O segments directly into guest RAM
    MachOLoadResult load_res = MachOLoader::load_from_buffer(
        data,
        size,
        guest_memory,
        res.phys_load_base,
        memory_map.ram_size(),
        kaslr_slide,
        nullptr);

    if (!load_res.is_success()) {
        res.success = false;
        res.error_message = "Mach-O segment staging failed: " + load_res.error_message;
        return res;
    }

    res.macho_result = std::move(load_res);

    // 4. Calculate Entry Points and Kernel Staging Top
    res.virt_kernel_base = res.macho_result.lowest_vmaddr + kaslr_slide;
    res.phys_entry_point = res.macho_result.entry_point;
    res.virt_entry_point = res.macho_result.lowest_vmaddr + (res.phys_entry_point - res.phys_load_base);

    // 16 KB page-aligned top of kernel data
    std::uint64_t raw_top = res.phys_load_base + res.macho_result.total_vmsize;
    res.top_of_kernel_data = (raw_top + 16383ULL) & ~16383ULL;
    res.effective_kernel_size = res.macho_result.total_vmsize;
    res.segment_count = res.macho_result.segments.size();
    res.kext_count = res.macho_result.fileset_entries.size();
    res.sha256_hex = res.macho_result.sha256_hex;

    res.success = true;
    return res;
}

std::string AppleKernelLoader::generate_kernel_report(const AppleKernelLoadResult& result) {
    std::stringstream ss;
    ss << "# iPhone 15 Native iOS Kernelcache Staging Report\n\n";

    ss << "| Metric / Parameter | Value |\n";
    ss << "| :--- | :--- |\n";
    ss << "| **Staging Status** | " << (result.success ? "**SUCCESS**" : "**FAILED**") << " |\n";
    ss << "| **Physical Staging Base** | `0x" << std::hex << result.phys_load_base << std::dec << "` |\n";
    ss << "| **Virtual Kernel Base** | `0x" << std::hex << result.virt_kernel_base << std::dec << "` |\n";
    ss << "| **Physical Entry Point (PC)** | `0x" << std::hex << result.phys_entry_point << std::dec << "` |\n";
    ss << "| **Virtual Entry Point** | `0x" << std::hex << result.virt_entry_point << std::dec << "` |\n";
    ss << "| **Top of Kernel Data** | `0x" << std::hex << result.top_of_kernel_data << std::dec << "` (16 KB aligned) |\n";
    ss << "| **Effective Kernel Size** | " << result.effective_kernel_size << " bytes ("
       << (result.effective_kernel_size / (1024 * 1024)) << " MB) |\n";
    ss << "| **Segments Loaded** | " << result.segment_count << " |\n";
    ss << "| **Embedded KEXTs** | " << result.kext_count << " |\n";
    ss << "| **SHA-256 Digest** | `" << result.sha256_hex << "` |\n\n";

    if (!result.macho_result.segments.empty()) {
        ss << "### Staged Mach-O Segments\n\n";
        ss << "| Segment Name | Virtual Address Range | Size (VM) | File Size | Physical Address | Protection |\n";
        ss << "| :--- | :--- | :--- | :--- | :--- | :--- |\n";
        for (const auto& s : result.macho_result.segments) {
            std::string prot;
            if (s.initprot & kVmProtRead) prot += "R";
            if (s.initprot & kVmProtWrite) prot += "W";
            if (s.initprot & kVmProtExecute) prot += "X";

            ss << "| `" << s.name << "` | `0x" << std::hex << s.vmaddr << " - 0x" << (s.vmaddr + s.vmsize)
               << "` | " << std::dec << s.vmsize << " | " << s.filesize << " | `0x"
               << std::hex << s.mapped_address << std::dec << "` | `" << prot << "` |\n";
        }
        ss << "\n";
    }

    if (!result.macho_result.fileset_entries.empty()) {
        ss << "### Embedded Fileset Entries (KEXTs)\n\n";
        ss << "| Entry Identifier | Virtual Address | File Offset |\n";
        ss << "| :--- | :--- | :--- |\n";
        for (const auto& e : result.macho_result.fileset_entries) {
            ss << "| `" << e.entry_id << "` | `0x" << std::hex << e.vmaddr << std::dec << "` | `0x"
               << std::hex << e.fileoff << std::dec << "` |\n";
        }
    }

    return ss.str();
}

} // namespace winios::loader
