#include "cpu/smp_interrupt_router.h"

#include <stdexcept>

namespace winios {

SmpInterruptRouter::SmpInterruptRouter(std::size_t max_cores)
    : queues_(max_cores) {}

void SmpInterruptRouter::ensure_core(std::size_t core_id) {
    if (core_id >= queues_.size()) {
        queues_.resize(core_id + 1);
    }
}

void SmpInterruptRouter::request_interrupt(std::size_t target_core, std::uint32_t intid,
                                          std::uint8_t priority, std::size_t source_core,
                                          bool is_fiq) {
    ensure_core(target_core);

    InterruptRequest req;
    req.target_core = target_core;
    req.source_core = source_core;
    req.intid = intid;
    req.priority = priority;
    req.type = classify_interrupt(intid);
    req.is_fiq = is_fiq;
    req.request_sequence = next_sequence_++;

    queues_[target_core].push_back(req);

    telemetry_.total_requested++;
    switch (req.type) {
    case InterruptType::SGI: telemetry_.sgi_count++; break;
    case InterruptType::PPI: telemetry_.ppi_count++; break;
    case InterruptType::SPI: telemetry_.spi_count++; break;
    case InterruptType::Extended: break;
    }
}

void SmpInterruptRouter::request_sgi(std::size_t source_core, std::uint32_t target_core_mask,
                                    std::uint32_t sgi_intid, std::uint8_t priority) {
    const std::uint32_t sanitized_intid = sgi_intid & 0xFu; // SGIs are 0..15
    for (std::size_t target = 0; target < sizeof(target_core_mask) * 8; ++target) {
        if ((target_core_mask & (1u << target)) != 0) {
            request_interrupt(target, sanitized_intid, priority, source_core, false);
        }
    }
}

bool SmpInterruptRouter::has_pending(std::size_t target_core) const noexcept {
    if (target_core >= queues_.size()) return false;
    return !queues_[target_core].empty();
}

std::size_t SmpInterruptRouter::pending_count(std::size_t target_core) const noexcept {
    if (target_core >= queues_.size()) return 0;
    return queues_[target_core].size();
}

std::optional<InterruptRequest> SmpInterruptRouter::peek_pending(std::size_t target_core) const noexcept {
    if (target_core >= queues_.size() || queues_[target_core].empty()) {
        return std::nullopt;
    }
    return queues_[target_core].front();
}

bool SmpInterruptRouter::deliver_next(std::size_t target_core, CPU& cpu, GICv3* gic) {
    if (target_core >= queues_.size() || queues_[target_core].empty()) {
        return false;
    }

    const InterruptRequest req = queues_[target_core].front();
    queues_[target_core].pop_front();

    // Awakening quiescent core
    if (cpu.is_waiting()) {
        cpu.wake_from_wfi();
        cpu.wake_from_wfe();
    }

    if (gic) {
        if (req.type == InterruptType::PPI) {
            gic->set_ppi_level(req.intid, true, target_core);
        } else if (req.type == InterruptType::SPI) {
            gic->set_spi_level(req.intid, true);
        }
    }

    // Deliver architectural exception to CPU
    cpu.raise_asynchronous_exception(req.is_fiq, req.intid, false, req.priority);

    telemetry_.total_delivered++;
    return true;
}

std::size_t SmpInterruptRouter::deliver_all_pending(std::size_t target_core, CPU& cpu, GICv3* gic) {
    std::size_t count = 0;
    while (deliver_next(target_core, cpu, gic)) {
        ++count;
    }
    return count;
}

void SmpInterruptRouter::clear_pending(std::size_t target_core) noexcept {
    if (target_core < queues_.size()) {
        queues_[target_core].clear();
    }
}

void SmpInterruptRouter::clear_all() noexcept {
    for (auto& q : queues_) {
        q.clear();
    }
}

void SmpInterruptRouter::restore_queue(std::size_t target_core, const std::vector<InterruptRequest>& requests) {
    ensure_core(target_core);
    queues_[target_core].clear();
    for (const auto& req : requests) {
        queues_[target_core].push_back(req);
    }
}

SmpInterruptRouterTelemetry SmpInterruptRouter::get_telemetry() const noexcept {
    return telemetry_;
}

void SmpInterruptRouter::reset_telemetry() noexcept {
    telemetry_ = SmpInterruptRouterTelemetry{};
}

} // namespace winios
