#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "memory/memory.h"

namespace winios {

class PSCIService;

// Phase 42/43 multicore cluster.
//
// Phase 42's deterministic round-robin scheduler remains the default and the
// reference execution mode. Phase 43 adds an optional parallel engine that
// runs one host worker thread per active guest core. The two modes share the
// same guest-visible state and locking primitives; the parallel engine is
// never entered implicitly.
class SMPCluster {
public:
    static constexpr std::size_t kDefaultCores = 2;

    explicit SMPCluster(std::size_t num_cores = kDefaultCores, std::size_t ram_size = 0x10000);
    ~SMPCluster();

    SMPCluster(const SMPCluster&) = delete;
    SMPCluster& operator=(const SMPCluster&) = delete;

    // Core management
    std::size_t core_count() const noexcept { return cores_.size(); }
    CPU& get_core(std::size_t index);
    const CPU& get_core(std::size_t index) const;

    // Shared resources
    Memory& get_memory() noexcept { return *memory_; }
    const Memory& get_memory() const noexcept { return *memory_; }

    GICv3& get_gic() noexcept { return gic_; }
    const GICv3& get_gic() const noexcept { return gic_; }

    // Phase 50: Authoritative Virtual-Time Architecture
    VirtualTime& get_virtual_time() noexcept { return virtual_time_; }
    const VirtualTime& get_virtual_time() const noexcept { return virtual_time_; }

    // Core lifecycle
    void initialize_core(std::size_t index, std::uint64_t entry_address);
    void start_secondary_core(std::size_t index, std::uint64_t entry_address);
    void halt_core(std::size_t index) noexcept;
    void resume_core(std::size_t index) noexcept;
    bool is_core_halted(std::size_t index) const noexcept;

    // Phase 48: PSCI service
    void enable_psci();
    PSCIService* get_psci_service() noexcept;
    const PSCIService* get_psci_service() const noexcept;

    // Cross-core event signaling (SEV / WFE)
    void broadcast_sev(std::size_t sender_core);

    // Cross-core TLB invalidations (TLBI *IS)
    void broadcast_tlbi_all(std::size_t sender_core);
    void broadcast_tlbi_va(std::size_t sender_core, std::uint64_t va, std::uint16_t asid, bool last_level);
    void broadcast_tlbi_vaa(std::size_t sender_core, std::uint64_t va, bool last_level);
    void broadcast_tlbi_asid(std::size_t sender_core, std::uint16_t asid);

    // Global Exclusive Monitor
    void register_exclusive_reservation(std::size_t core_id, std::uint64_t address, std::size_t size);
    bool check_and_clear_exclusive_reservation(std::size_t core_id, std::uint64_t address, std::size_t size);
    void invalidate_exclusive_reservations(std::size_t writer_core_id, std::uint64_t address, std::size_t size);
    // Used by the Memory store observer: every ordinary store invalidates any
    // overlapping reservation regardless of which core issued it. The caller
    // already holds the affected memory stripe's write lock.
    void invalidate_all_exclusive_reservations(std::uint64_t address, std::size_t size);
    void clear_exclusive_reservation(std::size_t core_id);

    // Deterministic round-robin scheduler (reference mode, unchanged policy).
    std::size_t step_round_robin();
    std::size_t run_deterministic(std::size_t max_total_instructions);

    // --- Phase 43: parallel engine -----------------------------------------
    // All guest stores are serialized through this mutex so that a
    // load-exclusive / store-exclusive pair, and each read-modify-write LSE
    // atomic, is atomic with respect to every other guest store.
    std::recursive_mutex& store_mutex() noexcept { return store_mutex_; }

    // True while worker threads exist. CPU timer/counter accesses consult this
    // to decide between the per-core deterministic counter and the cluster
    // shared atomic counter.
    bool is_parallel_enabled() const noexcept {
        return parallel_started_.load(std::memory_order_acquire);
    }

    // Cluster-shared guest counter used across the cluster.
    std::uint64_t shared_counter_load() const noexcept {
        return virtual_time_.get_physical_counter();
    }
    void shared_counter_store(std::uint64_t value) noexcept {
        virtual_time_.set_physical_counter(value);
    }
    void shared_counter_increment() noexcept {
        virtual_time_.increment_physical_counter();
    }

    // Spawns one worker per core. Idempotent. Enables concurrent guest-memory
    // locking. Must not be called concurrently with itself.
    void start_parallel();
    // Signals workers to exit and joins them. Idempotent. Returns guest memory
    // to the unresolved (deterministic) access path.
    void stop_parallel();
    bool parallel_started() const noexcept { return is_parallel_enabled(); }

    // Prevents workers from beginning a new run epoch. A run that is already
    // in progress is asked to stop; run_parallel returns normally afterwards.
    void pause_parallel();
    void resume_parallel();
    bool parallel_paused() const noexcept {
        return parallel_paused_.load(std::memory_order_acquire);
    }

    // Runs all cores in parallel until `max_total_instructions` guest
    // instructions have retired across the cluster (0 = until quiescence) or
    // until every runnable core is halted/waiting. Returns the number retired
    // by this call. A guest exception raised on a worker is rethrown here.
    std::size_t run_parallel(std::size_t max_total_instructions);

    // Number of worker-thread guest exceptions observed since construction.
    std::size_t worker_exception_count() const noexcept {
        return worker_exception_count_.load(std::memory_order_relaxed);
    }

private:
    struct GlobalExclusiveReservation {
        bool valid{false};
        std::uint64_t address{0};
        std::size_t size{0};
    };

    void worker_main(std::size_t index);
    bool any_core_runnable() const noexcept;

    std::unique_ptr<Memory> memory_;
    GICv3 gic_;
    std::vector<std::unique_ptr<CPU>> cores_;
    std::vector<GlobalExclusiveReservation> reservations_;
    std::size_t current_scheduled_core_{0};
    std::unique_ptr<PSCIService> psci_service_;

    // Phase 43 synchronization.
    std::recursive_mutex store_mutex_;
    mutable std::mutex reservations_mutex_;

    std::atomic<bool> parallel_started_{false};
    std::atomic<bool> parallel_paused_{false};
    VirtualTime virtual_time_;
    std::atomic<std::size_t> worker_exception_count_{0};

    // Worker lifecycle / epoch control.
    std::vector<std::thread> workers_;
    std::mutex control_mutex_;
    std::condition_variable control_cv_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> epoch_active_{false};
    std::atomic<std::size_t> epoch_budget_{0};
    std::atomic<std::size_t> epoch_instructions_{0};
    std::size_t workers_in_epoch_{0}; // guarded by control_mutex_
    std::exception_ptr worker_exception_{}; // guarded by control_mutex_
};

} // namespace winios
