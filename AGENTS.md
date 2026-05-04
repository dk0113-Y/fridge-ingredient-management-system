# AGENTS.md

## 1. 文件定位

本文档是本仓库的 Codex repository-level execution rule file，用于告诉 Codex 进入本仓库后如何读取上下文、修改文件、验证结果、提交推送和汇报。

本文档不定义最终系统架构，不替代 Project instructions，不替代 `docs/project_baseline.md`，不替代 `docs/system_final_design_cpp_only.md`，也不替代未来 `.agents/skills/*/SKILL.md` 下的任务专用 workflow。

文档分工：

- Project instructions：ChatGPT Project 顶层硬约束和引用入口。
- `docs/project_baseline.md`：项目推进索引与当前工程状态总览。
- `docs/gpt_codex_workflow.md`：GPT 如何生成 Codex prompt，以及 GPT 如何审查 Codex 结果。
- `AGENTS.md`：Codex 在本仓库内执行任务时遵循的仓库级规则。
- `.agents/skills/*/SKILL.md`：可复用的 Codex 专项实操 workflow。

如果 `AGENTS.md` 与当前用户 prompt 冲突，优先遵循当前用户 prompt，除非它违反仓库安全规则。如果 `AGENTS.md` 与当前源码或最近模块 README 冲突，应报告冲突，并以当前源码和最近模块 README 作为实现事实来源。

`.agents/skills/*/SKILL.md` 不替代本文档。Codex 仅在当前 prompt 明确调用某个 skill 时使用该 skill；skill 只补充专项实操流程，不用于开放式 brainstorming，也不承担 GPT 方案讨论或 Codex prompt 生成职责。如果 skill 与 `AGENTS.md` 冲突，应报告冲突并遵循 `AGENTS.md`，除非当前用户 prompt 明确且安全地覆盖相关要求。

## 2. 必读文件顺序

Codex 修改文件前应按以下顺序读取上下文：

1. `docs/project_baseline.md`
2. `README.md`
3. `docs/gpt_codex_workflow.md`
4. 当任务涉及最终架构、赛题得分点、答辩口径、模块职责或目标设计时，读取 `docs/system_final_design_cpp_only.md`
5. 相关模块 README
6. 当任务涉及构建或运行时行为时，读取 `cpp/CMakeLists.txt` 和相关 `configs`
7. 相关源码文件

阅读原则：

- 当前实现状态的第一入口是 `docs/project_baseline.md`。
- 最终目标设计以 `docs/system_final_design_cpp_only.md` 为基线。
- GPT-generated prompt 风格、commit/push/report loop 和 GPT acceptance review 参考 `docs/gpt_codex_workflow.md`。
- 实际实现细节以当前源码和最近模块 README 为准。

## 3. 项目硬约束

- 本项目是研电赛冰箱食材识别与管理系统，面向家庭冰箱食材识别、事件判断和库存管理。
- 优先完成基础必做项和演示稳定的工程闭环，再推进扩展功能。
- 除既有小程序 frontend 外，系统端代码应遵循全 C/C++ 方向。
- 不恢复 Python/Flask 后端作为最终架构。
- 不推荐或实现 heavy cloud-first models 作为核心 pipeline。
- 保持端侧部署约束意识：embedded/edge AIoT，最好在 1 TOPS 以内，理想目标约 0.5 TOPS。
- 当前 partial-take-out 支持限定在 fruit/vegetable 场景。
- 除非用户明确要求，不实现 beverage liquid-volume recognition 或部分饮用识别。
- mock/debug 路径不等价于真实 ONNX inference。
- `event.json` 主要用于调试、追溯、回放和答辩/演示证据，不是 Python backend import pipeline。

## 4. 当前工程边界

当前工程状态以 `docs/project_baseline.md` 为准。本节只作短提醒：

- Module 1 已有 event/keyframe baseline，但仍需要真实摄像头输入和板端验证。
- Module 2 已有 YOLO runtime，优先 ONNX Runtime，OpenCV DNN fallback，并包含 `YoloDiffAnalyzer`、session replay 和 mock/debug fallback。
- Module 3 当前是 independent fine-grained recognizer skeleton，尚未接入主事件/库存链路。
- Module 4 当前是 in-memory `InventoryEngine` / rule baseline，SQLite adapter / persistence 尚未完成。
- Module 5 当前是 local service facade / JSON response baseline，真实 HTTP server 和小程序联调尚未完成。

