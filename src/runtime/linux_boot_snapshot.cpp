#include "runtime/linux_boot_snapshot.h"

#include <cstring>
#include <sstream>

namespace winios::runtime {

namespace {

inline void write_u32(std::vector<std::uint8_t>& buf, std::uint32_t val) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&val);
    buf.insert(buf.end(), p, p + 4);
}

inline void write_u64(std::vector<std::uint8_t>& buf, std::uint64_t val) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&val);
    buf.insert(buf.end(), p, p + 8);
}

inline std::uint32_t read_u32(const std::uint8_t*& p) {
    std::uint32_t val;
    std::memcpy(&val, p, 4);
    p += 4;
    return val;
}

inline std::uint64_t read_u64(const std::uint8_t*& p) {
    std::uint64_t val;
    std::memcpy(&val, p, 8);
    p += 8;
    return val;
}

} // anonymous namespace

LinuxBootPlatformSnapshot LinuxBootSnapshotManager::capture(const LinuxBootSession& session) {
    LinuxBootPlatformSnapshot snap{};
    snap.magic = LinuxBootPlatformSnapshot::kMagic;
    snap.version = LinuxBootPlatformSnapshot::kVersion;
    snap.total_instructions = session.total_instructions_executed();
    snap.status = session.status();

    const auto& plat = session.platform();
    snap.physical_counter = plat.virtual_time().get_physical_counter();
    snap.platform_hash = plat.compute_platform_hash();

    // 1. Capture CPU states
    snap.cpus.resize(plat.core_count());
    for (std::size_t c = 0; c < plat.core_count(); ++c) {
        const auto& cpu = plat.get_cpu(c);
        auto& cpu_snap = snap.cpus[c];

        cpu_snap.core_id = static_cast<std::uint16_t>(c);
        cpu_snap.active = plat.is_core_active(c);
        cpu_snap.halted = cpu.is_halted();
        cpu_snap.pc = cpu.get_program_counter();
        cpu_snap.sp = cpu.get_stack_pointer();
        cpu_snap.nzcv = cpu.get_nzcv();
        cpu_snap.current_el = static_cast<std::uint8_t>(cpu.get_current_el());
        cpu_snap.daif = static_cast<std::uint8_t>(cpu.get_daif());

        cpu_snap.sctlr_el1 = cpu.get_sctlr_el1();
        cpu_snap.cpacr_el1 = cpu.get_cpacr_el1();
        cpu_snap.vbar_el1  = cpu.get_vbar_el1();
        cpu_snap.elr_el1   = cpu.get_elr_el1();
        cpu_snap.spsr_el1  = cpu.get_spsr_el1();
        cpu_snap.esr_el1   = cpu.get_esr_el1();
        cpu_snap.tpidr_el0 = cpu.get_tpidr_el0();

        for (std::size_t r = 0; r < CPU::kRegisterCount; ++r) {
            cpu_snap.gpr[r] = cpu.read_register(r);
        }
    }

    // 2. Capture Guest RAM
    const auto& mem = plat.memory();
    snap.ram_bytes.resize(mem.size());
    if (mem.size() > 0) {
        std::memcpy(snap.ram_bytes.data(), mem.raw_data(), mem.size());
    }

    // 3. Capture Peripherals
    if (plat.uart()) {
        snap.uart_state = plat.uart()->serialize_state();
    }
    if (plat.dma()) {
        snap.dma_state = plat.dma()->serialize_state();
    }

    // 4. Capture GICv3
    snap.gic_distributor = plat.gic().get_distributor_state();
    snap.gic_redistributors.resize(plat.core_count());
    for (std::size_t c = 0; c < plat.core_count(); ++c) {
        snap.gic_redistributors[c] = plat.gic().get_redistributor(c);
    }

    // 5. Capture Milestone report
    snap.milestone_report = session.milestone_tracker().dump_report();

    return snap;
}

