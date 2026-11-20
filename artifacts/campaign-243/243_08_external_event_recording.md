# Stage 243.8: Input and External Event Recording

**Campaign**: 243
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Audited external event boundaries:

- **Host Event Ingestion**: UI events, keypresses, and touch interactions enter via `InputRouter`.
- **Replay Boundary**: External GUI interaction is captured at the event queue boundary; unrecorded background host window movements are explicitly excluded from guest replay.
