// =============================================================================
// diff_winios_runner.cpp
//
// Campaign 191-V Stage V2:
//   Persistent stdin/stdout differential execution harness for WinIOS ARM64 CPU.
//   Runs instruction sequences with 16 KB Stage-1 Normal-Memory MMU enabled
//   (matching ARMv8 EL0 unaligned Normal memory semantics) so external oracles
//   (Unicorn Engine / QEMU TCG ARM64) can compare post-instruction state.
// =============================================================================

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"

using namespace winios;

namespace {

constexpr std::uint64_t kRamSize   = 0x100000ULL; // 1 MB
constexpr std::uint64_t kL2Pa      = 0x0E0000ULL; // 16 KB aligned
constexpr std::uint64_t kL3Pa      = 0x0E4000ULL; // 16 KB aligned
constexpr std::uint64_t kVbarPa    = 0x080000ULL;

void init_mmu(CPU& cpu, Memory& mem) {
    for (std::uint64_t off = 0; off < 0x8000ULL; off += 8u) {
        mem.write_u64(static_cast<std::size_t>(kL2Pa + off), 0ULL);
    }
    mem.write_u64(static_cast<std::size_t>(kL2Pa), kL3Pa | 0x3ULL);

    auto map_16k = [&](std::uint64_t va, std::uint64_t pa, std::uint8_t ap,
                       bool uxn, bool pxn) {
        const std::uint64_t l3_idx = (va >> 14u) & 0x7FFULL;
        const std::uint64_t desc =
            (pa & 0x0000FFFFFFFFC000ULL)
            | 0x3ULL
            | (0ULL << 2u) // AttrIndx=0 -> 0xFF Normal
            | (static_cast<std::uint64_t>(ap & 0x3u) << 6u)
            | (1ULL << 10u) // AF=1
            | (uxn ? (1ULL << 54u) : 0ULL)
            | (pxn ? (1ULL << 53u) : 0ULL);
        mem.write_u64(static_cast<std::size_t>(kL3Pa + l3_idx * 8u), desc);
    };

    // [0x00000, 0x20000): RX code window
    for (std::uint64_t va = 0x00000ULL; va < 0x20000ULL; va += 0x4000ULL) {
        map_16k(va, va, 3u /* EL0+EL1 RO */, false, false);
    }
    // [0x40000, 0x60000): RW+NX data window
    for (std::uint64_t va = 0x40000ULL; va < 0x60000ULL; va += 0x4000ULL) {
        map_16k(va, va, 1u /* EL0+EL1 RW */, true, true);
    }
    // VBAR page EL1-only RX
    map_16k(kVbarPa, kVbarPa, 2u, true, false);
    mem.write_u32(static_cast<std::size_t>(kVbarPa + 0x400u), 0xD503201Fu);

    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(0x00300000ULL);
    cpu.set_vbar_el1(kVbarPa);
    const std::uint64_t tcr =
        28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.get_mmu().set_ttbr0_el1(kL2Pa);
    cpu.get_mmu().invalidate_tlb_all();
    // M=1, DZE=1, WXN=1, SA0=0 (allow arbitrary SP probe values cleanly), A=0
    cpu.set_sctlr_el1((0x00C50838ULL & ~(1ULL << 4u)) | (1ULL << 0u) | (1ULL << 14u) | (1ULL << 19u));
    cpu.set_current_el(0);
}

std::vector<std::uint8_t> decode_hex(const std::string& hex) {
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        const unsigned int b = std::stoul(hex.substr(i, 2), nullptr, 16);
        out.push_back(static_cast<std::uint8_t>(b & 0xFFu));
    }
    return out;
}

