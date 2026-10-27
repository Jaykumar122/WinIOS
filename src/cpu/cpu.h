#pragma once
#include <cstddef>
#include <cstdint>

class Memory;

// Small, deliberately bounded model of the AArch64 general-purpose register
// file, stack pointer, and program counter.
class CPU {
public:
    // AArch64 exposes X0..X30 as the general-purpose registers. X31 is not a
    // general-purpose register (it is the zero register / stack pointer in
    // instruction encodings), so it is deliberately not addressable here.
    static constexpr std::size_t kRegisterCount = 31;

    CPU();

    // Clears every general-purpose register, stack pointer, and program
    // counter to zero.
    void reset();

    // Checked accessors; throw std::out_of_range for index >= kRegisterCount.
    std::uint64_t read_register(std::size_t index) const;
    void write_register(std::size_t index, std::uint64_t value);

    std::uint64_t get_program_counter() const noexcept { return program_counter_; }
    void set_program_counter(std::uint64_t address) noexcept { program_counter_ = address; }

    std::uint64_t get_stack_pointer() const noexcept { return stack_pointer_; }
    void set_stack_pointer(std::uint64_t value) noexcept { stack_pointer_ = value; }

    // Fetches and executes one 32-bit instruction at the current PC.
    // Unsupported encodings throw std::runtime_error and memory errors from
    // the fetch are propagated.
    void step(Memory& memory);

    // Executes at most max_instructions instructions and returns the count
    // actually executed. A zero limit performs no fetch.
    std::size_t run(Memory& memory, std::size_t max_instructions);

    // Demonstration only: performs host arithmetic on the register file.
    void run_demo();

private:
    std::uint64_t registers_[kRegisterCount]{};
    std::uint64_t stack_pointer_{};
    std::uint64_t program_counter_{};
};
