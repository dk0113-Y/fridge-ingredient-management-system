# 任务分工说明

## 目标

面向比赛第一阶段，先完成一个“软件先行”的本地闭环：

1. C/C++ 侧读取本地视频。
2. 识别冰箱真实交互事件。
3. 导出结构化 `event.json`。
4. Python 侧接收事件。
5. 更新 SQLite 库存状态。
6. 在本地 Web 页面展示库存、最近事件、待确认项和手动修正入口。

## A 同学任务

### 负责范围

A 同学负责 `cpp/` 下的 C/C++ 视觉主链。

### 具体职责

- 维护 `cpp/CMakeLists.txt`，保证第一阶段目标可构建。
- 实现并持续完善以下模块：
  - `video_io`
  - `roi_motion`
  - `frame_selector`
  - `event_detector`
  - `main.cpp`
- 将第一阶段事件输出限定为以下四类：
  - `no_change`
  - `put_in`
  - `take_out`
  - `partial_take_out_candidate`
- 输出以下调试与联调文件：
  - `data/sessions/<session_id>/before.*`
  - `data/sessions/<session_id>/after.*`
  - `data/sessions/<session_id>/overlay.*`
  - `data/sessions/<session_id>/event.json`
- 分类器只保留接口与占位实现，不接真实模型。
- 扩展以下典型场景测试：
  - 手部干扰
  - 短时遮挡
  - 整理食材但库存不变
  - 低照度场景
- 在关键位置保留清晰 TODO，说明：
  - 后续接入分类模型
  - 后续接入摄像头视频流
  - 后续迁移到板端

### 交付物

- 可运行的 `fridge_vision_demo`
- 与 `shared/event_schema.json` 对齐的稳定 `event.json`
- C++ 测试用例与更新后的视觉流程文档

## B 同学任务

### 负责范围

B 同学负责 `python/` 下的后端与库存闭环。

### 具体职责

- 维护 `python/requirements.txt`。
- 实现并持续完善以下模块：
  - `app/main.py`
  - `app/api`
  - `app/services`
  - `app/models`
  - `app/db`
  - `app/schemas`
- 维护与第一阶段一致的 SQLite 表结构：
  - `inventory_items`
  - `events`
  - `pending_confirmations`
- 读取 `data/sessions/**/event.json`，并保证事件导入幂等。
- 实现库存更新规则：
  - `put_in` -> `count + 1`
  - `take_out` -> `count - 1`
  - `partial_take_out_candidate` -> 更新 `remain_level` 和/或写入待确认项
  - `no_change` -> 只记录事件
- 提供并维护以下接口：
  - `GET /health`
  - `GET /events`
  - `GET /inventory`
  - `POST /confirm`
- 保证本地页面可用于演示：
  - 当前库存
  - 最近事件
  - 待确认项
  - 手动修正入口
- 补充事件接入、库存更新、确认流程相关测试。

### 交付物

- 可运行的 Flask 后端
- 可初始化的 SQLite 数据库
- 可演示的本地 Web 页面
- 通过的后端测试

## 共享任务

### 共享协议

- 维护 `shared/event_schema.json` 作为 C++ 与 Python 之间的唯一协议。
- 任何协议变更都必须在同一分支或同一 PR 中同步修改两侧实现。

### 共享文档

- 保持以下文档与实现一致：
  - `docs/architecture.md`
  - `docs/vision_pipeline.md`
  - `docs/backend_api.md`
  - `docs/db_design.md`
  - `docs/dev_plan.md`
  - `docs/task_split.md`

### 联调要求

- 以 `dev` 作为集成分支。
- 日常开发统一在 `feature/*` 分支进行。
- 合并前至少跑通一次本地端到端检查：
  1. C++ 生成 `event.json`
  2. Python 成功导入事件
  3. SQLite 正确更新状态
  4. Web 页面反映最终结果

### 协作规则

- 不要随意修改事件类型命名。
- 不要绕过 `shared/event_schema.json` 私自扩字段。
- 生成文件除已跟踪占位外，不提交到 Git。
- 阈值或库存规则调整后，要同步更新文档。

## 里程碑

### M1：闭环骨架完成

- C++ 可以导出合法事件 JSON。
- Python 可以导入 JSON 并写入 SQLite。
- 本地页面可以展示库存与最近事件。

### M2：面向比赛场景增强

- C++ 测试覆盖手部干扰、遮挡、整理无变化、低照度。
- Python 可以通过待确认项处理模糊事件。
- 本地端到端演示可以重复跑通。

### M3：硬件迁移前收口

- 接口稳定到足以支撑板端迁移。
- 分类器、摄像头流、嵌入式运行时的 TODO 足够明确。
- 文档已经为下一阶段实现做好铺垫。

## 验收标准

### 视觉侧

- 给定有效本地输入，C++ demo 能产出关键帧、差分图和 `event.json`。
- 导出的事件类型必须属于第一阶段规定的四类之一。
- 导出的 JSON 字段必须符合共享协议要求。

### 后端侧

- 后端能导入未处理过的事件 JSON，且不会重复入库。
- 库存更新符合第一阶段规则。
- 模糊的局部取出事件会进入待确认流程。
- 手动修正可以通过页面或 API 修改库存。

### 端到端演示

- `put_in` 事件会增加库存。
- `take_out` 事件会减少库存。
- `partial_take_out_candidate` 事件会出现在待确认列表中。
- `no_change` 事件只记录，不改变库存。

### 文档与协作

- 分工清晰到 A、B 两位同学可以并行开发。
- 分支流程保持 `main` / `dev` / `feature/*`。
- 合并到 `dev` 的代码能被另一位同学稳定复现。
