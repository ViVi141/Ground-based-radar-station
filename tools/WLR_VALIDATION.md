# WLR 弹丸探测离线验证

日期：2026-08-16
脚本：`tools/simulate_wlr_projectile.py`（无 DEM 杂波，与局内 `ApplyWlrFidelity` 关闭地面杂波一致）

## 一、验证目标

0.01 m² 弹丸（82 mm 迫击炮）在旋转扫描下能否过 SNR 门限，以及照射窗口是否够 `WeaponLocateMinHits = 3`。

当前局内配置：
- US：8 km，500 kW，25°，10 RPM，门限 **4.0 dB**，更新 0.05 s
- USSR：10 km，P-18 默认 **250 kW**，30°，6 RPM，门限 5.0 dB

## 二、当前配置（PASS）

| 指标 | US 8 km | USSR 10 km |
|---|---|---|
| 波束中心最大 SNR | **8.6 dB**（门限 4.0 → PASS） | **23.8 dB**（门限 5.0 → PASS） |
| 仰角 15 / 25 / 40 / 55° | 全部 DET | 全部 DET |
| 照射窗口（约 90 s 弹道） | 136（≥3，PASS） | 154（≥3，PASS） |

美军方位偏移：0° 8.6 dB DET，10° 4.7 dB DET，**12° 3.0 dB miss**（低于 4 dB 门限，波束边缘）。

苏军 23.8 dB 对应真实 250 kW。旧报告里的 29.8 dB 是脚本误用 1 MW（+6 dB）造成的，不是射频更强。

## 三、功率敏感性（US 8 km，波束中心）

| 峰值功率 | 最大 SNR | 对 6 dB 门限 |
|---|---|---|
| 120 kW | 2.4 dB | FAIL |
| 250 kW | 5.6 dB | FAIL |
| **500 kW（现用）** | **8.6 dB** | PASS |

现用门限是 4 dB，500 kW 中心有约 4.6 dB 余量。

## 四、实现位置

`scripts/Game/GBRS/GBRS_RadarStationConfig.c`：
- `CreateUsWlr()`：500 kW，门限 4.0 dB
- `CreateUssrWlr()`：P-18 250 kW，门限 5.0 dB；`m_DemClutterScale = 0.10`（局内 WLR 已关 DEM 杂波，该缩放不影响这条离线链）

## 五、剩余风险

1. 离线链按热噪声限制；若以后重新打开 WLR DEM 杂波，这条结论不能直接用。
2. 美军 12° 方位边缘会 miss，靠多次中心照射凑满 3 hit。
3. 弹道按 82 mm 固定；低仰角段仍可能漏。
4. 解算质量需局内打炮确认（`RDF_RadarShellFireAutoTest`）。