bool LinuxBootSnapshotManager::restore(LinuxBootSession& target_session,
                                      const LinuxBootPlatformSnapshot& snapshot,
                                      std::string* out_error) {
    if (snapshot.magic != LinuxBootPlatformSnapshot::kMagic) {
        if (out_error) *out_error = "Invalid snapshot magic";
        return false;
    }

    auto& plat = target_session.platform();

    // 1. Verify capacity and restore RAM
    if (plat.memory().size() != snapshot.ram_bytes.size()) {
        if (out_error) {
            *out_error = "RAM size mismatch between target session (" +
                         std::to_string(plat.memory().size()) + ") and snapshot (" +
                         std::to_string(snapshot.ram_bytes.size()) + ")";
        }
        return false;
    }
    std::memcpy(plat.memory().raw_data(), snapshot.ram_bytes.data(), snapshot.ram_bytes.size());

    // 2. Restore Virtual Time
    plat.virtual_time().set_physical_counter(snapshot.physical_counter);

    // 3. Restore CPUs
    if (plat.core_count() != snapshot.cpus.size()) {
        if (out_error) *out_error = "Core count mismatch between target session and snapshot";
        return false;
    }

    for (std::size_t c = 0; c < snapshot.cpus.size(); ++c) {
        const auto& cpu_snap = snapshot.cpus[c];
        auto& cpu = plat.get_cpu(c);

        plat.set_core_active(c, cpu_snap.active);
        cpu.set_halted(cpu_snap.halted);
        cpu.set_program_counter(cpu_snap.pc);
        cpu.set_stack_pointer(cpu_snap.sp);
        cpu.set_nzcv(cpu_snap.nzcv);
        cpu.set_current_el(cpu_snap.current_el);
        cpu.set_daif(cpu_snap.daif);

        cpu.set_sctlr_el1(cpu_snap.sctlr_el1);
        cpu.set_cpacr_el1(cpu_snap.cpacr_el1);
        cpu.set_vbar_el1(cpu_snap.vbar_el1);
        cpu.set_elr_el1(cpu_snap.elr_el1);
        cpu.set_spsr_el1(cpu_snap.spsr_el1);
        cpu.set_esr_el1(cpu_snap.esr_el1);
        cpu.set_tpidr_el0(cpu_snap.tpidr_el0);

        for (std::size_t r = 0; r < CPU::kRegisterCount; ++r) {
            cpu.write_register(r, cpu_snap.gpr[r]);
        }
    }

    // 4. Restore Peripherals
    if (plat.uart() && !snapshot.uart_state.empty()) {
        (void)plat.uart()->deserialize_state(snapshot.uart_state.data(), snapshot.uart_state.size());
    }
    if (plat.dma() && !snapshot.dma_state.empty()) {
        (void)plat.dma()->deserialize_state(snapshot.dma_state.data(), snapshot.dma_state.size());
    }

    // 5. Restore GICv3
    plat.gic().set_distributor_state(snapshot.gic_distributor);
    for (std::size_t c = 0; c < snapshot.gic_redistributors.size(); ++c) {
        plat.gic().get_redistributor(c) = snapshot.gic_redistributors[c];
    }

    // 6. Restore Session Metadata
    target_session.set_status(snapshot.status);
    target_session.set_total_instructions(snapshot.total_instructions);

    return true;
}

std::vector<std::uint8_t> LinuxBootSnapshotManager::serialize(const LinuxBootPlatformSnapshot& snapshot) {
    std::vector<std::uint8_t> buf;
    buf.reserve(snapshot.ram_bytes.size() + 4096);

    write_u32(buf, snapshot.magic);
    write_u32(buf, snapshot.version);
    write_u64(buf, snapshot.total_instructions);
    write_u64(buf, snapshot.physical_counter);
    write_u64(buf, snapshot.platform_hash);
    write_u32(buf, static_cast<std::uint32_t>(snapshot.status));

    // CPUs
    write_u32(buf, static_cast<std::uint32_t>(snapshot.cpus.size()));
    for (const auto& cpu : snapshot.cpus) {
        write_u32(buf, cpu.core_id);
        buf.push_back(cpu.active ? 1 : 0);
        buf.push_back(cpu.halted ? 1 : 0);
        buf.push_back(cpu.nzcv);
        buf.push_back(cpu.current_el);
        buf.push_back(cpu.daif);

        write_u64(buf, cpu.pc);
        write_u64(buf, cpu.sp);
        write_u64(buf, cpu.sctlr_el1);
        write_u64(buf, cpu.cpacr_el1);
        write_u64(buf, cpu.vbar_el1);
        write_u64(buf, cpu.elr_el1);
        write_u32(buf, cpu.spsr_el1);
        write_u32(buf, cpu.esr_el1);
        write_u64(buf, cpu.tpidr_el0);

        for (std::size_t r = 0; r < 31; ++r) {
            write_u64(buf, cpu.gpr[r]);
        }
    }

    // RAM
    write_u64(buf, static_cast<std::uint64_t>(snapshot.ram_bytes.size()));
    if (!snapshot.ram_bytes.empty()) {
        const std::size_t cur = buf.size();
        buf.resize(cur + snapshot.ram_bytes.size());
        std::memcpy(buf.data() + cur, snapshot.ram_bytes.data(), snapshot.ram_bytes.size());
    }

    // UART
    write_u64(buf, static_cast<std::uint64_t>(snapshot.uart_state.size()));
    if (!snapshot.uart_state.empty()) {
        const std::size_t ucur = buf.size();
        buf.resize(ucur + snapshot.uart_state.size());
        std::memcpy(buf.data() + ucur, snapshot.uart_state.data(), snapshot.uart_state.size());
    }

    return buf;
}

