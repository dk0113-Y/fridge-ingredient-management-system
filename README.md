# Smart Fridge Ingredient Recognition and Inventory Management

基于 C++17/CMake 的端侧冰箱食材视觉识别与库存事件判定 baseline。系统围绕一次冰箱交互中的 `before` / `after` 关键帧，完成 YOLO 粗分类与计数、前后帧差异分析、库存规则更新、本地 HTTP 接口和可追溯调试产物。

当前仓库以全 C/C++ 系统端架构为主线；Python 不作为后端服务。保留的 Python 内容仅用于小程序前端目录和离线模型工具。

> 当前实现已具备软件调试闭环，但不等同于完整硬件部署。真实摄像头长期运行、板端推理、小程序真机/LAN 联调和真实场景稳定性仍需单独验证。

## 项目目标

目标流程：

```text
事件检测
  -> 稳定 before/after 关键帧
  -> YOLO 粗分类与计数
  -> detection matching / frame diff
  -> 事件判定
  -> 库存更新或 pending review
  -> 本地 HTTP 查询与人工确认
```

当前事件类型：

- `put_in`
- `take_out`
- `partial_take_out_candidate`
- `no_change`
- `uncertain`

`partial_take_out_candidate` 仅用于 `fruit_vegetable`。当前不处理饮料液体体积或部分饮用识别。

## 技术栈

- C++17
- CMake / Ninja / CTest
- OpenCV：视频/图像 I/O、预处理和 OpenCV DNN fallback
- ONNX Runtime：优先的 YOLO ONNX inference backend
- YOLO ONNX model
- SQLite3：可选库存快照持久化
- project-local C++ socket HTTP server
- JSON debug/evidence artifacts

ONNX Runtime、OpenCV DNN 和 SQLite 均为条件构建能力。依赖不可用时，部分 mock/debug 或 in-memory 路径仍可运行，但不能据此声明真实 ONNX 或持久化验证完成。

## 系统结构

```text
Video / frame directory
  -> Module 1: ROI motion + stable keyframes
  -> Module 2: YoloRuntime + YoloDiffAnalyzer
  -> stage2 detections / crops / final event.json
  -> Module 4: InventoryEngine + pending review
       `-> optional SQLiteInventoryStore
  -> Module 5: LocalServiceFacade + LocalHttpServer
  -> local debug client / retained mini-program adapter
```

### 模块状态

| Module | 当前实现 | 当前边界 |
|---|---|---|
| Module 1 `event_capture` | local video/frame input、ROI motion summary、stable keyframe selection、runtime config、debug output | 真实摄像头、板端解码和长期运行待验证 |
| Module 2 `yolo_analysis` | `YoloRuntime`、ONNX Runtime 优先、OpenCV DNN fallback、C++ preprocessing/decoding、`YoloDiffAnalyzer`、session replay、crop planning | 真实场景稳定性和板端 backend 待验证；mock 不等于真实推理 |
| Module 3 `fine_grained` | independent C++ client skeleton、mock mode、provider-neutral config | 未接入 Module 1/2/4 主链路，未验证真实 provider |
| Module 4 `inventory` | in-memory `InventoryEngine`、event log、pending review、manual correction、可选 `SQLiteInventoryStore` | SQLite 的板端、小程序联动和长期稳定性待验证 |
| Module 5 `local_service` | JSON facade、lightweight local HTTP server、查询/确认/手动修正路由 | 非生产级 Web 服务；小程序真机、板端 HTTP 和长期运行待验证 |

最终目标设计与当前工程状态分别见：

- [`docs/system_final_design_cpp_only.md`](docs/system_final_design_cpp_only.md)
- [`docs/project_baseline.md`](docs/project_baseline.md)

## 仓库结构

```text
.
|-- AGENTS.md
|-- cpp/
|   |-- CMakeLists.txt
|   |-- main.cpp
|   |-- configs/
|   |-- module_1_event_capture/
|   |-- module_2_yolo_analysis/
|   |-- module_3_fine_grained/
|   |-- module_4_inventory/
|   |-- module_5_local_service/
|   `-- tests/
|-- data/
|   |-- runtime/
|   `-- test_sessions/
|-- docs/
|   |-- project_baseline.md
|   |-- system_final_design_cpp_only.md
|   |-- backend_api_cpp_only.md
|   `-- vision_pipeline.md
|-- models/
|   |-- best.onnx
|   `-- best.pt
`-- python/
    |-- miniprogram/
    `-- model_tools/
