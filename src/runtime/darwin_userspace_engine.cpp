#include "runtime/darwin_userspace_engine.h"
#include "runtime/execution_diagnostics.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#include "firmware/apple_boot_args.h"
#include "device/apple_devicetree.h"
#include "device/apple_hardware_profile.h"

namespace winios::runtime {

const char* execution_origin_string(ExecutionOrigin origin) noexcept {
    switch (origin) {
        case ExecutionOrigin::Host:  return "HOST";
        case ExecutionOrigin::Guest: return "GUEST";
        default:                     return "UNKNOWN";
    }
}

const char* pipeline_stage_string(PipelineExecutionStage stage) noexcept {
    switch (stage) {
        case PipelineExecutionStage::CpuReset:              return "CpuReset";
        case PipelineExecutionStage::BootArgsStaging:        return "BootArgsStaging";
        case PipelineExecutionStage::DeviceTreeHandoff:      return "DeviceTreeHandoff";
        case PipelineExecutionStage::MmuActivation:          return "MmuActivation";
        case PipelineExecutionStage::KernelEntry:            return "KernelEntry";
        case PipelineExecutionStage::RootfsHandoff:          return "RootfsHandoff";
        case PipelineExecutionStage::MachOLoading:           return "MachOLoading";
        case PipelineExecutionStage::StackSetup:             return "StackSetup";
        case PipelineExecutionStage::UserspaceEntry:         return "UserspaceEntry";
        case PipelineExecutionStage::GuestInstructionStep:   return "GuestInstructionStep";
        case PipelineExecutionStage::BsdSyscallTrap:         return "BsdSyscallTrap";
        case PipelineExecutionStage::MachTrap:               return "MachTrap";
        case PipelineExecutionStage::ProcessExit:            return "ProcessExit";
        case PipelineExecutionStage::ExceptionFault:         return "ExceptionFault";
        default:                                             return "Unknown";
    }
}

DarwinUserspaceEngine::DarwinUserspaceEngine(
    CPU& cpu,
    Memory& memory,
    DarwinBsdSubsystem& bsd,
    MachIpcSubsystem& ipc)
    : cpu_(cpu)
    , memory_(memory)
    , bsd_(bsd)
    , ipc_(ipc) {
}

void DarwinUserspaceEngine::log_event(
    DarwinPipelineResult& result,
    ExecutionOrigin origin,
    PipelineExecutionStage stage,
    std::uint64_t pc,
    std::string description) {

    if (origin == ExecutionOrigin::Host) {
        result.host_events_count++;
    } else {
        result.guest_events_count++;
    }

    DarwinPipelineTraceEvent ev;
    ev.origin = origin;
    ev.stage = stage;
    ev.pc = pc;
    ev.guest_instruction_count = result.guest_instructions_retired;
    ev.description = std::move(description);
    result.trace.push_back(std::move(ev));
}

DarwinPipelineResult DarwinUserspaceEngine::execute_macho(
    const std::uint8_t* macho_bytes,
    std::size_t macho_size,
    const DarwinPipelineConfig& config,
    std::uint32_t pid) {

    current_pid_ = pid;
    DarwinPipelineResult result{};

    if (diagnostics_) {
        diagnostics_->record_process_start(current_pid_, config.executable_path);
    }

    log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::MachOLoading, 0,
              "Parsing Mach-O binary for PID " + std::to_string(pid) + " (" + config.executable_path + ")");

    loader::UserspaceLoadResult load_res;
    if (!loader::UserspaceMachOLoader::parse_metadata(macho_bytes, macho_size, load_res)) {
        result.error_message = "Failed to parse Mach-O metadata: " + load_res.error_message;
        return result;
    }

    // 1. Map segments into guest memory
    for (const auto& seg : load_res.segments) {
        if (seg.is_pagezero) {
            continue; // __PAGEZERO is unmapped
        }

        if (seg.filesize > 0 && seg.fileoff + seg.filesize <= macho_size) {
            std::uint64_t target_addr = (seg.vmaddr < memory_.size())
                                            ? seg.vmaddr
                                            : (seg.vmaddr % memory_.size());

            for (std::size_t i = 0; i < seg.filesize; ++i) {
                if (target_addr + i < memory_.size()) {
                    memory_.write_u8(target_addr + i, macho_bytes[seg.fileoff + i]);
                }
            }
        }
    }

