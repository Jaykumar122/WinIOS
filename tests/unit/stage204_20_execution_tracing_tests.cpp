#include "runtime/execution_diagnostics.h"
#include <iostream>
#include <string>
#include <vector>
#include <cassert>

using namespace winios::runtime;

static int g_failed_tests = 0;

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            ++g_failed_tests; \
        } \
    } while (0)

void test_lifecycle_and_image_attribution() {
    ExecutionDiagnostics diag;
    diag.reset();
    diag.set_enabled(true);

    diag.record_process_start(1001, "/usr/bin/sample_app");
    TEST_CHECK(diag.lifecycle().pid == 1001);
    TEST_CHECK(diag.lifecycle().executable_path == "/usr/bin/sample_app");
    TEST_CHECK(!diag.lifecycle().exited);

    diag.register_image_region("/usr/lib/libSystem.B.dylib", 0x180000000ULL, 0x00100000ULL);
    diag.register_image_region("/usr/bin/sample_app",        0x100000000ULL, 0x00040000ULL);

    TEST_CHECK(diag.resolve_image_name(0x100001000ULL) == "/usr/bin/sample_app");
    TEST_CHECK(diag.resolve_image_name(0x180050000ULL) == "/usr/lib/libSystem.B.dylib");
    TEST_CHECK(diag.resolve_image_name(0x200000000ULL) == "unknown");

    diag.record_process_exit(42);
    TEST_CHECK(diag.lifecycle().exited);
    TEST_CHECK(diag.lifecycle().exit_code == 42);
}

void test_instruction_tracing_and_queries() {
    ExecutionDiagnostics diag;
    diag.reset();
    diag.set_enabled(true);

    diag.register_image_region("/usr/bin/sample_app", 0x100000000ULL, 0x00040000ULL);

    std::vector<RegisterDiff> diffs1 = {
        {0, 0, 42}
    };
    diag.log_instruction_step(0x100000100ULL, 0x100000104ULL, 0xD2800540u, 0, diffs1, "mov x0, #42", 0x16FD00000ULL, 0, 100, 1001, 1);

    std::vector<RegisterDiff> diffs2 = {
        {16, 0, 1}
    };
    diag.log_instruction_step(0x100000104ULL, 0x100000108ULL, 0xD2800030u, 0, diffs2, "mov x16, #1", 0x16FD00000ULL, 0, 110, 1001, 1);

    TEST_CHECK(diag.step_count() == 2);
    const auto* s0 = diag.get_step(0);
    TEST_CHECK(s0 != nullptr);
    TEST_CHECK(s0->pc_before == 0x100000100ULL);
    TEST_CHECK(s0->image_name == "/usr/bin/sample_app");
    TEST_CHECK(s0->register_diffs.size() == 1);
    TEST_CHECK(s0->register_diffs[0].reg_index == 0);
    TEST_CHECK(s0->register_diffs[0].new_value == 42);

    auto steps_pc = diag.find_steps_by_pc(0x100000104ULL);
    TEST_CHECK(steps_pc.size() == 1);
    TEST_CHECK(steps_pc[0].instruction_raw == 0xD2800030u);

    auto steps_x0 = diag.find_steps_by_reg_write(0);
    TEST_CHECK(steps_x0.size() == 1);
    TEST_CHECK(steps_x0[0].step_index == 0);

    auto steps_x16 = diag.find_steps_by_reg_write(16);
    TEST_CHECK(steps_x16.size() == 1);
    TEST_CHECK(steps_x16[0].step_index == 1);
}

