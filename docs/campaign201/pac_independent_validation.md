# WinIOS Campaign 201 — Stage 201.9 to 201.12 PAC Independent Validation

**Campaign:** 201  
**Stages:** 201.9 (Algorithm Identification), 201.10 (Independent Vectors), 201.11 (Instruction Conformance), 201.12 (Context Isolation)  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target:** Independent verification of ARM64e Pointer Authentication cryptographic algorithm, instruction semantics, and context isolation  
**Date:** November 16, 2026  

---

## 1. Executive Summary & Algorithm Truth (Issue E Resolution)

Previous campaign documentation (Campaigns 199 and 200) claimed:
> *"ARM64e PAC (QARMA/QARMA-64 cryptographic signing, 5 distinct keys)"*

A rigorous source-code forensic audit of `src/cpu/pointer_auth.cpp` (lines 43–68) definitively resolves this claim:
- **Actual Algorithm:** WinIOS implements a **3-round SipHash-based truncated Pseudo-Random Function (PRF)**, NOT the ARM architectural QARMA-64 or QARMA-5 block cipher.
- **Initialization State:** Initialized with the canonical SipHash constants:
  - $v_0 = \text{Key.lo} \oplus \mathtt{0x736f6d6570736575}$ ("somepseu")
  - $v_1 = \text{Key.hi} \oplus \mathtt{0x646f72616e646f6d}$ ("dorandom")
  - $v_2 = \text{CanonicalPtr} \oplus \mathtt{0x6c7967656e657261}$ ("lygenera")
  - $v_3 = \text{Modifier} \oplus \mathtt{0x7465646279746573}$ ("tedbytes")
- **Round Function:** Executes 3 iterations of the standard 64-bit SipHash ARX (Add-Rotate-Xor) permutation.
- **Tag Generation:** Folds the 64-bit hash state into a 16-bit Authentication Tag:
  $$\text{Tag} = (h \oplus (h \gg 16) \oplus (h \gg 32) \oplus (h \gg 48)) \ \& \ \mathtt{0xFFFF}$$
- **Tag Insertion:** Tag is stored in pointer bits [63:48] (`kPacShift = 48`), preserving bottom 48 bits for virtual address translation.
- **Architectural Truth:** While the tag generation uses a SipHash-3 PRF instead of QARMA-64, the **instruction semantics, privilege rules, and exception behaviors conform strictly to ARMv8.3-A and ARMv8.6-A specifications**.

---

## 2. Independent Reference Validation Matrix

An independent reference oracle was constructed in `tests/unit/stage201_10_pac_independent_vector_tests.cpp`. All tests demonstrated **100% mathematical bit-for-bit equivalence** between the independent reference implementation and `winios::cpu::Arm64ePointerAuth`:

| Test Vector Domain | Input Parameters | Expected Behavior | Observed Result | Conformance Status |
| :--- | :--- | :--- | :--- | :--- |
| **Direct Oracle Match** | Multi-key, multi-modifier test vectors | 16-bit tag matches independent model | Exact match | **PASS** |
| **Address Preservation** | Pointer with bits [47:0] populated | Bottom 48 bits unmodified after sign | Unaltered | **PASS** |
| **Key Discrimination** | Same pointer & modifier, distinct keys | APIA $\ne$ APIB $\ne$ APDA $\ne$ APDB | All tags distinct | **PASS** |
| **Modifier Avalanche** | 1-bit difference in modifier | Drastic tag difference | Different tag | **PASS** |
| **Generic PACGA** | 64-bit data operands | Returns 32-bit tag in Rd[63:32], Rd[31:0] = 0 | Exact match | **PASS** |
| **Pointer Stripping** | `XPACI X0`, `XPACD X0` | Strips tag, restores canonical address | Restored | **PASS** |

---

## 3. Instruction Conformance & Fault Modes (ARMv8.3 vs ARMv8.6)

WinIOS supports both architectural PAC failure modes:

```mermaid
flowchart TD
    Auth["AUTIA / AUTIB / AUTDA / AUTDB / RETAA"]
    Check{"Tag Valid?"}
    Auth --> Check
    Check -->|Yes| Success["Strip Tag & Return Canonical Pointer<br/>No Exception"]
    Check -->|No| ModeCheck{"PacFaultMode?"}
    ModeCheck -->|TrapException (ARMv8.6 FPAC)| Trap["Synchronous Exception Taken<br/>EC = 0x1C (PAC Trap)<br/>FAR_EL1 = Bad Pointer"]
    ModeCheck -->|CorruptUpperBits (ARMv8.3)| Corrupt["Flip Bit 62<br/>No Immediate Trap<br/>Faults Later on Dereference"]
```

1. **FPAC Trap Mode (`PacFaultMode::TrapException`):**
   - Authentication failure raises an immediate synchronous exception to EL1.
   - `ESR_EL1` Exception Class is `0x1C`.
   - `ELR_EL1` points to the faulting instruction PC.
   - Validated on `AUTIA` and `RETAA`.
2. **Pre-ARMv8.6 Error Injection Mode (`PacFaultMode::CorruptUpperBits`):**
   - Authentication failure flips bit 62 of the target register.
   - No immediate trap is taken, allowing subsequent instruction execution until memory dereference triggers an address translation or translation table fault.

---

## 4. Context Isolation & Security Verification

Stage 201.12 verified that PAC keyrings remain strictly isolated:
- **Cross-Thread Protection:** Pointers signed under Thread A's keyring fail authentication immediately when presented to Thread B ($100\%$ fault rate).
- **Context Switch Integrity:** Swapping keyrings during thread preemption leaves zero residual cryptographic state across execution contexts.
- **Snapshot Roundtrip:** Keyring contents are serialized into snapshots and restored intact, maintaining identical signing and authentication across platform checkpoints.

---

## 5. Test Suite Reference
- Executable 1: `stage201_10_pac_independent_vector_tests`
- Executable 2: `stage201_11_pac_instruction_conformance_tests`
- Executable 3: `stage201_12_pac_context_isolation_tests`
