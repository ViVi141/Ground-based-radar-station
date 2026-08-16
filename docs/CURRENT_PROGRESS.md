# GBRS Conflict 建造问题 — 当前进度存档

> 最后更新：本次会话结束时的中间状态
> 状态：**未完成**，Conflict 中放置雷达站仍会直接生成成品实体，无法出现建造区/交互。

---

## 一、目标

让 GBRS 雷达站在 Conflict / Editor BUILDING 中：

1. 放置时先生成 FreeRoamBuilding 建造区（`FRB_RadarStation_S_01.et`）；
2. 玩家使用建造工具施工；
3. 建造完成后才生成完整雷达站；
4. 建成后可以通电、操作、接入基地敌情。

---

## 二、当前已完成/已修改的内容

### 1. 公开事件 API

新增：

```text
scripts/Game/GBRS/GBRS_RadarStationEvents.c
```

提供事件：

- `OnRadarContact`
- `OnRadarContactLost`
- `OnWlrSolution`
- `OnLockChanged`
- `OnRadarDestroyed`

### 2. Conflict 基地敌情接入

新增：

```text
scripts/Game/GBRS/GBRS_CampaignMilitaryBaseComponent.c
```

- `modded class SCR_CampaignMilitaryBaseComponent`
- 增加雷达接触标记
- 重写 `EvaluateEnemyPresence()`
- 让基地能感知雷达远程敌情

### 3. 雷达站组件扩展

修改：

```text
scripts/Game/GBRS/GBRS_RadarStationComponent.c
```

新增：

- 接触跟踪
- WLR 火控解算事件
- LOCK 状态事件
- 摧毁/关机事件
- Conflict 基地上报逻辑（直接在组件内实现，不依赖额外 Prefab 组件）

### 4. 建造布局兜底

修改：

```text
scripts/Game/GBRS/GBRS_PlaceableRegistryFix.c
```

- 在 `SCR_CampaignBuildingCompositionComponent.GetOutlineToSpawn()` 中增加兜底
- 当 Outline Manager 没有返回布局时，直接返回：

```text
Prefabs/Compositions/Misc/FreeRoamBuilding/Layouts/FRB_RadarStation_S_01.et
```

### 5. 修复重复组合体组件

修改：

```text
Prefabs/Compositions/Misc/FreeRoamBuilding/RadarStation_S_US_01.et
Prefabs/Compositions/Misc/FreeRoamBuilding/RadarStation_S_USSR_01.et
```

将：

```c
SCR_CampaignBuildingCompositionComponent "{69FCEDCE10011030}"
SCR_CampaignBuildingCompositionComponent "{69FCEDCE10021030}"
```

改为官方基础组件 ID：

```c
SCR_CampaignBuildingCompositionComponent "{5E96A067C097D570}"
```

原因：原来自定义 ID 会导致 E_ 实体上出现两个 `SCR_CampaignBuildingCompositionComponent`，造成重复初始化和布局生成异常。

### 6. 行尾统一

新增：

```text
.gitattributes
```

- 强制文本文件 LF
- 防止 Workbench 因 CRLF 解析失败

### 7. 其他

- 删除 `UserMaps.desc`（已 staged）
- 已移除临时调试文件：
  - `GBRS_RadarStationBaseLinkComponent.c`
  - `GBRS_DebugBuilding.c`

---

## 三、当前仍存在的问题

### 现象

在 Editor BUILDING 中放置 `E_RadarStation_S_US_01.et` 后：

- 直接生成完整雷达站实体；
- 没有出现建造区域/四角桩；
- 无法交互建造；
- 相当于“放置了成品”，而不是“放置了施工蓝图”。

### 已确认的日志信息

- 布局兜底已生效：
  - `GetOutlineToSpawn fallback -> GBRS radar layout`
- 布局组件已生成：
  - `after SpawnCompositionLayout m_CompositionLayout=1`
- 但仍出现：
  - `Cannot set entity as ACTIVE, it's not registered!`
- 最终行为仍是直接生成成品实体。

---

## 四、下一步排查方向

1. **重点对比官方 E_/Root 结构**
   - 官方可建造组合体的 `SCR_EditorLinkComponent` 与 `SCR_CampaignBuildingCompositionComponent` 如何配合；
   - 为什么官方放置时生成 Layout，而 GBRS 放置时直接生成成品。

2. **检查 E_ Prefab 的 LINKED_CHILDREN / VIRTUAL 行为**
   - 日志中实体 flags 含：
     - `PLACEABLE`
     - `VIRTUAL`
     - `LAYER`
     - `ORIENT_CHILDREN`
     - `LINKED_CHILDREN`
   - 需要确认这些 flags 是否导致 `SCR_EditorLinkComponent` 直接 Spawn 了成品子实体。

3. **确认 `SCR_EditorLinkComponent.IgnoreSpawning(true)` 是否在 GBRS E_ 上正确生效**
   - 如果 IgnoreSpawning 没生效，`SCR_EditorLinkComponent.EOnInit` 会直接 `SpawnComposition()`，从而生成成品雷达，而不是生成 Layout。

4. **可能需要调整 E_ Prefab 结构**
   - 官方 E_ 可建造组合体通常只包含：
     - `SCR_EditableEntityComponent`
     - `SCR_EditorLinkComponent`
     - 子实体 stub
   - 当前 GBRS E_ 是否继承了过多“成品”组件，导致被当作成品处理。

5. **考虑延迟初始化 RDF/雷达组件**
   - 在实体未注册时，RDF 组件尝试激活会产生：
     - `Cannot set entity as ACTIVE, it's not registered!`
   - 虽然可能不是主因，但建议后续在 `IsCompositionSpawned()` 前完全禁用 RDF 初始化。

---

## 五、当前 Git 状态摘要

```text
已修改/新增：
  .gitattributes
  Prefabs/Compositions/Misc/FreeRoamBuilding/RadarStation_S_US_01.et
  Prefabs/Compositions/Misc/FreeRoamBuilding/RadarStation_S_USSR_01.et
  scripts/Game/GBRS/GBRS_PlaceableRegistryFix.c
  scripts/Game/GBRS/GBRS_RadarStationComponent.c
  scripts/Game/GBRS/GBRS_CampaignMilitaryBaseComponent.c
  scripts/Game/GBRS/GBRS_RadarStationEvents.c
  docs/CURRENT_PROGRESS.md

已删除：
  UserMaps.desc
```

---

## 六、下次继续时的建议起点

1. 先 revert 或保留当前诊断结论；
2. 打开官方 `E_FieldHospital_M_US_01.et` 与当前 `E_RadarStation_S_US_01.et` 做逐字段 diff；
3. 重点检查：
   - `SCR_EditorLinkComponent`
   - 子实体 stub 的 `Flags`
   - `SCR_CampaignBuildingCompositionComponent`
4. 在 Workbench 中放置官方可建造物，确认官方是否也出现 `Cannot set entity as ACTIVE`；
5. 如果官方正常，则按官方 E_ 结构重建 GBRS E_ Prefab。
