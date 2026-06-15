# Smart Fridge Ingredient Recognition and Inventory Management

面向端侧 AI / 视觉识别 / C++ 工程系统的智能冰箱食材识别与库存管理项目。

本仓库来源于研电赛冰箱食材识别与管理系统，目标是在家庭冰箱场景中完成“开门交互检测、前后关键帧提取、YOLO 粗分类与计数、前后帧差异判定、库存更新、本地服务展示/确认”的基础闭环。当前系统正在向 [`docs/system_final_design_cpp_only.md`](docs/system_final_design_cpp_only.md) 定义的全 C/C++ 端侧架构推进；该文档是目标架构基线，不等同于当前所有能力都已完整验证。

## Project Overview / 项目简介

项目解决的问题是：冰箱内食材频繁进出后，用户很难持续维护准确库存。本系统尝试用低成本视觉前端捕获一次开门交互中的 `before` / `after` 关键帧，再通过 YOLO 检测和规则引擎判断 `put_in`、`take_out`、`partial_take_out_candidate`、`no_change` 或 `uncertain`，最后更新库存或进入人工确认流程。

适合作为作品展示的重点：

- 端侧视觉识别链路：Module 1 事件/关键帧 + Module 2 YOLO / diff analysis。
- C++ 工程组织：按 5 个模块拆分，使用 CMake 管理构建、测试和可选后端。
- 数据闭环：事件 JSON、stage2/final 调试产物、库存规则、pending review、可选 SQLite persistence baseline。
- 本地服务：C++ local service facade 和 lightweight HTTP server baseline，面向本地调试和未来小程序联调。
- 多人协作：保留 project baseline、final design、module README、Codex workflow、专项 skills 等协作文档。

## Scenario and Goal / 场景与目标

目标场景：

1. 用户打开冰箱并放入或取出食材。
2. 系统捕获交互前后的稳定关键帧。
3. 模型和规则判断食材数量/类别变化。
4. 高置信度事件自动更新库存。
5. 不确定事件或部分取出候选进入人工确认。
6. 本地服务向小程序或调试页面提供库存、事件和确认接口。

当前优先级是先完成基础可演示闭环，再推进真实摄像头长期验证、板端部署、小程序真实联调、细粒度识别和包装食品保质期推断。

## System Pipeline / 系统流程

```text
Video / frame input
  -> Module 1: motion summary + stable before/after keyframes
  -> Module 2: YOLO Runtime + detection matching + diff analysis
  -> stage2 JSON / crops / final event.json
  -> Module 4: InventoryEngine rules + pending review + optional SQLite snapshot
  -> Module 5: local service facade / local HTTP server
  -> mini-program or local debug client
```

`event.json` 在当前架构中主要用于调试、追溯、回放和答辩/演示证据，不是 Python backend import pipeline。

## Module Design / 模块设计

| Module | 当前角色 | 已实现或已有 baseline | 仍待完成 / 待验证 |
|---|---|---|---|
| Module 1 `event_capture` | 事件检测与关键帧提取 | ROI motion summary、stable before/after keyframe selection、runtime config、debug artifact output | 真实摄像头替换文件输入、板端视频解码、长时间稳定性 |
| Module 2 `yolo_analysis` | YOLO 粗分类、计数、差异分析 | `YoloRuntime`、ONNX Runtime 优先、OpenCV DNN fallback、ONNX output decoding、`YoloDiffAnalyzer`、session replay、mock/debug fallback | 真实场景稳定性、板端推理后端验证、长时间 Module 1 -> 2 联调 |
| Module 3 `fine_grained` | 细粒度识别增强层 | independent C++ client skeleton、mock mode、provider-neutral config、未来 HTTPS JSON path | 真实 provider 接入、写入主事件/库存链路、真实大模型调用验证 |
| Module 4 `inventory` | 库存规则与人工确认 | in-memory `InventoryEngine`、inventory mutation、event log、pending review、manual correction、可选 `SQLiteInventoryStore` baseline | SQLite 在小程序/板端/长期运行中的验证 |
| Module 5 `local_service` | 本地服务与调试接口 | `LocalServiceFacade`、lightweight C++ local HTTP server、`/health` `/inventory` `/events` `/pending` `/confirm` `/manual_update` | 小程序真实联调、板端 HTTP 部署、真实 camera/ONNX 闭环下的 HTTP 验证 |

## Repository Structure / 仓库结构

