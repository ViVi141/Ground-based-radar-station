# WLR 弹丸探测离线验证

日期：2026-08-20（扇扫几何与 `CreateUsWlr` / `CreateUssrWlr` 对齐）
脚本：`tools/simulate_wlr_projectile.py`（无 DEM 杂波，与局内 `ApplyWlrFidelity` 关闭地面杂波一致）

## 一、验证目标

0.01 m² 弹丸（82 mm 迫击炮）在 **±45° 扇扫** 下能否过 SNR 门限，以及照射窗口是否够 `WeaponLocateMinHits = 3`。

当前局内配置（`GBRS_RadarStationConfig`）：

- US：8 km，500 kW，方位 **12°**，**6 RPM** 自旋门，±45° 走廊、1.2 rad/s，门限 **4.0 dB**，更新 **0.15 s**
- USSR：10 km，P-18 默认 **250 kW**，方位 **15°**，同样扇扫，门限 5.0 dB，更新 0.15 s
- 仰角波束：`mortar_low` 15°/28°、`mortar_mid` 35°/30°、`mortar_high` 55°/28°

2026-08-16 报告测的是旧旋转扫描（US 25° / 10 RPM / 0.05 s，USSR 30° / 6 RPM）。中心 SNR 与波束宽度无关；照射窗口和方位边缘不可直接对比。

## 二、当前配置（PASS）

| 指标 | US 8 km | USSR 10 km |
|---|---|---|
| 波束中心最大 SNR | **8.9 dB**（门限 4.0 → PASS） | **24.1 dB**（门限 5.0 → PASS） |
| 仰角 15 / 25 / 40 / 55° | 全部 DET | 全部 DET |
| 扇扫照射窗口（约 90 s 弹道） | 80（≥3，PASS） | 100（≥3，PASS） |

美军方位偏移（12° 波束）：0° 8.9 dB DET，5° 4.7 dB DET，**10° -7.8 dB miss**。比旧 25° 波束更早掉出主瓣。

苏军 24.1 dB 对应真实 250 kW。旧报告里的 29.8 dB 是脚本误用 1 MW（+6 dB）造成的，不是射频更强。

## 三、功率敏感性（US 8 km，波束中心）

| 峰值功率 | 最大 SNR | 对 6 dB 门限 | 对局内 4 dB 门限 |
|---|---|---|---|
| 120 kW | 2.7 dB | FAIL | FAIL |
| 250 kW | 5.9 dB | FAIL | PASS |
| **500 kW（现用）** | **8.9 dB** | PASS | PASS |

现用门限是 4 dB，500 kW 中心有约 4.9 dB 余量。波束宽度不改变峰值增益（高斯主瓣峰值 1.0）。

## 四、实现位置

`scripts/Game/GBRS/GBRS_RadarStationConfig.c`：

- `CreateUsWlr()`：500 kW，门限 4.0 dB，方位 12°
- `CreateUssrWlr()`：P-18 250 kW，门限 5.0 dB，方位 15°
- `ApplyWlrProductFlags()`：±45° 扇扫、6 RPM、0.15 s、仰角三波束
- `m_DemClutterScale = 0.10` 写在苏军 WLR 上，但局内 `ApplyWlrFidelity` 已关 DEM 杂波，该缩放不影响这条离线链

## 五、剩余风险

1. 离线链按热噪声限制；若以后重新打开 WLR DEM 杂波，这条结论不能直接用。
2. 美军 10° 方位偏移已 miss；扇扫必须多次扫过走廊中心才能凑满 3 hit。
3. 弹道按 82 mm 固定；低仰角段仍可能漏。
4. 离线扇扫是三角波走廊模型，不是 RDF `SectorSweep` 1:1。解算质量需局内打炮确认。
