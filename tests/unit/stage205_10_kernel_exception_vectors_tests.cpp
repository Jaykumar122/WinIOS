#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/kernel_exception_vectors.h"

using namespace winios::cpu;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_vector_offsets() {
    std::cout << "[Test 1] Vector offset calculations..." << std::endl;

    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SP0, ExceptionVectorType::Synchronous) == 0x000, "CurrentEL_SP0 Sync offset");
    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SP0, ExceptionVectorType::IRQ) == 0x080, "CurrentEL_SP0 IRQ offset");
    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SP0, ExceptionVectorType::FIQ) == 0x100, "CurrentEL_SP0 FIQ offset");
    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SP0, ExceptionVectorType::SError) == 0x180, "CurrentEL_SP0 SError offset");

    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::Synchronous) == 0x200, "CurrentEL_SPx Sync offset");
    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::IRQ) == 0x280, "CurrentEL_SPx IRQ offset");
    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::FIQ) == 0x300, "CurrentEL_SPx FIQ offset");
    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::SError) == 0x380, "CurrentEL_SPx SError offset");

    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch64, ExceptionVectorType::Synchronous) == 0x400, "LowerEL_AArch64 Sync offset");
    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch64, ExceptionVectorType::IRQ) == 0x480, "LowerEL_AArch64 IRQ offset");
    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch64, ExceptionVectorType::FIQ) == 0x500, "LowerEL_AArch64 FIQ offset");
    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch64, ExceptionVectorType::SError) == 0x580, "LowerEL_AArch64 SError offset");

    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch32, ExceptionVectorType::Synchronous) == 0x600, "LowerEL_AArch32 Sync offset");
    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch32, ExceptionVectorType::IRQ) == 0x680, "LowerEL_AArch32 IRQ offset");
    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch32, ExceptionVectorType::FIQ) == 0x700, "LowerEL_AArch32 FIQ offset");
    TEST_CHECK(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch32, ExceptionVectorType::SError) == 0x780, "LowerEL_AArch32 SError offset");

    std::cout << "  -> Vector offsets verified." << std::endl;
}

