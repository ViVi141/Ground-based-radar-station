# CHANGELOG

## 1.1.7 — 2026-08-29

依赖 / Requires **RDF ≥ 1.1.6**.

### 中文

- 战斗手册：按原版/RHS 方式 Override `Configs/FieldManual/Categories/Gameplay/FM_Commanding.conf`，在 Gameplay → Commanding 下追加 Ground Radar 五页说明（`m_aEntries +{}`）。

### English

- Field Manual: overrides `Configs/FieldManual/Categories/Gameplay/FM_Commanding.conf` (same pattern as RHS) and appends Ground Radar pages under Gameplay → Commanding via `m_aEntries +{}`.

---

## 1.1.6 — 2026-08-27

依赖 / Requires **RDF ≥ 1.1.6**。

### 中文

- WLR：**360° 搜索发现炮弹**；出现航迹后天线自动收窄到该方位 ±40° 扇区做弹道采样，航迹消失约 8 s 后回到全周搜索。
- 显式 `SetWlrSectorSweep` 仍可锁定威胁走廊；Demo 不再强制扇区，走同一产品路径。
- 加固多人复制 / 锁定 / PPI 快照序号与 WLR 显示路径；限制重复弹道重算尖峰，优先复用 Baker 快照。

### English

- WLR: **360° search cues shells**, then the dish auto-narrows to a ±40° corridor for ballistic samples; returns to all-around scan about 8 s after the last cue.
- Explicit `SetWlrSectorSweep` still locks a threat corridor; Demo no longer forces a sector so it exercises the product path.
- Hardened replication / lock / PPI snapshot sequencing and the WLR display path; capped repeated ballistic re-solves and preferred Baker snapshots over 60 Hz re-bake.

---

## 1.1.5 — 2026-08-27

依赖 / Requires **RDF ≥ 1.1.6**。

### 中文

- 修复 Workbench / 单机场景下 PPI 快照未到达导致右侧面板为空的问题：现在直接从 RDF 传感器读取 live plots 与 tracks 填充 `ListBody`。
- 恢复右侧面板动态内容，去掉临时测试文本；现在列出航迹/点迹的方位、距离、高度、速度、类型、信噪比。
- 左下角 AzEl 增加点迹显示，并给所有 blip 加白色外晕以在深色背景上可见。
- 本地模式下航迹也直接回传给 HUD，避免确认航迹丢失。
- 统一菜单与烘焙器的极坐标聚类（±4° / 400 m），解决一机多点未完全合并的问题。
- 放宽航迹显示距离门到量程 1.1 倍，与点迹一致。

### English

- Fixed empty right-side contacts panel in local / Workbench runs: `ListBody` now populates from live RDF plots and tracks when the replicated snapshot has not arrived yet.
- Removed temporary test text; restored dynamic contact list with azimuth, range, altitude, speed, type and SNR.
- Added plot drawing to the bottom-left AzEl display and a white halo around every blip for visibility against the dark background.
- Local-mode tracks are also fed back to the HUD so confirmed tracks are no longer lost.
- Unified polar clustering (±4° / 400 m) between the menu and the PPI baker to fully merge multi-scatterer returns.
- Track display range gate relaxed to 1.1× zoom range, matching plots.

---

## 1.1.3 — 2026-08-27

依赖 / Requires **RDF ≥ 1.1.6**。

### 中文

- PPI 点迹余辉停在最后一次探测位置，不再用速度外推；波束离开后余辉不再滑走。
- 同一机体的多 scatterer（机身+旋翼）按 120 m 空间门合并，避免一机五点。
- 右侧目标栏：没有确认航迹时回退到点迹；未确认航迹也会上表。
- Demo / 设置推送不再清掉仍在请求的天线盯视；径向飞行时也会把被清掉的盯视拉回来。
- PD SEARCH 的 PPI / AzEl 同时画冻结点迹和 TWS 方块，扫描线跟 live 天线。

### English

- Plot afterglow is frozen at last detection (no persist coast).
- Multi-scatterer airframes cluster within 120 m.
- Contacts table falls back to plots when the track file is empty; tentatives are listed.
- Antenna stare survives settings push and Demo re-applies it if it was dropped.
- PD SEARCH paints frozen plots plus TWS squares; sweep still follows the live antenna.

---

## 1.1.1 — 2026-08-27

依赖 / Requires **RDF ≥ 1.1.6**。

### 中文

