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
| Codex 仓库级执行规范 | `AGENTS.md` | Codex 在本仓库中执行任务时应遵守的规则 |
| C++ 实现总览 | `cpp/README.md` | 五模块目录、构建命令、当前实现边界 |
| Module 1 视觉前段 | `docs/vision_pipeline.md` + `cpp/module_1_event_capture/README.md` | keyframe/event capture |
| Module 2 YOLO 分析 | `cpp/module_2_yolo_analysis/README.md` | YOLO Runtime、diff analyzer、session replay |
| Module 3 细粒度识别 | `cpp/module_3_fine_grained/README.md` | 独立 client skeleton / mock / provider config |
| Module 4 库存规则 | `cpp/module_4_inventory/README.md` | in-memory `InventoryEngine` baseline |
| Module 5 本地服务 | `docs/backend_api_cpp_only.md` + `cpp/README.md` + `cpp/module_5_local_service/README.md` | 目标接口、local service facade 与当前 local HTTP server baseline |
| 模型资产 | `models/README.md` | `best.pt` / `best.onnx` / runtime backend |
| 构建测试 | `cpp/CMakeLists.txt` + `cpp/README.md` | CMake options、test executables |
| GPT 方案讨论与工程收口 | `docs/gpt_solution_design_workflow.md` | GPT 与人类在进入 Codex 实操前进行方案讨论、可行性判断和工程收口的工作流 |
| GPT 生成 Codex 指令 | `docs/gpt_codex_workflow.md` | GPT 如何推荐 Codex 配置并生成 Codex prompt |
| GPT 对话交接提示词 | `docs/gpt_conversation_handoff_workflow.md` | 当前对话过长或需要新开对话时，GPT 如何生成 continuation prompt |
| Codex 专项实操 skills | `.agents/skills/*/SKILL.md` | 可复用的 Codex 专项任务 workflow；不得复制 `AGENTS.md` 或承担开放式 GPT 方案讨论 |

## 3. 项目目标简述

本项目是研电赛冰箱食材识别与管理系统，面向家庭冰箱场景，目标是完成端侧嵌入式 AIoT 系统。基础闭环是：

`事件检测 -> 关键帧 -> YOLO 粗分类与计数 -> 事件判断 -> 库存更新 -> 本地展示/小程序接口 -> 人工确认`

当前推进口径是优先完成基础必做项，再做大模型细分类、包装信息利用、保质期推断和板端部署优化等扩展。

## 4. 当前模块状态矩阵

| 模块 | 当前已实现 | 当前未完成 | 目标态 | 权威文档 |
|---|---|---|---|---|
| Module 1 事件检测与关键帧 | low-cost motion analysis<br>ROI motion summary<br>stable before/after keyframe selection<br>debug artifact output | 真实摄像头输入替换文件输入<br>板端视频解码与长时间运行验证 | 作为真实事件检测和关键帧提取前端 | `docs/vision_pipeline.md`<br>`cpp/module_1_event_capture/README.md` |
| Module 2 YOLO 分析 | `YoloRuntime`<br>ONNX Runtime 优先执行 `models/best.onnx`<br>OpenCV DNN fallback<br>ONNX output decoding<br>`YoloDiffAnalyzer`<br>detection matching<br>crop planning<br>session replay<br>mock/debug fallback<br>final 软件闭环 evidence 输出 | 真实场景稳定性验证<br>Module 1 -> Module 2 长时间联调<br>板端推理后端验证 | 对 before/after 做 YOLO 粗分类、计数、差异分析和事件判断 | `cpp/module_2_yolo_analysis/README.md`<br>`models/README.md` |
| Module 3 细粒度识别 | independent fine-grained recognizer client skeleton<br>mock mode<br>provider-neutral config<br>future HTTPS JSON request path | 真实 provider 接入<br>接入 Module 1/2/4 主链路<br>将 `fine_name` / `expiry_info` 写入库存流程 | 对变化目标裁剪图做细粒度识别与保质期辅助推断 | `cpp/module_3_fine_grained/README.md` |
| Module 4 库存规则 | in-memory `InventoryEngine`<br>inventory mutation<br>pending review<br>manual correction / confirmation flow<br>shelf-life rules baseline<br>Module 2 final event -> `InventoryEventInput` 软件闭环映射<br>可选 `SQLiteInventoryStore` 持久化 baseline | SQLite baseline 在小程序联动、板端 sqlite3 依赖与长期运行中的验证 | C/C++ SQLite 库存数据库与规则引擎 | `cpp/module_4_inventory/README.md`<br>`docs/system_final_design_cpp_only.md` |
| Module 5 本地服务 | local service facade<br>lightweight C++ local HTTP server baseline<br>GET `/health` `/inventory` `/events` `/pending`<br>POST `/confirm` `/manual_update`<br>session `final/` 中的 inventory/events/pending/closure report evidence | 小程序真实联调<br>板端 HTTP 部署验证<br>真实 camera/ONNX 闭环下的 HTTP 验证<br>长时间 HTTP/SQLite 稳定性验证 | 本地 HTTP API 与主控调度服务 | `docs/backend_api_cpp_only.md`<br>`cpp/README.md`<br>`cpp/module_5_local_service/README.md` |

