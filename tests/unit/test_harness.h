#pragma once

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

// Minimal assertion helpers. Failures are counted and reported; no dependency
// on assert() so NDEBUG cannot silently disable the checks.
namespace test {

inline int checks_run = 0;
inline int failures = 0;

inline void fail(const char* file, int line, std::string_view message) {
    ++failures;
    std::cerr << file << ':' << line << ": FAILED: " << message << '\n';
}

inline void check(bool condition, const char* expression, const char* file, int line) {
    ++checks_run;
    if (!condition) {
        fail(file, line, expression);
    }
}

template <typename Exception, typename Callable>
void expect_throw(Callable&& callable, const char* expression, const char* file, int line) {
    ++checks_run;
    try {
        callable();
    } catch (const Exception&) {
        return;
    } catch (const std::exception& error) {
        fail(file, line, std::string(expression)
                            + " threw a different exception: " + error.what());
        return;
    } catch (...) {
        fail(file, line, std::string(expression)
                            + " threw a non-standard exception");
        return;
    }
    fail(file, line, std::string(expression) + " did not throw");
}

inline int summarize(const char* suite) {
    if (failures == 0) {
        std::cout << suite << ": all " << checks_run << " checks passed\n";
        return 0;
    }
    std::cerr << suite << ": " << failures << " of " << checks_run
              << " checks failed\n";
    return 1;
}

} // namespace test

#define CHECK(expression) \
    test::check((expression), #expression, __FILE__, __LINE__)

#define CHECK_OUT_OF_RANGE(expression)                                        \
    test::expect_throw<std::out_of_range>(                                    \
        [&] { (void)(expression); }, #expression, __FILE__, __LINE__)

#define CHECK_INVALID_ARGUMENT(expression)                                    \
    test::expect_throw<std::invalid_argument>(                                \
        [&] { (void)(expression); }, #expression, __FILE__, __LINE__)
