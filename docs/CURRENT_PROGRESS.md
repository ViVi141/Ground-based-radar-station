# GBRS Conflict 建造 — 进度

> 最后更新：2026-08-29
> **版本：1.1.7**（RDF ≥ 1.1.6）
> 状态：**BUILDING 放置已正常**。放置 `E_RadarStation_S_*` 会先出 FRB 施工区，铲完才生成成品雷达。
> UI：顶栏 **OPTICS** 可选打开光学 PIP（默认关，占位保留布局）；工作台刷新 **60 Hz**；扫描线跟 live antenna；PPI snap ~30 Hz + 航迹 coast。
> Field Manual：Override `FM_Commanding.conf`（Gameplay → Commanding），`m_aEntries +{}` 追加 Ground Radar 说明页。

---

## 已完成

1. 公开事件 API：`scripts/Game/GBRS/GBRS_RadarStationEvents.c`
   - `OnRadarContact` / `OnRadarContactLost` / `OnNetworkContact` / `OnWlrSolution` / `OnLockChanged` / `OnRadarDestroyed`
2. Root 组合体使用官方 `SCR_CampaignBuildingCompositionComponent "{5E96A067C097D570}"`，避免 E_ 上出现重复组件。
3. Outline / 布局兜底：雷达始终使用 `FRB_RadarStation_S_01.et`，建造值 250。
4. **根因修复**：原版 `EvaluateBuildingStatus` 在 `ToBuildValue==0` 时会立刻 `SpawnComposition()` 并删掉四角桩。雷达强制建造值 250。
5. 施工期间推迟 RDF `Configure`，减轻 `Cannot set entity as ACTIVE, it's not registered!`。
6. E_ stub Flags 与官方 FreeRoam 对齐：`0x100000 0x403`。
7. Conflict 敌情消费者：`GBRS_CampaignRadarWarning` 订阅事件 API。开机雷达在锁定情报网（美 45.6 / 苏 39.6 MHz）发 `ScriptedRadioMessage`。本机 TX 2 km，更远走 HQ 覆盖或 `RelayTransceiver` 跳转。调到 RADAR NET 才播 ACP 语音。未调频玩家不收空情弹窗。施工完成发一次调频 briefing。不改 `m_bEnemiesPresent`。
8. 伤害调试 Print/Shape 默认关闭（组件 Attribute `m_bDebugDraw`）。
9. PD 关联门是共享 **8° / 600 m**（旧美军 14° / 900 m 会把编队合成一条）。PPI 显示聚类 8° / 700 m。
10. 多站 datalink：开机站每 0.5 s 向 `RDF_RadarDatalinkHub` 发布确认航迹；融合 overlay 走 `GBRS_RadarIffResolver`；新网航迹发 `OnNetworkContact`。
11. 苏军 PD tracker 离线链改为 MTD_BANK（与局内 `ApplyPulseDopplerHardware` 一致）。TwoPulse × VHF floor 0.01 会把 UH-1 点迹淹成 0 条；对齐后 1 条航迹/架 PASS。
12. **RDF 1.1.6 对齐（2026-08-25）**：
    - `ApplyPulseDopplerHardware` 关闭 Derive，leak 对齐 HwCalib `1e-9`；苏军走 `ApplyPulseDopplerHardwareVhf`（不加载 SHORAD profile，钉死 floor `0.01`）。
    - 离线：`calib_pd_full.suggest_mti_floor` 补 2π；`validate_rdf_drag_fit` / `ab_doppler_wlr` Nelder-Mead 第 5 顶点扰动 drag 轴。
    - 离线复跑：WLR drag-fit 中位落点误差 ~57–71 m（真空 ~289–351 m），接受率 100%；苏军 PD tracker PASS（1 条/架）；美军离线 TwoPulse 仍 FAIL（已知，需局内 Airborne 对照）。
13. Field Manual（2026-08-29）：Override `FM_Commanding.conf`；七页说明含 Interference & ECM、US vs USSR。

---

## 未做 / 已知限制

- 雷达施工垫仍跳过 `SpawnPreview`（E_ EditorLink 树会原生 AV）。
- 未完成雷达垫禁止原版拆除动作（同样为规避原生 AV）；残骸用 GBRS 铲具拆除。
- `chimeraMenus.conf` 必须整份覆盖原版菜单表才能注册 `GBRS_RadarStationMenu`。
- WLR 扇扫 / 切向悬停直升机仍建议局内实测（RDF 1.1.6 drag 拟合修复后优先复测落点）。
- 情报网 HQ / 中继跳转需局内核对。
- `GBRS_RadarStationComponent` 枢纽约 3141 行，接触 / datalink / 天线 / 建造门闩尚未拆文件。