void test_alignment_validation() {
    std::cout << "[Test 2] VBAR_EL1 alignment validation..." << std::endl;

    std::string err;
    TEST_CHECK(KernelExceptionVectors::validate_vector_table_alignment(0x0, &err), "0x0 aligned");
    TEST_CHECK(KernelExceptionVectors::validate_vector_table_alignment(0x800, &err), "0x800 aligned");
    TEST_CHECK(KernelExceptionVectors::validate_vector_table_alignment(0x1000, &err), "0x1000 aligned");
    TEST_CHECK(KernelExceptionVectors::validate_vector_table_alignment(0xFFFF'8000'0000'0800ULL, &err), "High kernel aligned");

    TEST_CHECK(!KernelExceptionVectors::validate_vector_table_alignment(0x1, &err), "0x1 unaligned");
    TEST_CHECK(!err.empty(), "error string populated");
    TEST_CHECK(err.find("not aligned") != std::string::npos, "error message content");

    err.clear();
    TEST_CHECK(!KernelExceptionVectors::validate_vector_table_alignment(0x7FC, &err), "0x7FC unaligned");
    TEST_CHECK(!err.empty(), "error string populated");

    TEST_CHECK(!KernelExceptionVectors::validate_vector_table_alignment(0x804, nullptr), "0x804 unaligned");

    std::cout << "  -> VBAR_EL1 alignment validation verified." << std::endl;
}

void test_esr_decoding() {
    std::cout << "[Test 3] ESR_EL1 syndrome decoding..." << std::endl;

    // SVC in AArch64: EC = 0x15 (bits 31:26), IL = 1 (bit 25), ISS = 0x0
    std::uint32_t esr_svc = (0x15U << 26) | (1U << 25);
    auto info_svc = KernelExceptionVectors::decode_esr(esr_svc);
    TEST_CHECK(info_svc.ec == 0x15, "SVC EC 0x15");
    TEST_CHECK(info_svc.il == true, "SVC IL true");
    TEST_CHECK(info_svc.iss == 0, "SVC ISS 0");
    TEST_CHECK(info_svc.description.find("SVC") != std::string::npos, "SVC description");

    // Data Abort Same EL: EC = 0x25, IL = 1, ISS = 0x1234
    std::uint32_t esr_dab = (0x25U << 26) | (1U << 25) | 0x1234;
    auto info_dab = KernelExceptionVectors::decode_esr(esr_dab);
    TEST_CHECK(info_dab.ec == 0x25, "Data abort EC 0x25");
    TEST_CHECK(info_dab.il == true, "Data abort IL true");
    TEST_CHECK(info_dab.iss == 0x1234, "Data abort ISS");
    TEST_CHECK(info_dab.description.find("Data Abort taken without a change") != std::string::npos, "Data abort description");

    // Instruction Abort Lower EL: EC = 0x20, IL = 0
    std::uint32_t esr_iab = (0x20U << 26);
    auto info_iab = KernelExceptionVectors::decode_esr(esr_iab);
    TEST_CHECK(info_iab.ec == 0x20, "Instruction abort EC 0x20");
    TEST_CHECK(info_iab.il == false, "Instruction abort IL false");
    TEST_CHECK(info_iab.description.find("Instruction Abort from a lower") != std::string::npos, "Instruction abort description");

    // SP alignment fault: EC = 0x26
    std::uint32_t esr_sp = (0x26U << 26) | (1U << 25);
    auto info_sp = KernelExceptionVectors::decode_esr(esr_sp);
    TEST_CHECK(info_sp.ec == 0x26, "SP fault EC 0x26");
    TEST_CHECK(info_sp.description.find("SP alignment fault") != std::string::npos, "SP fault description");

    // BRK instruction: EC = 0x3C
    std::uint32_t esr_brk = (0x3CU << 26) | (1U << 25) | 0x42;
    auto info_brk = KernelExceptionVectors::decode_esr(esr_brk);
    TEST_CHECK(info_brk.ec == 0x3C, "BRK EC 0x3C");
    TEST_CHECK(info_brk.iss == 0x42, "BRK ISS 0x42");
    TEST_CHECK(info_brk.description.find("BRK") != std::string::npos, "BRK description");

    std::cout << "  -> ESR_EL1 decoding verified." << std::endl;
}

void test_default_vector_table() {
    std::cout << "[Test 4] Default synthetic vector table generation..." << std::endl;

    auto table = KernelExceptionVectors::build_default_vector_table();
    TEST_CHECK(table.size() == 512, "Table size 512 words");
    TEST_CHECK(table.size() * sizeof(std::uint32_t) == KernelExceptionVectors::kVectorTableAlignment, "Table size 2048 bytes");

    // Check each vector slot (32 words apart)
    for (std::size_t vec = 0; vec < KernelExceptionVectors::kTotalVectors; ++vec) {
        std::size_t base = vec * 32;
        TEST_CHECK(table[base + 0] == 0xD503201FU, "Word 0 NOP");
        TEST_CHECK(table[base + 1] == 0xD69F03E0U, "Word 1 ERET");
        TEST_CHECK(table[base + 2] == 0xD503201FU, "Word 2 NOP");
    }

    std::cout << "  -> Default vector table verified." << std::endl;
}

void test_string_helpers() {
    std::cout << "[Test 5] String conversion helpers..." << std::endl;

    TEST_CHECK(std::string(exception_origin_string(ExceptionOrigin::CurrentEL_SP0)) == "CurrentEL_SP0", "CurrentEL_SP0 str");
    TEST_CHECK(std::string(exception_origin_string(ExceptionOrigin::CurrentEL_SPx)) == "CurrentEL_SPx", "CurrentEL_SPx str");
    TEST_CHECK(std::string(exception_origin_string(ExceptionOrigin::LowerEL_AArch64)) == "LowerEL_AArch64", "LowerEL_AArch64 str");
    TEST_CHECK(std::string(exception_origin_string(ExceptionOrigin::LowerEL_AArch32)) == "LowerEL_AArch32", "LowerEL_AArch32 str");

    TEST_CHECK(std::string(exception_vector_type_string(ExceptionVectorType::Synchronous)) == "Synchronous", "Synchronous str");
    TEST_CHECK(std::string(exception_vector_type_string(ExceptionVectorType::IRQ)) == "IRQ", "IRQ str");
    TEST_CHECK(std::string(exception_vector_type_string(ExceptionVectorType::FIQ)) == "FIQ", "FIQ str");
    TEST_CHECK(std::string(exception_vector_type_string(ExceptionVectorType::SError)) == "SError", "SError str");

    std::cout << "  -> String conversion helpers verified." << std::endl;
}

} // namespace

int main() {
    std::cout << "=== Stage 205.10 Kernel Exception Vectors Tests ===" << std::endl;

    test_vector_offsets();
    test_alignment_validation();
    test_esr_decoding();
    test_default_vector_table();
    test_string_helpers();

    if (g_failures > 0) {
        std::cerr << "FAILED: " << g_failures << " tests failed.\n";
        return 1;
    }

    std::cout << "All Stage 205.10 tests PASSED." << std::endl;
    return 0;
}
