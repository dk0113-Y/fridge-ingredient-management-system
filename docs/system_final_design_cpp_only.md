# 冰箱食材识别与管理系统最终方案基线（全 C/C++ 版本）

## 1. 文档定位

本文档用于覆盖旧的“C++ 视觉 + Python 后端”过渡方案，作为当前项目后续开发的最终方案 / 目标架构基线。

注意：本文档不是当前仓库完整实现清单。当前仓库已经转向全 C/C++ 架构，但 5 个模块的实现成熟度不同：

- 模块 1 已有事件检测、关键帧提取、ROI 运动分析和调试输出。
- 模块 2 已有独立 YOLO 分析链路，优先使用 ONNX Runtime 执行 `models/best.onnx`，OpenCV DNN 作为 fallback；没有这两个后端时仍可走 mock/debug/`.pgm` 路径，但不等价于真实 ONNX 推理。
- 模块 3 当前是独立 fine-grained recognizer client skeleton，支持 mock mode 和 provider config，尚未完整接入主事件 / 库存链路。
- 模块 4 当前以 in-memory `InventoryEngine` / rule engine baseline 为主，覆盖库存 mutation、pending review 和 manual correction；SQLite 是后续 adapter / persistence 目标，尚未完全落地。
- 模块 5 当前是 local service facade / JSON response baseline，覆盖 health、inventory、events、pending、confirm、manual-update 等响应；真实 embedded/local HTTP server 尚未完成。

最终方案约束如下：

- 除小程序外，系统端所有代码统一使用 C/C++ 实现。
- 小程序部分当前不做结构调整，保持既有功能边界不变。
- 系统优先完成赛题基础必做项，再补充大模型细分类等增强功能。
- 当前版本仅使用 YOLO 作为目标识别模型，不额外训练专门的部分取出模型。
- 部分取出功能仅对果蔬类启用，饮料类不做部分取出判断。

---

## 2. 总体架构

系统统一采用“全 C/C++ 一体化架构”，划分为 5 个模块：

1. 事件检测与关键帧提取模块
2. YOLO 粗分类与差异分析模块
3. 细粒度识别与保质期推断模块
4. 库存数据库与规则引擎模块
5. 主控调度与本地通信模块

说明：

- 模块 1、2、4 构成基础闭环主链。
- 模块 3 为增强识别层，不影响基础功能可运行性。
- 模块 5 负责统一调度所有模块，并对小程序或本地页面提供服务。

---

## 3. 总体流程

以下为目标流程，不代表当前每一步都已经完整接入主链路：

1. 主程序低成本监测冰箱门状态、亮度变化和 ROI 帧差。
2. 检测到有效开门交互后，提取 `before` 和 `after` 关键帧。
3. 对前后关键帧分别运行 YOLO，得到粗分类、目标框、数量和置信度。
4. 通过数量差分与同类框匹配，输出 `put_in`、`take_out`、`partial_take_out_candidate`、`no_change`、`uncertain`。
5. 对发生变化的目标区域进行裁剪。
6. 若需要细分类，则调用外部大模型接口，获取细粒度名称和相关补充信息；当前模块 3 仍是独立 client skeleton / mock + provider config。
7. 主程序内部直接生成事件对象并更新库存状态；SQLite 是目标数据库方案，当前模块 4 仍以 in-memory 规则引擎为主。
8. 同时输出 `event.json` 作为调试日志与可追溯记录。
9. 本地 HTTP 服务向小程序或本地页面提供库存、事件、确认和修正接口；当前模块 5 尚未接入真实 HTTP server，已有的是 local service facade / JSON response baseline。

---

## 4. 模块设计

## 4.1 模块 1：事件检测与关键帧提取

### 目标

确认是否发生真实取放事件，并输出用于后续识别的关键帧。

### 输入

- 摄像头视频流或测试视频
- ROI 配置
- 门状态变化阈值
- 帧差阈值
- 亮度变化阈值

### 输出

- `before.jpg`
- `after.jpg`
- `overlay.jpg`
- `session_id`
- `event_window_meta`

### 核心逻辑

1. 常驻运行低成本检测。
2. 门打开后进入事件窗口。
3. 在窗口内选取稳定的前后关键帧。
4. 若变化不足或结果不稳定，则输出 `no_change` 或 `uncertain`。

### 对应评分点

- 真实事件识别
- 系统完整性与稳定性

---

## 4.2 模块 2：YOLO 粗分类与差异分析

### 目标

对 `before` 和 `after` 图像分别做检测，并根据前后差异判定事件。

