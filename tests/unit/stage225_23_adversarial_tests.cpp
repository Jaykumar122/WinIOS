#include <iostream>
#include <vector>
#include <cstdint>
#include <memory>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "security/capability_manager.h"
#include "devices/device_registry.h"
#include "devices/audio_service.h"
#include "devices/sensor_service.h"
#include "devices/network_service.h"
#include "devices/notification_service.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::security;
using namespace winios::devices;

int main() {
    std::cout << "[Test Stage 225.23] Adversarial Device & Sandboxing Security Tests...\n" << std::flush;

    Memory mem(0x10000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x8000000ULL, 0x400000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_adversarial");
    uas.set_asid(15);
    uas.set_owner_pid(99);

    // Map a small 16KB valid user memory buffer at 0x10000
    uas.map_user_page(0x10000, ppm.allocate_page(), PagePermissions::UserReadWrite);

    // Initialize Devices
    auto& reg = DeviceRegistry::get();
    reg.reset();
    auto audio_svc = std::make_shared<VirtualAudioService>();
    auto sensor_svc = std::make_shared<VirtualSensorService>();
    auto net_svc = std::make_shared<VirtualNetworkService>();
    auto notif_svc = std::make_shared<VirtualNotificationService>();

    reg.register_device(audio_svc);
    reg.register_device(sensor_svc);
    reg.register_device(net_svc);
    reg.register_device(notif_svc);
    reg.initialize_all();
    reg.start_all();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    auto cm = std::make_shared<CapabilityManager>();
    cm->bind_identity(99, "com.winios.attacker");
    // Grant NO capabilities initially!
    dispatcher.set_capability_manager(cm.get());

    BsdProcess proc(99, 1, "AttackerProc");
    proc.set_address_space(&uas);
    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);
    CPU cpu;

    // 1. Permission checks: Attempting device enum without Capability::DeviceEnumerate
    cpu.write_register(16, kSysWiniosDeviceEnum);
    cpu.write_register(0, 0x10000); // buf_va
    cpu.write_register(1, 10);      // max_count
    cpu.write_register(2, 0);       // class_filter
    cpu.write_register(3, 0);       // out_count_va
    (void)dispatcher.handle_syscall(cpu, proc, uas, sched);
    // Should fail with -kErrnoEPERM (-1)
    int64_t ret = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(ret == -static_cast<int64_t>(kErrnoEPERM));

    // Check that security audit logged the denial
    auto audit = cm->get_audit_stats();
    TEST_ASSERT(audit.total_denials >= 1);

    // 2. Permission checks: Audio submit without Capability::AudioOutput
    cpu.write_register(16, kSysWiniosAudioSubmit);
    cpu.write_register(0, 0x10000);
    cpu.write_register(1, 256);
    (void)dispatcher.handle_syscall(cpu, proc, uas, sched);
    ret = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(ret == -static_cast<int64_t>(kErrnoEPERM));

    // 3. Permission checks: Sensor sample without Capability::SensorAccess
    cpu.write_register(16, kSysWiniosSensorSample);
    cpu.write_register(0, 1);
    cpu.write_register(1, 0x10000);
    (void)dispatcher.handle_syscall(cpu, proc, uas, sched);
    ret = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(ret == -static_cast<int64_t>(kErrnoEPERM));

    // 4. Grant capabilities for input validation fuzzing
    cm->grant_mask(99, Capability::DeviceEnumerate | Capability::AudioOutput | Capability::SensorAccess |
                       Capability::NetworkAccess | Capability::NotificationPost);

    // 5. Faulty pointer checks: Passing NULL buffer
    cpu.write_register(16, kSysWiniosDeviceEnum);
    cpu.write_register(0, 0); // NULL
    cpu.write_register(1, 10);
    cpu.write_register(2, 0);
    cpu.write_register(3, 0);
    (void)dispatcher.handle_syscall(cpu, proc, uas, sched);
    ret = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(ret == -static_cast<int64_t>(kErrnoEFAULT));

    // 6. Passing Unmapped Address (e.g. 0xDEADBEEF000ULL)
    cpu.write_register(16, kSysWiniosDeviceEnum);
    cpu.write_register(0, 0xDEADBEEF000ULL);
    cpu.write_register(1, 10);
    cpu.write_register(2, 0);
    cpu.write_register(3, 0);
    (void)dispatcher.handle_syscall(cpu, proc, uas, sched);
    ret = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(ret == -static_cast<int64_t>(kErrnoEFAULT));

    // 7. Oversized audio submission (> 64 KB)
    cpu.write_register(16, kSysWiniosAudioSubmit);
    cpu.write_register(0, 0x10000);
    cpu.write_register(1, 70000); // 70 KB > 64 KB limit
    (void)dispatcher.handle_syscall(cpu, proc, uas, sched);
    ret = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(ret == -static_cast<int64_t>(kErrnoEINVAL));

    // 8. Oversized network packet (> 1514 MTU)
    cpu.write_register(16, kSysWiniosNetSend);
    cpu.write_register(0, 0x10000);
    cpu.write_register(1, 2048); // 2048 > 1514 MTU
    (void)dispatcher.handle_syscall(cpu, proc, uas, sched);
    ret = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(ret == -static_cast<int64_t>(kErrnoEINVAL));

    // 9. Stale/bogus device handle to close
    cpu.write_register(16, kSysWiniosDeviceClose);
    cpu.write_register(0, 0x9999999912345678ULL); // Invalid handle
    (void)dispatcher.handle_syscall(cpu, proc, uas, sched);
    ret = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(ret == -static_cast<int64_t>(kErrnoEBADF));

    std::cout << "[Test Stage 225.23] PASS: Adversarial and sandboxing stress verified.\n";
    return 0;
}