```

系统实现集中在 `cpp/`。仓库不存在以 Python/Flask 为主线的后端导入服务。

## 输入与输出

### Module 1 输入

- local video file
- frame directory
- ROI 与 motion/stability config

### Module 1/2 session 输入

典型 session：

```text
<session>/
`-- stage1/
    |-- stage1_event.json
    |-- before.jpg
    `-- after.jpg
```

兼容读取根目录下的 `event.json`、`before.jpg` 和 `after.jpg`。

### Module 2 与软件闭环输出

```text
<session>/
|-- stage2/
|   |-- detections_before.json
|   |-- detections_after.json
|   |-- module2_result.json
|   `-- crops/
`-- final/
    |-- event.json
    |-- inventory_response.json
    |-- events_response.json
    |-- pending_response.json
    `-- software_closure_report.json
```

`event.json` 用于调试、追溯、回放和演示证据，不是 Python backend import pipeline。

## 构建

Windows / Ninja：

```powershell
cmake -S cpp -B build/cpp -G Ninja -D FRIDGE_USE_OPENCV=ON
cmake --build build/cpp
ctest --test-dir build/cpp --output-on-failure
```

显式配置 ONNX Runtime SDK：

```powershell
cmake -S cpp -B build/cpp -G Ninja `
  -D FRIDGE_USE_OPENCV=ON `
  -D FRIDGE_USE_ONNXRUNTIME=ON `
  -D FRIDGE_ONNXRUNTIME_ROOT=<onnxruntime-sdk-root>
```

启用 SQLite 检测：

```powershell
cmake -S cpp -B build/cpp -G Ninja -D FRIDGE_USE_SQLITE=ON
```

说明：

- `FRIDGE_USE_ONNXRUNTIME` 默认为 `ON`，但只有找到 SDK headers/library 才启用 backend。
- `FRIDGE_USE_SQLITE` 默认为 `ON`，但只有找到 sqlite3 development files 才构建 persistence targets。
- 如果 OpenCV DNN 和 ONNX Runtime 都不可用，mock/debug/`.pgm` 路径仍可能运行，但不会执行 `models/best.onnx`。

## 运行示例

### Session replay

```powershell
build/cpp/fridge_module2_session_runner.exe `
  --session-dir data/test_sessions/module12_realtime_live/<session_id> `
  --module2-mode mock `
  --config cpp/configs/module_2_yolo.cfg
```

真实 ONNX Runtime mode：

```powershell
build/cpp/fridge_module2_session_runner.exe `
  --session-dir data/test_sessions/module12_realtime_live/<session_id> `
  --module2-mode real_onnx_runtime
```

该命令要求构建时实际找到 ONNX Runtime；否则不能作为真实 inference 证据。

### SQLite persistence

```powershell
build/cpp/fridge_module2_session_runner.exe `
  --session-dir data/test_sessions/module12_realtime_live/<session_id> `
  --module2-mode mock `
  --enable-sqlite-persistence `
  --sqlite-db data/runtime/fridge_inventory.db
```

`--sqlite-db` 会隐式启用 persistence。若 binary 未编译 sqlite3 support，请求 SQLite 时应明确失败，而不是静默退回 in-memory。

### Local HTTP server

```powershell
build/cpp/fridge_local_service_server.exe `
  --host 127.0.0.1 `
  --port 8080
```

可选 SQLite：

```powershell
build/cpp/fridge_local_service_server.exe `
  --host 127.0.0.1 `
  --port 8080 `
  --enable-sqlite-persistence `
  --sqlite-db data/runtime/fridge_inventory.db
```

## 本地服务接口

| Method | Route | 作用 |
|---|---|---|
| GET | `/health` | 服务与数据库状态 |
| GET | `/inventory` | 当前库存 |
| GET | `/events` | 事件记录 |
| GET | `/pending` | 待确认事件 |
| POST | `/confirm` | 确认或拒绝 pending event |
| POST | `/manual_update` | 手动修正库存 |

