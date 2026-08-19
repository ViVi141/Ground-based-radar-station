# 防空（PD SEARCH）离线验证报告

日期：2026-08-19（RDF 1.1.0 波段杂波对齐）
脚本：`tools/simulate_pd_search.py`（`simulate_clutter_cover.py` + `gbrs_rdf_band.py` + `calib_pd_full.py` MTD 路径）

玩法射频：美军 X 波段 SHORAD 7 km；苏军 P-18 式 VHF 预警 10 km。

## 一、验证目标

UH-1 级目标（均值 RCS ~16 m²）在当前局内预设下的探测概率：

- US SHORAD：7 km，10 RPM，2.5° 波束，SNR 门限 8 dB，CFAR-CA，DEM 杂波 1.0，**TwoPulse 离线链**（切向行见局限）
- USSR P-18 VHF：10 km，6 RPM，6° 波束，SNR 门限 5 dB，CFAR-GO，DEM 杂波 **0.50**，**MTD_BANK 离线链**（与局内 `GBRS_RadarStationConfig` 一致）

杂波 σ⁰ 按载频选 VHF/X 波段表（RDF 1.1.0 `GetBand()`），见 `tools/gbrs_rdf_band.py`。

## 二、结果（当前配置）

### US SHORAD（7 km）— PASS，热噪声限制

| 场景 | Pd_snr | Pd_cfar | 均值 SNR |
|---|---|---|---|
| 波束中心 迎头 50 m/s | 98% | 90% | 24.3 dB |
| 扫描照射 迎头 | 95% | 87% | 22.4 dB |
| 5 km 扫描照射 | 99% | 96% | 27.5 dB |
| 3 km 低空迎头 | 100% | 100% | 32.5 dB |

### USSR P-18 VHF（10 km，DEM 0.50，MTD_BANK）— PASS

| 场景 | Pd_snr | Pd_cfar | 均值 SNR |
|---|---|---|---|
| 波束中心 迎头 50 m/s | 100% | 100% | 48.0 dB |
| 波束中心 慢径向 5 m/s | 100% | 100% | 48.0 dB |
| 7 km 波束中心 迎头 | 100% | 100% | 53.9 dB |
| 波束中心 切向 0 m/s | 0% | 0% | -7.9 dB |

`calib_pd_full.py` 在波段表 + MTD 下推荐 `dem_clutter_scale=0.50`（R50 机身/旋翼均 ≥ 10 km）。

## 三、RDF 1.1.0 对齐变更

| 项 | 变更 |
|---|---|
| 杂波 σ⁰ | 按载频 VHF/L/S/C/X 五表（`gbrs_rdf_band.py`） |
| 植被衰减 | 按波段缩放（VHF 0.20× … X 1.0×） |
| RCS | OBB 零滚转投影（`AspectRcsFromExtents3D` 路径） |
| `m_DemClutterScale`（苏军） | 0.10 → **0.50**（旧值针对单表 X 波段 + 错误标定） |

## 四、离线链局限

1. **美军切向 0 m/s 显示 0%**：离线 TwoPulse；局内 MTD_BANK + 旋翼边带仍可探测。
2. **苏军切向 0 m/s MTD 路径仍 0%**：慢径向 / 旋翼边带请用局内 `GBRS_RadarStationAirborneAutoTest`。
3. **random-dwell ~1%**：单帧波束占空比；看扫描照射行。

## 五、结论

- **美军 7 km 防空强度足够**（Pd_cfar 87–90%）。
- **苏军 10 km 在 RDF 1.1.0 波段杂波 + DEM 0.50 下离线 PASS**（MTD 迎头 Pd_cfar 100%）。
- 局内最终验收仍建议 `GBRS_RadarStationDemo` / `GBRS_RadarStationAirborneAutoTest`。