void test_syscall_and_memory_tracing() {
    ExecutionDiagnostics diag;
    diag.reset();
    diag.set_enabled(true);

    diag.log_memory_op(MemoryOpType::Write, 0x16FD00010ULL, 8, 0x12345678ULL, 1001, 1);
    diag.log_memory_op(MemoryOpType::Read,  0x16FD00010ULL, 8, 0x12345678ULL, 1001, 1);

    TEST_CHECK(diag.memory_op_count() == 2);
    const auto* mop = diag.get_memory_op(0);
    TEST_CHECK(mop != nullptr);
    TEST_CHECK(mop->type == MemoryOpType::Write);
    TEST_CHECK(mop->address == 0x16FD00010ULL);

    std::array<std::uint64_t, 6> args = {1, 0x100002000ULL, 14, 0, 0, 0};
    diag.log_syscall(TrapKind::BsdSyscall, 4, "SYS_write", args, 14, false, 0, 1001, 1);

    std::array<std::uint64_t, 6> exit_args = {0, 0, 0, 0, 0, 0};
    diag.log_syscall(TrapKind::BsdSyscall, 1, "SYS_exit", exit_args, 0, false, 0, 1001, 1);

    TEST_CHECK(diag.syscall_count() == 2);
    auto write_sc = diag.find_syscalls_by_number(4);
    TEST_CHECK(write_sc.size() == 1);
    TEST_CHECK(write_sc[0].name == "SYS_write");
    TEST_CHECK(write_sc[0].return_value == 14);
}

void test_memory_faults_and_mach_ipc() {
    ExecutionDiagnostics diag;
    diag.reset();
    diag.set_enabled(true);

    diag.register_image_region("/usr/bin/sample_app", 0x100000000ULL, 0x00040000ULL);

    diag.log_memory_fault(1001, 1, 0x100000050ULL, 0x0F, true, false, "Permission fault on read-only segment");
    TEST_CHECK(diag.memory_fault_count() == 1);
    const auto* mf = diag.get_memory_fault(0);
    TEST_CHECK(mf != nullptr);
    TEST_CHECK(mf->fault_va == 0x100000050ULL);
    TEST_CHECK(mf->image_name == "/usr/bin/sample_app");
    TEST_CHECK(mf->is_write);

    diag.log_mach_ipc_op(1001, 1, 0x103, 1000, 64, "mach_msg_send", true, "SUCCESS");
    TEST_CHECK(diag.mach_ipc_count() == 1);
    const auto* ipc = diag.get_mach_ipc_op(0);
    TEST_CHECK(ipc != nullptr);
    TEST_CHECK(ipc->port_name == 0x103);
    TEST_CHECK(ipc->is_send);
}

void test_thread_state_transitions() {
    ExecutionDiagnostics diag;
    diag.reset();
    diag.set_enabled(true);

    diag.log_thread_transition(1001, 1, "NEW", "RUNNING", "Thread started execution");
    diag.log_thread_transition(1001, 1, "RUNNING", "BLOCKED", "Waiting on pthread_cond_t");
    diag.log_thread_transition(1001, 1, "BLOCKED", "RUNNING", "Condition signaled");
    diag.log_thread_transition(1001, 1, "RUNNING", "TERMINATED", "Thread exited normally");

    TEST_CHECK(diag.thread_transition_count() == 4);
    const auto* t0 = diag.get_thread_transition(0);
    TEST_CHECK(t0 != nullptr);
    TEST_CHECK(t0->from_state == "NEW");
    TEST_CHECK(t0->to_state == "RUNNING");
}

void test_bounded_capacity_ring_eviction() {
    ExecutionDiagnostics diag;
    diag.reset();
    diag.set_enabled(true);
    diag.set_max_capacity(5);

    for (std::uint64_t i = 0; i < 10; ++i) {
        std::vector<RegisterDiff> diffs;
        diag.log_instruction_step(0x1000 + i * 4, 0x1004 + i * 4, 0xD503201F, 0, diffs, "nop", 0x2000, 0, 100 + i, 1001, 1);
    }

    TEST_CHECK(diag.step_count() == 5);
    const auto* first = diag.get_step(0);
    TEST_CHECK(first != nullptr);
    // Oldest 5 (0..4) evicted, remaining should be 5..9
    TEST_CHECK(first->step_index == 5);
    const auto* last = diag.get_step(4);
    TEST_CHECK(last != nullptr);
    TEST_CHECK(last->step_index == 9);
}

