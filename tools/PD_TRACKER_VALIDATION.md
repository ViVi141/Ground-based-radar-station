# PD 轨道碎片化离线验证

脚本：`tools/simulate_pd_tracker.py`
输出：`tools/out/pd_tracker_validation.json`

## 为什么需要这个验证

原来的 `simulate_pd_search.py` 只验证探测概率：

- UH-1 在波束中心/扫描照射下的 Pd
- SNR / CFAR 是否够

它没有验证：

- 机械扫描下 tracker 能否跨波束间隙 coast
- 测量噪声是否会把同一架飞机拆成多条 track
- PPI 上同时存在的航迹数量是否正常

所以防空也可能出现“混乱感知和绘制”。

## 验证内容

模拟 3 架 UH-1 从不同方位径向飞向雷达，统计：

- `tracks_per_aircraft`：每架飞机被拆成几条航迹
- `fragmented_aircraft`：被拆成多条航迹的飞机数量
- `stable_aircraft`：能形成稳定确认航迹的飞机数量
- `max_alive` / `mean_alive`：PPI 上同时存在的航迹数量
- `fragmentation_ratio`：总航迹数 / 飞机数

## 当前结果（离线模型）

局内 `ApplyFullFidelity` 关联门是共享 **8° / 600 m**（美军只把 coast 加到 16 s）。

| 阵营 | 配置 | 每架飞机航迹数 | 判定 |
|---|---|---|---|
| US | 2026-08-20（关噪声、8°/600m、maxMiss 600、coast 16s） | 4 / 4 / 4 | FAIL |
| US | 历史（关噪声、14°/900m、maxMiss 600、coast 16s） | 4 / 4 / 4 | FAIL |
| US | 旧（噪声 3.5、4°/400m、maxMiss 6） | 7 / 7 / 7 | FAIL |
| USSR | 历史（关噪声、8°/600m、maxMiss 600） | 1 / 1 / 1 | PASS |
| USSR | 旧（噪声 3.5、4°/400m、maxMiss 6） | 11 / 14 / 10 | FAIL |

说明：

- 苏军宽波束 + 慢扫描在历史离线模型下能稳定保持 1 条航迹/架
- 美军窄波束 + 快扫描把门从 14°/900 m 收回 8°/600 m 后仍是 4 条/架；PPI 8°/700 m 显示聚类只治画面。RDF tracker 需 `AirborneAutoTest` 对照
- 2026-08-20 用当前 `make_ussr()` 重跑时苏军 0 条点迹（探测链与 tracker 脚本未对齐），**不覆盖** 上表历史 PASS，也不代表局内苏军搜不到机

## 运行方法

```bash
python tools/simulate_pd_tracker.py
```

## 局限

- 简化 GNN tracker，不是 RDF 1:1
- 没有模拟 JPDA、PRF 盲速、旋翼边带等全部细节
- 主要用于快速暴露轨道碎片化和 PPI 航迹数量异常