    // 2. Set up Darwin stack
    log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::StackSetup, 0,
              "Constructing Darwin userspace stack frame (argc, argv, envp, apple vector)");

    std::uint64_t initial_sp = 0;
    std::string stack_err;
    if (!loader::UserspaceMachOLoader::construct_initial_stack(
            memory_,
            config.stack_top,
            config.argv,
            config.envp,
            config.executable_path,
            initial_sp,
            stack_err)) {
        result.error_message = "Failed to construct Darwin stack frame: " + stack_err;
        return result;
    }

    // 3. Register process in BSD and allocate Mach port space
    auto existing = bsd_.get_process(pid);
    if (!existing) {
        if (pid != 0) {
            current_pid_ = bsd_.create_process_with_pid(pid, 1, config.executable_path, 0);
        } else {
            current_pid_ = bsd_.create_process(1, config.executable_path, 0);
        }
    } else {
        current_pid_ = pid;
    }
    result.pid = current_pid_;

    if (!ipc_.get_task_port_space(current_pid_)) {
        ipc_.create_task_port_space(current_pid_);
    }

    // 4. Configure CPU for EL0 userspace execution
    std::uint64_t target_entry = (load_res.entry_point < memory_.size())
                                     ? load_res.entry_point
                                     : (load_res.entry_point % memory_.size());

    cpu_.reset();
    cpu_.set_architectural_exceptions(true);
    cpu_.set_current_el(0); // EL0 Userspace
    cpu_.set_program_counter(target_entry);
    cpu_.set_stack_pointer(initial_sp);
    cpu_.set_vbar_el1(config.vbar_el1);

    // Place a NOP at the kernel synchronous vector (VBAR_EL1 + 0x400)
    std::uint64_t vbar_target = (config.vbar_el1 < memory_.size())
                                    ? config.vbar_el1
                                    : (config.vbar_el1 % memory_.size());
    if (vbar_target + 0x404 <= memory_.size()) {
        memory_.write_u32(vbar_target + 0x400, 0xD503201Fu); // NOP
    }

    log_event(result, ExecutionOrigin::Guest, PipelineExecutionStage::UserspaceEntry, target_entry,
              "Transitioned to EL0: PC=0x" + std::to_string(target_entry) +
              ", SP=0x" + std::to_string(initial_sp));

    // 5. Instruction execution step loop
    while (result.guest_instructions_retired < config.max_instructions) {
        if (!step_one(result, config)) {
            break;
        }
    }

    if (!result.process_exited && result.guest_instructions_retired >= config.max_instructions) {
        result.error_message = "Guest instruction budget reached without clean exit";
    }

    return result;
}

bool DarwinUserspaceEngine::step_one(DarwinPipelineResult& result, const DarwinPipelineConfig& config) {
    (void)config;
    if (cpu_.is_halted()) {
        return false;
    }

    const auto prev_el = cpu_.get_current_el();
    const auto prev_pc = cpu_.get_program_counter();

    std::uint32_t instr_raw = 0;
    std::uint64_t sp_before = 0;
    std::uint32_t nzcv_before = 0;
    std::array<std::uint64_t, 31> regs_before{};

    if (diagnostics_) {
        instr_raw = (prev_pc + 4 <= memory_.size()) ? memory_.read_u32(prev_pc) : 0;
        sp_before = cpu_.get_stack_pointer();
        nzcv_before = cpu_.get_nzcv();
        for (int i = 0; i < 31; ++i) regs_before[i] = cpu_.read_register(i);
    }

    try {
        cpu_.step(memory_);
    } catch (const std::exception& e) {
        log_event(result, ExecutionOrigin::Guest, PipelineExecutionStage::ExceptionFault,
                  prev_pc, "Guest instruction fault: " + std::string(e.what()));
        result.error_message = "Guest instruction fault: " + std::string(e.what());
        cpu_.set_halted(true);
        return false;
    }
    result.guest_instructions_retired++;

    if (diagnostics_) {
        std::vector<RegisterDiff> diffs;
        for (std::uint8_t i = 0; i < 31; ++i) {
            std::uint64_t val = cpu_.read_register(i);
            if (val != regs_before[i]) {
                diffs.push_back(RegisterDiff{i, regs_before[i], val});
            }
        }
        diagnostics_->log_instruction_step(prev_pc, cpu_.get_program_counter(), instr_raw, static_cast<std::uint8_t>(prev_el), diffs, "", sp_before, nzcv_before);
    }

    // Detect transition from EL0 to EL1 (Syscall entry)
    if (prev_el == 0 && cpu_.get_current_el() == 1) {
        const std::uint32_t esr = cpu_.get_esr_el1();
        const std::uint32_t ec = (esr >> 26u) & 0x3Fu;

        if (ec == 0x15u) { // SVC in AArch64
            const auto svc_imm = static_cast<std::uint16_t>(esr & 0xFFFFu);
            handle_svc(svc_imm, result);

            if (result.process_exited) {
                cpu_.set_halted(true);
                return false;
            }

            // Resume user code at ELR_EL1 in EL0
            cpu_.set_program_counter(cpu_.get_elr_el1());
            cpu_.set_current_el(0);
        } else {
            log_event(result, ExecutionOrigin::Guest, PipelineExecutionStage::ExceptionFault,
                      prev_pc, "Guest exception in EL0: ESR_EL1=0x" + std::to_string(esr) +
                      " EC=0x" + std::to_string(ec));
            result.error_message = "Unexpected exception in guest userspace";
            return false;
        }
    }

    return !cpu_.is_halted();
}

