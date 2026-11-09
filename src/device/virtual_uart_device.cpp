#include "device/virtual_uart_device.h"

#include <cstring>

namespace winios::device {

VirtualUartDevice::VirtualUartDevice(std::uint32_t device_id,
                                     std::uint64_t mmio_base,
                                     std::uint64_t mmio_size,
                                     std::uint32_t irq_line)
    : VirtualDevice(device_id, "PL011_UART", VirtualDeviceType::GenericMmio, mmio_base, mmio_size, irq_line) {
    status_ = VirtualDeviceStatus::Idle;
}

std::uint64_t VirtualUartDevice::read_register(std::uint64_t offset, unsigned width) {
    std::lock_guard<std::mutex> guard(lock_);
    (void)width;

    switch (offset) {
    case kRegDR: {
        std::uint32_t val = 0;
        if (!rx_fifo_.empty()) {
            val = rx_fifo_.front();
            rx_fifo_.pop_front();
        }
        if (rx_fifo_.empty()) {
            reg_fr_ |= kFR_RXFE;
            reg_fr_ &= ~kFR_RXFF;
            reg_ris_ &= ~(1u << 4); // Clear RX interrupt
        }
        update_interrupts();
        return val;
    }
    case kRegRSR:
        return reg_rsr_;
    case kRegFR:
        return reg_fr_;
    case kRegILPR:
        return reg_ilpr_;
    case kRegIBRD:
        return reg_ibrd_;
    case kRegFBRD:
        return reg_fbrd_;
    case kRegLCRH:
        return reg_lcrh_;
    case kRegCR:
        return reg_cr_;
    case kRegIFLS:
        return reg_ifls_;
    case kRegIMSC:
        return reg_imsc_;
    case kRegRIS:
        return reg_ris_;
    case kRegMIS:
        return reg_ris_ & reg_imsc_;
    // AMBA PrimeCell and Peripheral Identification Registers (PL011 r1p5)
    case 0xFE0: return 0x11u; // UARTPeriphID0
    case 0xFE4: return 0x10u; // UARTPeriphID1
    case 0xFE8: return 0x34u; // UARTPeriphID2
    case 0xFEC: return 0x00u; // UARTPeriphID3
    case 0xFF0: return 0x0Du; // UARTPCellID0
    case 0xFF4: return 0xF0u; // UARTPCellID1
    case 0xFF8: return 0x05u; // UARTPCellID2
    case 0xFFC: return 0xB1u; // UARTPCellID3
    default:
        return 0;
    }
}

void VirtualUartDevice::write_register(std::uint64_t offset, std::uint64_t value, unsigned width) {
    std::lock_guard<std::mutex> guard(lock_);
    (void)width;
    const std::uint32_t val32 = static_cast<std::uint32_t>(value);

    switch (offset) {
    case kRegDR: {
        const std::uint8_t byte = static_cast<std::uint8_t>(val32 & 0xFFu);
        output_buffer_.push_back(byte);
        // TX completes immediately in virtual environment
        reg_ris_ |= (1u << 5); // TX interrupt
        update_interrupts();
        break;
    }
    case kRegRSR:
        reg_rsr_ = 0;
        break;
    case kRegILPR:
        reg_ilpr_ = val32;
        break;
    case kRegIBRD:
        reg_ibrd_ = val32;
        break;
    case kRegFBRD:
        reg_fbrd_ = val32;
        break;
    case kRegLCRH:
        reg_lcrh_ = val32;
        break;
    case kRegCR:
        reg_cr_ = val32;
        break;
    case kRegIFLS:
        reg_ifls_ = val32;
        break;
    case kRegIMSC:
        reg_imsc_ = val32;
        update_interrupts();
        break;
    case kRegICR:
        reg_ris_ &= ~val32; // Clear bits written as 1
        update_interrupts();
        break;
    default:
        break;
    }
}

void VirtualUartDevice::reset() noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    reg_dr_   = 0;
    reg_rsr_  = 0;
    reg_fr_   = kFR_TXFE | kFR_RXFE;
    reg_ilpr_ = 0;
    reg_ibrd_ = 0;
    reg_fbrd_ = 0;
    reg_lcrh_ = 0x70u;
    reg_cr_   = kCR_UARTEN | kCR_TXE | kCR_RXE;
    reg_ifls_ = 0;
    reg_imsc_ = 0;
    reg_ris_  = 0;
    rx_fifo_.clear();
    output_buffer_.clear();
    status_ = VirtualDeviceStatus::Idle;
    trigger_interrupt(false);
}

