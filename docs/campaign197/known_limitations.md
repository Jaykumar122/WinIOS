# WinIOS Campaign 197: Known Limitations & Architectural Boundaries

## 1. Emulation Host Boundary & Memory Model Simulation
* **Host Hardware TSO vs. Guest Weak Memory**:
  The WinIOS host is an x86-64 Windows 11 machine. x86-64 enforces hardware Total Store Order (TSO), preventing Store-Load and Load-Load reordering at the hardware level. To achieve strict ARMv8-A weak-memory conformance, WinIOS employs an axiomatic and buffer-modeled simulation layer (`GuestMemoryOrderModel`). While the model accurately flags missing barriers and evaluates canonical litmus tests according to ARM DDI 0487 axiomatic rules, host-thread execution of guest code is bounded by host memory primitives.
* **Instruction Interleaving Granularity**:
  Multi-core SMP interleaving is simulated at the instruction boundary. Microarchitectural store forwarding latency and multi-level cache hierarchy pipelines are modeled logically rather than via cycle-accurate hardware synthesis.

## 2. Synthetic Kernel vs. Genuine Proprietary iOS Boot
* **Synthetic Execution Environment**:
  WinIOS executes freestanding Mach-O binaries, Darwin userspace applications, and synthetic kernel microservices. It does NOT claim to boot unmodified Apple iOS `kernelcache` binaries, nor does it emulate proprietary Apple Secure Enclave Processor (SEP) coprocessors.
* **Apple Silicon Proprietary Extensions**:
  The emulation models standard ARMv8-A and ARMv8.1-A LSE instructions, standard system registers, and GICv3 interrupt handling. Apple-proprietary custom silicon registers (e.g., specific undocumented SPRR/GXF registers) are emulated only to the extent required for Darwin runtime ABI compatibility.

## 3. MMU and Memory Hierarchy
* **16KB Page Granule**:
  Memory translation strictly implements the 16KB granule standard on Apple Silicon (A16 Bionic / `T8120`). Systems or test cases expecting 4KB or 64KB page granules must adhere to the 16KB page alignment contract.
* **Single Stage Translation (EL0/EL1)**:
  Current testing covers Stage-1 EL0 and EL1 virtual address translation with ASID isolation. Two-stage translation (EL2 hypervisor stage-2 translation) is out of scope for Campaign 197.

## 4. Exclusive Monitor Scope
* **Coherence Domain**:
  The exclusive reservation monitor operates across the emulated SMP cluster with a 64-byte granule. External bus observers or non-coherent DMA channels interacting with exclusive addresses must be routed through `VirtualDMA` interfaces to participate in reservation invalidation.
