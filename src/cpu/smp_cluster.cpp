#include "cpu/smp_cluster.h"
#include "cpu/psci.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace winios {
namespace {
constexpr auto kIdlePollInterval = std::chrono::microseconds(50);
constexpr auto kControllerPollInterval = std::chrono::milliseconds(1);
} // namespace

SMPCluster::SMPCluster(std::size_t num_cores, std::size_t ram_size)
    : memory_(std::make_unique<Memory>(ram_size)) {
    const std::size_t count = std::max(std::size_t{1}, std::min(num_cores, GICv3::kMaxCores));
    gic_.set_core_count(count);
    reservations_.resize(count);

    // Every store that reaches the Memory layer invalidates any overlapping
    // global exclusive-monitor reservation while the stripe write lock is
    // held. This makes normal stores and LDXR/STXR reservations mutually
    // ordered without requiring Memory to know about cores.
    memory_->set_store_mutex(&store_mutex_);
    memory_->set_store_observer(
        [this](std::size_t offset, std::size_t width) {
            invalidate_all_exclusive_reservations(offset, width);
        });

    for (std::size_t i = 0; i < count; ++i) {
        auto core = std::make_unique<CPU>();
        core->set_core_id(i);
        core->bind_virtual_time(&virtual_time_, i);
        core->set_smp_cluster(this);
        if (i > 0) {
            core->set_halted(true); // Secondary cores start halted
            virtual_time_.set_core_offline(i, true);
        }
        cores_.push_back(std::move(core));
    }
}

SMPCluster::~SMPCluster() {
    stop_parallel();
}

CPU& SMPCluster::get_core(std::size_t index) {
    if (index >= cores_.size()) {
        throw std::out_of_range("SMPCluster: core index out of range");
    }
    return *cores_[index];
}

const CPU& SMPCluster::get_core(std::size_t index) const {
    if (index >= cores_.size()) {
        throw std::out_of_range("SMPCluster: core index out of range");
    }
    return *cores_[index];
}

void SMPCluster::initialize_core(std::size_t index, std::uint64_t entry_address) {
    auto& core = get_core(index);
    core.reset();
    core.set_core_id(index);
    core.bind_virtual_time(&virtual_time_, index);
    virtual_time_.reset_core(index);
    core.set_smp_cluster(this);
    core.initialize_entry(entry_address, *memory_);
    core.start();
}

void SMPCluster::start_secondary_core(std::size_t index, std::uint64_t entry_address) {
    if (index >= cores_.size()) {
        throw std::out_of_range("SMPCluster: core index out of range");
    }
    auto& core = *cores_[index];
    core.reset();
    core.set_core_id(index);
    core.bind_virtual_time(&virtual_time_, index);
    virtual_time_.reset_core(index);
    core.set_smp_cluster(this);
    core.set_program_counter(entry_address);
    core.start();
}

void SMPCluster::halt_core(std::size_t index) noexcept {
    if (index < cores_.size()) {
        cores_[index]->set_halted(true);
        virtual_time_.set_core_offline(index, true);
    }
}

void SMPCluster::resume_core(std::size_t index) noexcept {
    if (index < cores_.size()) {
        cores_[index]->set_halted(false);
        virtual_time_.set_core_offline(index, false);
    }
}

bool SMPCluster::is_core_halted(std::size_t index) const noexcept {
    if (index < cores_.size()) {
        return cores_[index]->is_halted();
    }
    return true;
}

void SMPCluster::broadcast_sev(std::size_t sender_core) {
    for (std::size_t i = 0; i < cores_.size(); ++i) {
        if (i != sender_core) {
            cores_[i]->set_event_register(true);
            cores_[i]->wake_from_wfe();
        }
    }
}

void SMPCluster::broadcast_tlbi_all(std::size_t sender_core) {
    for (std::size_t i = 0; i < cores_.size(); ++i) {
        if (i != sender_core) {
            cores_[i]->get_mmu().invalidate_tlb_all();
        }
    }
}

