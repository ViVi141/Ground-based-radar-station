# 防空（PD SEARCH）离线验证报告

日期：2026-08-16
脚本：`tools/simulate_pd_search.py`（复用 `simulate_clutter_cover.py` 的 1:1 物理链 + `make_us`/`make_ussr` 防空预设）

玩法射频（不是进近雷达手册）：美军 X 波段 SHORAD 7 km；苏军 P-18 式 VHF 预警 10 km。外形分别是 AN/TPN-19 / Tesla RPL-5。

## 一、验证目标

UH-1 级目标（均值 RCS ~16 m²）在当前局内预设下的探测概率：
- US SHORAD：7 km，10 RPM，2.5° 波束，SNR 门限 8 dB，CFAR-CA，DEM 杂波 1.0
- USSR P-18 VHF：10 km，6 RPM，6° 波束，SNR 门限 5 dB，CFAR-GO，DEM 杂波 **0.10**

## 二、结果（当前配置）

### US SHORAD（7 km）— PASS，热噪声限制

| 场景 | Pd_snr | Pd_cfar | 均值 SNR |
|---|---|---|---|
| 波束中心 迎头 50 m/s | 98% | 90% | 24.3 dB |
| 扫描照射 迎头 | 95% | 87% | 22.4 dB |
| 5 km 扫描照射 | 99% | 96% | 27.5 dB |
| 3 km 低空迎头 | 100% | 100% | 32.5 dB |

扫描照射（±1.25° 均匀）几乎不降 Pd。余量约 16 dB（24 dB − 8 dB 门限）。

### USSR P-18 VHF（10 km，DEM 0.10）— PASS，杂波仍可见

| 场景 | Pd_snr | Pd_cfar | 均值 SNR |
|---|---|---|---|
| 波束中心 迎头 | 77% | 77% | 8.6 dB |
| 扫描照射 迎头 | 77% | 77% | 8.6 dB |
| 7 km 扫描照射 | 73% | 73% | 7.2 dB |

比 2026-08-15 在 DEM 0.25 时的 57% / 6.3 dB 明显改善。宽波束下扫描照射与波束中心几乎同 Pd。7 km 略低于 10 km 是擦地角杂波几何，不是功率不够。

`calib_pd_full.py`（MTD 泄漏 + 相参积累）：US 在 DEM 1.0 时机身 R50 ≥ 10 km、旋翼 R50 ≥ 7 km；USSR 在 DEM 0.10 时两者 R50 都到脚本最大 10 km。不建议再把苏军 DEM 抬回 0.25。

## 三、已应用调参（历史）

| 参数 | USSR 原 → 现 | 依据 |
|---|---|---|
| `m_DemClutterScale` | 0.25 → **0.10** | 10 km 迎头 Pd 约 59% → 77% |
| 其他 | 不变 | 功率 / 门限 / 波束不改 |

## 四、离线链局限

1. **切向 0 m/s 离线显示 0%**：离线链是 TwoPulse MTI 零多普勒对消；局内是 **MTD_BANK 16 bin**，切向直升机靠旋翼边带仍可探测。
2. **random-dwell ~1%**：单帧波束占空比，不是整圈漏检。看扫描照射行。

## 五、结论

- **美军 7 km 防空强度足够**（Pd_cfar 87–90%，热噪声限制）。
- **苏军 10 km 预警可用**（DEM 0.10 下 Pd_cfar 77%，过 5 dB 门限约 3.6 dB）。
- 切向 / 悬停直升机请用局内 `GBRS_RadarStationAirborneAutoTest`。
