#pragma once

#include <iostream>
#include <cstdlib>
#include <string_view>

namespace winios::test {

using AssertionFailureHandler = void (*)(const char* expr_str,
                                         const char* file,
                                         int line,
                                         std::string_view msg);

inline void default_assertion_failure_handler(const char* expr_str,
                                              const char* file,
                                              int line,
                                              std::string_view msg) {
    std::cerr << "\n[TEST_ASSERT FAILED] " << file << ":" << line << "\n"
              << "  Expression: " << expr_str << "\n";
    if (!msg.empty()) {
        std::cerr << "  Message:    " << msg << "\n";
    }
    std::cerr << std::flush;
    std::exit(1);
}

inline AssertionFailureHandler& get_assertion_failure_handler() {
    static AssertionFailureHandler handler = &default_assertion_failure_handler;
    return handler;
}

inline void set_assertion_failure_handler(AssertionFailureHandler handler) {
    get_assertion_failure_handler() = handler ? handler : &default_assertion_failure_handler;
}

inline void report_assertion_failure(const char* expr_str,
                                     const char* file,
                                     int line,
                                     std::string_view msg = {}) {
    get_assertion_failure_handler()(expr_str, file, line, msg);
}

} // namespace winios::test

#define TEST_ASSERT_1_ARG(cond) \
    do { \
        if (!(cond)) { \
            ::winios::test::report_assertion_failure(#cond, __FILE__, __LINE__, ""); \
        } \
    } while (0)

#define TEST_ASSERT_2_ARGS(cond, msg) \
    do { \
        if (!(cond)) { \
            ::winios::test::report_assertion_failure(#cond, __FILE__, __LINE__, (msg)); \
        } \
    } while (0)

#define TEST_ASSERT_EXPAND(x) x
#define GET_3RD_ARG(arg1, arg2, arg3, ...) arg3
#define TEST_ASSERT_MACRO_CHOOSER(...) TEST_ASSERT_EXPAND(GET_3RD_ARG(__VA_ARGS__, TEST_ASSERT_2_ARGS, TEST_ASSERT_1_ARG))

#undef TEST_ASSERT
#define TEST_ASSERT(...) TEST_ASSERT_EXPAND(TEST_ASSERT_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__))