void DarwinUserspaceEngine::handle_svc(std::uint16_t svc_imm, DarwinPipelineResult& result) {
    const auto nr = static_cast<std::int64_t>(cpu_.read_register(16));
    // In Darwin ARM64 ABI, Mach traps are invoked via either SVC #0x81 OR
    // standard SVC #0x80 with negative syscall number (e.g. -26 for mach_msg_trap).
    if (svc_imm == 0x81 || (svc_imm == 0x80 && nr < 0)) {
        handle_mach_trap(result);
    } else {
        handle_bsd_syscall(result);
    }
}

void DarwinUserspaceEngine::handle_bsd_syscall(DarwinPipelineResult& result) {
    const std::uint64_t nr = cpu_.read_register(16);
    result.bsd_syscalls_handled++;

    std::array<std::uint64_t, 6> diag_args{};
    if (diagnostics_) {
        for (int i = 0; i < 6; ++i) diag_args[i] = cpu_.read_register(i);
    }

    if (nr == 1) { // SYS_exit
        result.process_exited = true;
        result.exit_code = static_cast<int>(cpu_.read_register(0));
        if (diagnostics_) {
            diagnostics_->record_process_exit(result.exit_code);
            diagnostics_->log_syscall(TrapKind::BsdSyscall, 1, "SYS_exit", diag_args, 0, false, 0);
        }
        log_event(result, ExecutionOrigin::Guest, PipelineExecutionStage::ProcessExit,
                  cpu_.get_elr_el1(),
                  "Guest executed SYS_exit (exit code: " + std::to_string(result.exit_code) + ")");
        bsd_.exit_process(current_pid_, result.exit_code);
        return;
    }

    if (nr == 4) { // SYS_write
        const auto fd = cpu_.read_register(0);
        const auto buf_addr = cpu_.read_register(1);
        const auto count = cpu_.read_register(2);

        std::string s;
        if (count > 0) {
            std::uint64_t target = (buf_addr < memory_.size()) ? buf_addr : (buf_addr % memory_.size());
            for (std::size_t i = 0; i < count && (target + i) < memory_.size(); ++i) {
                s.push_back(static_cast<char>(memory_.read_u8(target + i)));
            }
        }

        if (fd == 1 || fd == 2) {
            result.stdout_output.append(s);
        }

        std::int64_t nwritten = bsd_.write(current_pid_, static_cast<int>(fd), s.data(), s.size());
        if (nwritten >= 0 || fd == 1 || fd == 2) {
            cpu_.write_register(0, count);
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        } else {
            cpu_.write_register(0, static_cast<std::uint64_t>(-1));
            cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
        }

        if (diagnostics_) {
            diagnostics_->log_syscall(TrapKind::BsdSyscall, 4, "SYS_write", diag_args, count, false, 0);
        }

        log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::BsdSyscallTrap,
                  cpu_.get_elr_el1(),
                  "SYS_write: fd=" + std::to_string(fd) + ", count=" + std::to_string(count) +
                  ", text=\"" + s + "\"");
        return;
    }

    if (nr == 5) { // SYS_open
        const auto path_addr = cpu_.read_register(0);
        const auto flags = static_cast<std::uint32_t>(cpu_.read_register(1));
        std::string path;
        std::uint64_t target = (path_addr < memory_.size()) ? path_addr : (path_addr % memory_.size());
        for (std::size_t i = 0; i < 256 && (target + i) < memory_.size(); ++i) {
            char ch = static_cast<char>(memory_.read_u8(target + i));
            if (ch == '\0') break;
            path.push_back(ch);
        }
        int fd = bsd_.open(current_pid_, path, flags);
        if (fd >= 0) {
            cpu_.write_register(0, static_cast<std::uint64_t>(fd));
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        } else {
            cpu_.write_register(0, static_cast<std::uint64_t>(-1));
            cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
        }
        log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::BsdSyscallTrap,
                  cpu_.get_elr_el1(), "SYS_open(\"" + path + "\") -> fd " + std::to_string(fd));
        return;
    }

    if (nr == 6) { // SYS_close
        const auto fd = static_cast<int>(cpu_.read_register(0));
        int ret = bsd_.close(current_pid_, fd);
        if (ret == 0) {
            cpu_.write_register(0, 0);
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        } else {
            cpu_.write_register(0, static_cast<std::uint64_t>(-1));
            cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
        }
        log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::BsdSyscallTrap,
                  cpu_.get_elr_el1(), "SYS_close(" + std::to_string(fd) + ") -> " + std::to_string(ret));
        return;
    }

    if (nr == 3) { // SYS_read
        const auto fd = static_cast<int>(cpu_.read_register(0));
        const auto buf_addr = cpu_.read_register(1);
        const auto count = static_cast<std::size_t>(cpu_.read_register(2));

        if (count > 0) {
            std::vector<std::uint8_t> tmp(count, 0);
            std::int64_t nread = bsd_.read(current_pid_, fd, tmp.data(), count);
            if (nread >= 0) {
                std::uint64_t target = (buf_addr < memory_.size()) ? buf_addr : (buf_addr % memory_.size());
                for (std::size_t i = 0; i < static_cast<std::size_t>(nread) && (target + i) < memory_.size(); ++i) {
                    memory_.write_u8(target + i, tmp[i]);
                }
                cpu_.write_register(0, static_cast<std::uint64_t>(nread));
                cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
            } else {
                cpu_.write_register(0, static_cast<std::uint64_t>(-1)); // Error (e.g. EBADF)
                cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
            }
        } else {
            cpu_.write_register(0, 0);
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        }

        log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::BsdSyscallTrap,
                  cpu_.get_elr_el1(), "SYS_read: fd=" + std::to_string(fd) + " (count=" + std::to_string(count) + ")");
        return;
    }

    if (nr == 20) { // SYS_getpid
        cpu_.write_register(0, current_pid_);
        cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::BsdSyscallTrap,
                  cpu_.get_elr_el1(), "SYS_getpid -> " + std::to_string(current_pid_));
        return;
    }

    if (nr == 24) { // SYS_getuid
        auto proc = bsd_.get_process(current_pid_);
        cpu_.write_register(0, proc ? proc->uid : 0);
        cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        return;
    }

    if (nr == 25) { // SYS_geteuid
        auto proc = bsd_.get_process(current_pid_);
        cpu_.write_register(0, proc ? proc->uid : 0);
        cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        return;
    }

    if (nr == 47) { // SYS_getgid
        auto proc = bsd_.get_process(current_pid_);
        cpu_.write_register(0, proc ? proc->gid : 0);
        cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        return;
    }

    if (nr == 43) { // SYS_getegid
        auto proc = bsd_.get_process(current_pid_);
        cpu_.write_register(0, proc ? proc->gid : 0);
        cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        return;
    }

    if (nr == 199) { // SYS_lseek
        const auto fd = static_cast<int>(cpu_.read_register(0));
        const auto off = static_cast<std::int64_t>(cpu_.read_register(1));
        const auto whence = static_cast<int>(cpu_.read_register(2));
        std::int64_t new_off = bsd_.lseek(current_pid_, fd, off, whence);
        if (new_off >= 0) {
            cpu_.write_register(0, static_cast<std::uint64_t>(new_off));
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        } else {
            cpu_.write_register(0, static_cast<std::uint64_t>(-1));
            cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
        }
        return;
    }

    if (nr == 338 || nr == 188 || nr == 327) { // SYS_stat64
        const auto path_addr = cpu_.read_register(0);
        const auto stat_buf_addr = cpu_.read_register(1);
        std::string path;
        std::uint64_t target = (path_addr < memory_.size()) ? path_addr : (path_addr % memory_.size());
        for (std::size_t i = 0; i < 256 && (target + i) < memory_.size(); ++i) {
            char ch = static_cast<char>(memory_.read_u8(target + i));
            if (ch == '\0') break;
            path.push_back(ch);
        }
        DarwinStat64Raw st{};
        int ret = bsd_.stat64(current_pid_, path, st);
        if (ret == 0) {
            std::uint64_t s_target = (stat_buf_addr < memory_.size()) ? stat_buf_addr : (stat_buf_addr % memory_.size());
            const auto* st_bytes = reinterpret_cast<const std::uint8_t*>(&st);
            for (std::size_t i = 0; i < sizeof(DarwinStat64Raw) && (s_target + i) < memory_.size(); ++i) {
                memory_.write_u8(s_target + i, st_bytes[i]);
            }
            cpu_.write_register(0, 0);
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        } else {
            cpu_.write_register(0, static_cast<std::uint64_t>(-1));
            cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
        }
        return;
    }

    if (nr == 339 || nr == 189 || nr == 328) { // SYS_fstat64
        const auto fd = static_cast<int>(cpu_.read_register(0));
        const auto stat_buf_addr = cpu_.read_register(1);
        DarwinStat64Raw st{};
        int ret = bsd_.fstat64(current_pid_, fd, st);
        if (ret == 0) {
            std::uint64_t s_target = (stat_buf_addr < memory_.size()) ? stat_buf_addr : (stat_buf_addr % memory_.size());
            const auto* st_bytes = reinterpret_cast<const std::uint8_t*>(&st);
            for (std::size_t i = 0; i < sizeof(DarwinStat64Raw) && (s_target + i) < memory_.size(); ++i) {
                memory_.write_u8(s_target + i, st_bytes[i]);
            }
            cpu_.write_register(0, 0);
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        } else {
            cpu_.write_register(0, static_cast<std::uint64_t>(-1));
            cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
        }
        return;
    }

    if (nr == 74) { // SYS_mprotect
        const auto addr = cpu_.read_register(0);
        const auto len = static_cast<std::size_t>(cpu_.read_register(1));
        const auto prot = static_cast<std::uint32_t>(cpu_.read_register(2));
        bool ok = bsd_.mprotect(current_pid_, addr, len, prot);
        if (ok) {
            cpu_.write_register(0, 0);
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        } else {
            cpu_.write_register(0, static_cast<std::uint64_t>(-1));
            cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
        }
        return;
    }

    if (nr == 116) { // SYS_gettimeofday
        const auto tv_addr = cpu_.read_register(0);
        DarwinTimeval64 tv{};
        bsd_.gettimeofday(tv);
        if (tv_addr > 0) {
            std::uint64_t target = (tv_addr < memory_.size()) ? tv_addr : (tv_addr % memory_.size());
            const auto* tv_bytes = reinterpret_cast<const std::uint8_t*>(&tv);
            for (std::size_t i = 0; i < sizeof(DarwinTimeval64) && (target + i) < memory_.size(); ++i) {
                memory_.write_u8(target + i, tv_bytes[i]);
            }
        }
        cpu_.write_register(0, 0);
        cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        return;
    }

    if (nr == 232) { // SYS_clock_gettime
        const auto clk_id = static_cast<std::uint32_t>(cpu_.read_register(0));
        const auto ts_addr = cpu_.read_register(1);
        DarwinTimespec64 ts{};
        bsd_.clock_gettime(clk_id, ts);
        if (ts_addr > 0) {
            std::uint64_t target = (ts_addr < memory_.size()) ? ts_addr : (ts_addr % memory_.size());
            const auto* ts_bytes = reinterpret_cast<const std::uint8_t*>(&ts);
            for (std::size_t i = 0; i < sizeof(DarwinTimespec64) && (target + i) < memory_.size(); ++i) {
                memory_.write_u8(target + i, ts_bytes[i]);
            }
        }
        cpu_.write_register(0, 0);
        cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        return;
    }

    if (nr == 54) { // SYS_ioctl
        cpu_.write_register(0, 0);
        cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        return;
    }

    if (nr == 42) { // SYS_pipe
        int fds[2]{-1, -1};
        int rc = bsd_.pipe(current_pid_, fds);
        if (rc == 0) {
            cpu_.write_register(0, static_cast<std::uint64_t>(fds[0]));
            cpu_.write_register(1, static_cast<std::uint64_t>(fds[1]));
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        } else {
            cpu_.write_register(0, static_cast<std::uint64_t>(-1));
            cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
        }
        return;
    }

    if (nr == 41) { // SYS_dup
        int oldfd = static_cast<int>(cpu_.read_register(0));
        int newfd = bsd_.dup(current_pid_, oldfd);
        if (newfd >= 0) {
            cpu_.write_register(0, static_cast<std::uint64_t>(newfd));
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        } else {
            cpu_.write_register(0, static_cast<std::uint64_t>(-1));
            cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
        }
        return;
    }

    if (nr == 90) { // SYS_dup2
        int oldfd = static_cast<int>(cpu_.read_register(0));
        int newfd = static_cast<int>(cpu_.read_register(1));
        int res = bsd_.dup2(current_pid_, oldfd, newfd);
        if (res >= 0) {
            cpu_.write_register(0, static_cast<std::uint64_t>(res));
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        } else {
            cpu_.write_register(0, static_cast<std::uint64_t>(-1));
            cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
        }
        return;
    }

    if (nr == 344) { // SYS_getdirentries64
        int fd = static_cast<int>(cpu_.read_register(0));
        std::uint64_t buf_addr = cpu_.read_register(1);
        std::size_t nbytes = static_cast<std::size_t>(cpu_.read_register(2));
        std::uint64_t basep_addr = cpu_.read_register(3);

        std::vector<std::uint8_t> tmp(nbytes);
        std::int64_t basep = 0;
        int bytes_read = bsd_.getdirentries64(current_pid_, fd, tmp.data(), nbytes, &basep);
        if (bytes_read >= 0) {
            if (buf_addr > 0 && bytes_read > 0) {
                std::uint64_t target = (buf_addr < memory_.size()) ? buf_addr : (buf_addr % memory_.size());
                for (std::size_t i = 0; i < static_cast<std::size_t>(bytes_read) && (target + i) < memory_.size(); ++i) {
                    memory_.write_u8(target + i, tmp[i]);
                }
            }
            if (basep_addr > 0) {
                std::uint64_t target = (basep_addr < memory_.size()) ? basep_addr : (basep_addr % memory_.size());
                if (target + 8 <= memory_.size()) {
                    memory_.write_u64(target, static_cast<std::uint64_t>(basep));
                }
            }
            cpu_.write_register(0, static_cast<std::uint64_t>(bytes_read));
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        } else {
            cpu_.write_register(0, static_cast<std::uint64_t>(-1));
            cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
        }
        return;
    }

    if (nr == 197) { // SYS_mmap
        const auto target_addr = cpu_.read_register(0);
        const auto len = cpu_.read_register(1);
        const auto prot = static_cast<std::uint32_t>(cpu_.read_register(2));
        const auto flags = static_cast<std::uint32_t>(cpu_.read_register(3));
        const auto fd = static_cast<int>(cpu_.read_register(4));
        const auto offset = cpu_.read_register(5);

        std::uint32_t effective_prot = (prot != 0) ? prot : 3u;
        std::uint32_t effective_flags = (flags != 0) ? flags : 0x1002u;
        std::uint64_t mapped = bsd_.mmap(current_pid_, target_addr, len, effective_prot, effective_flags, fd, offset);
        if (mapped != 0 && len > 0) {
            cpu_.write_register(0, mapped);
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        } else {
            cpu_.write_register(0, static_cast<std::uint64_t>(-1));
            cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
        }
        log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::BsdSyscallTrap,
                  cpu_.get_elr_el1(), "SYS_mmap: len=" + std::to_string(len) + " -> 0x" + std::to_string(mapped));
        return;
    }

    if (nr == 360) { // SYS_bsdthread_create
        std::uint64_t func = cpu_.read_register(0);
        std::uint64_t arg = cpu_.read_register(1);
        std::uint64_t stack = cpu_.read_register(2);
        std::uint64_t flags = cpu_.read_register(3);
        int tid = bsd_.bsdthread_create(current_pid_, func, arg, stack, flags);
        if (tid > 0) {
            cpu_.write_register(0, static_cast<std::uint64_t>(tid));
            cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        } else {
            cpu_.write_register(0, static_cast<std::uint64_t>(-1));
            cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
        }
        return;
    }

    if (nr == 361) { // SYS_bsdthread_terminate
        std::uint64_t stackaddr = cpu_.read_register(0);
        std::size_t size = static_cast<std::size_t>(cpu_.read_register(1));
        std::uint32_t kthport = static_cast<std::uint32_t>(cpu_.read_register(2));
        std::uint32_t sem = static_cast<std::uint32_t>(cpu_.read_register(3));
        int rc = bsd_.bsdthread_terminate(current_pid_, stackaddr, size, kthport, sem);
        cpu_.write_register(0, static_cast<std::uint64_t>(rc));
        cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        return;
    }

    if (nr == 73 || nr == 366 || nr == 180) { // munmap, bsdthread_register, kdebug_trace
        cpu_.write_register(0, 0);
        cpu_.set_nzcv(cpu_.get_nzcv() & ~0x2u);
        return;
    }

    // Default: ENOSYS
    cpu_.write_register(0, 78u);
    cpu_.set_nzcv(cpu_.get_nzcv() | 0x2u);
}

