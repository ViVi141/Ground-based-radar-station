# GBRS — Ground-based radar station (Arma Reforger addon)

Enfusion/Arma Reforger mod that adds two placeable air-search radar stations
(US RPL-5, USSR TPN-19). In-game code is Enforce Script (`.c`) under
`scripts/Game/GBRS/`; the addon manifest is `addon.gproj`. Alongside the mod
lives an offline Python radar-physics harness that mirrors the in-game
detection chain 1:1 for tuning/validation.

## Cursor Cloud specific instructions

### What can and cannot run in this Linux VM
- The mod itself (Enforce Script, prefabs, `resourceDatabase.rdb`) can only be
  compiled/played inside the Windows-only **Enfusion Workbench** (Arma Reforger
  Tools). It also depends on the base game and an external **RDF radar
  framework** addon. None of that is available or reproducible headless on
  Linux — do not attempt to build/run the mod here.
- The only component runnable in this VM is the **Python simulation harness**
  in `scripts/Game/GBRS/`. Treat that as the "application" for local dev.

### Running the Python sims (from `scripts/Game/GBRS/`)
- `python3 simulate_snr_gates.py` — pure stdlib; fits `DetectionSnrDb` /
  `DemClutterScale` detection gates from logged SNRs. Always works, no data
  needed.
- `python3 simulate_clutter_cover.py` — full RDF `PhysicalDetect` + CFAR +
  Swerling chain over real Everon terrain; writes `out/clutter_mti_report.json`
  and PNG plots.
- `python3 simulate_uh1_southbound.py` — UH-1 fly-by scenario; writes
  `out/uh1_southbound_report.json` + PNG.
- Reports/PNGs land in `scripts/Game/GBRS/out/`, which is a **tracked**
  directory. Regenerated files show up as `git` modifications — `git checkout`
  them if you don't intend to commit new sim results.

### DEM data gotcha (needed by the two numpy sims)
- `simulate_clutter_cover.py` / `simulate_uh1_southbound.py` load a terrain crop
  via `gbrs_eden_dem.load_eden_crop`. The real game-baked DEM lives under
  `~/Documents/My Games/ArmaReforgerWorkbench/profile/RDF/DemData/GM_Eden/`
  (Windows-generated) and is **not** in the repo.
- The committed NPZ caches `out/eden_crop_radar.npz` and
  `out/eden_crop_north_airfield.npz` already contain the real crop data, but the
  loader still calls `_parse_manifest(GM_Eden/manifest.csv)` **before** it checks
  the cache. So the sims fail with `FileNotFoundError: .../GM_Eden/manifest.csv`
  unless that manifest header exists.
- Fix without touching game files: create a minimal V3 manifest so the cache
  branch is taken (values below just need to be parseable; the terrain itself
  comes from the committed NPZ):

  ```bash
  python3 - <<'PY'
  import numpy as np
  from pathlib import Path
  d = np.load('scripts/Game/GBRS/out/eden_crop_radar.npz')
  cell = float(d['cell_m']); bx = float(d['bounds_min_x']); bz = float(d['bounds_min_z'])
  root = Path.home()/'Documents'/'My Games'/'ArmaReforgerWorkbench'/'profile'/'RDF'/'DemData'/'GM_Eden'
  (root/'tiles').mkdir(parents=True, exist_ok=True)
  span = 4096
  lines = ['RDF_DEM_MANIFEST_V3','world GM_Eden',
           'bounds_min_x %f'%bx,'bounds_min_z %f'%bz,
           'bounds_max_x %f'%(bx+span*cell),'bounds_max_z %f'%(bz+span*cell),
           'cell_m %f'%cell,'tile_cells 64','tile_count_x 64','tile_count_z 64']
  (root/'manifest.csv').write_text('\n'.join(lines)+'\n', encoding='utf-8')
  print('wrote', root/'manifest.csv')
  PY
  ```

  This manifest is outside the repo and is not recreated by the startup update
  script, so re-run it if the two numpy sims report a missing `manifest.csv`.