## 5. 当前基础闭环完成度

| 环节 | 当前状态 | 说明 |
|---|---|---|
| 真实事件检测 | 已有 baseline | Module 1 已有低成本运动分析和事件分类，仍需真实摄像头和长时间场景验证 |
| 关键帧提取 | 已有 baseline | 已有 stable before/after keyframe selection 和调试输出 |
| YOLO 粗分类与计数 | 部分完成 | Module 2 已有 ONNX Runtime / OpenCV DNN 路径和 mock/debug fallback，真实场景稳定性仍待验证 |
| 前后帧差异分析 | 已有 baseline | `YoloDiffAnalyzer` 已覆盖匹配、差异分析和 crop planning |
| 部分取出候选 | 已有 baseline | 仅限果蔬类 `partial_take_out_candidate`，不覆盖饮料体积识别 |
| 细粒度识别 | 部分完成 | Module 3 是独立 client skeleton / mock / provider config，未接主链路 |
| 库存规则更新 | 已有软件闭环 baseline | Module 4 已有 in-memory mutation、pending review 和 manual correction；Module 2 final event 可映射到 `InventoryEventInput` 并应用到 `InventoryEngine` |
| SQLite 持久化 | runtime closure / HTTP mutation 可选接入 baseline | Module 4 已有可选 `SQLiteInventoryStore`，可保存/恢复 inventory、event_log、pending_review、inventory_change_log；Module 2 session replay、module12 live harness 和 Module 5 HTTP server 可在 sqlite3 可用且显式启用时 load/apply/save `InventoryEngine` 快照，并在相关 debug evidence 中输出 SQLite 状态；小程序 / 板端长期运行仍待验证 |
| 本地服务接口 | baseline 完成 | Module 5 已有 JSON response facade 和 lightweight C++ local HTTP server baseline；`fridge_debug_local_http_server` 通过真实 socket 请求验证 health/inventory/events/pending/manual_update/invalid confirm；mini-program 真实联调、板端部署和长时间稳定性仍待验证 |
| 小程序联调 | 尚未完成 | 小程序结构保留，真实接口联调待做 |
| 板端部署 | 待验证 | 视频解码、推理后端、运行资源和长时间稳定性需验证 |
| 演示与测试日志 | 已有 baseline | 已有 session、stage2、final/event JSON 和软件闭环 response/report 调试输出，仍需真实场景演示链路打磨 |

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
- 继续验证 Module 2 -> Module 4 -> Module 5 软件闭环在真实场景、真实 ONNX 和长时间运行下的稳定性。
- 跑通基础演示链路。

### P1：持久化与服务联调

