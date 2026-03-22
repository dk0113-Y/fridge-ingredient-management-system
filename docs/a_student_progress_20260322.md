# A 同学阶段进展说明（2026-03-22）

## 1. 当前定位

本说明用于同步 `cpp/` 侧视觉主链在 2026-03-22 的实现进度，方便与负责 `python/` 后端、库存和 Web 的同学对齐。

当前阶段仍然严格按赛题第一阶段目标推进：

1. 一段本地视频视为一次操作会话
2. 先做 ROI 内真实事件识别和物体存在性判断
3. 当前不接真实分类模型，只为后续粗分类预留接口
4. C++ 只输出结构化事件结果，不承担数据库和 Web 逻辑

## 2. 今日已完成内容

### 2.1 C++ 主链已打通

当前 `cpp/` 已可完成以下闭环：

1. 读取本地视频或调试帧目录
2. 在 ROI 内做运动前景分析
3. 从“稳定 -> 扰动 -> 再稳定”过程中提取 `before` / `after`
4. 对前后稳定帧做变化区域分析
5. 输出事件类型：
   - `no_change`
   - `put_in`
   - `take_out`
   - `partial_take_out_candidate`
6. 输出联调用文件：
   - `before.jpg`
   - `after.jpg`
   - `overlay.jpg`
   - `event.json`
   - `debug.json`

### 2.2 输出目录已整理

当前所有单次运行结果统一输出到：

`data/sessions/<视频名_时间>/`

单次结果目录内固定包含：

1. `before.jpg`
2. `after.jpg`
3. `overlay.jpg`
4. `event.json`
5. `debug.json`

说明：

- 原来的 `diff.jpg` 已取消，改为 `overlay.jpg`
- `overlay.jpg` 是在 `after` 图上叠加 ROI 和变化区域框，更适合人工排查识别是否盯准了目标区域

### 2.3 配置与调试能力已补齐

当前 demo 支持：

1. 默认读取 `configs/vision_stage1.cfg`
2. 命令行覆盖 `--config`
3. 命令行覆盖 `--roi x,y,w,h`
4. 命令行覆盖 `--roi-id name`

`debug.json` 当前会额外记录：

1. 输入路径
2. 配置路径
3. ROI 信息
4. 阈值参数
5. `before_index` / `after_index`
6. 关键 transition 摘要
7. 输出文件路径

### 2.4 测试与验证已补充

当前已覆盖的典型测试包括：

1. `no_change`
2. `put_in`
3. `take_out`
4. `partial_take_out_candidate`
5. 低照度放入
6. 手部短时遮挡但最终无变化
7. ROI 外扰动忽略
8. 整理位置但库存不变
9. 尾部黑帧/结束黑屏不应误选为 `after`
10. `overlay` 调试图绘制检查

## 3. 今天的批量测试结果

已对当前仓库中的 `video/` 目录样例完成批量运行，当前输出已同步到仓库的 `data/sessions/`。

本次会话结果如下：

| 会话目录 | 识别结果 | 备注 |
| --- | --- | --- |
| `fangru_20260322_205710` | `put_in` | 放入样例 |
| `fangrunachu_20260322_205718` | `no_change` | 放入再拿出后回到原状态 |
| `nachu_20260322_205729` | `take_out` | 取出样例 |
| `nachufangru_20260322_205736` | `no_change` | 取出再放回后回到原状态 |

说明：

- 从当前批量结果看，基础“放入 / 取出 / 最终无库存变化”三类已能区分
- `overlay.jpg` 可用于快速核查算法是否框住了真实变化区域

## 4. 与 Python 同学的协作边界

当前边界保持如下，不建议跨界修改：

### C++ 负责

1. 输入视频
2. 关键帧提取
3. 事件识别
4. 输出 `event.json`
5. 输出调试图与 `debug.json`

### Python 负责

1. 扫描 `data/sessions/**/event.json`
2. 事件入库
3. 库存更新
4. 待确认事件管理
5. Web 展示与交互

## 5. 当前仍未完成的部分

以下内容仍属于 A 同学后续工作重点：

1. 真实场景下更多视频样例的阈值标定
2. `partial_take_out_candidate` 的进一步稳定化
3. 粗分类接口后续接入轻量模型
4. 面向 Ubuntu / 嵌入式 Linux 的性能裁剪和部署适配
5. 批量测试结果自动汇总工具

## 6. 建议同伴当前如何联调

对负责 Python 的同学，当前可直接基于以下假设联调：

1. C++ 每次运行后会在 `data/sessions/<session_id>/event.json` 生成事件结果
2. `event_type` 已限制在第一阶段的 4 类中
3. `event.json` 协议仍对齐 `shared/event_schema.json`
4. 不需要等待分类模型即可先打通库存更新闭环

## 7. 当前结论

截至 2026-03-22，A 同学负责的第一阶段 C++ 视觉主链已经具备“可运行、可调试、可联调”的最小闭环，已经能够支撑后端同学继续推进事件导入、库存更新和 Web 展示。
