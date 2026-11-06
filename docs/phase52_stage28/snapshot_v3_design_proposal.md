# Snapshot v3 Design Proposal
## Architectural Enhancements for Ultra-Dense & Large-Memory Multicore Snapshots

---

## 1. Motivation

While `.wsnap v2` performs excellently for 8 cores with sub-millisecond serialization, future systems scaling beyond 8 cores or managing gigabyte-scale guest physical RAM require next-generation snapshot architecture:

1. **Memory Compression & Deduplication**:
   - Zero-page elimination and LZ4/Zstandard chunked compression of sparse guest physical RAM.
2. **Incremental Snapshots**:
   - Page-dirty tracking bitmap (`DirtyPageBitmask`) allowing differential delta snapshots relative to a base snapshot.
3. **Core Descriptor Extensibility**:
   - Flexible TLV (Type-Length-Value) records per core to support SIMD/NEON (V0..V31), SVE vectors, and EL2/EL3 hypervisor registers without breaking header structure.

---

## 2. Proposed .wsnap v3 Header & Section Layout

```
+-------------------------------------------------------------------+
| Magic "WSNP" (4B) | Version 3 (2B) | Flags (2B) | Core Count (2B) |
+-------------------------------------------------------------------+
| Base Snapshot UUID (16B) | Snapshot Sequence (8B)                 |
+-------------------------------------------------------------------+
| Canonical Cluster SHA-256 (32B)                                   |
+-------------------------------------------------------------------+
| Section Index Offset (8B) | Section Count (4B) | CRC32C (4B)      |
+-------------------------------------------------------------------+
```

---

## 3. Implementation Horizon

Targeted for implementation in Phase 53 alongside multi-gigabyte memory scaling.
