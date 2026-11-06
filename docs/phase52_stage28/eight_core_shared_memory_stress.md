# Eight-Core Shared Memory Stress
## Coherent Memory Access, Barrier Semantics and Interleaved Access Verification

---

## 1. Shared Memory Model

`SmpSharedMemory` coordinates memory operations across all 8 virtual cores into a single physical guest RAM backing store:

1. **Deterministic Single-Threaded Interleaving**:
   - Because the scheduler runs on a single host thread, guest core reads and writes are interleaved according to quantum boundaries and scheduling decisions deterministically.
   - Core ID tagging (`read_u64(core_id, addr)`, `write_u32(core_id, addr, val)`) provides complete attribution for auditing and event capture.

2. **Sequential Consistency Across Quantum Slices**:
   - Within a quantum slice, core $i$ sees its own writes immediately.
   - When core $i$ yields or exhausts its quantum, its memory modifications are permanently in the backing store and immediately visible to core $j$ in subsequent slices.

---

## 2. Eight-Core Shared Memory Stress Workload (Workload 2)

- **Scenario**: 8 virtual cores concurrently accessing shared memory counters at `0x2000 + i * 64`.
- **Interleaving Pattern**: Each core executes `ADD` operations, stores results to distinct cache lines, and validates read-backs from neighboring core lines.
- **Verification**: Zero data corruptions, zero race conditions, identical memory state across repeated executions.
