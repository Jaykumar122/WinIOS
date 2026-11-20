# Stage 243.11: TLB and Translation Invalidation Integration

**Campaign**: 243
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Integrated invalidation signaling with MMU translation cache:

- **Signal Path**: `AsidManager::allocate_asid` -> `needs_tlb_flush` -> `inval_cb_` -> `MMU::invalidate_tlb_all()`.
- **Slot Retirement**: `AsidManager::release_asid` -> `inval_cb_` -> `MMU::invalidate_tlb_asid(asid)`.
- **Direct Lookup Baseline**: Direct page-table walker verifies translation tables; software TLB invalidation ensures zero stale cache hits.