### 粗分类类别

- `fruit_vegetable`：果蔬类
- `meat_egg_fresh`：肉蛋生鲜类
- `drink`：饮料类
- `packaged_food`：包装食品类

### YOLO 输出字段

- `coarse_class`
- `confidence`
- `bbox = [x1, y1, x2, y2]`

### 框匹配规则

同一粗分类下，优先按 IoU 匹配；若 IoU 不足，再按中心点距离匹配。

匹配后得到：

- `matched_pairs`
- `new_boxes`
- `disappeared_boxes`

### 事件类型

- `no_change`
- `put_in`
- `take_out`
- `partial_take_out_candidate`
- `uncertain`

### 判定顺序

1. 先判断是否存在有效变化。
2. 再根据 `new_boxes` 和 `disappeared_boxes` 判 `put_in` 或 `take_out`。
3. 若数量未变，则仅对果蔬类继续判断 `partial_take_out_candidate`。
4. 其余复杂情况归入 `uncertain`。

### 部分取出规则

当前版本不训练专门的部分取出模型，只在 YOLO 结果基础上做规则判断。

#### 启用范围

仅对 `fruit_vegetable` 启用。

#### 不启用范围

- `drink`
- `packaged_food`
- `meat_egg_fresh`

#### 判定依据

对前后都存在、数量未变化的果蔬目标：

- 若框面积变化明显
- 或框内前后裁剪图差异明显
- 或外观形态变化明显

则记为 `partial_take_out_candidate`。

### 截图规则

- `put_in`：裁剪 `after` 中新增框
- `take_out`：裁剪 `before` 中消失框
- `partial_take_out_candidate`：同时裁剪 `before` 与 `after` 的对应框

### 对应评分点

- 物体分类能力
- 真实取放行为识别
- 部分取出处理（果蔬类）

---

## 4.3 模块 3：细粒度识别与保质期推断

### 目标

对变化目标进一步做细粒度识别，并补充保质期信息。

### 定位

增强识别层，不承担主链路第一道硬判定。

### 目标实现方式

主程序使用 C/C++ 调用外部大模型接口，不使用 Python。

推荐方式：

- 使用 `libcurl` 发起 HTTP 请求
- 使用 `nlohmann/json` 解析响应

### 输出内容

- `fine_name`
- `llm_confidence`
- `expiry_info`

### 当前实现边界

当前仓库中的模块 3 是独立 fine-grained recognizer client skeleton，支持 mock mode、provider-neutral config 和未来 HTTPS JSON request path。它尚未完整接入模块 1/2/4 的主事件 / 库存链路。

### 粗分类对应策略

- 果蔬类：输出细粒度名称与置信度
- 肉蛋生鲜类：输出细粒度名称与置信度
- 饮料类：输出细粒度名称与置信度
- 包装食品类：输出细粒度名称、置信度，并尝试根据包装信息推断保质期

### 保质期规则

采用双轨制：

1. 包装食品优先尝试从包装信息推断
2. 若失败则回退到本地规则表
3. 果蔬类、肉蛋生鲜类、饮料类统一走本地规则表

### 对应评分点

- 细粒度识别扩展能力
- 包装信息利用与管理智能化

---

## 4.4 模块 4：库存数据库与规则引擎

### 目标

完成库存更新、事件记录、待确认管理、保质期管理和人工修正。

### 目标数据库方案

最终方案统一使用 SQLite，由 C/C++ 直接调用 `sqlite3` C API 管理。当前仓库尚未完成 SQLite adapter / persistence 接入，模块 4 仍以 in-memory `InventoryEngine` 和规则引擎 baseline 为主；未来 adapter 应替换或扩展存储层，并尽量保持 public rule-engine API 稳定。

### 建议数据表

- `inventory`
- `event_log`
- `pending_review`
- `shelf_life_rules`
- `inventory_change_log`

### 库存更新原则

- `put_in`：库存增加
- `take_out`：库存减少
- `partial_take_out_candidate`：默认进入待确认流程
- `uncertain`：不直接写入正式库存，进入待确认流程

### 处理流程

1. 主程序生成事件对象
2. 规则引擎校验字段完整性与置信度
3. 高置信度事件自动更新库存
4. 特殊事件进入待确认队列
5. 用户确认后再写入正式库存状态

### 对应评分点

- 库存管理策略
- 出入库逻辑完整性
- 人工修正闭环

---

## 4.5 模块 5：主控调度与本地通信

### 目标

统一调度各模块，负责系统对外服务与联调。

### 目标主要职责

