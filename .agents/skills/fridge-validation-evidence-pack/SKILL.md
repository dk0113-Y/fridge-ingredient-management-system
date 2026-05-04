---
name: fridge-validation-evidence-pack
description: Turn a smart-fridge Codex task into verifiable competition evidence for testing, demo, report, paper, and defense. Use when a task affects recognition results, event judgment, inventory updates, local service output, demo chain, build/test procedure, module integration, hardware/camera/board validation, or report/paper/defense materials.
---

# fridge-validation-evidence-pack

## Purpose

把一次 Codex 任务的结果整理成可验证的比赛证据，服务 testing、demo、report、paper 和 defense，避免把 mock-only 或未验证能力写成已完成。

## When to use

- 任务影响 recognition results、event judgment、inventory updates、local service output。
- 任务影响 demo chain、build/test procedure、module integration。
- 任务涉及 hardware/camera/board validation。
- 任务输出会被用于 report、paper、defense 或演示材料。

## Do not use for

- 不影响可见行为的纯 refactor。
- 没有工程证据的开放式写作。
- `AGENTS.md` 已覆盖的通用 validation rules。

## Assumptions

- `AGENTS.md` 仍是 repository-level rule source。
- 本 skill 只添加 evidence packaging 专项流程。
- GPT/Codex prompt 仍必须提供 task-specific goal、scope、allowed files、validation 和 commit message。
- 本 skill 不用于开放式 brainstorming，不负责生成 Codex prompt，也不定义最终架构。

## Required inputs

- 本次任务要证明的能力或链路。
- 输入材料路径：image/video/session/mock JSON/manual operation。
- 输出材料路径：logs、JSON、crops、overlays、database state、API response 或 screenshots。
- 实际运行或需要运行的 commands。

## Required reading

- `docs/project_baseline.md`
- 相关 module README
- 相关 source files
- 涉及服务/API 时读取 `docs/backend_api_cpp_only.md`
- 涉及模型或推理后端时读取 `models/README.md`

## Workflow

1. 识别必须证明什么：build success、inference path、event classification、inventory mutation、API output、replay consistency、board deployability、demo result。
2. 识别 input evidence：image/video/session file、camera source、mock input、JSON input、manual operation。
3. 识别 output evidence：logs、`event.json`、stage2/final JSON、crop images、overlay images、database state、API response、screenshots。
4. 分类 evidence level：mock/debug、replay、local ONNX/OpenCV inference、real camera、board-side run、human hardware verification。
5. 记录 commands：build/test/replay/run commands used or required。
6. 说明可用于哪些材料：demo video、defense slides、report/paper experiment section。
7. 标出不得夸大的部分：unverified hardware、cloud provider behavior、mock-only result、target-only feature。

## Checks

- evidence 是否能复现实验结论。
- 输出 artifact 是否能关联到输入 session 或操作。
- 推理结果是否区分 mock/debug、OpenCV DNN、ONNX Runtime、真实 camera/board。
- report/defense wording 是否区分 implemented、baseline、target、unverified。

## Required output

- Evidence objective
- Inputs used/needed
- Outputs produced/needed
- Commands run/needed
- Evidence level
- Demo/defense/report value
- Unverified items
- Suggested artifact paths to preserve

## Validation evidence

- 优先保留命令输出摘要、session ID、artifact paths、JSON 文件名、API response 和截图/裁剪图路径。
- 如果真实硬件或摄像头无法由 Codex 验证，明确写为 human hardware verification needed。

## Documentation updates

- 若 evidence 改变当前完成度或验证状态，更新 `docs/project_baseline.md`。
- 若 evidence 支撑具体模块行为，更新对应 module README 或测试说明。
