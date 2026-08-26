# CHANGELOG

## 1.1.0 — 2026-08-26

Requires **RDF ≥ 1.1.6**.

### RDF 1.1.6 alignment

- `ApplyPulseDopplerHardwareEx` / `ApplyPulseDopplerHardwareVhf`: pin US / USSR MTI floors without `DeriveMtdLeakageFromSigmaVr` fallback; USSR VHF never loads SHORAD HwCalib.
- Offline tools mirror RDF fixes: `suggest_mti_floor` 2π factor; Nelder-Mead drag simplex 5th vertex on drag axis only.
- Offline re-run: WLR drag-fit median impact error ~57–71 m (100% acceptance); USSR PD tracker PASS (1 track/airframe).

### Workstation UI

- **OPTICS** mode-bar toggle: optical PIP opt-in (default off).
- 60 Hz menu feed / HUD redraw; PPI sweep needle follows live antenna; ~30 Hz plot snap + track coast between snaps.
- Mode bar shows `GBRS v1.1.0`.
