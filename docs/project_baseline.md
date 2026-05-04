# 项目推进基线与工程状态索引

本文档是 ChatGPT Project、Codex 和团队开发使用的“项目推进索引与当前工程状态总览”。
本文档不替代最终方案设计文档。
最终方案设计以 `docs/system_final_design_cpp_only.md` 为准。
当前具体实现细节以各模块 README、`CMakeLists.txt`、`configs` 和源码为准。
如果本文档与源码或模块 README 冲突，应以当前源码和最近模块 README 为准，并同步更新本文档。

## 1. 文档定位

本文件用于项目推进、AI 辅助开发和工程状态快速对齐。

- 不替代 `docs/system_final_design_cpp_only.md`。
- 不替代各模块 README。
- 用于告诉 GPT/Codex 当前项目应该先读什么、当前做到哪里、下一步优先做什么。

## 2. 权威文档与阅读入口

| 场景 | 优先阅读文件 | 说明 |
|---|---|---|
| 最终方案 / 目标架构 | `docs/system_final_design_cpp_only.md` | 全 C/C++ 最终方案设计基线 |
| 当前仓库入口 | `README.md` | 仓库目录、关键文档和当前限制 |
| C++ 实现总览 | `cpp/README.md` | 五模块目录、构建命令、当前实现边界 |
| Module 1 视觉前段 | `docs/vision_pipeline.md` + `cpp/module_1_event_capture/README.md` | keyframe/event capture |
| Module 2 YOLO 分析 | `cpp/module_2_yolo_analysis/README.md` | YOLO Runtime、diff analyzer、session replay |
| Module 3 细粒度识别 | `cpp/module_3_fine_grained/README.md` | 独立 client skeleton / mock / provider config |
| Module 4 库存规则 | `cpp/module_4_inventory/README.md` | in-memory `InventoryEngine` baseline |
| Module 5 本地服务 | `docs/backend_api_cpp_only.md` + `cpp/README.md` | 目标接口与当前 local service facade |
| 模型资产 | `models/README.md` | `best.pt` / `best.onnx` / runtime backend |
| 构建测试 | `cpp/CMakeLists.txt` + `cpp/README.md` | CMake options、test executables |

## 3. 项目目标简述

本项目是研电赛冰箱食材识别与管理系统，面向家庭冰箱场景，目标是完成端侧嵌入式 AIoT 系统。基础闭环是：

`事件检测 -> 关键帧 -> YOLO 粗分类与计数 -> 事件判断 -> 库存更新 -> 本地展示/小程序接口 -> 人工确认`

当前推进口径是优先完成基础必做项，再做大模型细分类、包装信息利用、保质期推断和板端部署优化等扩展。

## 4. 当前模块状态矩阵

| 模块 | 当前已实现 | 当前未完成 | 目标态 | 权威文档 |
|---|---|---|---|---|
| Module 1 事件检测与关键帧 | low-cost motion analysis<br>ROI motion summary<br>stable before/after keyframe selection<br>debug artifact output | 真实摄像头输入替换文件输入<br>板端视频解码与长时间运行验证 | 作为真实事件检测和关键帧提取前端 | `docs/vision_pipeline.md`<br>`cpp/module_1_event_capture/README.md` |
| Module 2 YOLO 分析 | `YoloRuntime`<br>ONNX Runtime 优先执行 `models/best.onnx`<br>OpenCV DNN fallback<br>ONNX output decoding<br>`YoloDiffAnalyzer`<br>detection matching<br>crop planning<br>session replay<br>mock/debug fallback | 真实场景稳定性验证<br>Module 1 -> Module 2 长时间联调<br>板端推理后端验证 | 对 before/after 做 YOLO 粗分类、计数、差异分析和事件判断 | `cpp/module_2_yolo_analysis/README.md`<br>`models/README.md` |
| Module 3 细粒度识别 | independent fine-grained recognizer client skeleton<br>mock mode<br>provider-neutral config<br>future HTTPS JSON request path | 真实 provider 接入<br>接入 Module 1/2/4 主链路<br>将 `fine_name` / `expiry_info` 写入库存流程 | 对变化目标裁剪图做细粒度识别与保质期辅助推断 | `cpp/module_3_fine_grained/README.md` |
| Module 4 库存规则 | in-memory `InventoryEngine`<br>inventory mutation<br>pending review<br>manual correction / confirmation flow<br>shelf-life rules baseline | SQLite adapter / persistence<br>`event_log` / `inventory_change_log` 持久化<br>与真实 HTTP server、小程序联动验证 | C/C++ SQLite 库存数据库与规则引擎 | `cpp/module_4_inventory/README.md`<br>`docs/system_final_design_cpp_only.md` |
| Module 5 本地服务 | local service facade<br>health / inventory / events / pending / confirm / manual-update JSON responses | 真实 embedded/local HTTP server<br>端口监听<br>路由绑定<br>小程序真实联调 | 本地 HTTP API 与主控调度服务 | `docs/backend_api_cpp_only.md`<br>`cpp/README.md` |

