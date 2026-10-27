#pragma once
#include <cstddef>
#include <cstdint>

// Host-side model of the AArch64 general-purpose register file and program
// counter. It does not fetch, decode, or execute instructions.
class CPU {
public:
    // AArch64 exposes X0..X30 as the general-purpose registers. X31 is not a
    // general-purpose register (it is the zero register / stack pointer in
    // instruction encodings), so it is deliberately not addressable here.
    static constexpr std::size_t kRegisterCount = 31;

    CPU();

    // Clears every general-purpose register and the program counter to zero.
    void reset();

    // Checked accessors; throw std::out_of_range for index >= kRegisterCount.
    std::uint64_t read_register(std::size_t index) const;
    void write_register(std::size_t index, std::uint64_t value);

    std::uint64_t get_program_counter() const noexcept { return program_counter_; }
    void set_program_counter(std::uint64_t address) noexcept { program_counter_ = address; }

    // Demonstration only: performs host arithmetic on the register file and
    // prints the result. It does not decode or execute ARM64 instructions.
    void run_demo();

private:
    std::uint64_t registers_[kRegisterCount]{};
    std::uint64_t program_counter_{};
};
