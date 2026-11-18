#include "platform/platform_error.h"
#include "platform/service_lifecycle.h"
#include <iostream>
#include <memory>
#include <vector>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::platform;

class DummyService : public IManagedService {
public:
    DummyService(std::string name, std::vector<std::string> deps, bool fail_init = false)
        : tracker_(name, deps), fail_init_(fail_init) {}

    [[nodiscard]] const std::string& service_name() const noexcept override {
        return tracker_.name();
    }

    [[nodiscard]] ServiceState service_state() const noexcept override {
        return tracker_.state();
    }

    [[nodiscard]] std::vector<std::string> service_dependencies() const override {
        return tracker_.dependencies();
    }

    PlatformError service_init() override {
        PlatformError err = tracker_.begin_initialization();
        if (!err.is_ok()) return err;
        init_calls++;
        if (fail_init_) {
            PlatformError ferr = PlatformError::internal_error(PlatformDomain::Service, "Simulated init failure");
            tracker_.fail_initialization(ferr);
            return ferr;
        }
        tracker_.complete_initialization();
        return PlatformError::ok();
    }

    PlatformError service_shutdown() noexcept override {
        if (tracker_.state() == ServiceState::Stopped) {
            return PlatformError::ok();
        }
        PlatformError err = tracker_.begin_shutdown();
        if (!err.is_ok()) return err;
        shutdown_calls++;
        tracker_.complete_shutdown();
        return PlatformError::ok();
    }

    ServiceLifecycleTracker tracker_;
    bool fail_init_{false};
    int init_calls{0};
    int shutdown_calls{0};
};

static void test_platform_error_model() {
    PlatformError ok = PlatformError::ok();
    TEST_ASSERT(ok.is_ok());
    TEST_ASSERT(!ok);
    TEST_ASSERT(ok.to_errno() == 0);
    TEST_ASSERT(ok.to_guest_status() == 0);

    PlatformError inv = PlatformError::invalid_argument(PlatformDomain::Settings, "Width exceeds 3840", 101);
    TEST_ASSERT(!inv.is_ok());
    TEST_ASSERT(inv);
    TEST_ASSERT(inv.category() == PlatformErrorCategory::InvalidArgument);
    TEST_ASSERT(inv.domain() == PlatformDomain::Settings);
    TEST_ASSERT(inv.code() == 101);
    TEST_ASSERT(inv.to_errno() == 22); // EINVAL
    TEST_ASSERT(inv.to_guest_status() == -1); // WINIOS_ERR_INVALID_PARAM

    // Test sanitization of sensitive host path
    PlatformError sec = PlatformError::io_error(PlatformDomain::Vfs, "Failed opening C:\\secret\\file.txt", 500);
    std::string str = sec.to_string();
    TEST_ASSERT(str.find("C:\\") == std::string::npos);
    TEST_ASSERT(str.find("[host_path]") != std::string::npos);

    // Test PlatformResult<T>
    PlatformResult<int> res_ok(42);
    TEST_ASSERT(res_ok.is_ok());
    TEST_ASSERT(res_ok.value() == 42);

    PlatformResult<int> res_err(PlatformError::permission_denied(PlatformDomain::Security, "Access denied"));
    TEST_ASSERT(!res_err.is_ok());
    TEST_ASSERT(res_err.error().to_errno() == 1); // EPERM
    TEST_ASSERT(res_err.error().to_guest_status() == -4); // WINIOS_ERR_ACCESS_DENIED

    std::cout << "[PASS] test_platform_error_model\n";
}

static void test_service_lifecycle_transitions() {
    DummyService svc("TestService", {});
    TEST_ASSERT(svc.service_state() == ServiceState::Uninitialized);

    PlatformError err = svc.service_init();
    TEST_ASSERT(err.is_ok());
    TEST_ASSERT(svc.service_state() == ServiceState::Running);
    TEST_ASSERT(svc.init_calls == 1);

    // Idempotent init
    err = svc.service_init();
    TEST_ASSERT(err.is_ok());

    // Degraded state
    svc.tracker_.mark_degraded(PlatformError::service_unavailable(PlatformDomain::Service, "Partial network loss"));
    TEST_ASSERT(svc.service_state() == ServiceState::Degraded);

    // Shutdown
    err = svc.service_shutdown();
    TEST_ASSERT(err.is_ok());
    TEST_ASSERT(svc.service_state() == ServiceState::Stopped);
    TEST_ASSERT(svc.shutdown_calls == 1);

    // Idempotent shutdown
    err = svc.service_shutdown();
    TEST_ASSERT(err.is_ok());
    TEST_ASSERT(svc.shutdown_calls == 1); // Not called again

    std::cout << "[PASS] test_service_lifecycle_transitions\n";
}

static void test_service_coordinator_dependency_ordering_and_rollback() {
    ServiceCoordinator coord;

    // Service A depends on B, B depends on C
    auto svcC = std::make_shared<DummyService>("ServiceC", std::vector<std::string>{});
    auto svcB = std::make_shared<DummyService>("ServiceB", std::vector<std::string>{"ServiceC"});
    auto svcA = std::make_shared<DummyService>("ServiceA", std::vector<std::string>{"ServiceB"});

    // Register in mixed order
    TEST_ASSERT(coord.register_service(svcA).is_ok());
    TEST_ASSERT(coord.register_service(svcC).is_ok());
    TEST_ASSERT(coord.register_service(svcB).is_ok());

    // Start all -> C, then B, then A
    PlatformError err = coord.start_all();
    TEST_ASSERT(err.is_ok());
    TEST_ASSERT(svcC->service_state() == ServiceState::Running);
    TEST_ASSERT(svcB->service_state() == ServiceState::Running);
    TEST_ASSERT(svcA->service_state() == ServiceState::Running);

    // Stop all -> reverse order: A, then B, then C
    err = coord.stop_all();
    TEST_ASSERT(err.is_ok());
    TEST_ASSERT(svcA->service_state() == ServiceState::Stopped);
    TEST_ASSERT(svcB->service_state() == ServiceState::Stopped);
    TEST_ASSERT(svcC->service_state() == ServiceState::Stopped);

    // Test partial-initialization rollback
    ServiceCoordinator coord2;
    auto s1 = std::make_shared<DummyService>("S1", std::vector<std::string>{});
    auto s2 = std::make_shared<DummyService>("S2", std::vector<std::string>{"S1"}, /*fail_init=*/true);
    coord2.register_service(s1);
    coord2.register_service(s2);

    err = coord2.start_all();
    TEST_ASSERT(!err.is_ok()); // Failed on S2
    // S1 must have been rolled back to Stopped!
    TEST_ASSERT(s1->service_state() == ServiceState::Stopped);
    TEST_ASSERT(s2->service_state() == ServiceState::Failed);

    std::cout << "[PASS] test_service_coordinator_dependency_ordering_and_rollback\n";
}

int main() {
    std::cout << "=== Running Stage 229 Platform Error & Lifecycle Tests ===\n";
    test_platform_error_model();
    test_service_lifecycle_transitions();
    test_service_coordinator_dependency_ordering_and_rollback();
    std::cout << "All Stage 229 Platform Error & Lifecycle Tests Passed!\n";
    return 0;
}