- SQLite runtime closure baseline 已接入 session replay / live harness；HTTP mutation path 也可在 sqlite3 编译且显式启用时保存快照；下一步仍需小程序联调，并做板端 sqlite3 长期运行验证。
- 真实 HTTP server baseline 已有；后续重点是小程序联调、板端部署和长时间稳定性验证。
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
- GPT 与人类讨论新方案、技术路线、改进项或论文/答辩表达时，应先遵循 `docs/gpt_solution_design_workflow.md`；若方案已收口并需要 Codex 执行，再转入 `docs/gpt_codex_workflow.md`。
- GPT 生成 Codex 指令时，应遵循 `docs/gpt_codex_workflow.md`，先向用户给出执行前提示，再输出可复制给 Codex 的任务 prompt。
- 当用户要求为新对话生成提示词、转移上下文或让新聊天继续当前任务时，应遵循 `docs/gpt_conversation_handoff_workflow.md`。
- Codex 在仓库中执行任务时，应先读取 `AGENTS.md`；若任务来自 GPT 生成的 prompt，还应遵循 `docs/gpt_codex_workflow.md` 中的协作闭环。
- 当 prompt 调用某个 skill 时，Codex 应先遵循 `AGENTS.md`，再按对应 `.agents/skills/<skill>/SKILL.md` 执行专项流程。
- Skills 只负责专项实操流程，不替代 `AGENTS.md`、`docs/gpt_solution_design_workflow.md` 或 `docs/gpt_codex_workflow.md`。
- 若问题涉及最终架构、赛题得分点或答辩口径，应继续读 `docs/system_final_design_cpp_only.md`。
- 若问题涉及具体模块实现，应继续读对应模块 README、`CMakeLists.txt`、`configs` 和源码。
- 若 `AGENTS.md`、本文档、源码或模块 README 之间存在冲突，应明确指出冲突，并以当前源码和最近模块 README 作为实现事实来源。
- Codex 修改代码前，应先读本文档、`README.md`、相关模块 README 和相关源码。
- Codex 完成影响工程状态的任务后，应同步更新本文档中相关模块状态和未完成清单。

## 9. 文档维护边界

本文档只维护项目推进索引与当前工程状态总览，包括：当前工程状态入口、权威文档入口、模块状态矩阵、基础闭环完成度、明确不做或暂不做事项、P0/P1/P2 优先级，以及简洁的 GPT/Codex 使用规则。

本文档不应展开最终架构设计，不替代 `docs/system_final_design_cpp_only.md`；不替代模块 README 或源码作为实现事实来源；不展开 GPT 方案讨论 workflow、GPT-to-Codex prompt workflow、Codex 仓库级执行规则或未来 Codex skills；也不应变成论文、报告或答辩稿。

文档归属规则：

- 最终架构、目标设计、赛题得分点叙事：`docs/system_final_design_cpp_only.md`。
- 开放式 GPT + human 方案讨论与工程收口：`docs/gpt_solution_design_workflow.md`。
- GPT 生成 Codex prompt 与 post-push review：`docs/gpt_codex_workflow.md`。
- GPT 对话交接提示词 workflow：`docs/gpt_conversation_handoff_workflow.md`。
- Codex 仓库级执行规则：`AGENTS.md`。
- 详细实现行为：模块 README files 和 source code。
- 未来可复用 Codex 任务 workflow：`.agents/skills/*/SKILL.md`。

更新纪律：

- 向本文档添加内容前，先判断内容是否应放在更具体的文档中。
- 若任务改变当前项目状态、优先级、模块完成度、构建/测试路径、model runtime status、SQLite status、HTTP server / mini-program status 或 Module 3 integration status，应更新本文档。
- 若任务只改变具体实现行为，应更新相关模块 README、源码注释或局部文档。
- 若任务改变最终目标架构，应更新 `docs/system_final_design_cpp_only.md`。
- 若任务改变 GPT 规划 workflow，应更新 `docs/gpt_solution_design_workflow.md`。
- 若任务改变 Codex prompt 生成 workflow，应更新 `docs/gpt_codex_workflow.md`。
- 若任务改变 GPT 对话交接提示词 workflow，应更新 `docs/gpt_conversation_handoff_workflow.md`。
- 若任务改变 Codex 仓库执行规则，应更新 `AGENTS.md`。

冲突规则：如果本文档与当前源码或最近模块 README 冲突，应报告冲突，并以当前源码或最近模块 README 作为 implementation truth；只有当冲突影响项目状态或阅读指引时，才同步更新本文档。

## 2026-05-05 status note: mini-program C++ API adapter

The retained mini-program now has an API adapter baseline for the current C++
local HTTP service routes: `GET /health`, `GET /inventory`, `GET /events`,
`GET /pending`, `POST /confirm`, and `POST /manual_update`. This updates the
mini-program consumer layer only; the C++ backend contract remains the source of
truth.

Validation status remains limited to repository build/test, static
mini-program checks, and local HTTP debug where run. Real WeChat Developer Tools
operation, phone LAN testing, board deployment, real camera validation, real
ONNX Runtime validation, and long-running HTTP/SQLite stability remain pending
unless separately recorded.
