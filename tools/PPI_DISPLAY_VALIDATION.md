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

## Phase 1 — Reproduce / 复现

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

## Phase 3 — Enforce mapping / Enforce 映射

| Python change | Enforce file | Change |
|---------------|--------------|--------|
| `use_measured_anchor` | `GBRS_RadarStationHud.c` | `TrackDrawWorldPos()` prefers `m_Positions` last hit |
| same | `GBRS_PpiPanel.c` | World CRT track draw uses same anchor |
| `single_layer_pd` | `GBRS_RadarStationHud.c` | `DrawSearchPlots()` always calls `PlotCoveredByCachedTrack()` |
| sweep ENU fix | `GBRS_ConsoleSession.c` | `forward = (cos az, sin az)` |
| `merge_birth_by_scatterer` | *(Python-only for now)* | RDF birth uses predicted gate; display clustering + scatterer id mitigate in-game |

## Phase 4 — Lint / 格式

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