void SMPCluster::broadcast_tlbi_va(std::size_t sender_core, std::uint64_t va, std::uint16_t asid, bool last_level) {
    for (std::size_t i = 0; i < cores_.size(); ++i) {
        if (i != sender_core) {
            cores_[i]->get_mmu().invalidate_tlb_va(va, asid, last_level);
        }
    }
}

void SMPCluster::broadcast_tlbi_vaa(std::size_t sender_core, std::uint64_t va, bool last_level) {
    for (std::size_t i = 0; i < cores_.size(); ++i) {
        if (i != sender_core) {
            cores_[i]->get_mmu().invalidate_tlb_vaa(va, last_level);
        }
    }
}

void SMPCluster::broadcast_tlbi_asid(std::size_t sender_core, std::uint16_t asid) {
    for (std::size_t i = 0; i < cores_.size(); ++i) {
        if (i != sender_core) {
            cores_[i]->get_mmu().invalidate_tlb_asid(asid);
        }
    }
}

void SMPCluster::register_exclusive_reservation(std::size_t core_id,
                                                std::uint64_t address,
                                                std::size_t size) {
    std::lock_guard<std::mutex> lock(reservations_mutex_);
    if (core_id < reservations_.size()) {
        reservations_[core_id] = GlobalExclusiveReservation{true, address, size};
    }
}

bool SMPCluster::check_and_clear_exclusive_reservation(std::size_t core_id,
                                                       std::uint64_t address,
                                                       std::size_t size) {
    std::lock_guard<std::mutex> lock(reservations_mutex_);
    if (core_id >= reservations_.size()) return false;
    auto& res = reservations_[core_id];
    if (res.valid && res.address == address && res.size == size) {
        res = GlobalExclusiveReservation{false, 0, 0};
        return true;
    }
    return false;
}

void SMPCluster::invalidate_exclusive_reservations(std::size_t writer_core_id,
                                                   std::uint64_t address,
                                                   std::size_t size) {
    std::lock_guard<std::mutex> lock(reservations_mutex_);
    const std::uint64_t write_end = address + size;
    for (std::size_t i = 0; i < reservations_.size(); ++i) {
        if (i != writer_core_id && reservations_[i].valid) {
            const std::uint64_t res_end =
                reservations_[i].address + reservations_[i].size;
            if (address < res_end && reservations_[i].address < write_end) {
                reservations_[i].valid = false;
            }
        }
    }
}

void SMPCluster::invalidate_all_exclusive_reservations(std::uint64_t address,
                                                       std::size_t size) {
    std::lock_guard<std::mutex> lock(reservations_mutex_);
    const std::uint64_t write_end = address + size;
    for (auto& reservation : reservations_) {
        if (!reservation.valid) continue;
        const std::uint64_t res_end = reservation.address + reservation.size;
        if (address < res_end && reservation.address < write_end) {
            reservation.valid = false;
        }
    }
}

void SMPCluster::clear_exclusive_reservation(std::size_t core_id) {
    std::lock_guard<std::mutex> lock(reservations_mutex_);
    if (core_id < reservations_.size()) {
        reservations_[core_id] = GlobalExclusiveReservation{false, 0, 0};
    }
}

std::size_t SMPCluster::step_round_robin() {
    std::size_t instructions_stepped = 0;
    const std::size_t count = cores_.size();
    for (std::size_t k = 0; k < count; ++k) {
        const std::size_t core_idx = (current_scheduled_core_ + k) % count;
        auto& core = *cores_[core_idx];
        if (!core.is_halted() && !core.is_waiting()) {
            core.step(*memory_);
            ++instructions_stepped;
        }
    }
    current_scheduled_core_ = (current_scheduled_core_ + 1) % count;
    return instructions_stepped;
}