## 5. 修改范围与禁止事项

Codex 必须：

- 严格待在任务 scope 内。
- 优先做 minimal、reviewable changes。
- 修改前读取相关 docs/source。
- 除非任务明确要求重构，否则保持现有模块边界。
- 聚焦解决当前任务，避免宽泛重写。

Codex 不得：

- 未经用户明确要求就新建 branch 或 PR。
- 未经明确要求修改 model files、data files、generated binaries、large assets 或 mini-program structure。
- 修改 credentials、API keys、private tokens 或 secret files。
- 未实际运行 tests 就声称 tests passed。
- 把 target architecture feature 写成 current implemented feature。
- 把 mock/debug output 当作真实部署正确性的证明。
- 未经明确用户指示改变项目全 C/C++ 方向。

## 6. 构建与测试规则

典型 Windows/Ninja build：

```powershell
cmake -S cpp -B build/cpp -G Ninja -D FRIDGE_USE_OPENCV=ON
cmake --build build/cpp
ctest --test-dir build/cpp --output-on-failure
```

ONNX Runtime build option：

```powershell
cmake -S cpp -B build/cpp -G Ninja -D FRIDGE_USE_OPENCV=ON -D FRIDGE_USE_ONNXRUNTIME=ON -D FRIDGE_ONNXRUNTIME_ROOT=<onnxruntime-sdk-root>
```

测试与验证规则：

- 如果 OpenCV / ONNX Runtime 不可用，必须清楚报告限制。
- 如果当前环境无法运行 build/test，不得假装已经通过。
- documentation-only tasks 通常运行 `git diff`、`git diff --name-only` 和 `git diff --check` 即可。
- C++ code tasks 应在可能时运行相关 build/tests。
- hardware/camera/board tasks 中，Codex 可以准备代码和本地检查，但真实硬件必须由 human 验证。
- 不得把无法验证的外部依赖、硬件行为或 cloud provider 行为写成已验证。

## 7. 文档同步规则

如果任务影响以下内容，必须同步检查并按需更新 `docs/project_baseline.md`：

- 模块完成状态
- 当前限制
- P0/P1/P2 优先级
- 构建/测试命令
- model runtime backend status
- SQLite persistence status
- HTTP server / mini-program integration status
- Module 3 main-chain integration status
- `AGENTS.md` 或未来 `.agents/skills` usage rules

实现行为变化时，也应更新对应 module README。

如果只是 tiny internal changes，且不影响项目状态、模块边界、构建测试方式或用户可见行为，可以不更新 `docs/project_baseline.md`，但 final report 应说明原因。

## 8. Direct-main commit and push policy

默认工作流是直接在当前 `main` 分支工作，不创建新分支，不创建 PR，除非用户明确要求。

可以 commit 并 push 到 `origin/main` 的情况：

- documentation-only tasks 或 small safe changes 已完成范围检查和必要验证。
- code tasks 已运行相关 validation，或明确报告 validation 无法运行且任务允许继续。

push 前必须：

- 运行 `git status`。
- 运行 `git diff` 或 `git diff --name-only`。
- 完成任务要求的 validation checks。
- 确认 changed files 都在任务 scope 内。
- 使用 concise commit message。

不得 push 的情况：

- tests fail，并且失败由当前 changes 引起。
- task scope 已经超出 prompt。
- 存在 unrelated uncommitted changes，且无法安全区分本次任务修改。
- modified files 包含 prohibited files。
- implementation 不符合 Human + GPT 已同意的 plan。
- user explicitly asked not to push。

## 9. Codex final report requirements

Codex 完成任务后应报告：

- Branch
- Commit pushed
- Commit hash
- Changed files
- Summary of changes
- Validation run
- Validation result
- Files intentionally not changed
- Remaining risks / unverified items
- Whether `docs/project_baseline.md` was updated
- Recommended next step

如果没有 push，必须说明原因，并给出 human 下一步需要执行的 exact command 或 decision。

## 10. Language and style

- 除非用户另有要求，user-facing summaries 使用中文。
- 技术路径、commands、identifiers、class/function names、config names 和 JSON keys 保持原样。
- 对不确定事项要明确说明。
- 优先给出 concrete file paths、commands、validation steps 和 changed-file lists。
- 不夸大完成度，不把 planned target 写成 implemented current state。
