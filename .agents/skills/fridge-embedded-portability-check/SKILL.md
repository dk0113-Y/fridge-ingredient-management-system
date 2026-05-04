---
name: fridge-embedded-portability-check
description: Check whether a smart-fridge change remains suitable for embedded/edge deployment under the project constraints. Use when a task affects C++ code structure, inference backend, OpenCV or ONNX Runtime usage, CMake options, model runtime assumptions, SQLite, HTTP server, camera/video input, board deployment, external dependencies, or cloud provider usage.
---

# fridge-embedded-portability-check

## Purpose

检查变更是否仍适合本项目的 embedded/edge AIoT 部署约束，避免引入难以上板、难以构建或 cloud-heavy 的核心依赖。

## When to use

- 任务影响 C++ code structure、inference backend、OpenCV / ONNX Runtime usage。
- 任务影响 CMake options、model runtime assumptions、SQLite / HTTP server。
- 任务影响 camera/video input、board deployment、external dependencies。
- 任务涉及 cloud provider usage 或网络依赖。

## Do not use for

- 与部署无关的纯 Markdown edits。
- GPT-side planning。
- `AGENTS.md` 已覆盖的通用仓库安全检查。

## Assumptions

- `AGENTS.md` 仍是 repository-level rule source。
- 本 skill 只添加 embedded portability 专项流程。
- GPT/Codex prompt 仍必须提供 task-specific goal、scope、allowed files、validation 和 commit message。
- 本 skill 不用于开放式 brainstorming，不负责生成 Codex prompt，也不定义最终架构。

## Required inputs

- 变更涉及的 dependency、build option、runtime backend 或 hardware path。
- 目标运行环境假设：Windows、Ubuntu、board/RV1106、camera、network。
- 可运行的 local validation 和需要 human 执行的 hardware validation。

## Required reading

- `docs/project_baseline.md`
- `cpp/CMakeLists.txt` 和相关 configs
- 相关 module README
- 相关 source files
- 涉及推理时读取 `models/README.md`
- 涉及服务/API 时读取 `docs/backend_api_cpp_only.md`

## Workflow

1. 检查 language/runtime direction：除既有 mini-program frontend 外，system-side code 是否仍走全 C/C++。
2. 检查 dependency impact：new dependency、build option、binary size、deployment complexity、Windows/Ubuntu/RV1106 compatibility。
3. 检查 inference path：ONNX Runtime first、OpenCV DNN fallback、mock/debug distinction、`best.onnx` deployment asset。
4. 检查 edge constraints：避免 heavy cloud-first core pipeline，保持 1 TOPS / around 0.5 TOPS target 意识。
5. 检查 hardware validation：Codex 能本地验证什么，哪些必须 human board/camera test。
6. 检查 CMake/config implications：是否需要新增 option、config key 或文档。
7. 检查 fallback behavior：没有 OpenCV、ONNX Runtime、camera、provider key 或 network 时系统如何退化。

## Checks

- 新依赖是否可在目标平台安装和交叉部署。
- CMake option 默认值是否保持现有低门槛构建。
- mock/debug 路径是否仍清楚区分于真实 inference。
- cloud/provider 调用是否只是增强层，不成为核心闭环必需条件。
- 失败路径是否给出 clear error、fallback 或 pending/manual flow。

## Required output

- Deployment-sensitive change
- New or changed dependency
- Build/CMake impact
- Runtime/backend impact
- Edge suitability risk
- Local validation
- Human hardware validation needed
- Documentation updates needed

## Validation evidence

- 使用 CMake configure/build/test、runtime smoke test、backend selection log、config diff 或 manual board checklist。
- 对 Codex 无法验证的 camera/board/provider/network 行为，明确标注未验证。

## Documentation updates

- CMake option、dependency、runtime backend 或 deployment assumption 改变时，更新 `cpp/README.md`、相关 module README、`models/README.md` 或 `docs/backend_api_cpp_only.md`。
- 若影响项目状态、构建/测试路径、model runtime backend status 或 board validation 状态，更新 `docs/project_baseline.md`。