std::size_t SMPCluster::run_deterministic(std::size_t max_total_instructions) {
    std::size_t total = 0;
    while (max_total_instructions == 0 || total < max_total_instructions) {
        const std::size_t stepped = step_round_robin();
        if (stepped == 0) {
            std::vector<bool> active_mask(cores_.size(), false);
            bool any_waiting = false;
            for (std::size_t i = 0; i < cores_.size(); ++i) {
                if (!cores_[i]->is_halted()) {
                    active_mask[i] = true;
                    if (cores_[i]->is_waiting()) {
                        any_waiting = true;
                    }
                }
            }
            if (any_waiting && virtual_time_.advance_to_earliest_deadline(active_mask)) {
                for (std::size_t i = 0; i < cores_.size(); ++i) {
                    if (active_mask[i] && cores_[i]->is_waiting()) {
                        cores_[i]->poll_wait_wakeup();
                    }
                }
                continue;
            }
            // All active cores are halted or waiting
            break;
        }
        total += stepped;
    }
    return total;
}

// ---------------------------------------------------------------------------
// Phase 43 parallel engine
// ---------------------------------------------------------------------------

void SMPCluster::start_parallel() {
    if (parallel_started_.load(std::memory_order_acquire)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(control_mutex_);
        quit_.store(false, std::memory_order_release);
        parallel_paused_.store(false, std::memory_order_release);
        epoch_active_.store(false, std::memory_order_release);
        epoch_budget_.store(0, std::memory_order_release);
        epoch_instructions_.store(0, std::memory_order_release);
        workers_in_epoch_ = 0;
        worker_exception_ = nullptr;
    }

    // Concurrent guest-memory locking must be active before any worker can
    // step. The shared counter is the parallel guest clock.
    memory_->set_concurrent_mode(true);
    parallel_started_.store(true, std::memory_order_release);

    workers_.reserve(cores_.size());
    for (std::size_t i = 0; i < cores_.size(); ++i) {
        workers_.emplace_back(&SMPCluster::worker_main, this, i);
    }
}

void SMPCluster::stop_parallel() {
    if (!parallel_started_.load(std::memory_order_acquire)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(control_mutex_);
        quit_.store(true, std::memory_order_release);
        epoch_active_.store(false, std::memory_order_release);
    }
    control_cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    parallel_started_.store(false, std::memory_order_release);
    parallel_paused_.store(false, std::memory_order_release);
    memory_->set_concurrent_mode(false);
}

void SMPCluster::pause_parallel() {
    parallel_paused_.store(true, std::memory_order_release);
    control_cv_.notify_all();
}

void SMPCluster::resume_parallel() {
    parallel_paused_.store(false, std::memory_order_release);
    control_cv_.notify_all();
}

bool SMPCluster::any_core_runnable() const noexcept {
    for (const auto& core : cores_) {
        if (!core->is_halted() && !core->is_waiting()) {
            return true;
        }
    }
    return false;
}

