# CHANGELOG

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
