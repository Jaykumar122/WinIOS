#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <thread>
#include <atomic>

#include "kernel/process_identity.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Process Identity Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::kernel;

static void test_generation_exhaustion_retirement() {
    std::cout << "[Stage 231.9] Testing generation exhaustion and permanent slot retirement...\n";

    ProcessIdentityManager manager(100, 105, 0); // Small pool
    ApplicationIdentity app{"com.sample.exhaust"};

    uint32_t target_pid = 100;
    // Set slot generation to near exhaustion (kMaxGeneration - 1)
    manager.set_slot_generation(target_pid, ProcessIdentityManager::kMaxGeneration - 1);

    // Fixed PID allocation will attempt to increment generation to kMaxGeneration
    ProcessHandle h = manager.allocate_fixed_pid(target_pid, app);
    // Generation reached kMaxGeneration -> slot must be retired immediately!
    TEST_ASSERT(!h.is_valid());
    TEST_ASSERT(manager.is_retired_pid(target_pid));

    // Trying again on the same PID must fail; never wrap back to 1
    ProcessHandle h_retry = manager.allocate_fixed_pid(target_pid, app);
    TEST_ASSERT(!h_retry.is_valid());
    TEST_ASSERT(manager.is_retired_pid(target_pid));
}

static void test_token_entropy_and_uniqueness() {
    std::cout << "[Stage 231.9] Testing CSPRNG process security token uniqueness and validation...\n";

    ProcessIdentityManager manager(100, 1000, 0);
    ApplicationIdentity app{"com.sample.tokenapp"};

    std::set<std::string> token_set;
    std::vector<ProcessHandle> handles;

    for (int i = 0; i < 200; ++i) {
        ProcessHandle h = manager.allocate_process(app);
        TEST_ASSERT(h.is_valid());
        handles.push_back(h);

        std::string token = manager.get_process_token(h);
        TEST_ASSERT(token.size() == 32); // 128-bit hex
        TEST_ASSERT(token_set.insert(token).second); // Must be strictly unique

        // Verify token validation
        TEST_ASSERT(manager.validate_handle_and_token(h, token));

        // Tampered token must fail
        std::string bad_token = token;
        bad_token[0] ^= 0x01;
        TEST_ASSERT(!manager.validate_handle_and_token(h, bad_token));
    }
}

static void test_stale_handle_rejection() {
    std::cout << "[Stage 231.9] Testing stale handle rejection after termination...\n";

    ProcessIdentityManager manager(100, 100, 0); // 1-PID pool
    ApplicationIdentity app{"com.sample.stale"};

    // 1. Allocate Gen 1
    ProcessHandle h1 = manager.allocate_process(app);
    TEST_ASSERT(h1.is_valid() && h1.pid == 100 && h1.generation == 1);
    TEST_ASSERT(manager.is_valid_handle(h1));

    // 2. Terminate Gen 1
    TEST_ASSERT(manager.terminate_process(h1));
    TEST_ASSERT(!manager.is_valid_handle(h1));

    // 3. Allocate again -> gets PID 100 with Gen 2
    ProcessHandle h2 = manager.allocate_process(app);
    TEST_ASSERT(h2.is_valid() && h2.pid == 100 && h2.generation == 2);
    TEST_ASSERT(manager.is_valid_handle(h2));

    // Stale handle h1 must be rejected
    TEST_ASSERT(!manager.is_valid_handle(h1));

    // Attempting to terminate using stale handle h1 must fail
    TEST_ASSERT(!manager.terminate_process(h1));
    TEST_ASSERT(manager.is_valid_handle(h2)); // h2 still alive
}

static void test_concurrent_allocation_stress() {
    std::cout << "[Stage 231.9] Testing concurrent process allocation and release...\n";

    ProcessIdentityManager manager(100, 2000, 4);
    ApplicationIdentity app{"com.sample.concurrent"};

    std::atomic<bool> start_flag{false};
    std::atomic<int> success_count{0};

    auto worker = [&]() {
        while (!start_flag.load()) {
            std::this_thread::yield();
        }
        for (int i = 0; i < 50; ++i) {
            ProcessHandle h = manager.allocate_process(app);
            if (h.is_valid()) {
                std::string token = manager.get_process_token(h);
                if (manager.validate_handle_and_token(h, token)) {
                    success_count++;
                }
                manager.terminate_process(h);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back(worker);
    }

    start_flag.store(true);
    for (auto& t : threads) {
        t.join();
    }

    TEST_ASSERT(success_count.load() > 0);
    std::cout << "  -> Successfully processed " << success_count.load() << " concurrent operations.\n";
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 231.9: Process Identity Hardening Tests     \n";
    std::cout << "====================================================\n";

    test_generation_exhaustion_retirement();
    test_token_entropy_and_uniqueness();
    test_stale_handle_rejection();
    test_concurrent_allocation_stress();

    std::cout << "\n[Stage 231.9] ALL TESTS PASSED SUCCESSFULLY.\n";
    return 0;
}
