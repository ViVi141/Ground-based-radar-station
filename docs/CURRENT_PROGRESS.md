# GBRS Conflict 建造 — 进度

> 最后更新：2026-08-16
> 状态：**BUILDING 放置已正常**。放置 `E_RadarStation_S_*` 会先出 FRB 施工区，铲完才生成成品雷达。

---

## 已完成

1. 公开事件 API：`scripts/Game/GBRS/GBRS_RadarStationEvents.c`
   - `OnRadarContact` / `OnRadarContactLost` / `OnWlrSolution` / `OnLockChanged` / `OnRadarDestroyed`
2. Root 组合体使用官方 `SCR_CampaignBuildingCompositionComponent "{5E96A067C097D570}"`，避免 E_ 上出现重复组件。
3. Outline / 布局兜底：雷达始终使用 `FRB_RadarStation_S_01.et`，建造值 250。
4. **根因修复**：原版 `EvaluateBuildingStatus` 在 `ToBuildValue==0` 时会立刻 `SpawnComposition()` 并删掉四角桩。雷达强制建造值 250。
5. 施工期间推迟 RDF `Configure`，减轻 `Cannot set entity as ACTIVE, it's not registered!`。
6. E_ stub Flags 与官方 FreeRoam 对齐：`0x100000 0x403`。

---

## 未做 / 已知限制

- Conflict 基地敌情订阅（`EvaluateEnemyPresence`）未实现；事件 API 已有，尚无基地消费者。
- 雷达施工垫仍跳过 `SpawnPreview`（E_ EditorLink 树会原生 AV）。
- 未完成雷达垫禁止原版拆除动作。
- WLR / 切向悬停直升机仍建议局内实测。
