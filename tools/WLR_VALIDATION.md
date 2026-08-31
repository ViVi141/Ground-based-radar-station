# WLR 弹丸探测离线验证

日期：2026-08-31（功率重平衡）；几何对齐 `CreateUsWlr` / `CreateUssrWlr`
脚本：`tools/simulate_wlr_projectile.py`、`tools/out/balance_rf_sweep.py`（无 DEM 杂波，与局内 `ApplyWlrFidelity` 一致）

## 一、验证目标

0.01 m² 弹丸（82 mm 迫击炮）在全周机械扫描 + 自动收窄下能否过 SNR 门限。

当前局内配置（`GBRS_RadarStationConfig`）：

- US：8 km，**250 kW**，方位 **24°**，**6 RPM**，门限 **4.0 dB**，更新 **0.08 s**
- USSR：10 km，**CreateP18Like** VHF **250 kW** / 20 dBi，方位 **30°**，门限 5.0 dB
- 仰角：`flat` / `mortar_low` / `mortar_mid` / `mortar_high`

## 二、当前配置（PASS）

| 指标 | US 8 km | USSR 10 km |
|---|---|---|
| 波束中心最大 SNR | **5.9 dB**（门限 4.0 → PASS，余量 +1.9） | **~24 dB**（门限 5.0 → PASS） |
| 相对旧 500 kW | −3.0 dB | 不变（本就 250 kW） |

200 kW 中心仅 4.9 dB（余量 +0.9），过紧，不采用。Firefinder 公开峰值 ~23 kW 带 chirp；本产品无 chirp，**250 kW** 是可玩的下限。

## 三、功率敏感性（US 8 km，波束中心，2026-08-31）

| 峰值功率 | 最大 SNR | 对局内 4 dB 门限 |
|---|---|---|
| 23 kW（Firefinder 量级） | −4.5 dB | FAIL |
| 100 kW | 1.9 dB | FAIL |
| 200 kW | 4.9 dB | PASS（紧） |
| **250 kW（现用）** | **5.9 dB** | **PASS** |
| 500 kW（旧） | 8.9 dB | PASS |

## 四、实现位置

- `CreateUsWlr()`：**250 kW**，门限 4.0 dB，方位 **24°**
- `CreateUssrWlr()`：**CreateP18Like** VHF（160 MHz / **250 kW** / 20 dBi），门限 5.0 dB，方位 **30°**
- `ApplyWlrProductFlags()`：360° → 航迹后自动 ±40°、6 RPM

## 五、剩余风险

1. 离线链按热噪声限制；若重新打开 WLR DEM 杂波，结论不能直接用。
2. 美军方位偏移 10° 时边缘 SNR 约 1.7 dB（门限下），仍靠多次扫过走廊中心凑 hit。
3. 解算质量需局内打炮确认。
