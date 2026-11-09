#include "sndl/sndl_source_hooks.h"
#include <atomic>

namespace winios::sndl {

namespace {
std::atomic<SndlRecorder*> g_active_recorder{nullptr};
thread_local SndlRecorder* thread_recorder = nullptr;
thread_local bool thread_override = false;
} // namespace

void set_active_recorder(SndlRecorder* recorder) noexcept {
    g_active_recorder.store(recorder, std::memory_order_release);
}

SndlRecorder* get_active_recorder() noexcept {
    return thread_override ? thread_recorder
        : g_active_recorder.load(std::memory_order_acquire);
}

ScopedThreadRecorder::ScopedThreadRecorder(SndlRecorder* recorder) noexcept
    : previous_(thread_recorder), previous_override_(thread_override) {
    thread_recorder = recorder;
    thread_override = true;
}

ScopedThreadRecorder::~ScopedThreadRecorder() {
    thread_recorder = previous_;
    thread_override = previous_override_;
}

} // namespace winios::sndl