- 空搜量程：美军 **12 km**，苏军 **16 km**（WLR 仍为 8 / 10 km，弹丸 SNR 不够再加）。
- 空搜关联门加到 **10° / 1200 m**，高速喷气进出波束时不再轻易断档乱跳。
- **LOCK** 工作台页开放：自动锁最近载具；在 PPI 上点击航迹可指定锁定（供 SAM/AAA 火控桥）。
- WLR 改回 **360° 机械扫描**（加宽方位波束 + 低仰角瓣），不再默认只扫东侧 ±45°。
- 旋翼边带多普勒不再当机体速度：悬停直升机不再显示成几百米每秒，PPI coast 也不会把点迹甩飞。

### English

- Air-search range: US **12 km**, USSR **16 km** (WLR stays 8 / 10 km).
- Wider air-search gates (**10° / 1200 m**) so jets survive the time between beam passes.
- **LOCK** mode is live: auto-acquire vehicles; click a PPI track to designate (fire-control bridge).
- WLR is all-around mechanical scan again (wider az beam + low-elevation lobe). Default ±45° east corridor is gone.
- Rotor-sideband Doppler is no longer treated as body speed, so hovering helicopters stop reading as hundreds of m/s.

---

## 1.1.0 — 2026-08-26

依赖 / Requires **RDF ≥ 1.1.6**。

### 中文

#### 射频与 RDF 对齐

- 美军 / 苏军 PD：关闭 `DeriveMtdLeakageFromSigmaVr`，钉死 MTI floor（美 `1e-4` / 苏 `0.01`）与 leak `1e-9`；苏军 VHF 不再加载美军 SHORAD HwCalib。
- 离线工具对齐 RDF 1.1.6：`suggest_mti_floor` 补 `2π`；Nelder-Mead 第 5 顶点只扰动 drag。
- 离线复跑：WLR drag-fit 中位落点误差约 57–71 m（接受率 100%）；苏军 PD tracker PASS（1 条航迹/架）。

#### 工作台 UI

- 顶栏 **OPTICS**：光学 PIP 可选（默认关，占位保留布局，避免 Az/El 被错误拉伸）。
- 工作台刷新约 **60 Hz**；扫描线跟 live 天线；PPI 快照约 30 Hz + 航迹软 coast。
- 顶栏显示 `GBRS v1.1.0`。
- **修复**：打开工作台后 PPI 量程被错误锁在 2 km——现默认跟随射频上限（美空搜 7 km / 苏 10 km / WLR 8–10 km）；仅在操作员手动缩放后锁定。

#### WLR（反炮）

- 关联门由 8°/600 m 收至 **5°/350 m**，启用 **JPDA**，coast 缩短为 6 s，减轻同走廊连发粘轨。
- 显示侧对 RDF 解做消毒：拦截离谱 TOF/跨度；发射↔落点与位置弦反向时自动对调后再画 LCH/IMP。
- 炮弹箭头优先：合格 LCH→IMP → 位置弦 → 滤波速度；炮弹不再用径向多普勒兜底（避免“倒着飞”）。
- Demo：发射点选干地；射程约 700 m；连发方位 ±3° 错开、间隔 8 s。

#### 文档

- 新增本 CHANGELOG；README / 进度页标注版本与 RDF ≥ 1.1.6。

---

### English

#### RF / RDF alignment

- US / USSR PD: disable `DeriveMtdLeakageFromSigmaVr`; pin MTI floors (US `1e-4` / USSR `0.01`) and leak `1e-9`; USSR VHF never loads SHORAD HwCalib.
- Offline tools mirror RDF 1.1.6 (`2π` MTI suggest; Nelder-Mead drag-axis dart).
- Offline re-run: WLR drag-fit median impact ~57–71 m (100% acceptance); USSR PD tracker PASS.

#### Workstation UI

- **OPTICS** mode-bar toggle (PIP opt-in, default off; slot stays sized so Az/El does not stretch).
- ~60 Hz feed/redraw; sweep follows live antenna; ~30 Hz PPI snaps + soft track coast.
- Mode bar shows `GBRS v1.1.0`.
- **Fix**: PPI no longer locks to a 2 km placeholder on open — follows RF max until the operator zooms.

#### WLR

- Association gates **5° / 350 m**, **JPDA** on, coast 6 s (less same-corridor track glue).
- Display sanitize: reject absurd TOF/span; swap launch↔impact when the position chord disagrees.
- Shell chevron: LCH→IMP → chord → filtered vel (no LOS×radial fallback).
- Demo: dry-land launch pad, ~700 m standoff, ±3° az stagger, 8 s interval.

#### Docs

- Added this CHANGELOG; README / progress note version and RDF ≥ 1.1.6.