void VirtualUartDevice::update_interrupts() {
    const std::uint32_t mis = reg_ris_ & reg_imsc_;
    trigger_interrupt(mis != 0);
}

void VirtualUartDevice::inject_input(char c) {
    std::lock_guard<std::mutex> guard(lock_);
    rx_fifo_.push_back(static_cast<std::uint8_t>(c));
    reg_fr_ &= ~kFR_RXFE;
    if (rx_fifo_.size() >= 16) {
        reg_fr_ |= kFR_RXFF;
    }
    reg_ris_ |= (1u << 4); // RX interrupt
    update_interrupts();
}

void VirtualUartDevice::inject_input(const std::string& text) {
    for (char c : text) {
        inject_input(c);
    }
}

std::string VirtualUartDevice::get_output_text() const {
    std::lock_guard<std::mutex> guard(lock_);
    return std::string(output_buffer_.begin(), output_buffer_.end());
}

void VirtualUartDevice::clear_output() {
    std::lock_guard<std::mutex> guard(lock_);
    output_buffer_.clear();
}

bool VirtualUartDevice::is_interrupt_asserted() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return (reg_ris_ & reg_imsc_) != 0;
}

std::vector<std::uint8_t> VirtualUartDevice::serialize_state() const {
    std::lock_guard<std::mutex> guard(lock_);
    const std::uint32_t rx_sz = static_cast<std::uint32_t>(rx_fifo_.size());
    const std::uint32_t out_sz = static_cast<std::uint32_t>(output_buffer_.size());
    std::vector<std::uint8_t> data(44 + 4 + rx_sz + 4 + out_sz);
    std::size_t offset = 0;

    auto append_u32 = [&](std::uint32_t val) {
        std::memcpy(data.data() + offset, &val, 4);
        offset += 4;
    };

    append_u32(reg_dr_);
    append_u32(reg_rsr_);
    append_u32(reg_fr_);
    append_u32(reg_ilpr_);
    append_u32(reg_ibrd_);
    append_u32(reg_fbrd_);
    append_u32(reg_lcrh_);
    append_u32(reg_cr_);
    append_u32(reg_ifls_);
    append_u32(reg_imsc_);
    append_u32(reg_ris_);

    append_u32(rx_sz);
    if (rx_sz > 0) {
        for (std::uint8_t b : rx_fifo_) {
            data[offset++] = b;
        }
    }

    append_u32(out_sz);
    if (out_sz > 0) {
        std::memcpy(data.data() + offset, output_buffer_.data(), out_sz);
        offset += out_sz;
    }

    return data;
}

bool VirtualUartDevice::deserialize_state(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 44) return false;
    std::lock_guard<std::mutex> guard(lock_);
    std::size_t offset = 0;

    auto read_u32 = [&]() -> std::uint32_t {
        std::uint32_t val = 0;
        std::memcpy(&val, data + offset, 4);
        offset += 4;
        return val;
    };

    reg_dr_   = read_u32();
    reg_rsr_  = read_u32();
    reg_fr_   = read_u32();
    reg_ilpr_ = read_u32();
    reg_ibrd_ = read_u32();
    reg_fbrd_ = read_u32();
    reg_lcrh_ = read_u32();
    reg_cr_   = read_u32();
    reg_ifls_ = read_u32();
    reg_imsc_ = read_u32();
    reg_ris_  = read_u32();

    rx_fifo_.clear();
    output_buffer_.clear();

    if (offset + 4 <= size) {
        std::uint32_t rx_sz = read_u32();
        if (offset + rx_sz <= size) {
            for (std::uint32_t i = 0; i < rx_sz; ++i) {
                rx_fifo_.push_back(data[offset + i]);
            }
            offset += rx_sz;
        }
    }

    if (offset + 4 <= size) {
        std::uint32_t out_sz = read_u32();
        if (offset + out_sz <= size) {
            output_buffer_.assign(data + offset, data + offset + out_sz);
            offset += out_sz;
        }
    } else if (offset < size) {
        // Fallback for legacy format
        output_buffer_.assign(data + offset, data + size);
    }

    update_interrupts();
    return true;
}

} // namespace winios::device
