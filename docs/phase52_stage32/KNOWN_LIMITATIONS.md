# WinIOS — Virtual DMA & Replay Known Limitations
## Scope Boundaries & Future Architecture

---

## 1. Intentional Architectural Boundaries

1. **Synchronous DMA Stepping:**
   - DMA transfers execute synchronously with respect to the submitting virtual core's MMIO register write. True asynchronous host-thread DMA is intentionally not implemented to preserve single-host-thread deterministic scheduling.
2. **Scatter-Gather Limits:**
   - Scatter-gather descriptor chains are bounded to a maximum depth of 16 descriptors (`max_descriptors_per_chain = 16`). Deeper chains are deterministically rejected with `ErrorChainTooDeep`.
3. **Single Transfer Length Cap:**
   - Individual descriptors cannot declare transfer lengths exceeding 64 KiB (`max_transfer_bytes = 65536`).
4. **Topology Ceiling:**
   - The virtual device bus and DMA replay engine support up to 8 virtual cores (IDs 0–7). Core topologies > 8 remain strictly rejected by topology validation.
5. **No Direct Physical Hardware Passthrough:**
   - Real host PCI-passthrough and VFIO DMA remain outside the scope of WinIOS deterministic replay. Only virtualized guest DMA is supported.