std::string encode_hex(const std::uint8_t* ptr, std::size_t len) {
    static const char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        s.push_back(kHex[(ptr[i] >> 4u) & 0xFu]);
        s.push_back(kHex[ptr[i] & 0xFu]);
    }
    return s;
}

} // namespace

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream hdr(line);
        std::string tag;
        hdr >> tag;
        if (tag == "QUIT") break;
        if (tag != "CASE") return 2;

        std::size_t step_count = 0;
        std::uint64_t init_pc = 0;
        std::uint64_t init_sp = 0;
        unsigned int init_nzcv = 0;
        std::uint64_t data_addr = 0;
        std::size_t data_len = 0;
        hdr >> std::dec >> step_count
            >> std::hex >> init_pc >> init_sp >> init_nzcv >> data_addr
            >> std::dec >> data_len;

        std::string x_line, q_line, code_line, data_line;
        if (!std::getline(std::cin, x_line) ||
            !std::getline(std::cin, q_line) ||
            !std::getline(std::cin, code_line) ||
            !std::getline(std::cin, data_line)) {
            return 3;
        }

        CPU cpu;
        Memory mem(kRamSize);
        init_mmu(cpu, mem);

        {
            std::istringstream xs(x_line);
            std::string xtag;
            xs >> xtag;
            for (std::size_t i = 0; i < 31; ++i) {
                std::uint64_t val = 0;
                xs >> std::hex >> val;
                cpu.write_register(i, val);
            }
        }
        {
            std::istringstream qs(q_line);
            std::string qtag;
            qs >> qtag;
            for (std::size_t i = 0; i < 32; ++i) {
                std::string tok;
                qs >> tok;
                const auto colon = tok.find(':');
                const std::uint64_t lo = std::stoull(tok.substr(0, colon), nullptr, 16);
                const std::uint64_t hi = std::stoull(tok.substr(colon + 1), nullptr, 16);
                cpu.write_vector_register(i, lo, hi);
            }
        }
        {
            std::istringstream cs(code_line);
            std::string ctag, chex;
            cs >> ctag >> chex;
            auto code_bytes = decode_hex(chex);
            for (std::size_t i = 0; i < code_bytes.size() && (init_pc + i) < kRamSize; ++i) {
                mem.write_u8(static_cast<std::size_t>(init_pc + i), code_bytes[i]);
            }
        }
        {
            std::istringstream ds(data_line);
            std::string dtag, dhex;
            ds >> dtag >> dhex;
            auto data_bytes = decode_hex(dhex);
            for (std::size_t i = 0; i < data_bytes.size() && (data_addr + i) < kRamSize; ++i) {
                mem.write_u8(static_cast<std::size_t>(data_addr + i), data_bytes[i]);
            }
        }

        cpu.set_program_counter(init_pc);
        cpu.set_stack_pointer(init_sp);
        cpu.set_nzcv(static_cast<std::uint8_t>(init_nzcv & 0xFu));

        std::string status = "OK";
        std::size_t steps_done = 0;
        std::uint32_t fault_esr = 0;
        for (std::size_t s = 0; s < step_count; ++s) {
            try {
                cpu.step(mem);
                ++steps_done;
                if (cpu.get_current_el() != 0) {
                    status = "FAULT";
                    fault_esr = cpu.get_esr_el1();
                    break;
                }
            } catch (const std::exception& ex) {
                status = std::string("ERR:") + ex.what();
                for (char& ch : status) {
                    if (ch == ' ' || ch == '\n' || ch == '\r') ch = '_';
                }
                break;
            }
        }

        std::cout << "RESULT " << status
                  << " " << std::dec << steps_done
                  << " " << std::hex << cpu.get_program_counter()
                  << " " << cpu.get_stack_pointer()
                  << " " << static_cast<unsigned int>(cpu.get_nzcv())
                  << " " << fault_esr << "\n";

        std::cout << "X";
        for (std::size_t i = 0; i < 31; ++i) {
            std::cout << " " << std::hex << cpu.read_register(i);
        }
        std::cout << "\n";

        std::cout << "Q";
        for (std::size_t i = 0; i < 32; ++i) {
            const auto q = cpu.read_vector_register(i);
            std::cout << " " << std::hex << q.low << ":" << q.high;
        }
        std::cout << "\n";

        std::vector<std::uint8_t> out_data(data_len, 0);
        for (std::size_t i = 0; i < data_len && (data_addr + i) < kRamSize; ++i) {
            out_data[i] = mem.read_u8(static_cast<std::size_t>(data_addr + i));
        }
        std::cout << "DATA " << (data_len > 0 ? encode_hex(out_data.data(), data_len) : "-") << "\n" << std::flush;
    }
    return 0;
}