void test_zero_overhead_when_disabled() {
    ExecutionDiagnostics diag;
    diag.reset();
    diag.set_enabled(false);
    TEST_CHECK(!diag.is_enabled());

    std::vector<RegisterDiff> diffs;
    diag.log_instruction_step(0x1000, 0x1004, 0xD503201F, 0, diffs, "nop", 0x2000, 0, 100, 1001, 1);
    diag.log_memory_op(MemoryOpType::Read, 0x1000, 4, 0, 1001, 1);
    std::array<std::uint64_t, 6> args{};
    diag.log_syscall(TrapKind::BsdSyscall, 1, "SYS_exit", args, 0, false, 0, 1001, 1);
    diag.log_memory_fault(1001, 1, 0x1000, 0, false, false, "fault");
    diag.log_mach_ipc_op(1001, 1, 1, 1, 1, "op", true, "ok");
    diag.log_thread_transition(1001, 1, "A", "B", "r");

    TEST_CHECK(diag.step_count() == 0);
    TEST_CHECK(diag.memory_op_count() == 0);
    TEST_CHECK(diag.syscall_count() == 0);
    TEST_CHECK(diag.memory_fault_count() == 0);
    TEST_CHECK(diag.mach_ipc_count() == 0);
    TEST_CHECK(diag.thread_transition_count() == 0);
}

void test_crash_diagnostic_report_generation() {
    ExecutionDiagnostics diag;
    diag.reset();
    diag.set_enabled(true);

    diag.register_image_region("/usr/bin/faulty_app", 0x100000000ULL, 0x00020000ULL);

    std::vector<RegisterDiff> diffs;
    diag.log_instruction_step(0x100000080ULL, 0x100000084ULL, 0xF9400000u, 0, diffs, "ldr x0, [x0]", 0x16FD00000ULL, 0, 1000, 2042, 1);
    diag.log_memory_fault(2042, 1, 0x0ULL, 0x07, false, false, "Translation fault (null pointer dereference)");

    std::string report = diag.format_crash_diagnostic_report(2042, 1, 0x0ULL, "Null pointer dereference in guest instruction");

    TEST_CHECK(!report.empty());
    TEST_CHECK(report.find("WINIOS CRASH DIAGNOSTIC REPORT") != std::string::npos);
    TEST_CHECK(report.find("Process ID:    2042") != std::string::npos);
    TEST_CHECK(report.find("Thread ID:     1") != std::string::npos);
    TEST_CHECK(report.find("Fault Address: 0x0") != std::string::npos);
    TEST_CHECK(report.find("Null pointer dereference") != std::string::npos);
    TEST_CHECK(report.find("ldr x0, [x0]") != std::string::npos);
}

void test_json_and_csv_export() {
    ExecutionDiagnostics diag;
    diag.reset();
    diag.set_enabled(true);

    diag.record_process_start(500, "/bin/echo");
    std::vector<RegisterDiff> diffs;
    diag.log_instruction_step(0x1000, 0x1004, 0xD503201F, 0, diffs, "nop", 0x2000, 0, 10, 500, 1);
    std::array<std::uint64_t, 6> args{1, 0x1000, 5, 0, 0, 0};
    diag.log_syscall(TrapKind::BsdSyscall, 4, "SYS_write", args, 5, false, 0, 500, 1);

    std::string json = diag.export_trace_json();
    TEST_CHECK(json.find("\"pid\": 500") != std::string::npos);
    TEST_CHECK(json.find("\"executable\": \"/bin/echo\"") != std::string::npos);

    std::string csv = diag.export_trace_csv();
    TEST_CHECK(csv.find("step,pc_before,pc_after,instruction_raw,current_el,sp,nzcv,disassembly,register_diffs") != std::string::npos);

    std::string sc_csv = diag.export_syscalls_csv();
    TEST_CHECK(sc_csv.find("kind,number,name,arg0,arg1,arg2,arg3,arg4,arg5,return_value,is_error,errno_code") != std::string::npos);
    TEST_CHECK(sc_csv.find("SYS_write") != std::string::npos);
}

int main() {
    std::cout << "=== Running Stage 204.20 Execution Diagnostics Tests ===" << std::endl;

    test_lifecycle_and_image_attribution();
    test_instruction_tracing_and_queries();
    test_syscall_and_memory_tracing();
    test_memory_faults_and_mach_ipc();
    test_thread_state_transitions();
    test_bounded_capacity_ring_eviction();
    test_zero_overhead_when_disabled();
    test_crash_diagnostic_report_generation();
    test_json_and_csv_export();

    if (g_failed_tests == 0) {
        std::cout << "ALL STAGE 204.20 EXECUTION DIAGNOSTICS TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cerr << g_failed_tests << " TEST(S) FAILED!" << std::endl;
        return 1;
    }
}
