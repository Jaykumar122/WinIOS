# Stage 243.12: Address-Space Reuse and Stale-State Adversarial Testing

**Campaign**: 243
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Adversarially tested address-space recycling:

- **ASID Pool Rollover**: Forced multiple rollovers across small ASID pools.
- **Stale Token Rejection**: Tokens allocated in prior generations fail validation in new epochs (`is_valid_in_epoch == false`).
- **Zero Memory Bleed**: Recycling an ASID slot guarantees that physical pages from previous owners are not accessible to new tenants.