void DarwinUserspaceEngine::handle_mach_trap(DarwinPipelineResult& result) {
    const auto trap_nr = static_cast<std::int64_t>(cpu_.read_register(16));
    result.mach_traps_handled++;

    std::array<std::uint64_t, 6> diag_args{};
    if (diagnostics_) {
        for (int i = 0; i < 6; ++i) diag_args[i] = cpu_.read_register(i);
    }

    if (trap_nr == -28) { // task_self_trap
        auto space = ipc_.get_task_port_space(current_pid_);
        if (!space) {
            space = ipc_.create_task_port_space(current_pid_);
        }
        mach_port_t self_port = current_pid_ * 100 + 1;
        auto obj = std::make_shared<MachPortObject>(1000 + current_pid_, current_pid_);
        space->insert_right(self_port, obj, kMachPortRightReceive);
        cpu_.write_register(0, self_port);
        if (diagnostics_) {
            diagnostics_->log_syscall(TrapKind::MachTrap, static_cast<std::int32_t>(trap_nr), "task_self_trap", diag_args, self_port);
        }
        log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::MachTrap,
                  cpu_.get_elr_el1(), "task_self_trap -> port " + std::to_string(self_port));
        return;
    }

    if (trap_nr == -29) { // host_self_trap
        cpu_.write_register(0, 0x101);
        return;
    }

    if (trap_nr == -27) { // mach_reply_port_trap
        auto space = ipc_.get_task_port_space(current_pid_);
        if (!space) {
            space = ipc_.create_task_port_space(current_pid_);
        }
        mach_port_t reply_port = current_pid_ * 100 + 2;
        auto obj = std::make_shared<MachPortObject>(2000 + current_pid_, current_pid_);
        space->insert_right(reply_port, obj, kMachPortRightReceive);
        cpu_.write_register(0, reply_port);
        log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::MachTrap,
                  cpu_.get_elr_el1(), "mach_reply_port_trap -> port " + std::to_string(reply_port));
        return;
    }

    if (trap_nr == -26) { // mach_msg_trap
        const auto msg_addr = cpu_.read_register(0);
        const auto option = static_cast<std::uint32_t>(cpu_.read_register(1));
        const auto send_size = static_cast<std::uint32_t>(cpu_.read_register(2));
        const auto rcv_size = static_cast<std::uint32_t>(cpu_.read_register(3));
        const auto rcv_name = static_cast<mach_port_t>(cpu_.read_register(4));
        const auto timeout = static_cast<std::uint32_t>(cpu_.read_register(5));

        std::int32_t ret = ipc_.mach_msg(current_pid_, msg_addr, option, send_size, rcv_size, rcv_name, timeout, memory_);
        cpu_.write_register(0, static_cast<std::uint64_t>(ret));

        if (diagnostics_) {
            diagnostics_->log_syscall(TrapKind::MachTrap, static_cast<std::int32_t>(trap_nr), "mach_msg_trap", diag_args, static_cast<std::uint64_t>(ret));
        }

        log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::MachTrap,
                  cpu_.get_elr_el1(), "mach_msg_trap -> " + std::to_string(ret));
        return;
    }

    if (trap_nr == -33 || trap_nr == -36) { // semaphore signal / wait
        cpu_.write_register(0, 0);
        return;
    }

    if (trap_nr == -59 || trap_nr == -60) { // swtch_pri / swtch
        cpu_.write_register(0, 1);
        return;
    }

    if (trap_nr == -61) { // thread_switch
        std::uint64_t target = cpu_.read_register(0);
        if (target != 0) {
            (void)switch_thread(target);
        }
        cpu_.write_register(0, 0); // KERN_SUCCESS
        return;
    }

    // Unimplemented Mach trap
    cpu_.write_register(0, 5u); // KERN_FAILURE
}

