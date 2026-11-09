#include "device/virtual_platform.h"

#include <algorithm>
#include <stdexcept>

namespace winios::device {

namespace {

// FNV-1a 64-bit hash combiner
inline void hash_combine(std::uint64_t& seed, std::uint64_t val) noexcept {
    seed ^= val;
    seed *= 1099511628211ULL;
}

} // namespace

VirtualPlatform::VirtualPlatform(const VirtualPlatformConfig& config)
    : config_(config) {
    if (config_.core_count == 0 || config_.core_count > 8) {
        throw std::invalid_argument("VirtualPlatform core count must be between 1 and 8");
    }

    // 1. Instantiate System RAM
    memory_ = std::make_unique<Memory>(config_.ram_size);

    // 2. Configure GICv3
    gic_.set_core_count(config_.core_count);
    GICv3::DistributorState dist = gic_.get_distributor_state();
    dist.gicd_ctlr = 0x3u; // Enable Group 0 and Group 1
    // Enable UART and DMA SPIs
    if (config_.uart_irq >= 32 && config_.uart_irq < 64) {
        dist.gicd_isenabler[1] |= (1u << (config_.uart_irq - 32));
        dist.gicd_igroupr[1]   |= (1u << (config_.uart_irq - 32)); // Group 1
        dist.gicd_ipriorityr[config_.uart_irq] = 0x80u;
    }
    if (config_.dma_irq >= 32 && config_.dma_irq < 64) {
        dist.gicd_isenabler[1] |= (1u << (config_.dma_irq - 32));
        dist.gicd_igroupr[1]   |= (1u << (config_.dma_irq - 32)); // Group 1
        dist.gicd_ipriorityr[config_.dma_irq] = 0x80u;
    }
    gic_.set_distributor_state(dist);

    // 3. Configure Per-Core Redistributor & CPU
    cpus_.reserve(config_.core_count);
    core_active_.resize(config_.core_count, false);

    for (std::size_t c = 0; c < config_.core_count; ++c) {
        auto cpu = std::make_unique<CPU>();
        cpu->set_core_id(static_cast<std::uint16_t>(c));
        cpu->bind_virtual_time(&virtual_time_, c);

        // Enable PPIs in Redistributor (Timer PPI 27 & 30)
        auto& r = gic_.get_redistributor(c);
        r.isenabler0 |= (1u << 27) | (1u << 30);
        r.igroupr0   |= (1u << 27) | (1u << 30); // Group 1
        r.ipriorityr[27] = 0x80u;
        r.ipriorityr[30] = 0x80u;

        // Configure CPU interface
        gic_.write_icc_pmr(0xFF, c);
        gic_.write_icc_igrpen1(1, c);

        cpus_.push_back(std::move(cpu));
    }

    // 4. Peripheral Interconnect Bus
    device_bus_ = std::make_shared<VirtualDeviceBus>();

    // 5. Virtual UART Console Device
    uart_ = std::make_shared<VirtualUartDevice>(0x55415254u, config_.uart_base, 0x1000u, config_.uart_irq);
    device_bus_->register_device(uart_);

    // 6. Virtual DMA Device
    dma_ = std::make_shared<VirtualDmaDevice>(config_.dma_base, 0x1000u, config_.dma_irq);
    device_bus_->register_device(dma_);

    // Reset platform to clean initial state
    reset(config_.boot_entry);
}

VirtualPlatform::~VirtualPlatform() = default;

CPU& VirtualPlatform::get_cpu(std::size_t index) {
    if (index >= cpus_.size()) {
        throw std::out_of_range("CPU index out of range in VirtualPlatform");
    }
    return *cpus_[index];
}

const CPU& VirtualPlatform::get_cpu(std::size_t index) const {
    if (index >= cpus_.size()) {
        throw std::out_of_range("CPU index out of range in VirtualPlatform");
    }
    return *cpus_[index];
}

bool VirtualPlatform::is_core_active(std::size_t index) const noexcept {
    if (index < core_active_.size()) {
        return core_active_[index];
    }
    return false;
}

void VirtualPlatform::set_core_active(std::size_t index, bool active) noexcept {
    if (index < core_active_.size()) {
        core_active_[index] = active;
    }
}

void VirtualPlatform::reset(std::uint64_t entry_point) {
    const std::uint64_t ep = (entry_point != 0) ? entry_point : config_.boot_entry;

    // Reset virtual devices
    if (device_bus_) {
        device_bus_->reset_all();
    }

    // Reset GIC distributor / redistributor state
    GICv3::DistributorState dist = gic_.get_distributor_state();
    dist.gicd_ctlr = 0x3u;
    gic_.set_distributor_state(dist);

    for (std::size_t c = 0; c < cpus_.size(); ++c) {
        auto& cpu = *cpus_[c];
        cpu.reset();
        cpu.set_cpacr_el1(0x00300000); // Full FP access
        cpu.set_program_counter(ep);

        // Core 0 active by default on cold reset; secondaries halted
        core_active_[c] = (c == 0);
    }
}

bool VirtualPlatform::step_core(std::size_t core_id) {
    if (core_id >= cpus_.size() || !core_active_[core_id]) {
        return false;
    }

    auto& cpu = *cpus_[core_id];
    cpu.step(*memory_);
    return true;
}

std::size_t VirtualPlatform::step_round_robin() {
    std::size_t stepped = 0;
    for (std::size_t c = 0; c < cpus_.size(); ++c) {
        if (core_active_[c]) {
            cpus_[c]->step(*memory_);
            stepped++;
        }
    }
    return stepped;
}

std::size_t VirtualPlatform::run_deterministic(std::size_t max_total_instructions) {
    std::size_t total = 0;
    while (total < max_total_instructions) {
        const std::size_t stepped = step_round_robin();
        if (stepped == 0) break;
        total += stepped;
    }
    return total;
}

bool VirtualPlatform::validate_memory_map() const noexcept {
    // Collect all system memory regions and verify non-overlapping
    struct MemRegion {
        std::uint64_t start;
        std::uint64_t end;
        const char* name;
    };

    const std::uint64_t gicd_end = config_.gicd_base + 0x10000u;
    const std::uint64_t gicr_end = config_.gicr_base + (config_.core_count * 0x20000u);
    const std::uint64_t uart_end = config_.uart_base + 0x1000u;
    const std::uint64_t dma_end  = config_.dma_base  + 0x1000u;
    const std::uint64_t ram_end  = config_.ram_base  + config_.ram_size;

    const MemRegion regions[] = {
        {config_.gicd_base, gicd_end, "GICD"},
        {config_.gicr_base, gicr_end, "GICR"},
        {config_.uart_base, uart_end, "UART"},
        {config_.dma_base,  dma_end,  "DMA"},
        {config_.ram_base,  ram_end,  "RAM"}
    };

    constexpr std::size_t n = sizeof(regions) / sizeof(regions[0]);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (std::max(regions[i].start, regions[j].start) < std::min(regions[i].end, regions[j].end)) {
                return false; // Collision detected
            }
        }
    }
    return true;
}

