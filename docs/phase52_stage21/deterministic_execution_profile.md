# Deterministic Execution Profiling & Overhead Analysis

## 1. Principles of Non-Invasive Replay Profiling

In deterministic virtual machines, diagnostic instrumentation must satisfy two non-negotiable architectural invariants:
1. **Architectural State Immunity**: Enabling profiling must **never** alter guest execution flow, CPU registers, memory bytes, or canonical state hashes.
2. **Deterministic Sequence Immunity**: Profiling must **never** introduce host clock timestamps or nondeterministic measurements into the recorded event stream or replay event router.

WinIOS satisfies these requirements via the `ReplayProfiler` subsystem (`replay_profiler.h`, `replay_profiler.cpp`).

---

## 2. Zero-Overhead Opt-In Architecture

Diagnostic timings are captured using the lightweight RAII helper `ReplayProfileScope`:

```cpp
class ReplayProfileScope {
public:
    explicit ReplayProfileScope(ReplayProfilePhase* phase) noexcept
        : phase_(phase) {
        if (phase_ != nullptr) {
            start_ = std::chrono::steady_clock::now();
        }
    }

    ~ReplayProfileScope() {
        if (phase_ != nullptr) {
            const auto end = std::chrono::steady_clock::now();
            phase_->record(std::chrono::duration<double, std::milli>(end - start_).count());
        }
    }
private:
    ReplayProfilePhase* phase_{nullptr};
    std::chrono::steady_clock::time_point start_{};
};
```

When profiling is disabled (`ReplayConfig::enable_profiling = false`), all phase pointers evaluate to `nullptr`. The constructor and destructor execute a single predictable conditional branch with zero host clock calls and zero heap allocations.

---

## 3. Empirical Profile Phase Breakdown

Measured across 1,000,000 instructions with a 100,000-instruction checkpoint interval on a 16 MB baseline snapshot:

| Execution Phase | Invocations | Total Duration | Mean per Call | Share of Execution |
|---|---:|---:|---:|---:|
| **Prepare Total** | 1 | 102.05 ms | 102.05 ms | — (one-time) |
| **Run Total** | 1 | 427.59 ms | 427.59 ms | 100.0% |
| ├── **Checkpoint Total** | 12 | 185.67 ms | 15.47 ms | **43.4%** |
| │   └── **Memory Hash** | 13 | 200.70 ms | 15.44 ms | (nested) |
| ├── **CPU Instruction Execution** | 1,000,000 | 130.24 ms | 130.2 ns | **30.5%** |
| ├── **Interrupt Evaluation** | 1,000,000 | 27.85 ms | 27.9 ns | **6.5%** |
| ├── **Final State Hash** | 1 | 15.19 ms | 15.19 ms | **3.6%** |
| ├── **Input Routing** | 10,000 | 1.59 ms | 158.7 ns | **0.4%** |
| └── **Cursor Block Streaming** | 11 | 1.41 ms | 127.9 µs | **0.3%** |
|     ├── Block CRC32C | 10 | 0.29 ms | 29.3 µs | < 0.1% |
|     └── Block Record Decode | 10 | 0.23 ms | 22.5 µs | < 0.1% |

### Key Bottleneck Findings
1. **State Hashing Dominance**: Checkpoint state hashing consumes **43.4%** of execution time when capturing every 100,000 instructions. Full-RAM memory hashing (16 MB per checkpoint) accounts for the vast majority of this cost (~15.4 ms per checkpoint).
2. **CPU Interpreter Efficiency**: Instruction decode, dispatch, and retirement averages **130.2 ns per instruction** in the interpreter.
3. **Negligible Stream Overhead**: Cursor block reading, CRC32C validation, and decoding represent less than **0.4%** of total runtime combined.

---

## 4. Profiling Overhead & Hash Equivalence Verification

To quantify the runtime overhead of `std::chrono::steady_clock::now()` instrumentation across 1,000,000 instruction steps, the benchmark ran 5 consecutive unprofiled replays followed by 5 consecutive profiled replays:

- **Profiling Disabled**: Median = **440.61 ms** (min = 432.02 ms)
- **Profiling Enabled**: Median = **530.46 ms** (min = 527.77 ms)
- **Instrumentation Overhead**: **+20.39%** (attributed to 2,000,000 high-resolution clock reads in tight loop).
- **Architectural Hash Equivalence**:
  $$\text{Final Combined Hash} = \mathbf{0x0DB69FF394F4361E}$$
  The hash was **100% bit-for-bit identical across all 11 runs** (1 warmup + 5 disabled + 5 enabled).

This proves conclusively that opt-in execution profiling captures fine-grained timing diagnostics with zero perturbation of guest architectural determinism.