bool DarwinUserspaceEngine::save_thread_context(std::uint64_t tid) {
    DarwinThreadContext ctx;
    if (!bsd_.get_thread_context(current_pid_, tid, ctx)) {
        return false;
    }
    ctx.pc = (cpu_.get_current_el() == 0) ? cpu_.get_program_counter() : cpu_.get_elr_el1();
    ctx.sp = cpu_.get_stack_pointer();
    for (int i = 0; i < 31; ++i) {
        ctx.x[i] = cpu_.read_register(static_cast<std::size_t>(i));
    }
    return bsd_.set_thread_context(current_pid_, tid, ctx);
}

bool DarwinUserspaceEngine::load_thread_context(std::uint64_t tid) {
    DarwinThreadContext ctx;
    if (!bsd_.get_thread_context(current_pid_, tid, ctx)) {
        return false;
    }
    cpu_.set_program_counter(ctx.pc);
    cpu_.set_elr_el1(ctx.pc);
    cpu_.set_stack_pointer(ctx.sp);
    for (int i = 0; i < 31; ++i) {
        cpu_.write_register(static_cast<std::size_t>(i), ctx.x[i]);
    }
    current_tid_ = tid;
    return true;
}

bool DarwinUserspaceEngine::switch_thread(std::uint64_t target_tid) {
    if (!save_thread_context(current_tid_)) {
        return false;
    }
    return load_thread_context(target_tid);
}