## 5. 当前基础闭环完成度

| 环节 | 当前状态 | 说明 |
|---|---|---|
| 真实事件检测 | 已有 baseline | Module 1 已有低成本运动分析和事件分类，仍需真实摄像头和长时间场景验证 |
| 关键帧提取 | 已有 baseline | 已有 stable before/after keyframe selection 和调试输出 |
| YOLO 粗分类与计数 | 部分完成 | Module 2 已有 ONNX Runtime / OpenCV DNN 路径和 mock/debug fallback，真实场景稳定性仍待验证 |
| 前后帧差异分析 | 已有 baseline | `YoloDiffAnalyzer` 已覆盖匹配、差异分析和 crop planning |
| 部分取出候选 | 已有 baseline | 仅限果蔬类 `partial_take_out_candidate`，不覆盖饮料体积识别 |
| 细粒度识别 | 部分完成 | Module 3 是独立 client skeleton / mock / provider config，未接主链路 |
| 库存规则更新 | 已有 baseline | Module 4 已有 in-memory mutation、pending review 和 manual correction |
| SQLite 持久化 | 尚未完成 | SQLite 是目标 adapter / persistence 方案，当前未落地 |
| 本地服务接口 | 部分完成 | Module 5 已有 JSON response facade，尚无真实 HTTP server |
| 小程序联调 | 尚未完成 | 小程序结构保留，真实接口联调待做 |
| 板端部署 | 待验证 | 视频解码、推理后端、运行资源和长时间稳定性需验证 |
| 演示与测试日志 | 已有 baseline | 已有 session、stage2、final/event JSON 等调试输出，仍需演示链路打磨 |

## 6. 当前明确不做或暂不做事项

- 不恢复 Python/Flask 后端作为最终架构。
- 除小程序外，系统端代码统一走 C/C++。
- 不额外训练专门的部分取出模型。
- 部分取出仅对果蔬类启用。
- 饮料类不做液体体积 / 部分饮用识别。
- 小程序结构当前不做大改。
- mock/debug 路径不等价于真实 ONNX 推理。
- `event.json` 主要用于调试、追溯和答辩展示，不再作为 Python 后端导入主链路。

## 7. 下一阶段推进优先级

### P0：基础闭环

- 稳定 Module 1 -> Module 2 联调。
- 接入真实摄像头输入。
- 验证 YOLO 推理稳定性。
- 稳定 event JSON / stage2 / final 输出链路。
- 对接 Module 4 库存规则与待确认流程。
- 跑通基础演示链路。

### P1：持久化与服务联调

- SQLite adapter / persistence。
- 真实 HTTP server。
- 小程序接口联调。
- 事件日志、库存日志、pending review 闭环。

### P2：增强能力与交付质量

- Module 3 真实 provider 接入。
- 细粒度识别写入库存。
- 包装食品保质期推断。
- 板端部署优化。
- 长时间稳定性测试。
- 答辩材料与演示视频优化。

## 8. GPT / Codex 使用规则

- GPT 回答当前工程状态、生成 Codex 指令、分析模块实现前，应先读本文档。
- 若问题涉及最终架构、赛题得分点或答辩口径，应继续读 `docs/system_final_design_cpp_only.md`。
- 若问题涉及具体模块实现，应继续读对应模块 README、`CMakeLists.txt`、`configs` 和源码。
- 若本文档与源码冲突，应明确指出冲突，以源码和最近模块 README 为准，并建议更新本文档。
- Codex 修改代码前，应先读本文档、`README.md`、相关模块 README 和相关源码。
- Codex 完成影响工程状态的任务后，应同步更新本文档中相关模块状态和未完成清单。
