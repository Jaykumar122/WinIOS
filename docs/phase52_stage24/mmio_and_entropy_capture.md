# MMIO, Entropy & Host Input Event Capture

## 1. Non-Deterministic Peripheral Inputs
Memory-Mapped I/O (MMIO) and hardware random number generation (RNDR) are primary sources of non-determinism in execution environments.
Normal guest RAM accesses (loads and stores to DRAM) are fully deterministic given identical CPU states and memory contents, and are **NOT** recorded in SNDL. Only accesses targeting device regions or entropy generators are recorded.

---

## 2. Event Registry
| Event Type | Value | Boundary | Description |
|---|---|---|---|
| `MmioReadResult` | 50 | `DuringInstruction` | MMIO load result: `payload1 = paddr`, `payload2 = value`, `payload3 = width_bytes`, `payload_size = width_bytes`. |
| `EntropyReadResult` | 51 | `DuringInstruction` | Hardware random number generation (`RNDR`): `payload1 = random_val`. |
| `HostInputInjected` | 52 | `AsynchronousInput` | Peripheral input injected from host (e.g. keyboard, UART, touch event): `source_id = device_id`, `payload1 = raw_data`. |

---

## 3. Byte Width and Addressing
For `MmioReadResult`:
- Supports 1, 2, 4, and 8-byte access widths.
- Captured at `DuringInstruction` boundary, identifying the originating `core_id` and instruction `pc`.
- Allows replay to inject exact device responses without simulating full device internal hardware state machines.
