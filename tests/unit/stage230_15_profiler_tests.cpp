#include "platform/app_performance_profiler.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace winios::platform;

static void test_process_profiler_lifecycle() {
    std::cout << "[Test 1] AppPerformanceProfiler lifecycle & metric tracking...\n";
    AppPerformanceProfiler profiler;
    profiler.reset();

    profiler.register_process_launch(101, "com.winios.appstartup", 1250);
    profiler.record_process_instructions(101, 5000);
    profiler.record_process_syscall(101, 592, 10);
    profiler.record_process_syscall(101, 593, 20);
    profiler.record_process_memory_delta(101, 65536);
    profiler.record_process_memory_delta(101, 32768);
    profiler.record_process_window(101);

    auto m_opt = profiler.get_process_metrics(101);
    assert(m_opt.has_value());
    assert(m_opt->pid == 101);
    assert(m_opt->bundle_id == "com.winios.appstartup");
    assert(m_opt->launch_latency_us == 1250);
    assert(m_opt->instructions_retired == 5000);
    assert(m_opt->syscalls_dispatched == 2);
    assert(m_opt->total_syscall_ticks == 30);
    assert(m_opt->average_syscall_latency_ticks() == 15.0);
    assert(m_opt->current_memory_bytes == 98304);
    assert(m_opt->peak_memory_bytes == 98304);
    assert(m_opt->windows_created == 1);
    assert(m_opt->is_active);

    // Free some memory
    profiler.record_process_memory_delta(101, -32768);
    auto m2 = profiler.get_process_metrics(101);
    assert(m2->current_memory_bytes == 65536);
    assert(m2->peak_memory_bytes == 98304); // Peak retained

    profiler.mark_process_terminated(101);
    auto m3 = profiler.get_process_metrics(101);
    assert(!m3->is_active);

    std::string report = profiler.generate_developer_report(101);
    assert(report.find("com.winios.appstartup") != std::string::npos);
    assert(report.find("TERMINATED") != std::string::npos);
    assert(report.find("1250 us") != std::string::npos);
    assert(report.find("98304 bytes") != std::string::npos);

    std::cout << "  -> Developer report:\n" << report << "\n";
    std::cout << "  -> Process profiler lifecycle passed.\n";
}

static void test_multi_app_profiling_summary() {
    std::cout << "[Test 2] Multi-app system profiling summary...\n";
    AppPerformanceProfiler profiler;
    profiler.reset();

    profiler.register_process_launch(101, "com.winios.appstartup", 1100);
    profiler.record_process_instructions(101, 10000);
    profiler.record_process_syscall(101, 592, 5);

    profiler.register_process_launch(102, "com.winios.appsettings", 1400);
    profiler.record_process_instructions(102, 25000);
    profiler.record_process_syscall(102, 581, 15);
    profiler.record_process_syscall(102, 582, 12);

    profiler.register_process_launch(103, "com.winios.appdocs", 1800);
    profiler.record_process_instructions(103, 30000);
    profiler.record_process_syscall(103, 574, 25);

    auto all = profiler.get_all_metrics();
    assert(all.size() == 3);

    std::string summary = profiler.generate_system_summary();
    assert(summary.find("Active Apps Profiled: 3") != std::string::npos);
    assert(summary.find("com.winios.appstartup") != std::string::npos);
    assert(summary.find("com.winios.appsettings") != std::string::npos);
    assert(summary.find("com.winios.appdocs") != std::string::npos);

    std::cout << "  -> Multi-app summary:\n" << summary << "\n";
    std::cout << "  -> Multi-app profiling summary passed.\n";
}

int main() {
    std::cout << "=== Stage 230.15: Application Profiling & Performance Report Tests ===\n";
    test_process_profiler_lifecycle();
    test_multi_app_profiling_summary();
    std::cout << "=== All Stage 230.15 Tests PASSED ===\n";
    return 0;
}