LinuxBootPlatformSnapshot LinuxBootSnapshotManager::deserialize(const void* data,
                                                              std::size_t size,
                                                              std::string* out_error) {
    LinuxBootPlatformSnapshot snap{};
    if (!data || size < 36) {
        if (out_error) *out_error = "Snapshot payload too small";
        return snap;
    }

    const auto* p = static_cast<const std::uint8_t*>(data);
    const auto* end = p + size;

    snap.magic = read_u32(p);
    if (snap.magic != LinuxBootPlatformSnapshot::kMagic) {
        if (out_error) *out_error = "Invalid snapshot magic";
        return snap;
    }

    snap.version = read_u32(p);
    snap.total_instructions = read_u64(p);
    snap.physical_counter = read_u64(p);
    snap.platform_hash = read_u64(p);
    snap.status = static_cast<LinuxSessionStatus>(read_u32(p));

    // CPUs
    std::uint32_t num_cpus = read_u32(p);
    snap.cpus.resize(num_cpus);
    for (std::uint32_t c = 0; c < num_cpus; ++c) {
        auto& cpu = snap.cpus[c];
        cpu.core_id = static_cast<std::uint16_t>(read_u32(p));
        cpu.active = (*p++ != 0);
        cpu.halted = (*p++ != 0);
        cpu.nzcv = *p++;
        cpu.current_el = *p++;
        cpu.daif = *p++;

        cpu.pc = read_u64(p);
        cpu.sp = read_u64(p);
        cpu.sctlr_el1 = read_u64(p);
        cpu.cpacr_el1 = read_u64(p);
        cpu.vbar_el1 = read_u64(p);
        cpu.elr_el1 = read_u64(p);
        cpu.spsr_el1 = read_u32(p);
        cpu.esr_el1 = read_u32(p);
        cpu.tpidr_el0 = read_u64(p);

        for (std::size_t r = 0; r < 31; ++r) {
            cpu.gpr[r] = read_u64(p);
        }
    }

    // RAM
    std::uint64_t ram_sz = read_u64(p);
    if (p + ram_sz > end) {
        if (out_error) *out_error = "Truncated RAM payload";
        return snap;
    }
    snap.ram_bytes.resize(static_cast<std::size_t>(ram_sz));
    if (ram_sz > 0) {
        std::memcpy(snap.ram_bytes.data(), p, static_cast<std::size_t>(ram_sz));
    }
    p += ram_sz;

    // UART
    if (p + 8 <= end) {
        std::uint64_t uart_sz = read_u64(p);
        if (p + uart_sz <= end) {
            snap.uart_state.resize(static_cast<std::size_t>(uart_sz));
            if (uart_sz > 0) {
                std::memcpy(snap.uart_state.data(), p, static_cast<std::size_t>(uart_sz));
            }
            p += uart_sz;
        }
    }

    return snap;
}

bool LinuxBootSnapshotManager::verify_deterministic_replay(LinuxBootSession& reference_session,
                                                          LinuxBootSession& restored_session,
                                                          std::size_t steps,
                                                          std::string* out_divergence) {
    for (std::size_t i = 0; i < steps; ++i) {
        reference_session.step(1);
        restored_session.step(1);

        const auto& ref_plat = reference_session.platform();
        const auto& res_plat = restored_session.platform();

        if (ref_plat.compute_platform_hash() != res_plat.compute_platform_hash()) {
            if (out_divergence) {
                *out_divergence = "Platform state hash divergence at step " + std::to_string(i) +
                                  " (ref PC=0x" + std::to_string(ref_plat.get_cpu(0).get_program_counter()) +
                                  ", res PC=0x" + std::to_string(res_plat.get_cpu(0).get_program_counter()) + ")";
            }
            return false;
        }
    }

    return true;
}

} // namespace winios::runtime
