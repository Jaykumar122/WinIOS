#include "sndl/sndl_source_hooks.h"
#include <atomic>

namespace winios::sndl {

namespace {
std::atomic<SndlRecorder*> g_active_recorder{nullptr};
} // namespace

void set_active_recorder(SndlRecorder* recorder) noexcept {
    g_active_recorder.store(recorder, std::memory_order_release);
}

SndlRecorder* get_active_recorder() noexcept {
    return g_active_recorder.load(std::memory_order_acquire);
}

} // namespace winios::sndl
