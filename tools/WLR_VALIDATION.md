# WLR 弹丸探测离线验证与调参结论

日期：2026-08-15
脚本：`tools/simulate_wlr_projectile.py`（复用 `simulate_clutter_cover.py` 的 1:1 物理链）

## 一、验证目标

GBRS WLR（反炮兵）需在旋转扫描下探测 0.01 m² 弹丸并解算发射/落点。验证两点：
1. SNR 可行性（8 km US / 10 km USSR 弹丸探测）；
2. 旋转扫描命中预算（WeaponLocate 需 ≥5 次命中）。

## 二、发现的问题（当前配置 FAIL）

| 指标 | US 8km（当前） | USSR 10km（当前） |
|---|---|---|
| 波束中心最大 SNR | **2.4 dB**（gate 6 → FAIL） | **-1.6 dB**（gate 5 → FAIL） |
| 波束边缘（12°）SNR | -3.2 dB | ~-6 dB |
| 照射窗口数（25s 飞行） | 45（≥5，PASS） | 51（≥5，PASS） |

- **SNR 不足是主因**：当前 120 kW + 6/5 dB 门限对 0.01 m² 弹丸不可达；
- **命中窗口充足**：旋转扫描本身不是瓶颈（波束每次扫过产生多个 dwell）；
- 波束宽度对中心 SNR 无影响（绝对增益固定 32 dBi），只影响边缘衰减；
- 方位偏移显著恶化 SNR：10° 偏移 -4 dB、12° 偏移 -5.6 dB。

## 三、离线调参推荐（已验证 PASS）

| 参数 | US 推荐 | USSR 推荐 |
|---|---|---|
| 峰值功率 `m_PeakPowerW` | **500,000 W** | **1,000,000 W** |
| SNR 门限 `m_DetectionSnrDb` | **2.0 dB** | **0.0 dB** |
| 波束宽度 `m_AzimuthBeamwidthDeg` | 25°（不变） | 30°（不变） |
| 转速 `m_ScanRpm` | 10（不变） | 6（不变） |
| 仰角波束 | 18/35/55°（不变） | 18/35/55°（不变） |

推荐组合下（波束中心 @ 15° 俯仰）：
- US 8km：8.6 dB（中心）、4.7 dB（10°）、3.0 dB（12°）——25° 波束内 12° 全 DET；
- USSR 10km：7.6 dB（中心）、5.0 dB（10°）、1.6 dB（15°）——30° 波束全宽 DET。

## 四、实现位置

改动在 `scripts/Game/GBRS/GBRS_RadarStationConfig.c`：
- `CreateUsWlr()`：`m_DetectionSnrDb 6→2`，`m_Hardware.m_PeakPowerW 120000→500000`
- `CreateUssrWlr()`：`m_DetectionSnrDb 5→0`，`m_Hardware.m_PeakPowerW 120000→1000000`

## 五、剩余风险

1. **波束边缘盲区**（US 15° 外）：弹丸在波束边缘时 miss，但 5-hit 门槛可靠中心命中凑够——风险可接受；
2. **弹道模型固定 82mm**：非 82mm 弹丸解算有偏差（RDF 设计简化）；
3. **低仰角盲区**（<18° 弹道段）：发射初期/近落点漏检（有意避免地面杂波）；
4. 建议后续用游戏内 `RDF_RadarShellFireAutoTest` 或手动发射迫击炮实测验证。
