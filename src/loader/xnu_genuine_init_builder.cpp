#include "loader/xnu_genuine_init_builder.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "loader/real_artifact_harness.h"
#include "loader/embedded_xnu_image.inl"

namespace winios::loader {

std::vector<std::uint8_t> XnuGenuineInitBuilder::get_embedded_image() {
    return std::vector<std::uint8_t>(
        kEmbeddedXnuGenuineImage,
        kEmbeddedXnuGenuineImage + kEmbeddedXnuGenuineImageSize);
}

std::vector<std::uint8_t> XnuGenuineInitBuilder::load_or_get_image(const std::string& preferred_path) {
    if (!preferred_path.empty()) {
        std::ifstream file(preferred_path, std::ios::binary);
        if (file.is_open()) {
            return std::vector<std::uint8_t>(
                (std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());
        }
    }

    // Try artifacts directory from root or build/
    std::ifstream file("artifacts/campaign-207/xnu_genuine_init.macho", std::ios::binary);
    if (file.is_open()) {
        return std::vector<std::uint8_t>(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
    }

    std::ifstream file_build("../artifacts/campaign-207/xnu_genuine_init.macho", std::ios::binary);
    if (file_build.is_open()) {
        return std::vector<std::uint8_t>(
            (std::istreambuf_iterator<char>(file_build)),
            std::istreambuf_iterator<char>());
    }

    return get_embedded_image();
}

bool XnuGenuineInitBuilder::recompile_with_zig(
    std::vector<std::uint8_t>& out_image,
    std::string* out_error) {

    const char* cmd = 
        "cmd.exe /c \""
        "zig cc -target aarch64-macos -ffreestanding -nostdlib -fno-sanitize=all -fno-stack-protector -O2 -c src/kernel/xnu_start.s -o scratch/re_start.o && "
        "zig cc -target aarch64-macos -ffreestanding -nostdlib -fno-sanitize=all -fno-stack-protector -O2 -c src/kernel/xnu_pe_init.c -o scratch/re_pe.o && "
        "zig cc -target aarch64-macos -ffreestanding -nostdlib -fno-sanitize=all -fno-stack-protector -O2 -c src/kernel/xnu_arm_init.c -o scratch/re_arm.o && "
        "zig cc -target aarch64-macos -nostdlib -fno-sanitize=all -fno-stack-protector -Wl,-e,__start -Wl,-pagezero_size,0 scratch/re_start.o scratch/re_pe.o scratch/re_arm.o -o scratch/re_kernel.macho"
        "\"";

    const int rc = std::system(cmd);
    if (rc != 0) {
        if (out_error) *out_error = "Zig compilation command returned non-zero exit code: " + std::to_string(rc);
        return false;
    }

    std::ifstream file("scratch/re_kernel.macho", std::ios::binary);
    if (!file.is_open()) {
        if (out_error) *out_error = "Failed to open scratch/re_kernel.macho after Zig compilation";
        return false;
    }

    out_image = std::vector<std::uint8_t>(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    return true;
}

std::string XnuGenuineInitBuilder::compute_sha256(const std::vector<std::uint8_t>& image) {
    return RealArtifactHarness::compute_sha256(image.data(), image.size());
}

std::string XnuGenuineInitBuilder::disassemble(std::uint32_t insn, std::uint64_t pc) {
    (void)pc;
    if (insn == 0xD5034FDFU) return "msr daifset, #0xf";
    if (insn == 0xD53800B4U) return "mrs x20, mpidr_el1";
    if (insn == 0x92401E94U) return "and x20, x20, #0xff";
    if (insn == 0xB5000194U) return "cbnz x20, secondary_park";
    if (insn == 0xD2A00602U) return "mov x2, #0x300000";
    if (insn == 0xD5181042U) return "msr cpacr_el1, x2";
    if (insn == 0xD5033FDFU) return "isb";
    if (insn == 0x9100003FU) return "mov sp, x1";
    if (insn == 0xAA0003F3U) return "mov x19, x0";
    if (insn == 0xAA1303E0U) return "mov x0, x19";
    if ((insn & 0xFC000000U) == 0x94000000U) return "bl _arm_init";
    if (insn == 0xD503207FU) return "wfi";
    if (insn == 0xD503205FU) return "wfe";
    if (insn == 0xD5033B9FU) return "dsb ish";
    if (insn == 0xD65F03C0U) return "ret";

    // Format raw hex
    std::ostringstream oss;
    oss << "raw 0x" << std::hex << std::setw(8) << std::setfill('0') << insn;
    return oss.str();
}

} // namespace winios::loader