```text
.
|-- AGENTS.md                         # Codex repository-level rules
|-- README.md                         # Portfolio-oriented repository entry
|-- cpp/
|   |-- CMakeLists.txt                # C++17 build, optional OpenCV / ONNX Runtime / SQLite
|   |-- main.cpp
|   |-- configs/                      # Module runtime configs
|   |-- module_1_event_capture/
|   |-- module_2_yolo_analysis/
|   |-- module_3_fine_grained/
|   |-- module_4_inventory/
|   |-- module_5_local_service/
|   `-- tests/                        # C++ regression/debug executables
|-- data/
|   |-- runtime/                      # Runtime DB placeholder / local state
|   `-- test_sessions/                # Tracked debug/session evidence samples
|-- docs/
|   |-- project_baseline.md           # Current engineering status index
|   |-- system_final_design_cpp_only.md
|   |-- backend_api_cpp_only.md
|   `-- vision_pipeline.md
|-- models/
|   |-- best.onnx                     # C++ runtime deployment asset
|   `-- best.pt                       # Training/export source weight
`-- python/
    |-- miniprogram/                  # Retained mini-program frontend
    `-- model_tools/                  # Offline YOLO PT -> ONNX support script
```

There is no active top-level `src/`, `scripts/`, or `web/backend` directory in the current checkout. The system implementation is concentrated under `cpp/`; Python is retained for the mini-program and offline model tooling, not as the final backend.

## Implemented Features / 已实现内容

当前可以从代码和文档中确认的内容：

- C++17 / CMake module layout for the 5-module target system.
- Module 1: local video/frame-directory input path, ROI motion analysis, stable keyframe selection, `.jpg` or `.pgm` debug output.
- Module 2: YOLO runtime wrapper, ONNX Runtime first when SDK is found, OpenCV DNN fallback, C++ preprocessing/decoding, detection matching, crop planning, session replay, stage2/final JSON output.
- Module 4: `InventoryEngine` in-memory rule engine, inventory mutation, event log, pending review, manual correction / confirmation flow.
- Module 4 optional SQLite baseline: compiled only when sqlite3 development files are found; saves/restores inventory snapshots and related logs.
- Module 5: C++ local service facade and lightweight socket-based local HTTP server baseline.
- C++ tests/debug executables for Module 1/2/3/4/5, local HTTP server, software closure, SQLite persistence when available, and Module 1 -> 2 live harness.
- Retained mini-program frontend has an adapter layer targeting the C++ local HTTP route shape.

## Current Status / 当前状态

已实现但仍属于 baseline 或需继续验证的内容：

- Module 1/2/4/5 已具备软件调试闭环，但真实摄像头、真实 ONNX、板端部署和长时间稳定性仍需单独验证。
- `mock` / debug / `.pgm` 路径可用于本地回归，不等价于真实 ONNX inference。
- Module 3 当前是独立 fine-grained recognizer skeleton，不会自动更新库存，也尚未接入 Module 1/2/4 主链路。
- SQLite 是可选 persistence baseline；没有 sqlite3 时，系统仍走 in-memory inventory flow。
- Module 5 HTTP server 是本地/端侧 baseline，不代表已经完成小程序真机、LAN、板端或生产化服务验证。
- 部分取出候选限定在 `fruit_vegetable` 场景；饮料类不做液体体积或部分饮用识别。

目标方案和后续计划：

- 接入真实摄像头并稳定 Module 1 -> Module 2 长时间联调。
- 在真实 ONNX Runtime / OpenCV DNN 后端上验证 YOLO 推理稳定性。
- 做板端视频解码、资源占用和长期运行验证。
- 完成小程序真实联调和本地 HTTP / SQLite 长时间稳定性测试。
- 将 Module 3 细粒度识别结果接入库存主链路，但不把云端大模型作为核心闭环的必需条件。

## Build and Run / 构建与运行

典型 Windows / Ninja 构建：

```powershell
cmake -S cpp -B build/cpp -G Ninja -D FRIDGE_USE_OPENCV=ON
cmake --build build/cpp
ctest --test-dir build/cpp --output-on-failure
```

启用 ONNX Runtime 时需要提供 SDK：

```powershell
cmake -S cpp -B build/cpp -G Ninja `
  -D FRIDGE_USE_OPENCV=ON `
  -D FRIDGE_USE_ONNXRUNTIME=ON `
  -D FRIDGE_ONNXRUNTIME_ROOT=<onnxruntime-sdk-root>
