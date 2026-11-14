#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/macho_loader.h"
#include "loader/real_artifact_harness.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::loader;

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #cond << "\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

#define EXPECT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #a " != " #b \
                      << " (" << (a) << " != " << (b) << ")\n"; \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

static std::string resolve_path(const std::string& rel) {
    const std::vector<std::string> candidates = {
        rel,
        "WinIOS/" + rel,
        "../" + rel,
        "../../" + rel,
        "D:/WinISO26/WinIOS/" + rel,
        "D:/WinISO26/" + rel
    };
    for (const auto& c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f.is_open()) return c;
    }
    return rel;
}

static std::vector<std::uint8_t> read_binary_file(const std::string& path) {
    const std::string resolved = resolve_path(path);
    std::ifstream file(resolved, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Cannot open artifact: " << path << " (resolved: " << resolved << ")\n";
        return {};
    }
    const auto sz = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(sz);
    file.read(reinterpret_cast<char*>(bytes.data()), sz);
    return bytes;
}

void test_workload_1_smp_threads() {
    std::cout << "Running test_workload_1_smp_threads...\n";
    const std::string path = "tests/artifacts/bin/guest_kernel_smp_threads";
    auto bytes = read_binary_file(path);
    EXPECT_TRUE(!bytes.empty());
    EXPECT_EQ(bytes.size(), 50328u);

    const std::string sha = RealArtifactHarness::compute_sha256(bytes.data(), bytes.size());
    EXPECT_EQ(sha, "d30347dc1bc0dbf5e48594fdc42a8f5970254b18bb4a9c96a56399577ac7413c");

    Memory mem(0x400000ULL); // 4MB
    MachOLoadResult load_res = MachOLoader::load_from_buffer(bytes.data(), bytes.size(), mem, 0x0ULL, 0x400000ULL);
    EXPECT_TRUE(load_res.is_success());

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(load_res.entry_point);
    cpu.set_stack_pointer(0x70000ULL);

    std::size_t steps = 0;
    while (steps < 2000) {
        if (mem.read_u64(0x40008) == 0x534D5054ULL) { // "SMPT"
            break;
        }
        cpu.step(mem);
        ++steps;
    }

    EXPECT_EQ(mem.read_u64(0x40008), 0x534D5054ULL);
    EXPECT_EQ(mem.read_u64(0x40000), 10ULL); // 10 iterations under exclusive spinlock
    EXPECT_TRUE(steps > 20);
    std::cout << "  Retired " << steps << " instructions.\n";
}

void test_workload_2_page_walk() {
    std::cout << "Running test_workload_2_page_walk...\n";
    const std::string path = "tests/artifacts/bin/guest_kernel_page_walk";
    auto bytes = read_binary_file(path);
    EXPECT_TRUE(!bytes.empty());
    EXPECT_EQ(bytes.size(), 50152u);

    const std::string sha = RealArtifactHarness::compute_sha256(bytes.data(), bytes.size());
    EXPECT_EQ(sha, "938fbffca69d62cabdca7dafde37d1b41b3cc02c1fe8920ae2f13997547d0a24");

    Memory mem(0x400000ULL);
    MachOLoadResult load_res = MachOLoader::load_from_buffer(bytes.data(), bytes.size(), mem, 0x0ULL, 0x400000ULL);
    EXPECT_TRUE(load_res.is_success());

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(load_res.entry_point);
    cpu.set_stack_pointer(0x70000ULL);

    std::size_t steps = 0;
    while (steps < 2000) {
        if (mem.read_u64(0x40008) == 0x5047574BULL) { // "PGWK"
            break;
        }
        cpu.step(mem);
        ++steps;
    }

    EXPECT_EQ(mem.read_u64(0x40008), 0x5047574BULL);
    EXPECT_EQ(mem.read_u64(0x40000), 0x60000ULL); // Translated PA = 0x60000
    EXPECT_TRUE(steps > 15);
    std::cout << "  Retired " << steps << " instructions.\n";
}

void test_workload_3_ipc_pingpong() {
    std::cout << "Running test_workload_3_ipc_pingpong...\n";
    const std::string path = "tests/artifacts/bin/guest_kernel_ipc_pingpong";
    auto bytes = read_binary_file(path);
    EXPECT_TRUE(!bytes.empty());
    EXPECT_EQ(bytes.size(), 50424u);

    const std::string sha = RealArtifactHarness::compute_sha256(bytes.data(), bytes.size());
    EXPECT_EQ(sha, "1b87c7bd5d390e6194e78a5416ff7c8bbce8f6bf67f6d717b284eb8a246323d0");

    Memory mem(0x400000ULL);
    MachOLoadResult load_res = MachOLoader::load_from_buffer(bytes.data(), bytes.size(), mem, 0x0ULL, 0x400000ULL);
    EXPECT_TRUE(load_res.is_success());

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(load_res.entry_point);
    cpu.set_stack_pointer(0x70000ULL);

    std::size_t steps = 0;
    while (steps < 2000) {
        if (mem.read_u64(0x40008) == 0x50494E47ULL) { // "PING"
            break;
        }
        cpu.step(mem);
        ++steps;
    }

    EXPECT_EQ(mem.read_u64(0x40008), 0x50494E47ULL);
    EXPECT_EQ(mem.read_u64(0x40000), 5ULL); // 5 pingpong exchanges
    EXPECT_TRUE(steps > 25);
    std::cout << "  Retired " << steps << " instructions.\n";
}

int main() {
    std::cout << "=== WINIOS STAGE 194.18: INDEPENDENTLY COMPILED KERNEL WORKLOAD CORPUS TESTS ===\n";

    test_workload_1_smp_threads();
    test_workload_2_page_walk();
    test_workload_3_ipc_pingpong();

    std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed.\n";
    return g_fail == 0 ? 0 : 1;
}