调试请求：

```powershell
curl http://127.0.0.1:8080/health
curl http://127.0.0.1:8080/inventory
curl http://127.0.0.1:8080/events
curl http://127.0.0.1:8080/pending
```

接口字段和目标契约见
[`docs/backend_api_cpp_only.md`](docs/backend_api_cpp_only.md)。

## 测试与验证证据

`cpp/CMakeLists.txt` 定义了以下主要测试/debug targets：

- `fridge_vision_tests`
- `fridge_debug_module_1`
- `fridge_debug_module_2`
- `fridge_debug_module_3`
- `fridge_debug_module_4`
- `fridge_debug_module_5`
- `fridge_debug_software_closure`
- `fridge_debug_local_http_server`
- `fridge_debug_sqlite_persistence`，仅在 SQLite 可用时构建
- `fridge_debug_software_closure_sqlite`，仅在 SQLite 可用时构建
- `fridge_module12_realtime_live`

证据等级必须区分：

| Evidence level | 当前可用内容 | 不能证明 |
|---|---|---|
| Mock/debug | deterministic event、software closure、stage2/final JSON | 真实模型精度、真实摄像头效果 |
| Session replay | 已保存 session 的 before/after 重放与 artifact 输出 | 长时间 live camera 稳定性 |
| Local HTTP | 本地 socket route 与 mutation debug | 小程序真机、板端网络、生产级服务 |
| SQLite debug | snapshot save/load/reload，条件构建 | 板端 sqlite3 和长期一致性 |
| Local ONNX/OpenCV | backend 存在时可执行模型 | 当前 README 不声明已在所有环境验证 |
| Real camera / board | 需要人工硬件验证 | 当前仓库没有完成证明 |

建议保留的证据：

- CMake configure/build/CTest 输出
- session ID 与输入路径
- `stage2/*.json`
- `final/event.json`
- `final/*_response.json`
- `final/software_closure_report.json`
- HTTP response 与 SQLite database path
- 真实硬件测试时的 camera/board 配置和运行日志

## 当前状态与限制

当前可以确认：

- Module 1/2/4/5 已形成软件调试闭环。
- Module 2 支持 ONNX Runtime 优先、OpenCV DNN fallback 和 mock/debug fallback。
- Module 4 提供 in-memory inventory rules 与可选 SQLite snapshot persistence。
- Module 5 提供 project-local C++ HTTP baseline。
- 小程序 adapter 已面向 C++ route shape，但真实联调尚未完成。

仍未完成或未充分验证：

- 真实摄像头输入和长时间 Module 1 -> 2 运行
- 真实场景中的 YOLO accuracy / event accuracy 指标
- 板端视频解码、ONNX Runtime/OpenCV DNN、资源占用和稳定性
- 小程序 WeChat Developer Tools、手机 LAN 与板端服务联调
- 长时间 HTTP/SQLite 一致性
- Module 3 真实 provider 与主链路集成
- 包装食品保质期推断闭环

本项目是工程 baseline，不应描述为生产级库存系统。HTTP server 是轻量本地实现，不提供生产 Web framework 的并发、安全和运维能力。

## 模型与数据公开检查

仓库当前包含：

- `models/best.pt`
- `models/best.onnx`
- `data/test_sessions/` 下的调试图像与 JSON evidence

公开仓库前需人工确认：

- 模型训练数据、权重和导出文件的公开许可
- session 图像是否包含个人、家庭环境或队友隐私
- JSON、日志和截图是否包含绝对路径、设备信息、IP 或其他敏感字段
- 大文件是否需要改用 release asset、LFS 或仅保留下载说明

## 相关文档

- [`docs/project_baseline.md`](docs/project_baseline.md)：当前工程状态与优先级
- [`docs/system_final_design_cpp_only.md`](docs/system_final_design_cpp_only.md)：全 C/C++ 目标架构
- [`cpp/README.md`](cpp/README.md)：构建、targets 与模块边界
- [`docs/vision_pipeline.md`](docs/vision_pipeline.md)：事件检测和关键帧流程
- [`docs/backend_api_cpp_only.md`](docs/backend_api_cpp_only.md)：本地服务接口
- [`models/README.md`](models/README.md)：模型资产与 inference backend
