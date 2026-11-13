<USER_REQUEST>
# WINIOS MASTER ENGINEERING PROMPT
# CAMPAIGN 193 — ARM64 EXCEPTION ARCHITECTURE, EL0/EL1 PRIVILEGE TRANSITIONS & SYNTHETIC KERNEL EXECUTION

**Campaign:** 193  
**Stages:** 193.0–193.19 (20 stages)  
**Repository:** `D:\WinISO26\WinIOS\`  
**Host OS:** Windows 11 x64  
**Target architecture:** ARM64 / ARM64e  
**Target device identity:** iPhone 15 — `iPhone15,4 / D37AP / T8120`  
**Toolchain:** Visual Studio 18 2026, MSVC, C++20  
**Compiler policy:** `/W4 /WX /permissive-`  
**Baseline commit:** `9ff79b3`  
**Branch:** `campaign-191r`

---

## 1. PRIMARY OBJECTIVE

Continue WinIOS from the reported completion of Campaign 192.

Campaign 192 established a Darwin-oriented ARM64 EL0 userspace execution environment with:

- Mach-O loading and validation.
- dyld-style image loading, rebasing, binding and chained fixups.
- A 16KB-granule Stage-1 MMU implementation.
- Darwin BSD syscall and Mach trap emulation.
- File descriptors, VFS, memory mapping and TLS.
- Guest-side execution of independently compiled open-source Mach-O artifacts.
- ARM64 instruction differential testing.
- Execution tracing and reproducibility infrastructure.

Campaign 193 must advance the architecture toward a real ARM64 privilege and exception model.

The main objective is to implement and validate a **guest-executed EL0/EL1 transition architecture**, including exception entry, exception vectors, syndrome reporting, ERET, stack switching, interrupt delivery, address-space protection and a bounded synthetic kernel execution environment.

The work must be based on actual guest CPU state transitions wherever possible.

Do not implement an architecture that merely invokes host C++ callbacks and labels them as kernel execution.

### Required distinction

The campaign must distinguish these execution categories:

1. **HOST_MODEL:** Host-side implementation or simulation.
2. **GUEST_EL0:** Instructions executed by the emulated guest at EL0.
3. **GUEST_EL1:** Instructions executed by the emulated guest at EL1.
4. **GUEST_EXCEPTION:** A guest instruction causes an architectural exception and enters a guest exception vector.
5. **GUEST_INTERRUPT:** A guest interrupt is delivered through the guest interrupt architecture.
6. **SYNTHETIC_KERNEL:** Independently compiled guest kernel test code executes at EL1.
7. **APPLE_KERNEL:** Genuine Apple/XNU kernel execution, requiring separate authentic evidence.

Only use `APPLE_KERNEL` if actual genuine XNU execution is demonstrated. Campaign 193 must not claim this achievement merely because synthetic kernel tests pass.

---

## 2. NON-NEGOTIABLE REPOSITORY SAFETY

Repository path:

`D:\WinISO26\WinIOS\`

Before changing anything:

- Inspect the current Git branch, status and latest commits.
- Verify the expected baseline commit.
- Inspect all existing uncommitted and untracked files.
- Identify the current CPU, MMU, exception, interrupt, timer, GIC and runtime implementations.
- Read existing architecture documentation and campaign manifests.
- Establish a reproducible baseline.

### Protected files

Do not modify:

- `D:\WinISO26\WinIOS_README.md`
- `D:\WinISO26\WinIOS\docs\progress.md`

Do not create the following file if it does not already exist:

- `D:\WinISO26\WinISO26_README.md`

### Prohibited operations

Never execute:

- `git reset`
- `git clean`
- `git stash`
- Destructive checkout or restore operations.
- Commands that overwrite unrelated user modifications.
- Automatic remote pushes.

Preserve every existing user modification, untracked file, branch and working-tree change.

Do not delete or regenerate existing test artifacts simply to obtain passing results.

### Inferno research checkout

Leave this directory completely untouched:

`D:\WinISO26\Inferno-Research\Inferno`

Expected pinned commit:

`cc4302a99167abec69b714cfd00c38caece7e7de`

Verify its status without modifying it.

---

## 3. ENGINEERING RULES

1. Windows 11 is the development and execution host. Do not replace the project with Linux, WSL, Android emulation or browser-based simulation.
2. Use existing C++20 architecture and MSVC toolchain.
3. Preserve compatibility with existing ELF, Mach-O, Darwin runtime and replay functionality.
4. Reuse existing components when technically correct; avoid unnecessary parallel implementations.
5. Every new architectural behavior must have focused tests.
6. Tests must exercise real guest state transitions rather than only host helper functions.
7. Validate architectural assumptions against reliable ARM documentation and source code.
8. Record source references and explain any implementation deviations.
9. Do not fabricate Apple hardware specifications, undocumented register values or device addresses.
10. Do not silently turn unsupported instructions or architectural states into successful no-ops.
11. Preserve deterministic replay wherever possible.
12. Do not weaken, remove or bypass existing tests to achieve a green regression.

### Reference policy

Use authoritative ARM Architecture Reference Manual material and publicly available architectural documentation.

For Apple/XNU-specific behavior, use public Apple open-source source code and clearly identify which revision or source file supports the behavior.

Record:

- Reference name.
- URL or repository path.
- Relevant register, section or source location.
- Implementation decision.
- Any unresolved ambiguity.

Do not invent citations.

---

# CAMPAIGN 193 — IMPLEMENTATION STAGES

## Stage 193.0 — Baseline Audit and Architectural Inventory

Inspect the complete existing architecture before implementation.

Audit:

- CPU execution loop.
- Current exception representation.
- PSTATE and SPSR handling.
- Current EL0 execution.
- System-register implementation.
- MMU and translation tables.
- TLB implementation.
- GIC and interrupt controller.
- Timer implementation.
- SMP scheduling and deterministic execution.
- Snapshot and replay infrastructure.
- Existing guest execution diagnostics.
- Existing test registration and build configuration.

Produce:

`docs/campaign193/stage193_0_baseline_audit.md`

Include:

- Current supported architectural behavior.
- Missing behavior.
- Existing assumptions requiring validation.
- Existing components suitable for reuse.
- Architectural risks.
- Baseline Debug and Release test results.

Do not start feature implementation until the baseline has been recorded.

**Acceptance:** Complete inventory and reproducible baseline.

## Stage 193.1 — EL0/EL1 Execution-Level Architecture

Implement or harden the guest execution-level representation.

Requirements:

- Explicit CurrentEL representation.
- Valid EL0 and EL1 execution states.
- Correct privilege checks.
- Appropriate PSTATE handling.
- Defined exception return state.
- Invalid execution-level handling.
- Consistent integration with CPU execution.

Do not use a host-side boolean as the only representation of privilege.

Ensure that instruction execution, memory access and system-register access all consult the architectural execution state.

Tests must prove that the same operation can behave differently at EL0 and EL1 where the architecture requires it.

**Acceptance:** Guest privilege state is explicit and consistently enforced.

## Stage 193.2 — Exception Entry Architecture

Implement a centralized guest exception-entry mechanism.

Support the architectural exception classes required by the current implementation:

- Synchronous exceptions.
- IRQ exceptions.
- FIQ exceptions.
- Relevant instruction and data aborts.

Exception entry must preserve the required guest state, including:

- Previous execution state.
- Return address.
- Saved processor status.
- Exception syndrome where applicable.
- Exception origin.
- Relevant fault address.

Do not duplicate exception-entry logic across individual instruction handlers.

**Acceptance:** All supported exceptions enter through a common, testable architectural path.

## Stage 193.3 — Exception Syndrome Register

Implement a structured exception syndrome model.

Cover the applicable architectural fields:

- Exception Class (EC).
- Instruction Length (IL).
- Instruction Specific Syndrome (ISS).
- Data Fault Status Code (DFSC).
- Instruction Fault Status Code (IFSC).
- Write/not-write information.
- Access-size information.
- Register information where architecturally applicable.

Implement relevant guest-visible system-register access for:

- `ESR_EL1`
- `FAR_EL1`
- `ELR_EL1`
- `SPSR_EL1`

Do not populate fields with guessed values.

Create tests for representative synchronous exception classes and invalid-access conditions.

**Acceptance:** Guest exception syndrome values match the defined architectural behavior for tested cases.

## Stage 193.4 — Vector Table Architecture

Implement guest exception-vector selection using `VBAR_EL1`.

Support the relevant vector groups:

- Current EL with SP0.
- Current EL with SPx.
- Lower EL using AArch64.
- Lower EL using AArch32 only if that execution state is actually supported; otherwise reject it explicitly.

Implement vector offset selection for:

- Synchronous exceptions.
- IRQ.
- FIQ.
- SError where supported.

Verify vector alignment and invalid vector configuration behavior.

Tests must execute guest instructions that trigger exceptions and confirm the actual guest PC reaches the expected vector.

**Acceptance:** Exception vector dispatch is performed through guest architectural state.

## Stage 193.5 — ERET and Exception Return

Implement `ERET` with architectural validation.

Requirements:

- Restore the saved PSTATE from `SPSR_EL1`.
- Restore PC from `ELR_EL1`.
- Restore the correct execution level.
- Validate supported return states.
- Handle invalid exception returns through appropriate exception behavior.
- Preserve deterministic CPU state.

Test:

1. EL0 to EL1 through a synchronous exception.
2. EL1 exception-handler execution.
3. EL1 to EL0 using ERET.
4. Correct return PC.
5. Correct register preservation.
6. Invalid return-state rejection.

The test must execute the handler and ERET as guest instructions.

**Acceptance:** A guest can enter EL1, execute a handler and return to EL0.

## Stage 193.6 — System Register Access and Privilege Enforcement

Audit and extend the system-register decoder.

Cover relevant registers:

- `CurrentEL`
- `VBAR_EL1`
- `ESR_EL1`
- `ELR_EL1`
- `SPSR_EL1`
- `FAR_EL1`
- `SCTLR_EL1`
- `TTBR0_EL1`
- `TTBR1_EL1`
- `TCR_EL1`
- `MAIR_EL1`
- `TPIDR_EL0`
- `TPIDR_EL1`
- `SP_EL0`
- `SP_EL1`

Only implement registers whose semantics can be correctly represented.

Requirements:

- Correct access-level checks.
- Read/write behavior.
- Architectural reset values where established.
- Defined handling of unsupported registers.
- No silent acceptance of invalid accesses.

**Acceptance:** System-register access is privilege-aware and covered by guest execution tests.

## Stage 193.7 — MMU Translation and Fault Integration

Audit the existing 16KB Stage-1 translation implementation.

Do not assume the existing translation geometry is correct merely because earlier tests passed.

Validate:

- Translation granule.
- Address-size configuration.
- Translation table level selection.
- Block and page descriptor interpretation.
- Descriptor permissions.
- Access flag behavior.
- Privileged and unprivileged access.
- Execute-never behavior.
- Read/write restrictions.
- Translation fault levels.
- Table-walk failures.

Integrate translation failures with the exception architecture.

Implement appropriate guest-visible fault state:

- `FAR_EL1`
- `ESR_EL1`

Ensure that failed memory accesses do not partially modify memory when the architectural operation requires all-or-nothing behavior.

**Acceptance:** Guest memory faults produce architectural exceptions with verified syndrome information.

## Stage 193.8 — TLB and Translation Invalidation

Implement or validate the supported TLB behavior.

Review relevant operations:

- `TLBI VMALLE1`
- `TLBI VAE1`
- `TLBI ASIDE1`
- Other required variants only where their semantics are established.

Validate:

- Address-specific invalidation.
- ASID-aware invalidation.
- Global versus non-global mappings.
- Correct synchronization with translation-table updates.
- Deterministic replay behavior.

Do not treat all TLBI operations as equivalent full-cache flushes.

Create tests where stale translations would cause an observable failure if invalidation were incorrect.

**Acceptance:** Supported TLB operations produce distinguishable and correct behavior.

## Stage 193.9 — ASID and Address-Space Isolation

Implement a bounded address-space and ASID model.

Requirements:

- ASID representation.
- Address-space association.
- Translation context selection.
- Isolation between two guest address spaces.
- TLB entries associated with the correct address-space identity.
- Context-switch behavior.
- ASID invalidation behavior.

Create two guest processes or test contexts with deliberately conflicting virtual addresses.

Prove that each context sees its own mapped physical memory.

**Acceptance:** Cross-address-space memory isolation is demonstrated by guest execution tests.

## Stage 193.10 — EL0/EL1 Memory Protection

Implement and test privilege-aware memory permissions.

Cover:

- EL0-readable memory.
- EL0-writable memory.
- EL0-executable memory.
- EL1-only memory.
- Read-only pages.
- Execute-never pages.
- W^X policy.
- Permission changes.
- Translation fault delivery.

Ensure that guest EL0 cannot access EL1-only memory.

Ensure that guest EL1 can access permitted kernel memory.

Do not implement a protection bypass for test convenience.

**Acceptance:** Guest privilege separation is demonstrated with actual memory-access attempts.

## Stage 193.11 — Stack Management and Context Preservation

Implement guest stack selection and preservation across exception entry and return.

Cover:

- `SP_EL0`.
- `SP_EL1`.
- SP selection.
- Stack alignment.
- Exception stack switching.
- Register preservation.
- Nested handler behavior where supported.

Test:

- EL0 stack remains intact after exception entry.
- EL1 handler uses the expected stack.
- ERET restores the correct user stack.
- Stack-alignment violations produce the appropriate result.
- Context switching does not corrupt saved register state.

**Acceptance:** Guest stack and CPU state survive complete privilege transitions.

## Stage 193.12 — GIC Interrupt Delivery Architecture

Audit the existing interrupt controller implementation before extending it.

Validate the actual supported GIC model and distinguish:

- Distributor behavior.
- CPU interface behavior.
- Interrupt pending state.
- Interrupt enable state.
- Priority handling.
- Interrupt acknowledgment.
- End-of-interrupt behavior.
- Active interrupt state.
- Per-core interrupt state.

Do not claim full GICv3/GICv4 support unless the corresponding architecture is actually implemented.

Use the current project model where valid, and explicitly document any intentionally simplified behavior.

**Acceptance:** A guest interrupt can be raised, recognized, acknowledged and completed through the implemented guest interrupt interface.

## Stage 193.13 — IRQ/FIQ Exception Routing

Connect interrupt delivery to the new exception-entry architecture.

Requirements:

- IRQ exception entry.
- FIQ exception entry where supported.
- PSTATE interrupt masks.
- Exception routing configuration where implemented.
- Correct vector selection.
- Saved return state.
- Interrupt acknowledgment and completion.
- No duplicate interrupt delivery.
- Deterministic event ordering.

Create guest programs that enable interrupts, wait for an interrupt, execute the handler and return.

Do not simulate the entire process through a host callback.

**Acceptance:** Interrupt handlers execute at guest EL1 and return correctly.

## Stage 193.14 — Virtual Timer Interrupts

Integrate the existing timer subsystem with guest exception delivery.

Audit:

- Timer counter behavior.
- Compare values.
- Timer enable state.
- Timer mask state.
- Expiration.
- Interrupt pending state.
- Interrupt acknowledgment.
- Timer behavior across CPU context switches.

Implement only architecturally supported timer registers and behaviors.

Test:

1. Guest programs a timer.
2. Timer expires.
3. IRQ becomes pending.
4. Guest interrupt is delivered.
5. Guest EL1 timer handler executes.
6. Guest acknowledges or masks the interrupt.
7. Guest returns successfully.

**Acceptance:** Timer-driven guest interrupt execution works deterministically.

## Stage 193.15 — Exception Nesting and SMP Delivery

Extend exception handling to bounded nested and multi-core execution.

Requirements:

- Per-core exception state.
- Per-core exception stacks.
- Correct CPU-local interrupt state.
- Nested exception handling where supported.
- Interrupt priority and masking.
- Inter-core interrupt delivery.
- Correct CPU context preservation.
- Deterministic event ordering.
- No shared mutable exception state across cores unless architecturally required.

Test at least two cores with independent guest exception activity.

If existing SMP support cannot safely support a behavior, document the limitation instead of fabricating a successful result.

**Acceptance:** Supported multi-core exception and interrupt behavior passes deterministic tests.

## Stage 193.16 — Synthetic EL1 Kernel Runtime

Build a minimal, independently compiled ARM64 guest kernel test environment.

Use the existing Windows-hosted Zig/LLVM or supported cross-compilation toolchain.

The artifact must contain genuine ARM64 machine instructions and execute inside the existing WinIOS guest CPU.

Implement a bounded synthetic kernel entry path:

- Guest starts at EL0.
- Guest triggers a real architectural exception.
- Guest enters an EL1 vector.
- Guest executes kernel-handler instructions.
- Guest accesses permitted EL1 memory.
- Guest returns to EL0 using ERET.

Provide minimal guest-side diagnostic output through an explicitly documented virtual interface.

Do not make host C++ calls stand in for guest kernel instructions.

Keep the test kernel small, reproducible and independent of Apple proprietary artifacts.

Suggested artifacts:

- `synthetic_el1_kernel`
- `el0_exception_client`
- `exception_vector_test`
- `el1_memory_protection_test`

Record compiler version, exact build command, artifact size and SHA-256.

**Acceptance:** Independently compiled synthetic ARM64 kernel code executes at guest EL1.

## Stage 193.17 — Freestanding Kernel Integration Tests

Create an integrated guest test corpus.

At minimum, include:

1. EL0-to-EL1 synchronous exception.
2. EL1 vector dispatch.
3. ESR/FAR validation.
4. EL1 stack switching.
5. Guest system-register access.
6. ERET return to EL0.
7. EL0 access violation.
8. EL1-only memory access.
9. Translation fault handling.
10. TLB invalidation.
11. ASID isolation.
12. Timer-triggered IRQ.
13. Interrupt acknowledgment.
14. Two-core interrupt delivery.
15. Deterministic exception replay.

Each test must produce a machine-readable execution result.

Include:

- Test ID.
- Artifact identity.
- Guest entry PC.
- Execution level.
- Exception class.
- Exception vector.
- Handler PC.
- Return PC.
- Retired instruction count.
- Final CPU state hash.
- Expected result.
- Actual result.

**Acceptance:** Integrated guest tests demonstrate the complete supported privilege and exception architecture.

## Stage 193.18 — Architectural Conformance and Differential Validation

Perform a focused architecture audit.

Compare implemented behavior against authoritative ARM architectural references.

Where an independent reference emulator is available and appropriate, use it for differential validation of bounded instruction and exception scenarios.

Do not confuse instruction differential testing with complete architecture conformance.

Test:

- Exception entry state.
- Saved status.
- Return address.
- Vector selection.
- Syndrome fields.
- ERET behavior.
- Memory permission faults.
- Translation faults.
- Interrupt masking.
- Timer delivery.
- ASID isolation.

Record unsupported behavior explicitly.

Any disagreement between WinIOS and the reference must be investigated. Do not simply alter expected results to match the implementation.

**Acceptance:** All claimed supported behaviors have traceable reference expectations and reproducible tests.

## Stage 193.19 — Final Integration, Regression and Evidence Audit

Complete a full campaign audit.

Verify:

- No regression in previous EL0 Mach-O execution.
- No regression in dyld behavior.
- No regression in BSD syscalls.
- No regression in Mach IPC.
- No regression in VFS and memory mapping.
- No regression in TLS.
- No regression in CPU differential tests.
- No regression in deterministic replay.
- No regression in existing SMP behavior.
- No regression in snapshot and manifest compatibility.

Run complete Debug and Release validation.

Produce:

`docs/campaign193/CAMPAIGN_193_FINAL_REPORT.md`

Also produce:

- `docs/campaign193/manifest.json`
- `docs/campaign193/evidence_matrix.md`
- `docs/campaign193/architecture_decisions.md`
- `docs/campaign193/known_limitations.md`
- `docs/campaign193/test_results_debug.txt`
- `docs/campaign193/test_results_release.txt`

---

# 4. REQUIRED TESTING AND VALIDATION

For every stage:

1. Build the relevant Debug targets.
2. Run focused Debug tests.
3. Build the relevant Release targets.
4. Run focused Release tests.
5. Run relevant regression tests.
6. Record actual exit codes.
7. Preserve logs.
8. Record failures and explain their causes.

At the end of the campaign:

- Build the complete Debug configuration.
- Run the complete Debug CTest suite.
- Build the complete Release configuration.
- Run the complete Release CTest suite.
- Compare registered test counts with the baseline.
- Explain every skipped test.
- Verify no tests were silently disabled.
- Verify the protected files remain unchanged.
- Verify the Inferno checkout remains untouched.

Do not report a test as passed if it was not actually executed.

Do not report a build as successful merely because an earlier build artifact exists.

### Required test evidence

Every new test should identify whether it is:

- Host-model unit test.
- Guest EL0 execution test.
- Guest EL1 execution test.
- Guest exception-transition test.
- Guest interrupt-delivery test.
- Synthetic kernel integration test.
- Differential architecture test.

The final report must separate these categories.

---

# 5. PERFORMANCE AND DETERMINISM

Measure the effect of the new exception and privilege architecture.

Record:

- Baseline EL0 instruction throughput.
- EL0-to-EL1 transition overhead.
- EL1-to-EL0 return overhead.
- Exception handling instruction count.
- Timer interrupt overhead.
- TLB invalidation overhead.
- Address-space switching overhead.
- SMP interrupt overhead.

Do not optimize by removing architectural checks.

Verify deterministic replay across repeated runs.

Any change to snapshot or replay formats must include explicit versioning, backward-compatibility analysis and tests.

---

# 6. SECURITY AND CORRECTNESS REQUIREMENTS

The implementation must not:

- Permit EL0 access to protected EL1 memory.
- Accept invalid exception return states silently.
- Ignore memory permission faults.
- Treat all exception types as identical.
- Treat every interrupt as an IRQ if FIQ is separately supported.
- Bypass the guest MMU for convenience.
- Convert unsupported system-register accesses into successful reads.
- Allow host-side state changes to masquerade as guest instruction execution.
- Fabricate Apple-specific hardware configuration.
- Claim genuine XNU execution without genuine XNU evidence.

Use checked arithmetic for address calculations and bounds.

Avoid undefined behavior, data races and uninitialized architectural state.

---

# 7. CAMPAIGN EXECUTION STRATEGY

Execute stages sequentially in the specified order.

Use parallel workers only for isolated work that does not introduce conflicting modifications.

Suggested independent workstreams:

**Worker A — CPU and exceptions**
- Stages 193.1–193.6.

**Worker B — MMU and isolation**
- Stages 193.7–193.10.

**Worker C — Interrupts and timers**
- Stages 193.12–193.15.

**Worker D — Synthetic kernel and validation**
- Stages 193.16–193.18.

The primary agent must integrate and review all changes.

Do not let multiple workers modify the same architectural files simultaneously without coordination.

Do not start integration tests until dependent implementations are complete.

Do not mark a stage complete based only on code generation or successful compilation.

A stage is complete only when:

1. Implementation is present.
2. Focused tests pass.
3. Relevant regression tests pass.
4. Evidence is recorded.
5. Known limitations are documented.

If a stage reveals an architectural flaw in an earlier stage, fix the root cause and rerun the affected regression tests.

---

# 8. COMMIT AND GIT POLICY

Preserve the current branch.

Do not push to any remote.

Do not rewrite history.

Do not commit unrelated user changes.

A local commit is permitted only if:

- All 20 stages have been completed or explicitly classified.
- Debug and Release validation has completed.
- The working tree has been audited.
- Only intended Campaign 193 files are staged.
- No protected file was modified.
- No unrelated user changes are included.

If any acceptance condition is not satisfied, do not create the final campaign commit.

Record the resulting commit hash if one is created.

---

# 9. FINAL REPORT FORMAT

The final report must contain:

## A. Executive Summary
- Campaign result.
- Number of stages completed.
- Number of stages partially completed.
- Number of blocked stages.
- Final repository commit.

## B. Stage-by-Stage Results

For all 20 stages, report:

- Implementation summary.
- Files changed.
- Tests executed.
- Exact test counts.
- Debug result.
- Release result.
- Evidence classification.
- Remaining limitations.

## C. Architectural Results

Explicitly explain:

- How EL0 and EL1 are represented.
- How guest exceptions are entered.
- How exception vectors are selected.
- How ESR_EL1 and FAR_EL1 are populated.
- How ERET restores execution.
- How guest privilege affects memory access.
- How ASID isolation works.
- How interrupts reach guest EL1.
- How timer interrupts work.
- How synthetic kernel artifacts execute.

## D. Validation Results

Provide exact:

- Debug build result.
- Release build result.
- Debug CTest result.
- Release CTest result.
- Total registered tests.
- Passed tests.
- Failed tests.
- Skipped tests and reasons.
- Compiler warnings.
- Repeated determinism runs.

## E. Evidence Matrix

For every major claim, identify:

- Claim.
- Supporting artifact or log.
- Test ID.
- Execution category.
- Result.
- Limitations.

## F. Remaining Limitations

Explicitly identify:

- Unsupported ARM64 instructions.
- Unsupported exception classes.
- Unsupported system registers.
- Incomplete GIC behavior.
- Incomplete MMU behavior.
- Unsupported SMP behavior.
- Missing architectural features.
- Host-emulated components.
- Any Apple-specific behavior that remains unverified.

## G. Final Engineering Classification

Choose the strongest classification actually supported by the evidence:

- EL0 execution foundation.
- EL0/EL1 privilege-transition foundation.
- Synthetic ARM64 kernel execution foundation.
- Partial ARM64 exception and interrupt architecture.
- Broader architectural conformance.

Do not claim complete ARM64 architecture support or genuine iOS/XNU execution without evidence.

---

# 10. FINAL DEFINITION OF SUCCESS

Campaign 193 succeeds when WinIOS can demonstrate the following complete guest-side sequence:

1. An independently compiled ARM64 guest application executes at EL0.
2. The guest triggers an architectural exception.
3. The CPU saves the appropriate guest exception state.
4. The CPU selects the correct EL1 exception vector.
5. Guest EL1 handler instructions execute.
6. The handler reads the appropriate syndrome and fault information.
7. The handler accesses permitted EL1 memory.
8. The handler performs the required guest-side operations.
9. The handler executes ERET.
10. The CPU restores the correct EL0 execution state.
11. The original guest application resumes at the correct PC.
12. The complete transition is recorded and reproducible.
13. Debug and Release regressions remain green.

A passing host-side unit test alone is not sufficient.

A synthetic kernel running at guest EL1 is a meaningful milestone, but it is not proof of genuine Apple XNU or iOS execution.

**Begin with Stage 193.0. Inspect the actual repository state before modifying anything. Preserve all existing work. Execute the campaign sequentially and report only verified results.**
</USER_REQUEST>
<ADDITIONAL_METADATA>
The current local time is: 2026-11-14T00:05:06+05:30.
</ADDITIONAL_METADATA>