#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "device/virtual_device.h"

namespace winios::device {

// -----------------------------------------------------------------------------
// VirtualUartDevice: ARM PL011-compatible Virtual UART / Console
// Standard base: 0x09000000, size: 0x1000, default IRQ: 33
// -----------------------------------------------------------------------------
class VirtualUartDevice : public VirtualDevice {
public:
    static constexpr std::uint64_t kDefaultUartBase = 0x09000000ULL;
    static constexpr std::uint64_t kDefaultUartSize = 0x1000ULL;
    static constexpr std::uint32_t kDefaultUartIrq  = 33u;

    // PL011 Register Offsets
    static constexpr std::uint64_t kRegDR   = 0x00u; // Data Register
    static constexpr std::uint64_t kRegRSR  = 0x04u; // Receive Status / Error Clear
    static constexpr std::uint64_t kRegFR   = 0x18u; // Flag Register
    static constexpr std::uint64_t kRegILPR = 0x20u; // IrDA Low-Power Counter
    static constexpr std::uint64_t kRegIBRD = 0x24u; // Integer Baud Rate
    static constexpr std::uint64_t kRegFBRD = 0x28u; // Fractional Baud Rate
    static constexpr std::uint64_t kRegLCRH = 0x2Cu; // Line Control
    static constexpr std::uint64_t kRegCR   = 0x30u; // Control Register
    static constexpr std::uint64_t kRegIFLS = 0x34u; // Interrupt FIFO Level Select
    static constexpr std::uint64_t kRegIMSC = 0x38u; // Interrupt Mask Set/Clear
    static constexpr std::uint64_t kRegRIS  = 0x3Cu; // Raw Interrupt Status
    static constexpr std::uint64_t kRegMIS  = 0x40u; // Masked Interrupt Status
    static constexpr std::uint64_t kRegICR  = 0x44u; // Interrupt Clear Register

    // Flag Register Bits
    static constexpr std::uint32_t kFR_TXFE = (1u << 7); // Transmit FIFO empty
    static constexpr std::uint32_t kFR_RXFF = (1u << 6); // Receive FIFO full
    static constexpr std::uint32_t kFR_TXFF = (1u << 5); // Transmit FIFO full
    static constexpr std::uint32_t kFR_RXFE = (1u << 4); // Receive FIFO empty
    static constexpr std::uint32_t kFR_BUSY = (1u << 3); // UART busy

    // Control Register Bits
    static constexpr std::uint32_t kCR_RXE    = (1u << 9); // Receive enable
    static constexpr std::uint32_t kCR_TXE    = (1u << 8); // Transmit enable
    static constexpr std::uint32_t kCR_UARTEN = (1u << 0); // UART enable

    explicit VirtualUartDevice(std::uint32_t device_id = 0x55415254u, // 'UART'
                               std::uint64_t mmio_base = kDefaultUartBase,
                               std::uint64_t mmio_size = kDefaultUartSize,
                               std::uint32_t irq_line = kDefaultUartIrq);

    ~VirtualUartDevice() override = default;

    // VirtualDevice Interface
    std::uint64_t read_register(std::uint64_t offset, unsigned width) override;
    void write_register(std::uint64_t offset, std::uint64_t value, unsigned width) override;
    void reset() noexcept override;

    std::vector<std::uint8_t> serialize_state() const override;
    bool deserialize_state(const std::uint8_t* data, std::size_t size) override;

    // Console I/O inspection helpers
    void inject_input(char c);
    void inject_input(const std::string& text);

    [[nodiscard]] std::string get_output_text() const;
    void clear_output();

    [[nodiscard]] bool is_interrupt_asserted() const noexcept;
    [[nodiscard]] std::size_t output_bytes_count() const noexcept { return output_buffer_.size(); }

private:
    void update_interrupts();

    mutable std::mutex lock_;

    // PL011 Register State
    std::uint32_t reg_dr_{0};
    std::uint32_t reg_rsr_{0};
    std::uint32_t reg_fr_{kFR_TXFE | kFR_RXFE}; // Empty by default
    std::uint32_t reg_ilpr_{0};
    std::uint32_t reg_ibrd_{0};
    std::uint32_t reg_fbrd_{0};
    std::uint32_t reg_lcrh_{0x70u};             // 8-bit, FIFO enabled
    std::uint32_t reg_cr_{kCR_UARTEN | kCR_TXE | kCR_RXE}; // Enabled by default
    std::uint32_t reg_ifls_{0};
    std::uint32_t reg_imsc_{0};
    std::uint32_t reg_ris_{0};

    // Queues
    std::deque<std::uint8_t> rx_fifo_;
    std::vector<std::uint8_t> output_buffer_;
};

} // namespace winios::device