```

SQLite support 默认为 `ON`，但只有 CMake 找到 sqlite3 development files 时才会构建 SQLite adapter 和相关 debug executables：

```powershell
cmake -S cpp -B build/cpp -G Ninja -D FRIDGE_USE_SQLITE=ON
```

启动本地 HTTP server baseline：

```powershell
build/cpp/fridge_local_service_server.exe --host 127.0.0.1 --port 8080
```

常用调试请求：

```powershell
curl http://127.0.0.1:8080/health
curl http://127.0.0.1:8080/inventory
curl http://127.0.0.1:8080/events
curl http://127.0.0.1:8080/pending
```

## Test and Validation / 测试与验证

主要测试入口由 `cpp/CMakeLists.txt` 定义，包括：

- `fridge_vision_tests`
- `fridge_debug_module_1`
- `fridge_debug_module_2`
- `fridge_debug_module_3`
- `fridge_debug_module_4`
- `fridge_debug_module_5`
- `fridge_debug_software_closure`
- `fridge_debug_local_http_server`
- `fridge_debug_sqlite_persistence` and `fridge_debug_software_closure_sqlite` when SQLite is available
- `fridge_module12_realtime_live` for live harness validation

当前仓库保留了 `data/test_sessions/module12_realtime_live/` 下的 session evidence samples，可用于理解 stage1/stage2/final artifacts。真实摄像头、板端运行、真实 WeChat Developer Tools / phone LAN 测试、真实 ONNX Runtime 推理和长时间 HTTP/SQLite 稳定性仍需要人工或硬件环境验证。

## Collaboration / 协作说明

这个项目采用模块化协作方式推进：

- `docs/project_baseline.md` 维护当前工程状态、优先级和权威入口。
- `docs/system_final_design_cpp_only.md` 维护全 C/C++ 目标方案和答辩口径。
- `cpp/README.md` 与各模块 README 维护实现边界、构建方式和未验证项。
- `docs/gpt_codex_workflow.md`、`AGENTS.md` 和 `.agents/skills/*` 约束 AI 辅助开发、review、commit/push 和验证口径。

个人参与/可展示贡献可围绕以下方向表述：

- 参与端侧 C++ 模块化架构整理和工程边界收口。
- 参与 Module 1 -> Module 2 -> Module 4 -> Module 5 软件闭环设计、调试和文档化。
- 参与 YOLO / ONNX Runtime / OpenCV DNN runtime path 的工程接入与 fallback 边界说明。
- 参与库存规则、pending review、manual correction、SQLite persistence baseline 和本地 HTTP API baseline 的协作开发或验证。
- 参与构建测试命令、debug artifacts、session replay 和公开仓库 README/文档维护。

如用于简历或面试，建议按自己的实际贡献在上述条目中进一步标注“主导 / 参与 / 维护 / 文档整理”，避免把团队整体成果写成个人独立完成。

## Internship Skill Mapping / 求职能力映射

| 实习方向能力 | 仓库中的真实支撑 |
|---|---|
| C/C++ 工程组织 | `cpp/CMakeLists.txt`、5-module layout、C++17 libraries/executables/tests |
| 端侧 AI / 视觉推理 | Module 2 `YoloRuntime`、`models/best.onnx`、ONNX Runtime first、OpenCV DNN fallback |
| OpenCV / 图像处理 | Module 1 video/frame IO、ROI motion analysis、keyframe output；OpenCV optional build path |
| 前后帧事件判定 | Module 1 stable keyframes + Module 2 `YoloDiffAnalyzer` / detection matching / crop planning |
| JSON / 数据闭环 | stage1/stage2/final JSON artifacts、`event.json` debug evidence、Module 5 JSON responses |
| SQLite / 库存持久化 | Module 4 optional `SQLiteInventoryStore` baseline and SQLite debug tests when sqlite3 is available |
| 本地服务 / 边缘系统 | Module 5 local service facade and lightweight C++ HTTP server baseline |
| 多人协作与工程文档 | `docs/project_baseline.md`、module README、workflow docs、AGENTS/skills |
| 测试与验证意识 | CTest targets、debug executables、session replay、explicit mock vs real inference boundaries |

这些能力映射只基于当前仓库内容，不声明已经完成量产级硬件部署、线上服务、真实大模型接入或长期稳定性验证。

## Limitations and Next Steps / 限制与后续计划

明确限制：

- 真实 camera / board-side deployment 尚未由本仓库证明完成。
- real ONNX Runtime inference 依赖本地 SDK 和构建环境；没有 ONNX Runtime / OpenCV DNN 时只能走 mock/debug 路径。
- Module 3 的 cloud fine-grained recognizer 仍是 skeleton / mock / provider config，不代表已接入真实 provider。
- SQLite persistence 是可选 baseline，不等同于已经完成小程序/板端长期运行验证。
- `models/best.pt` 和 `models/best.onnx` 已在仓库中存在；公开前应确认模型权重和数据来源是否允许公开。
- `data/test_sessions/` 下包含调试图片和 JSON evidence；公开前应检查图片是否包含未脱敏场景、个人信息或队友隐私。

推荐下一步：

1. 准备一组脱敏、小体量、可复现的 demo session artifacts。
2. 在明确硬件环境上记录真实 camera + ONNX Runtime + HTTP server 验证日志。
3. 完成 mini-program 与 C++ local HTTP server 的真机/LAN 联调记录。
4. 根据实际个人贡献补充简历版贡献说明，保持“实现 / baseline / 未验证 / 计划”边界清楚。
