# 防空（PD SEARCH）离线验证报告

日期：2026-08-15
脚本：`tools/simulate_pd_search.py`（复用 `simulate_clutter_cover.py` 的 1:1 物理链 + `make_us`/`make_ussr` 防空预设）

## 一、验证目标

GBRS 防空 PD SEARCH 配置对 UH-1 级目标（均值 RCS ~16 m²，旋翼边带）的探测概率：
- US RPL-5：7 km，10 RPM，2.5° 波束，SNR 门限 8 dB，CFAR-CA，DEM 杂波 1.0
- USSR TPN-19：10 km，6 RPM，6° 波束，SNR 门限 5 dB，CFAR-GO，DEM 杂波 0.25

## 二、结果

### US RPL-5（7 km）— 优秀，无需调整

| 场景 | Pd_snr | Pd_cfar | 中位 SNR |
|---|---|---|---|
| 波束中心 迎头 50 m/s | 98% | 90% | 24.3 dB |
| 扫描照射 迎头 | 95% | 87% | 22.4 dB |
| 5 km 扫描照射 | 99% | 96% | 27.5 dB |

- US 是**热噪声限制**（杂波/热噪声 ≈ 0.1），SNR 余量充足；
- 扫描照射（±1.25° 均匀）几乎不降 Pd——2.5° 波束内探测稳定。

### USSR TPN-19（10 km）— 杂波限制，需调参

| 场景 | Pd_snr | Pd_cfar | 中位 SNR |
|---|---|---|---|
| 波束中心 迎头 | 57% | 57% | 6.3 dB |
| 扫描照射 迎头 | 57% | 57% | 4.6 dB |

- USSR 是**杂波限制**：DEM 杂波（缩放 0.25）比热噪声高 4 个数量级，主导噪声；
- 功率提升**无效**（杂波同比例上升）——VHF 地面 σ₀ 强；
- 中位 SNR 6.3 dB 刚过 5 dB 门限，RCS 波动导致 Pd 仅 57%。

## 三、离线调参（已应用）

| 参数 | USSR 原 → 新 | 依据 |
|---|---|---|
| `m_DemClutterScale` | 0.25 → **0.10** | Pd 59% → **80%**，中位 SNR 6.3 → 10.3 dB |
| 其他 | 不变 | 功率/门限/波束均不改 |

`m_DemClutterScale` 扫描（USSR 10km，UH-1 迎头，门限 5 dB）：
- 0.25 → Pd 59%（中位 6.3 dB）
- 0.10 → Pd 80%（中位 10.3 dB）
- 0.05 → Pd 89%（中位 13.3 dB）
- 0.02 → Pd 95%（中位 17.3 dB）

US RPL-5 保持 `m_DemClutterScale=1.0` 不变（热噪声限制，杂波占比仅 10%）。

## 四、离线链局限（影响结论可靠性）

1. **切向目标（0 m/s）离线显示 0%**：离线链只实现 TwoPulse MTI（`sin²` 对消零多普勒）；GBRS 实际用 **MTD_BANK 16 bin**，切向直升机靠**旋翼边带**（`FillDopplerSpectrum`）落入非零 bin 仍可探测。**切向 0% 是离线模型局限，非真实行为**；
2. **random-dwell 1%**：模拟单帧命中概率（波束占空比），非整体漏检率——机械扫描每圈多次照射，实际用"扫描照射"场景（87-96%）。

## 五、结论

- **US 防空无需调整**（7 km Pd 90%+，热噪声限制，余量充足）；
- **USSR 防空已调**：`m_DemClutterScale 0.25→0.10`，10 km Pd 59%→80%；
- 切向/悬停直升机的真实探测依赖 RDF 的 MTD+旋翼模型，离线链无法验证，建议游戏内实测（`RDF_RadarAirborneScanTest` 或 GBRS 自带的 `GBRS_RadarStationAirborneAutoTest` 绕飞场景）。
