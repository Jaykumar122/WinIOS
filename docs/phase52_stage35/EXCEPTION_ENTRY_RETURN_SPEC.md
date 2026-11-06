# Exception Entry, Vector Selection & Return Specification
**Architectural Exception Lifecycle in WinIOS**

---

## 1. Vector Offset Selection

The base address of the exception vector table is configured in `VBAR_EL1` (must be 2KB aligned, bits [10:0] == 0).
The vector offset is determined by the target Exception Level, the source Exception Level, and the stack pointer selection:

| Source EL | Source SP | Exception Type | Vector Table Offset |
|---|---|---|---|
| Current EL (EL1) | SP_EL0 | Synchronous | `+0x000` |
| Current EL (EL1) | SP_EL0 | IRQ / FIQ | `+0x080` / `+0x100` |
| Current EL (EL1) | SP_EL1 | Synchronous | `+0x200` |
| Current EL (EL1) | SP_EL1 | IRQ / FIQ | `+0x280` / `+0x300` |
| Lower EL (EL0, 64-bit) | SP_EL0 | Synchronous | `+0x400` |
| Lower EL (EL0, 64-bit) | SP_EL0 | IRQ / FIQ | `+0x480` / `+0x500` |
| Lower EL (32-bit AArch32) | Any | Any | `+0x600` / `+0x680` |

---

## 2. Exception Entry Architectural Lifecycle

When an exception is taken to EL1:
1. `PSTATE` is captured into `SPSR_EL1`:
   - `SPSR[31:28]` = `NZCV`
   - `SPSR[9:6]` = `DAIF` (all set to 1 upon entry to mask interrupts)
   - `SPSR[3:0]` = Source Execution Mode (`0b0000` for EL0t, `0b0100` for EL1t, `0b0101` for EL1h)
2. Return address is captured into `ELR_EL1`:
   - For synchronous faults (Data Abort, Instruction Abort): address of faulting instruction.
   - For `SVC`: address of next instruction (`PC + 4`).
3. Syndrome is written to `ESR_EL1`.
4. If fault generates address, `FAR_EL1` is populated.
5. `PC` is updated to `VBAR_EL1 + VectorOffset`.
6. Execution Level transitions to EL1; `PSTATE.SP` is set to 1 (SP_EL1).

---

## 3. Exception Return (`ERET`) Lifecycle

Executing `ERET` at EL1:
1. Validates `SPSR_EL1.M[3:0]` target mode:
   - Target mode must be EL0 (`0b0000`) or EL1 (`0b0100`, `0b0101`).
   - Transition to higher EL (EL2/EL3) is architecturally illegal.
2. If illegal:
   - Sets `PSTATE.IL = 1`.
   - On subsequent instruction fetch, CPU raises `IllegalExecutionState` synchronous exception.
3. If legal:
   - `PSTATE` is restored from `SPSR_EL1` (`NZCV`, `DAIF`).
   - `PC` is restored from `ELR_EL1`.
   - Current EL is updated to target EL.
