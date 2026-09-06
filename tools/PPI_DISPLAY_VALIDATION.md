# GBRS PPI Display Validation / PPI 显示路径验证

Offline Python harness for the PPI “点迹乱飞” (flying plots) issue.  
离线 Python 工具链，用于复现与修复 PPI 点迹乱飞问题。

## Root cause summary / 根因摘要

| Issue | Baseline symptom | Playable fix |
|-------|------------------|--------------|
| Filter coast draw | `FilteredPosition` advances between mechanical-scan hits → symbols slide/fling | Draw last measured hit (`m_Positions` tail) |
| Dual layer | Afterglow dot + track square for same contact | Hide afterglow when track covers plot (`PlotCoveredByCachedTrack`) |
| US PD fragmentation | 1 aircraft → 4 tracks (offline) | Birth merge keyed on scatterer + last-hit gate (Python); in-game relies on RDF scatterer + display clustering |
| Console CRT sweep | Forward vector sin/cos swapped 90° | `(cos az, sin az)` north-up ENU |
| Ghost coast TWS | Coasting tracks linger with no live air plot | `ShouldDisplayAirSearchTrack` (miss≥2 / coast>8s / empty scope) |
| Snapshot cadence | 30 Hz track push vs ~5 Hz EW mirror | `PPI_SNAPSHOT_INTERVAL_S = 0.2` (~5 Hz) |
| Afterglow life | Fixed 0.45 s vs ~1 sweep | `ResolvePlotAfterglowLifeS()` = scan period × 1.1 |

## SIGINT_RDF gap review (PR #3 follow-up)

Compared against local SIGINT_RDF EW scan→HUD evidence (authoritative, not cloned here).

| Pattern | SIGINT_RDF EW | GBRS before PR #3 | GBRS after PR #3 + follow-up |
|---------|---------------|-------------------|------------------------------|
| Plot draw anchor | measured `m_Position` | afterglow OK; track used `FilteredPosition` | track → `m_Positions` tail ✓ |
| Afterglow freeze | ~1 sweep × 1.1 | fixed 0.45 s | sweep-scaled life ✓ |
| Dual layer | plots + tracks, plot suppress | WLR-only suppress | all modes suppress ✓ |
| Anti-ghost TWS | `ShouldDisplayAirSearchTrack` | none | Config + Baker + Hud ✓ |
| PPI mirror rate | 5 Hz | 30 Hz | 5 Hz ✓ |
| Track draw (SIGINT) | still `FilteredPosition` | — | GBRS **better** (measured anchor) |
| Birth merge | RDF scatterer gate | Python-only | still Python-only; display clustering mitigates |

**Go/no-go:** Follow-up Enforce changes address the remaining high-value SIGINT gaps. Birth-merge in RDF tracker remains optional/future if in-game US still fragments after live test.

## Phase 3 — Enforce mapping / Enforce 映射

| Python / SIGINT change | Enforce file | Change |
|---------------|--------------|--------|
| `use_measured_anchor` | `GBRS_RadarStationHud.c` | `TrackDrawWorldPos()` prefers `m_Positions` last hit |
| same | `GBRS_PpiPanel.c` | World CRT track draw uses same anchor |
| `single_layer_pd` | `GBRS_RadarStationHud.c` | `DrawSearchPlots()` always calls `PlotCoveredByCachedTrack()` |
| sweep ENU fix | `GBRS_ConsoleSession.c` | `forward = (cos az, sin az)` |
| SIGINT anti-ghost | `GBRS_RadarStationConfig.c`, `GBRS_PpiDisplayBaker.c`, Hud, PpiPanel | `ShouldDisplayAirSearchTrack` |
| SIGINT 5 Hz mirror | `GBRS_RadarStationConstants.c` | `PPI_SNAPSHOT_INTERVAL_S = 0.2` |
| SIGINT afterglow | `GBRS_RadarStationConfig.c`, `GBRS_PpiDisplayBaker.c` | `ResolvePlotAfterglowLifeS()` |
| `merge_birth_by_scatterer` | *(Python-only)* | RDF birth uses predicted gate; display clustering + scatterer id mitigate in-game |

```bash
python3 tools/simulate_pd_tracker.py
python3 tools/simulate_wlr_position_track.py
python3 tools/simulate_ppi_display.py --policy baseline
```

**Baseline evidence (US):**

- PD tracker: **4 tracks/aircraft** (FAIL), gates 10°/1200 m
- PPI display: **intra-scan jump ~38 km** (filter coast), **dual-layer sep ~53 km**
- WLR filter: alpha-beta median max error **133–274 m** vs arc-lerp **0–3 m**

Artifacts: `tools/out/pd_tracker_validation.json`, `tools/out/ppi_display_validation.json`

## Phase 2 — Playable Python / 可玩指标

```bash
python3 tools/simulate_ppi_display.py --policy playable
python3 tools/simulate_pd_tracker.py   # see PLAYABLE row
```

**Playable metrics (after):**

| Faction | Fragmentation | Intra-scan jump | Dual-layer sep | Verdict |
|---------|---------------|-----------------|----------------|---------|
| US | 0 (1:1) | 0.0 m | 0.0 m | PASS |
| USSR | 0 (1:1) | 0.0 m | 0.0 m | PASS |

PD tracker PLAYABLE preset (scatterer birth merge + last-hit gate): US **1 track/aircraft PASS**.

## Phase 1 — Reproduce / 复现

Workbench Enforce lint (`arma-reforger-mcp lint_file`) requires a running Workbench NET API.  
This cloud agent VM has no Workbench — run locally:

```text
# With Workbench + Enable net API:
lint_project on modified scripts under scripts/Game/GBRS/
```

## Re-run commands / 重跑命令

```bash
python3 tools/simulate_ppi_display.py --policy both
python3 tools/simulate_pd_tracker.py
python3 tools/simulate_wlr_position_track.py
```

Expected: **playable** policy PASS for US and USSR; PD tracker **PLAYABLE** row PASS for US.