void SMPCluster::worker_main(std::size_t index) {
    CPU& core = *cores_[index];
    for (;;) {
        std::unique_lock<std::mutex> lock(control_mutex_);
        control_cv_.wait(lock, [this] {
            return quit_.load(std::memory_order_acquire)
                || (epoch_active_.load(std::memory_order_acquire)
                    && !parallel_paused_.load(std::memory_order_acquire));
        });
        if (quit_.load(std::memory_order_acquire)) {
            break;
        }
        if (!epoch_active_.load(std::memory_order_acquire)
            || parallel_paused_.load(std::memory_order_acquire)) {
            continue;
        }
        ++workers_in_epoch_;
        lock.unlock();

        while (!quit_.load(std::memory_order_acquire)
               && epoch_active_.load(std::memory_order_acquire)
               && !parallel_paused_.load(std::memory_order_acquire)) {
            if (core.is_halted()) {
                break;
            }
            if (core.is_waiting()) {
                // The owner thread re-evaluates its own timer and pending
                // interrupts; no other thread touches this core's timer state.
                try {
                    core.poll_wait_wakeup();
                } catch (...) {
                    std::lock_guard<std::mutex> guard(control_mutex_);
                    if (!worker_exception_) {
                        worker_exception_ = std::current_exception();
                        worker_exception_count_.fetch_add(1, std::memory_order_relaxed);
                    }
                    epoch_active_.store(false, std::memory_order_release);
                    control_cv_.notify_all();
                    break;
                }
                if (core.is_waiting()) {
                    std::this_thread::sleep_for(kIdlePollInterval);
                    continue;
                }
            }

            const std::size_t budget = epoch_budget_.load(std::memory_order_relaxed);
            if (budget != 0
                && epoch_instructions_.load(std::memory_order_relaxed) >= budget) {
                break;
            }

            try {
                core.step(*memory_);
            } catch (...) {
                std::lock_guard<std::mutex> guard(control_mutex_);
                if (!worker_exception_) {
                    worker_exception_ = std::current_exception();
                    worker_exception_count_.fetch_add(1, std::memory_order_relaxed);
                }
                epoch_active_.store(false, std::memory_order_release);
                control_cv_.notify_all();
                break;
            }
            epoch_instructions_.fetch_add(1, std::memory_order_relaxed);
        }

        lock.lock();
        --workers_in_epoch_;
        control_cv_.notify_all();
    }
}

std::size_t SMPCluster::run_parallel(std::size_t max_total_instructions) {
    start_parallel();

    if (parallel_paused_.load(std::memory_order_acquire)) {
        return 0;
    }

    std::unique_lock<std::mutex> lock(control_mutex_);

    // A previous run may have left an exception behind; surface it first.
    if (worker_exception_) {
        std::exception_ptr pending = worker_exception_;
        worker_exception_ = nullptr;
        lock.unlock();
        std::rethrow_exception(pending);
    }

    epoch_instructions_.store(0, std::memory_order_release);
    epoch_budget_.store(max_total_instructions, std::memory_order_release);
    epoch_active_.store(true, std::memory_order_release);
    control_cv_.notify_all();

    for (;;) {
        if (quit_.load(std::memory_order_acquire)) {
            break;
        }
        if (worker_exception_) {
            break;
        }
        if (parallel_paused_.load(std::memory_order_acquire)) {
            break;
        }
        if (max_total_instructions != 0
            && epoch_instructions_.load(std::memory_order_acquire)
                   >= max_total_instructions) {
            break;
        }
        if (!any_core_runnable()) {
            // Quiescence: every core is halted or waiting, and a waiting core
            // can only be woken by another guest core (no host time source).
            break;
        }
        control_cv_.wait_for(lock, kControllerPollInterval);
    }

    epoch_active_.store(false, std::memory_order_release);
    control_cv_.notify_all();
    control_cv_.wait(lock, [this] { return workers_in_epoch_ == 0; });

    // Read the retired count only after every worker has left the epoch. A
    // guest core may still have been inside step() when the controller
    // observed budget exhaustion or quiescence; that step's counter and
    // epoch increments happen in its epilogue. Joining the epoch first makes
    // the returned count agree exactly with the shared guest clock.
    const std::size_t retired = epoch_instructions_.load(std::memory_order_acquire);

    if (worker_exception_) {
        std::exception_ptr pending = worker_exception_;
        worker_exception_ = nullptr;
        lock.unlock();
        std::rethrow_exception(pending);
    }
    return retired;
}

void SMPCluster::enable_psci() {
    psci_service_ = std::make_unique<PSCIService>(*this);
    for (auto& core : cores_) {
        core->set_psci_service(psci_service_.get());
    }
}

PSCIService* SMPCluster::get_psci_service() noexcept {
    return psci_service_.get();
}

const PSCIService* SMPCluster::get_psci_service() const noexcept {
    return psci_service_.get();
}

} // namespace winios
