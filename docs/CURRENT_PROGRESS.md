# GBRS Conflict 建造 — 进度

> 最后更新：2026-08-20
> 状态：**BUILDING 放置已正常**。放置 `E_RadarStation_S_*` 会先出 FRB 施工区，铲完才生成成品雷达。

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

---

## 未做 / 已知限制

- 雷达施工垫仍跳过 `SpawnPreview`（E_ EditorLink 树会原生 AV）。
- 未完成雷达垫禁止原版拆除动作（同样为规避原生 AV）；残骸用 GBRS 铲具拆除。
- `chimeraMenus.conf` 必须整份覆盖原版菜单表才能注册 `GBRS_RadarStationMenu`。
- WLR 扇扫 / 切向悬停直升机仍建议局内实测。
- 情报网 HQ / 中继跳转需局内核对。
- `GBRS_RadarStationComponent` 枢纽约 3141 行，接触 / datalink / 天线 / 建造门闩尚未拆文件。
