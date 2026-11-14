# Stage 196.5 — DMB, DSB & ISB Semantics

## 1. Architectural Decoding & Implementation
- **Instruction Encodings (ARM DDI 0487):**
  - Group: `op1 = 3`, `crn = 3`, `rt = 31` (XZR)
  - `op2 = 4`: `DSB <opt>`
  - `op2 = 5`: `DMB <opt>`
  - `op2 = 6`: `ISB {<opt>}`
- **Option Field (`CRm`):**
  - `0b0001` (1): `OSHLD` (Outer Shareable, Loads only)
  - `0b0010` (2): `OSHST` (Outer Shareable, Stores only)
  - `0b0011` (3): `OSH`   (Outer Shareable, Loads & Stores)
  - `0b0101` (5): `NSHLD` (Non-shareable, Loads only)
  - `0b0110` (6): `NSHST` (Non-shareable, Stores only)
  - `0b0111` (7): `NSH`   (Non-shareable, Loads & Stores)
  - `0b1001` (9): `ISHLD` (Inner Shareable, Loads only)
  - `0b1010` (10): `ISHST` (Inner Shareable, Stores only)
  - `0b1011` (11): `ISH`   (Inner Shareable, Loads & Stores)
  - `0b1101` (13): `LD`    (Full system, Loads only)
  - `0b1110` (14): `ST`    (Full system, Stores only)
  - `0b1111` (15): `SY`    (Full system, Loads & Stores)
- **Modeled Semantics in Emulator:**
  - Store-only variants (`ST`, `ISHST`, `OSHST`, `NSHST`): Evaluated with release fence semantics (`std::atomic_thread_fence(memory_order_release)`).
  - Load-only variants (`LD`, `ISHLD`, `OSHLD`, `NSHLD`): Evaluated with acquire fence semantics (`std::atomic_thread_fence(memory_order_acquire)`).
  - Full barrier variants (`SY`, `ISH`, `OSH`, `NSH`): Evaluated with sequentially consistent fence semantics (`std::atomic_thread_fence(memory_order_seq_cst)`).
  - `DSB` and `ISB`: Enforce instruction retirement and synchronization barriers.

## 2. Evidence Classification
`GUEST_MEMORY_ORDER`, `HOST_MODEL`.
Verified via `stage196_5_barrier_tests`.