std::uint64_t VirtualPlatform::compute_platform_hash() const {
    std::uint64_t seed = 14695981039346656037ULL;

    // 1. CPU Core States
    for (std::size_t c = 0; c < cpus_.size(); ++c) {
        const auto& cpu = *cpus_[c];
        hash_combine(seed, static_cast<std::uint64_t>(c));
        hash_combine(seed, cpu.get_program_counter());
        hash_combine(seed, cpu.get_stack_pointer());
        hash_combine(seed, static_cast<std::uint64_t>(cpu.get_nzcv()));
        for (std::size_t r = 0; r < CPU::kRegisterCount; ++r) {
            hash_combine(seed, cpu.read_register(r));
        }
        hash_combine(seed, static_cast<std::uint64_t>(core_active_[c] ? 1 : 0));
    }

    // 2. GIC State (PMR and RPR for active cores)
    for (std::size_t c = 0; c < cpus_.size(); ++c) {
        hash_combine(seed, static_cast<std::uint64_t>(gic_.read_icc_pmr(c)));
        hash_combine(seed, static_cast<std::uint64_t>(gic_.read_icc_rpr(c)));
    }

    // 3. Virtual UART output byte count
    if (uart_) {
        hash_combine(seed, static_cast<std::uint64_t>(uart_->output_bytes_count()));
    }

    return seed;
}

} // namespace winios::device
