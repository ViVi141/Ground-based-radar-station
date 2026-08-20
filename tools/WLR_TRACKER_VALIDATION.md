# WLR 轨道碎片化离线验证

脚本：`tools/simulate_wlr_tracker.py`
输出：`tools/out/wlr_tracker_validation.json`

## 为什么需要这个验证

原来的 `simulate_wlr_projectile.py` 只验证：

- 炮弹 SNR 是否够
- 旋转扫描照射窗口是否够

它没有模拟：

- RDF tracker 的轨道关联
- 测量噪声
- miss / coast / prune
- 同一发炮弹是否被拆成多条 track
- PPI 上同时存在的轨道数量

所以游戏里出现“天女散花”时，旧离线验证无法发现。

## 验证内容

脚本模拟 30 秒、每 6 秒发射一发 82 mm 炮弹，并统计：

- `tracks_per_shell`：每一发炮弹被拆成几条轨道
- `fragmented_shells`：被拆成多条轨道的炮弹数量
- `stable_shells`：能形成稳定确认轨道的炮弹数量
- `max_alive` / `mean_alive`：PPI 上同时存在的轨道数量
- `max_confirmed_alive` / `mean_confirmed_alive`：PPI 上同时存在的已确认轨道数量
- `fragmentation_ratio`：总轨道数 / 炮弹数
- `verdict`：是否通过

## 判定标准

- `fragmented_shells == 0`
- `stable_shells == total_shells`

满足两者为 `PASS`。

## 当前结果

| 配置 | 炮弹数 | 每发轨道数 | 碎片炮弹 | 稳定炮弹 | 判定 |
|---|---|---|---|---|---|
| 当前 GBRS（关噪声、8°/600m、maxMiss 96） | 5 | 1 / 1 / 1 / 1 / 1 | 0 | 5 | PASS |
| 旧配置（噪声 3.5、4°/400m、maxMiss 6） | 5 | 3 / 1 / 1 / 0 / 0 | 1~2 | 3 | FAIL |

说明：

- 关联门 8° / 600 m 与局内 `ApplyWlrProductFlags` 一致。扫描几何是扇扫（见 `WLR_VALIDATION.md`），本脚本不模拟扇扫，只测 tracker 碎片
- 当前 WLR 配置在离线模型下不会把同一发炮弹拆成多条轨道
- 旧的“高噪声 + 紧门限 + 低 miss 上限”配置确实会产生轨道碎片化

## 运行方法

```bash
python tools/simulate_wlr_tracker.py
```

可选：

```bash
python tools/simulate_wlr_tracker.py --faction US
python tools/simulate_wlr_tracker.py --faction USSR
```

## 局限

- 这是简化 GNN tracker 模型，不是 RDF 游戏内代码的 1:1 移植
- 没有模拟 JPDA、PRF 盲速、多径/衍射细节
- 主要用于快速发现“轨道碎片化”和“PPI 轨道数量异常”这类问题
