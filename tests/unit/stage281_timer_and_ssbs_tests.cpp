// ============================================================================
// WinIOS Campaign 281: Master Engineering Execution
// Stages 281.2 - 281.8: Deterministic Timer Fixed-Rate / Coalescing Suite &
// ARM64 FEAT_SSBS System Register / PSTATE.SSBS / SPSR / Snapshot Persistence
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/deterministic_virtual_timer.h"
#include "cpu/smp_core_state.h"
#include "cpu/sysreg.h"
#include "memory/memory.h"
#include "replay/replay_state_hash.h"
#include "snapshot/snapshot_serializer.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while (0)

namespace {

using namespace winios::cpu;
using namespace winios::replay;
using namespace winios::snapshot;
using namespace winios::sysreg;

constexpr std::uint32_t encode_mrs(std::uint8_t op0, std::uint8_t op1, std::uint8_t crn,
                                   std::uint8_t crm, std::uint8_t op2, std::uint32_t rt) noexcept {
    const std::uint32_t o0 = (op0 == 2u) ? 0u : 1u;
    return 0xD5300000u
         | (o0 << 19u)
         | (static_cast<std::uint32_t>(op1 & 7u) << 16u)
         | (static_cast<std::uint32_t>(crn & 15u) << 12u)
         | (static_cast<std::uint32_t>(crm & 15u) << 8u)
         | (static_cast<std::uint32_t>(op2 & 7u) << 5u)
         | (rt & 31u);
}

constexpr std::uint32_t encode_msr_reg(std::uint8_t op0, std::uint8_t op1, std::uint8_t crn,
                                       std::uint8_t crm, std::uint8_t op2, std::uint32_t rt) noexcept {
    const std::uint32_t o0 = (op0 == 2u) ? 0u : 1u;
    return 0xD5100000u
         | (o0 << 19u)
         | (static_cast<std::uint32_t>(op1 & 7u) << 16u)
         | (static_cast<std::uint32_t>(crn & 15u) << 12u)
         | (static_cast<std::uint32_t>(crm & 15u) << 8u)
         | (static_cast<std::uint32_t>(op2 & 7u) << 5u)
         | (rt & 31u);
}

constexpr std::uint32_t encode_msr_ssbs_imm(std::uint32_t crm) noexcept {
    return 0xD500401Fu | (3u << 16u) | ((crm & 0xFu) << 8u) | (1u << 5u);
}

constexpr std::uint32_t kInsnMrsX0Ssbs  = encode_mrs(3, 3, 4, 2, 6, 0);
constexpr std::uint32_t kInsnMsrSsbsX2  = encode_msr_reg(3, 3, 4, 2, 6, 2);
constexpr std::uint32_t kInsnMsrSsbs0   = encode_msr_ssbs_imm(0);
constexpr std::uint32_t kInsnMsrSsbs1   = encode_msr_ssbs_imm(1);
constexpr std::uint32_t kInsnSvc0       = 0xD4000001u;
constexpr std::uint32_t kInsnEret       = 0xD69F03E0u;

int test_timer_regression_and_contracts() {
    DeterministicVirtualTimer timer(24000000ULL);

    // 1. Exact #1123 reproduction: fire=1000, current=1500, period=2000 -> next=3000, elapsed_periods=1, missed_ticks=0.
    timer.set_control(0, DeterministicVirtualTimer::kCtlEnable);
    timer.set_comparator(0, 1000ULL);
    timer.advance_counter(1500ULL);
    TEST_ASSERT(timer.is_interrupt_asserted(0), "Core 0 interrupt asserted at 1500");
    timer.acknowledge(0);
    const TimerRearmResult st = timer.rearm_periodic(0, 2000ULL);
    TEST_ASSERT(st.status == TimerRearmStatus::Success, "rearm_periodic must succeed");
    TEST_ASSERT(st.next_deadline == 3000ULL, "Fixed-rate next deadline must be 3000");
    TEST_ASSERT(timer.get_comparator(0) == 3000ULL, "Core 0 comparator must be 3000");
    TEST_ASSERT(st.elapsed_periods == 1ULL && st.missed_ticks == 0ULL, "One period elapsed, zero missed");

    // 2. Multi-period catch-up coalescing: deadline=3000, period=2000, counter=9500 -> n=((9500-3000)/2000)+1=4 -> next=11000
    timer.advance_counter(8000ULL); // counter = 9500
    timer.acknowledge(0);
    const TimerRearmResult st2 = timer.rearm_periodic(0, 2000ULL);
    TEST_ASSERT(st2.status == TimerRearmStatus::Success, "Multi-period catch-up must rearm");
    TEST_ASSERT(st2.next_deadline == 11000ULL && timer.get_comparator(0) == 11000ULL,
                "Catch-up deadline must preserve D + n*P phase");
    TEST_ASSERT(st2.elapsed_periods == 4ULL && st2.missed_ticks == 3ULL,
                "4 elapsed periods -> 3 missed ticks");

    // 3. Before deadline: early rearm when counter < deadline (counter=10999 < 11000) advances cleanly by 1 period to 13000.
    timer.advance_counter(1499ULL); // counter = 10999
    const TimerRearmResult st3 = timer.rearm_periodic(0, 2000ULL);
    TEST_ASSERT(st3.status == TimerRearmStatus::Success,
                "Early rearm prior to deadline must succeed and advance from previous deadline");
    TEST_ASSERT(timer.get_comparator(0) == 13000ULL && st3.elapsed_periods == 1ULL && st3.missed_ticks == 0ULL,
                "Early rearm must advance deadline from 11000 to 13000");

    // 4. Exact boundary firing: advance to counter=13000 (== deadline) -> n = 1, next = 15000.
    timer.advance_counter(2001ULL); // counter = 13000
    timer.acknowledge(0);
    const TimerRearmResult st4 = timer.rearm_periodic(0, 2000ULL);
    TEST_ASSERT(st4.status == TimerRearmStatus::Success, "Exact deadline match must rearm");
    TEST_ASSERT(timer.get_comparator(0) == 15000ULL, "Exact deadline must advance by 1 period");

    // 5. Serialize / Deserialize round-trip parity & deterministic replay.
    const auto snap = timer.serialize();
    DeterministicVirtualTimer restored(24000000ULL);
    TEST_ASSERT(restored.deserialize(snap.data(), snap.size()), "Timer snapshot must deserialize");
    TEST_ASSERT(restored.get_comparator(0) == 15000ULL, "Restored comparator must match");
    timer.advance_counter(3500ULL);    // counter = 16500
    restored.advance_counter(3500ULL); // counter = 16500
    TEST_ASSERT(restored.rearm_periodic(0, 2000ULL).status == TimerRearmStatus::Success, "Restored rearm must match");
    TEST_ASSERT(timer.rearm_periodic(0, 2000ULL).status == TimerRearmStatus::Success, "Live rearm must match");
    TEST_ASSERT(restored.get_comparator(0) == timer.get_comparator(0),
                "Replay determinism must hold across serialize/deserialize");

    // 6. Invalid period, out-of-range core, and DeadlineSaturated overflow guard.
    DeterministicVirtualTimer edge(24000000ULL);
    TEST_ASSERT(edge.rearm_periodic(99, 100ULL).status == TimerRearmStatus::InvalidCore,
                "Invalid core rejected");
    TEST_ASSERT(edge.rearm_periodic(0, 0ULL).status == TimerRearmStatus::InvalidPeriod,
                "Zero period rejected");
    edge.set_control(0, DeterministicVirtualTimer::kCtlEnable);
    edge.set_comparator(0, (std::numeric_limits<std::uint64_t>::max)() - 10ULL);
    edge.advance_counter((std::numeric_limits<std::uint64_t>::max)() - 5ULL);
    const TimerRearmResult st_ovf = edge.rearm_periodic(0, 100ULL);
    TEST_ASSERT(st_ovf.status == TimerRearmStatus::DeadlineSaturated,
                "Near-max uint64 deadline must saturate cleanly");
    TEST_ASSERT(edge.get_comparator(0) == (std::numeric_limits<std::uint64_t>::max)(),
                "Saturated deadline must clamp to UINT64_MAX");

    return 0;
}

int test_ssbs_sysreg_and_cpu_semantics() {
    // 1. SysReg table validation
    const auto* desc_by_name = find_descriptor_by_name("SSBS");
    const auto* desc_by_key  = find_descriptor_by_key(make_sysreg_key(3, 3, 4, 2, 6));
    const auto* desc_by_id   = find_descriptor_by_id(SysRegId::SSBS);
    TEST_ASSERT(desc_by_name != nullptr && desc_by_key != nullptr && desc_by_id != nullptr,
                "SSBS sysreg descriptor must exist");
    TEST_ASSERT(desc_by_name == desc_by_key && desc_by_name == desc_by_id,
                "All SSBS lookups must point to same descriptor");
    TEST_ASSERT(desc_by_name->key == 0x5A16u, "SSBS sysreg key must be 0x5A16");
    TEST_ASSERT(desc_by_name->min_el == 0u && desc_by_name->is_readable && desc_by_name->is_writable,
                "SSBS must be EL0+ RW");
    TEST_ASSERT(desc_by_name->res0_mask == 0xFFFFFFFFFFFFEFFFULL, "SSBS RES0 mask must preserve bit 12 only");

    // 2. Live trace disassembly + execution at EL1 & EL0
    Memory mem(0x4000);
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    TEST_ASSERT(!cpu.get_pstate_ssbs(), "PSTATE.SSBS must reset to 0");

    std::vector<std::string> traces;
    cpu.set_trace_sink([&](const CPU::TraceRecord& rec) {
        traces.push_back(rec.disassembly);
    });

    mem.write_u32(0x1000, kInsnMrsX0Ssbs);  // x0 <- 0
    mem.write_u32(0x1004, kInsnMsrSsbsX2);  // write ~0 -> only bit 12 preserved
    mem.write_u32(0x1008, kInsnMrsX0Ssbs);  // x0 <- 0x1000
    mem.write_u32(0x100C, kInsnMsrSsbs0);   // PSTATE.SSBS <- 0
    mem.write_u32(0x1010, kInsnMrsX0Ssbs);  // x0 <- 0
    mem.write_u32(0x1014, kInsnMsrSsbs1);   // PSTATE.SSBS <- 1
    mem.write_u32(0x1018, kInsnMrsX0Ssbs);  // x0 <- 0x1000

    cpu.set_program_counter(0x1000);
    cpu.write_register(2, 0xFFFFFFFFFFFFFFFFULL);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0ULL, "Initial MRS SSBS must read 0");
    cpu.step(mem);
    TEST_ASSERT(cpu.get_pstate_ssbs(), "MSR SSBS, X2 must set bit 12");
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0x1000ULL, "MRS SSBS must read bit 12 only (RES0 masked)");
    cpu.step(mem);
    TEST_ASSERT(!cpu.get_pstate_ssbs(), "MSR SSBS, #0 must clear SSBS");
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0ULL, "MRS SSBS after #0 must read 0");
    cpu.step(mem);
    TEST_ASSERT(cpu.get_pstate_ssbs(), "MSR SSBS, #1 must set SSBS");
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0x1000ULL, "MRS SSBS after #1 must read 0x1000");

    cpu.clear_trace_sink();
    TEST_ASSERT(traces.size() == 7u, "Expected 7 SSBS trace records");
    TEST_ASSERT(traces[0] == "MRS X0, SSBS", "MRS X0, SSBS trace mismatch: " + traces[0]);
    TEST_ASSERT(traces[1] == "MSR SSBS, X2", "MSR SSBS, X2 trace mismatch: " + traces[1]);
    TEST_ASSERT(traces[3] == "MSR SSBS, #0", "MSR SSBS, #0 trace mismatch: " + traces[3]);
    TEST_ASSERT(traces[5] == "MSR SSBS, #1", "MSR SSBS, #1 trace mismatch: " + traces[5]);

    // Verify EL0 also permits MSR SSBS, #imm and MRS/MSR Xt, SSBS
    cpu.set_current_el(0);
    cpu.write_register(2, 0ULL);
    cpu.set_program_counter(0x1004);
    cpu.step(mem); // MSR SSBS, X2 (=0) at EL0
    TEST_ASSERT(!cpu.get_pstate_ssbs(), "EL0 MSR SSBS, Xt must clear SSBS");
    cpu.set_program_counter(0x1014);
    cpu.step(mem); // MSR SSBS, #1 at EL0
    TEST_ASSERT(cpu.get_pstate_ssbs(), "EL0 MSR SSBS, #1 must set SSBS");

    // Reserved MSR SSBS, #2..#15 must trap cleanly when architectural_exceptions is true
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x2000);
    mem.write_u32(0x1020, encode_msr_ssbs_imm(2));
    cpu.set_program_counter(0x1020);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_current_el() == 1 && cpu.get_elr_el1() == 0x1020ULL,
                "Reserved MSR SSBS, #2 must raise synchronous undefined exception");
    TEST_ASSERT((cpu.get_spsr_el1() & (1u << 12u)) != 0u,
                "SPSR_EL1[12] must preserve PSTATE.SSBS=1 on exception entry");
    TEST_ASSERT(!cpu.get_pstate_ssbs(),
                "With SCTLR_EL1.DSSBS=0, exception entry to EL1 must clear PSTATE.SSBS");

    // Verify SCTLR_EL1.DSSBS (bit 44) = 1 sets PSTATE.SSBS on EL0->EL1 SVC entry and ERET restores old bit
    cpu.set_sctlr_el1(1ULL << 44u);
    cpu.set_pstate_ssbs(true);
    mem.write_u32(0x1030, kInsnEret);
    mem.write_u32(0x1040, kInsnMrsX0Ssbs);
    mem.write_u32(0x1044, kInsnMsrSsbs0);
    mem.write_u32(0x1048, kInsnSvc0);
    mem.write_u32(0x2400, kInsnMrsX0Ssbs); // EL1 lower-A64 Sync vector (VBAR + 0x400)
    mem.write_u32(0x2404, kInsnEret);

    cpu.set_elr_el1(0x1044);
    cpu.set_spsr_el1(0u); // EL0t, SSBS=0
    cpu.set_program_counter(0x1030);
    cpu.step(mem); // ERET -> EL0 at 0x1044 with SSBS=0
    TEST_ASSERT(cpu.get_current_el() == 0 && !cpu.get_pstate_ssbs(), "ERET must restore SSBS=0");
    cpu.step(mem); // 0x1044 MSR SSBS, #0 -> EL0 SSBS stays 0
    cpu.step(mem); // 0x1048 SVC #0 -> takes EL1 sync exception (DSSBS=1 -> SSBS=1 in EL1!)
    TEST_ASSERT(cpu.get_current_el() == 1, "SVC must enter EL1");
    TEST_ASSERT(cpu.get_pstate_ssbs(), "SCTLR_EL1.DSSBS=1 must set PSTATE.SSBS=1 upon EL1 entry");
    TEST_ASSERT((cpu.get_spsr_el1() & (1u << 12u)) == 0u, "SPSR_EL1[12] must capture old EL0 SSBS=0");
    TEST_ASSERT(cpu.get_program_counter() == 0x2400ULL, "SVC must branch to VBAR+0x400");
    cpu.step(mem); // 0x2400 MRS X0, SSBS inside EL1 handler
    TEST_ASSERT(cpu.read_register(0) == 0x1000ULL, "EL1 handler must observe DSSBS=1");
    cpu.step(mem); // 0x2404 ERET -> restores EL0 with SSBS=0
    TEST_ASSERT(cpu.get_current_el() == 0 && !cpu.get_pstate_ssbs(),
                "ERET after handler must restore EL0 SPSR_EL1[12]=0");

    // 4. Snapshot Serializer (1128B compatibility) + SmpCoreState + ReplayStateHash
    CPU s0, s1;
    s0.reset();
    s0.set_pstate_ssbs(true);
    const auto payload = serialize_cpu_state(s0);
    TEST_ASSERT(payload.size() == 1128u, "serialize_cpu_state payload must remain exactly 1128 bytes");
    TEST_ASSERT(deserialize_cpu_state(payload.data(), payload.size(), s1).success,
                "deserialize_cpu_state must succeed");
    TEST_ASSERT(s1.get_pstate_ssbs(), "Snapshot roundtrip must preserve PSTATE.SSBS");

    std::uint64_t r0 = 0, v0 = 0, sr0 = 0, r1 = 0, v1 = 0, sr1 = 0;
    ReplayStateHash::hash_cpu(s1, r0, v0, sr0);
    s1.set_pstate_ssbs(false);
    ReplayStateHash::hash_cpu(s1, r1, v1, sr1);
    TEST_ASSERT(r0 != r1, "ReplayStateHash regs+PSTATE digest must reflect PSTATE.SSBS changes");

    winios::SmpCoreState smp_a, smp_b;
    s1.set_pstate_ssbs(true);
    smp_a.capture_from_cpu(s1);
    TEST_ASSERT(smp_a.pstate_ssbs, "SmpCoreState::capture_from_cpu must capture pstate_ssbs");
    s1.set_pstate_ssbs(false);
    smp_b.capture_from_cpu(s1);
    TEST_ASSERT(smp_a != smp_b, "SmpCoreState equality must distinguish pstate_ssbs");
    TEST_ASSERT(smp_a.compute_canonical_hash() != smp_b.compute_canonical_hash(),
                "SmpCoreState canonical hash must distinguish pstate_ssbs");
    smp_a.apply_to_cpu(s1);
    TEST_ASSERT(s1.get_pstate_ssbs(), "SmpCoreState::apply_to_cpu must restore pstate_ssbs");

    return 0;
}

} // namespace

int main() {
    if (test_timer_regression_and_contracts() != 0) {
        return 1;
    }
    if (test_ssbs_sysreg_and_cpu_semantics() != 0) {
        return 1;
    }
    std::cout << "[PASS] stage281_timer_and_ssbs_tests\n";
    return 0;
}