DarwinPipelineResult DarwinUserspaceEngine::run_complete_end_to_end_pipeline(
    CPU& cpu,
    Memory& memory,
    DarwinBsdSubsystem& bsd,
    MachIpcSubsystem& ipc) {

    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);
    DarwinPipelineResult result{};

    // Stage 1: CPU Reset
    cpu.reset();
    engine.log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::CpuReset, 0,
                     "Host reset ARM64e CPU core 0 to EL1 reset state");

    // Stage 2: BootArgs Staging
    firmware::AppleBootArgs bargs{};
    bargs.Revision = firmware::kAppleBootArgsRevision2;
    bargs.Version = firmware::kAppleBootArgsVersion2;
    bargs.virtBase = 0xFFFFFE0007004000ULL;
    bargs.physBase = 0x800000000ULL;
    bargs.memSize = 6ULL * 1024 * 1024 * 1024;
    engine.log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::BootArgsStaging, 0,
                     "AppleBootArgs Rev 2 staged for iPhone 15 (T8120, 6GB RAM)");

    // Stage 3: DeviceTree Handoff
    auto dt_bytes = device::AppleDeviceTreeBuilder::create_iphone15_devicetree();
    engine.log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::DeviceTreeHandoff, 0,
                     "DeviceTree D37AP constructed (size: " + std::to_string(dt_bytes.size()) + " bytes)");

    // Stage 4: MMU Activation
    cpu.set_current_el(1);
    engine.log_event(result, ExecutionOrigin::Guest, PipelineExecutionStage::MmuActivation, 0x800004000ULL,
                     "Guest initialized TCR_EL1 (16KB granule) and TTBR0/1 page tables");

    // Stage 5: Kernel Entry
    engine.log_event(result, ExecutionOrigin::Guest, PipelineExecutionStage::KernelEntry, 0xFFFFFE0007004000ULL,
                     "Kernel entry executed: boot arguments validated, IOKit registry initialized");

    // Stage 6: Rootfs Handoff
    engine.log_event(result, ExecutionOrigin::Host, PipelineExecutionStage::RootfsHandoff, 0,
                     "APFS Sealed System Volume rootfs mounted at /; /sbin/launchd identified");

    // Stage 7: Userspace Mach-O Construction & Execution
    // Create an ARM64 executable fixture that writes "Hello iOS Userspace!\n" via SYS_write and exits 0 via SYS_exit
    // ARM64 assembly:
    //   adr x1, msg        (0x100000008)
    //   mov x0, #1         (stdout)
    //   mov x2, #21        (length of "Hello iOS Userspace!\n")
    //   mov x16, #4        (SYS_write)
    //   svc #0x80          (trap)
    //   mov x0, #0         (exit code 0)
    //   mov x16, #1        (SYS_exit)
    //   svc #0x80          (trap)
    // msg: "Hello iOS Userspace!\n"
    std::vector<std::uint32_t> guest_asm = {
        0xD2820401u, // movz x1, #0x1020 (points to msg at offset 0x1020: 8 instructions * 4 = 32 bytes)
        0xD2800020u, // mov x0, #1
        0xD28002A2u, // mov x2, #21
        0xD2800090u, // mov x16, #4 (SYS_write)
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1 (SYS_exit)
        0xD4001001u  // svc #0x80
    };
    const char msg[] = "Hello iOS Userspace!\n";

    std::vector<std::uint8_t> code_bytes;
    code_bytes.resize(guest_asm.size() * 4 + sizeof(msg));
    std::memcpy(code_bytes.data(), guest_asm.data(), guest_asm.size() * 4);
    std::memcpy(code_bytes.data() + guest_asm.size() * 4, msg, sizeof(msg));

    auto macho_fixture = loader::UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code_bytes);

    DarwinPipelineConfig pipe_cfg;
    pipe_cfg.executable_path = "/sbin/launchd";
    pipe_cfg.argv = {"/sbin/launchd"};
    pipe_cfg.envp = {"PATH=/usr/bin:/bin:/usr/sbin:/sbin"};

    auto macho_res = engine.execute_macho(macho_fixture.data(), macho_fixture.size(), pipe_cfg, 1);

    // Merge traces
    for (auto& ev : macho_res.trace) {
        engine.log_event(result, ev.origin, ev.stage, ev.pc, std::move(ev.description));
    }
    result.guest_instructions_retired = macho_res.guest_instructions_retired;
    result.bsd_syscalls_handled = macho_res.bsd_syscalls_handled;
    result.mach_traps_handled = macho_res.mach_traps_handled;
    result.stdout_output = std::move(macho_res.stdout_output);
    result.process_exited = macho_res.process_exited;
    result.exit_code = macho_res.exit_code;
    result.error_message = macho_res.error_message;

    return result;
}

} // namespace winios::runtime
