# Deterministic Multi-Core Scheduler Architecture Comparison

## 1. Problem Statement: Decoupling from Host OS Threading

In a deterministic multicore virtual machine, the relative scheduling of virtual CPU cores cannot be left to the host operating system thread scheduler (`std::jthread` / Windows thread pool). Because host OS preemption, quantum allocation, and core migration are nondeterministic, an identical replay requires an explicit, reproducible deterministic scheduler.

This document evaluates four architectural scheduling paradigms and recommends a concrete path for future WinIOS phases.

---

## 2. Comparative Evaluation Matrix

| Criterion | 1. Single-Instruction Round-Robin | 2. Fixed Instruction Quantum | 3. Event-Driven Scheduling | 4. Synchronization-Point (CREW) |
|---|---|---|---|---|
| **Scheduling Unit** | 1 instruction / core | Fixed quantum (e.g. 10,000 inst) | Distance to next recorded event | Free execution between locks/barriers |
| **Determinism Guarantee** | **Absolute** | **Absolute** | **Absolute** | Conditional on race-free tracking |
| **Host Parallelism** | **Zero** (single host thread) | **Zero** or chunked barriers | **Zero** (serialized) | **High** (multi-threaded host workers) |
| **Throughput Overhead** | **Extreme** ($10\times$–$20\times$ slowdown) | **Low** (< 15% slowdown) | **Moderate** ($2\times$ slowdown) | **High complexity** (page protection faults) |
| **Starvation Risk** | None | Spinlock starvation if quantum too large | Event starvation on compute loops | High lock contention |
| **Interpreter Compatibility** | Direct integration | Direct integration | Requires event lookahead | Requires host MMU page table hooks |
| **SNDL Recording Size** | Zero additional tokens | Zero tokens (implicit) | 1 token per schedule point | High (1 token per memory barrier) |
| **Implementation Complexity**| Low | **Low-Moderate** | Moderate | Very High |

---

## 3. Deep Dive into Architectural Options

### Option 1: Single-Instruction Round-Robin
- **Mechanics**: Core 0 steps 1 instruction $\rightarrow$ Core 1 steps 1 instruction $\rightarrow$ Core 2 steps 1 instruction $\dots$
- **Pros**: Perfectly eliminates inter-core race conditions; lockstep memory visibility.
- **Cons**: Severe cache thrashing on the host CPU; destroys interpreter loop unrolling and branch prediction; achieves under 0.5 MIPS cluster-wide.

### Option 2: Fixed Instruction Quantum Scheduling (Recommended)
- **Mechanics**: Each core executes an exact, deterministic instruction budget $Q$ (e.g. $Q = 10,000$ instructions) in fixed round-robin order.
- **Barrier Truncation**: If a core executes a barrier (`DSB`, `ISB`), wait-for-interrupt (`WFI`), or receives an SGI, its current quantum terminates early, passing ownership to the next core.
- **Pros**: Near-native interpreter execution speed during compute loops; zero additional log records needed for scheduling decisions; trivial to reproduce across runs.
- **Cons**: Cores do not execute simultaneously on separate host threads.

### Option 3: Event-Driven Scheduling
- **Mechanics**: Core $k$ executes until its instruction count reaches the timestamp of its next pending SNDL event.
- **Pros**: Matches event arrival boundaries precisely.
- **Cons**: Cores with sparse events run ahead by millions of instructions, creating massive inter-core clock skew and starvation for polling loops.

### Option 4: Synchronization-Point / CREW Scheduling
- **Mechanics**: Cores execute concurrently on multiple host OS threads. Guest physical memory pages are marked Read-Only or No-Access using host MMU page-fault protection (`VirtualProtect`). A write fault traps into the hypervisor, records a memory ownership transfer, and synchronizes the writing core.
- **Pros**: True host multi-threaded parallelism.
- **Cons**: Massive host OS page-fault overhead (up to 50 µs per fault); requires hypervisor-level memory management hooks; highly fragile.

---

## 4. Recommended Initial SMP Architecture

WinIOS recommends **Option 2: Fixed Instruction Quantum Scheduling with Adaptive Barrier Truncation**:
1. **Default Quantum Size**: $Q = 10,000$ retired instructions.
2. **Adaptive Yield Triggers**:
   - `WFI` / `WFE`: Core immediately relinquishes remaining quantum and enters suspended state until an interrupt awakens it.
   - `DSB` / `ISB`: Core yields to allow peer cores to observe prior memory updates.
   - `ICC_SGI1R_EL1`: Sending an IPI flushes current quantum and triggers target core evaluation.
3. **Execution Mode**: Single host thread iterating across virtual cores sequentially in round-robin order.

This model provides 100% deterministic reproducibility, zero host thread race hazards, excellent interpreter throughput, and zero requirement for complex host page-fault interception.
