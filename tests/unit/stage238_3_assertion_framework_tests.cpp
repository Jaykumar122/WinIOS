#include "tests/support/test_assert.h"

#include <iostream>
#include <string>
#include <vector>

static int g_custom_handler_calls = 0;
static std::string g_last_expr;
static std::string g_last_file;
static int g_last_line = 0;
static std::string g_last_msg;

static void custom_test_handler(const char* expr_str,
                                const char* file,
                                int line,
                                std::string_view msg) {
    ++g_custom_handler_calls;
    g_last_expr = expr_str ? expr_str : "";
    g_last_file = file ? file : "";
    g_last_line = line;
    g_last_msg = std::string(msg);
}

static void test_successful_assertions() {
    std::cout << "[Stage 238.3] Test 1: Successful assertions...\n";
    TEST_ASSERT(true);
    TEST_ASSERT(1 + 1 == 2);
    TEST_ASSERT(true, "True condition with message");
    TEST_ASSERT(10 > 5, "Math sanity");
}

static void test_single_evaluation() {
    std::cout << "[Stage 238.3] Test 2: Single evaluation verification...\n";
    int side_effect_counter = 0;
    TEST_ASSERT(++side_effect_counter == 1);
    if (side_effect_counter != 1) {
        std::cerr << "[-] Error: Side effect evaluated multiple times: " << side_effect_counter << "\n";
        std::exit(1);
    }

    TEST_ASSERT(++side_effect_counter == 2, "Second increment");
    if (side_effect_counter != 2) {
        std::cerr << "[-] Error: Two-arg assert evaluated multiple times: " << side_effect_counter << "\n";
        std::exit(1);
    }
}

static void test_failure_reporting_and_interception() {
    std::cout << "[Stage 238.3] Test 3: Failure interception and diagnostic reporting...\n";
    g_custom_handler_calls = 0;
    g_last_expr.clear();
    g_last_msg.clear();

    ::winios::test::set_assertion_failure_handler(&custom_test_handler);

    volatile int zero = 0;

    // Trigger failure without aborting
    TEST_ASSERT(zero != 0);
    if (g_custom_handler_calls != 1) {
        std::cerr << "[-] Error: Custom handler not called on failure\n";
        std::exit(1);
    }
    if (g_last_expr != "zero != 0") {
        std::cerr << "[-] Error: Expression string mismatch: " << g_last_expr << "\n";
        std::exit(1);
    }

    // Trigger 2-arg failure
    TEST_ASSERT(zero == 1, "Value inequality check");
    if (g_custom_handler_calls != 2) {
        std::cerr << "[-] Error: Custom handler call count mismatch: " << g_custom_handler_calls << "\n";
        std::exit(1);
    }
    if (g_last_msg != "Value inequality check") {
        std::cerr << "[-] Error: Message string mismatch: " << g_last_msg << "\n";
        std::exit(1);
    }

    // Restore default handler
    ::winios::test::set_assertion_failure_handler(nullptr);
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  Stage 238.3: Test Assertion Framework Verification Suite       \n";
    std::cout << "=================================================================\n";

    test_successful_assertions();
    test_single_evaluation();
    test_failure_reporting_and_interception();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL STAGE 238.3 ASSERTION FRAMEWORK TESTS PASSED!              \n";
    std::cout << "=================================================================\n";
    return 0;
}