- 摄像头/视频流接入
- 事件状态机控制
- 调用关键帧模块
- 调用 YOLO 模块
- 调用细分类模块
- 调用数据库与规则引擎
- 输出 `event.json` 调试日志
- 提供本地 HTTP API
- 记录日志与异常

### 当前实现边界

当前模块 5 是 local service facade / JSON response baseline，已能生成 health、inventory、events、pending、confirm、manual-update 等 JSON 响应；真实 embedded/local HTTP server、端口监听、路由绑定和小程序联调仍是后续任务。

### 小程序边界

小程序当前不做功能改动，因此本模块需保证接口字段尽量稳定，不频繁修改。

### 推荐 C/C++ 组件

- HTTP 服务：`cpp-httplib` 或 `CivetWeb`
- JSON：`nlohmann/json`
- 数据库：`sqlite3`
- 日志：`spdlog`
- 配置：`yaml-cpp` 或 `inih`

### 对应评分点

- 系统完整性
- 端侧可部署性
- 工程实现质量

---

## 5. event.json 协议定位

`event.json` 当前保留，但定位改为：

- 调试日志
- 事件回放记录
- 答辩展示材料

不再作为“Python 后端导入”的主链路依赖。

### 建议字段

```json
{
  "event_id": "20260329_001",
  "session_id": "session_20260329_01",
  "timestamp": "2026-03-29 14:32:10",
  "event_type": "put_in",
  "coarse_class": "packaged_food",
  "fine_name": "光明酸奶",
  "quantity_before": 0,
  "quantity_after": 1,
  "quantity_delta": 1,
  "before_frame_path": "data/sessions/session_20260329_01/before.jpg",
  "after_frame_path": "data/sessions/session_20260329_01/after.jpg",
  "crop_path": "data/sessions/session_20260329_01/crops/crop_01.jpg",
  "yolo_confidence": 0.82,
  "yolo_threshold": 0.50,
  "llm_confidence": 0.88,
  "in_date": "2026-03-29",
  "shelf_life_days": 14,
  "expiry_date": "2026-04-12",
  "expiry_source": "default_rule",
  "partial_take_supported": false,
  "needs_manual_review": false,
  "review_reason": "",
  "status": "pending_commit"
}
```

---

## 6. 对旧方案的正式替换说明

以下旧表述不再作为最终方案依据：

- Python 后端负责事件消费与数据库更新
- Flask 提供本地接口
- Python 扫描 `event.json` 再导入 SQLite

最终方案统一改为：

- C/C++ 主程序内部完成事件处理、库存更新与 HTTP 服务
- `event.json` 仅作为调试日志和可追溯记录
- 小程序继续通过稳定接口访问本地服务

当前实现中，库存持久化仍以 in-memory rule engine baseline 为主，HTTP 层仍是 local service facade，尚未等同于完整上线的本地 HTTP 服务。

---

## 7. 当前正式结论

1. 最终系统采用全 C/C++ 一体化架构；当前仓库已经按该方向组织，但不是所有目标能力都已完成。
2. 当前目标识别链路以 YOLO 为核心，模块 2 已有 ONNX Runtime / OpenCV DNN fallback 和 mock/debug 路径。
3. 部分取出仅对果蔬类启用，饮料类不做部分取出判断。
4. 小程序部分当前不做调整。
5. 先完成基础闭环，再补大模型细分类、SQLite persistence、真实 HTTP server 与包装信息推断。

---

## 8. 开发优先级

### 第一优先级

- 关键帧提取
- YOLO 粗分类
- 事件判定
- SQLite 库存更新 adapter / persistence
- C++ 本地 HTTP server 接入

### 第二优先级

- 框级匹配
- `uncertain` 与待确认流程
- 果蔬类部分取出候选

### 第三优先级

- 大模型细分类
- 包装食品保质期推断
- 调试日志完善与演示增强

---

## 9. 答辩统一表述

“本系统目标方案采用全 C/C++ 一体化架构，在端侧完成真实事件检测、关键帧提取、YOLO 粗分类、库存更新和本地通信服务；对于需要进一步确认的目标，再调用大模型完成细粒度识别与包装食品保质期推断。当前仓库已完成部分模块 baseline：模块 1/2/4/5 已具备可调试链路，模块 3 仍是独立 client skeleton；SQLite persistence、真实 HTTP server 和模块 3 主链路集成仍需后续完成。为保证工程可落地性，部分取出功能限定在果蔬类场景，饮料类暂不进行部分取出自动识别；小程序前端维持不变，仅通过稳定接口与本地服务交互。”
