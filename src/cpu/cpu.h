#pragma once
#include <cstdint>

class CPU {
public:
    CPU();
    void run_demo();

private:
    std::uint64_t registers_[31]{};
    std::uint64_t program_counter_{};
};
