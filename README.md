# Ground-based radar station (GBRS)

Arma Reforger 模组：把官方进近雷达模型做成可建造的 Conflict / Game Master 地面雷达站，探测走 [Radar Development Framework (RDF)](https://github.com/ViVi141/Radar-Development-Framework)。

- **Addon ID**：`Groundbasedradarstation`
- **GUID**：`69FCEDCE73FC5335`
- **Version**：1.1.7

## 依赖

| 模组 | GUID | 最低版本 |
|---|---|---|
| Radar Development Framework | `6892AB0669DF29AA` | **1.1.6** |
| 原版 | `58D0FB3206B6F859` | — |

## 放置

- **Conflict / FreeRoam**：施工卡车或基地建造菜单，铲完施工垫后才生成成品。
- **Game Master**：内容浏览器里的组合体。
- 可前出（`THEME_MILITARY`），不限制必须贴在基地里。

| | 美军 | 苏军 |
|---|---|---|
| 编辑器名 | US Ground Radar Station (AN/TPN-19) | USSR Ground Radar Station (RPL-5) |
| 成品 | `Prefabs/Compositions/Misc/FreeRoamBuilding/RadarStation_S_US_01.et` | `…/RadarStation_S_USSR_01.et` |
| 可放置 E_ | `E_RadarStation_S_US_01.et` | `E_RadarStation_S_USSR_01.et` |
| CAMPAIGN 造价 | 450 | 450 |
| 军衔 | 中士 (RANK_SERGEANT 3) | 同左 |
| 铲建值 | 250 | 250 |
| 基地建造点数 (PROPS) | 100 | 100 |

从 **基地施工台** 放置时，补给和建造点数是两道关：物资够但点数不够（HQ 上限 1200）会拒绝，提示基地组合体已达上限。100 点与原版中型车辆维修站同一档，高于停机坪（约 70）和小型宿舍，一座雷达会明显占掉基地额度。施工卡车本身不检查 HQ 建造点数（原版工事也一样）。

覆盖己方 Conflict 基地的雷达会把新空情和落在该基地半径内的 WLR 弹着点发到**锁定情报网**（官方手台，只听不发）：

| 阵营 | 频率 | VON 频道名 |
|---|---|---|
| 美军 | **45.6 MHz**（45600 kHz） | `RADAR NET` |
| 苏军 | **39.6 MHz**（39600 kHz） | `RADAR NET` |

开机才发射，用阵营加密。本机 TX 半径 **2 km**（AN/PRC-77）；更远靠己方 Conflict HQ 电台覆盖，或对开机的 `RelayTransceiver`（GM 塔、天线、指挥车）做中继跳转（最多 24）。手台本身不跳转。调到该频才会经电台 `OnDelivery` 播放自定义 ACP 语音（空情：网格 / 航向 / 高度；炮击：发射网格 / 落点网格 / ETA，美/俄语切片）。没调频的己方玩家**不收空情**，也不弹 `RADAR CONTACT`。施工完成会给同阵营发一次调频说明弹窗。CHANNEL 按键被拒。不会改原版 `EvaluateEnemyPresence`。不要调到排级网（美 48.0 / 苏 42.0）。

## 外形与射频

官方模型文件名曾把阵营写反。GBRS **按史实挂模型**：

| 阵营 | 外形（史实） | 天线 | 探测（玩法，不是进近雷达手册） |
|---|---|---|---|
| 美军 | **AN/TPN-19**（美空军野战着陆管制中心 / RAPCON） | 整面天线偏航 | 12 km 脉冲多普勒空搜（10 RPM，约 2.5°）；8 km WLR（360°，24°，6 RPM） |
| 苏军 | **Tesla RPL-5**（捷克联合无线电定位 / 进近雷达） | `antenna_rotation` 骨 | 16 km VHF 预警（P-18 式前端，6 RPM，约 6°）；10 km WLR（360°，30°，6 RPM） |

真实的 RPL-5 与 AN/TPN-19 都是机场进近 / 空管系统。本模组只用它们的外观，射频按 Conflict 空搜平衡，没有改成进近雷达量程。

## 工作台

靠近成品雷达，己方交互：

- 开关机（物资不够会拒绝开机；运行中扣不起则断电）
- 打开 PPI：`PD SEARCH` / `WLR` / `LOCK` / `MANUAL`（MANUAL 量程与转速钳在阵营空搜上限；LOCK 供火控消费，无 SAM/AAA 模组时仅站内锁定路径）
- 扫描体可视化（本地）

### Field Manual（战斗手册）

局内 `ESC` → **Field Manual** → **Gameplay** → **Commanding**：Ground Radar 七页（Quick Start / Workstation / RADAR NET / Supply & Capture / Reading the Scope / Interference & ECM / US vs USSR）。Override 原版 `FM_Commanding.conf`（GUID `{D9B6837CBB60A9EB}`）。

### Script Debugger 演示

Workbench **Play** 后在 Script Debugger 执行（一次只开一个；走成品射频，不走 ideal overlay）：

```
GBRS_RadarStationDemo.Start();      // 美军站 + PD SEARCH + 径向来回 Mi-8 + 打开 PPI
GBRS_RadarStationDemo.StartUssr();  // 苏军站
GBRS_RadarStationDemo.StartWlr();   // WLR + 周期性 82 mm 迫击炮弹
GBRS_RadarStationDemo.StartLock();  // 美军站 + LOCK（自动锁载具）
GBRS_RadarStationDemo.Probe();      // 强制一拍并打印点迹
GBRS_RadarStationDemo.Stop();
```

附近已有雷达就复用；没有则在玩家旁生成成品站并绕过物资开机。PPI 是 GBRS 工作台，不是 RDF AutoRunner 那套 HUD。↑/↓ 缩放显示距离。回归测试仍用 `GBRS_RadarStationAirborneAutoTest`。

### 搜索画面看到什么

主雷达搜索画面是皮回波：**没有二次雷达询问**，也**不会报载具型号**。

搜索模式会剥掉实体身份。列表里是匿名点迹的运动学：方位、距离、高度、速度、信噪比。类型栏多为 `ANON`。例外：正在辐射的雷达标 `EMIT`；WLR 把弹丸标成 `SHELL` 并估发射点 / 落点，不识别哪门炮。步兵不上屏。

多站 datalink overlay 另走 `GBRS_RadarIffResolver`（友绿 / 敌红 / 中立），按站点当前阵营上色。这不改变本地搜索点迹的匿名读出。

## 物资

开机期间每 **25 秒扣 15** 补给（约 0.6 / 秒）。

扣款顺序：

1. 覆盖这座站的 **己方 Conflict 基地**库存（站必须在该基地 `GetRadius()` 内；消费者阵营范围 135 m）
2. 本地地堡（容量 400）
3. 仍连着的施工卡车 / 建造提供者

## 占领与使用权

**覆盖基地被敌方占领（旗子易手，不是交火阶段）：**

1. 立刻断电，工作台关掉
2. 阵营跟着占领方走；占领方可以重新开机，之后扣**他们的**基地库存
3. 原阵营不能再开关机或开 PPI
4. 硬件外形不变（美军站仍是 AN/TPN-19 模型）

交火未结束时，雷达仍归防守方。

**基地半径外由施工卡车建造的前出雷达：** 阵营保持建造时的阵营。敌方没有使用权，只能打坏或铲残骸。远处基地易手不会把这座雷达交出去。

## 事件 API

其它模组可订阅 `GBRS_RadarStationEvents`：

- `OnRadarContact` / `OnRadarContactLost`
- `OnNetworkContact`（融合网里的新航迹，不是本站本地点迹）
- `OnWlrSolution`
- `OnLockChanged`
- `OnRadarDestroyed`

## 已知限制

- 本地 PPI 没有二次雷达询问；搜索点迹保持匿名。融合 overlay 才读 IFF
- 射频不是 RPL-5 / AN/TPN-19 的进近手册数据
- 对调模型后，编辑器缩略图可能仍是旧外观，需在 Workbench 重新生成预览
- WLR 全向扫描；悬停直升机速度读数建议局内再核一次
- 情报网本机 2 km；HQ / 中继跳转需局内核对

## 文档

- `docs/CHANGELOG.md` — 版本发布说明（中英）
- `docs/WORKSHOP_EN.txt` — Steam/Workshop 长描述（与代码量程对齐）
- `docs/WORKSHOP_UPDATE_1.1.7.txt` — 工坊更新说明（可粘贴）
- `docs/CURRENT_PROGRESS.md` — 施工垫 / 放置相关笔记
- `tools/PD_VALIDATION.md`、`tools/WLR_VALIDATION.md` — 离线探测标定
- `tools/WLR_TRACKER_VALIDATION.md` — WLR 轨道碎片化 / PPI 杂乱离线验证
- `tools/PD_TRACKER_VALIDATION.md` — PD 轨道碎片化 / PPI 杂乱离线验证
- `tools/WLR_ACCURACY_VALIDATION.md` — WLR 弹道解算精度离线验证
