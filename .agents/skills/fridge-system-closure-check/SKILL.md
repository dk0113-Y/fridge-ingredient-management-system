---
name: fridge-system-closure-check
description: Check whether a smart-fridge code or documentation change preserves or improves the end-to-end closure from event detection through keyframes, YOLO detection/counting, event judgment, inventory update, local API/display, manual confirmation, and demo evidence. Use when a task affects event detection, keyframe capture, YOLO diff logic, event JSON/stage2/final output, inventory update, pending review, local service JSON, mini-program-facing interface, or demo/test evidence.
---

# fridge-system-closure-check

## Purpose

检查一次代码或文档变更是否保持或增强本项目的端到端系统闭环，避免只改了局部却断开上游输入、下游消费、库存影响或演示证据。

## When to use

- 任务影响 event detection、keyframe capture、YOLO detection/counting、event diff logic。
- 任务影响 `event.json`、stage2/final output、inventory update、pending review、uncertain handling。
- 任务影响 local service JSON、mini-program-facing interface、demo/test evidence。

## Do not use for

- 与系统行为无关的纯文本修改。
- 开放式方案 brainstorming。
- `AGENTS.md` 已覆盖的通用仓库安全检查。

## Assumptions

- `AGENTS.md` 仍是 repository-level rule source。
- 本 skill 只添加专项闭环检查步骤。
- GPT/Codex prompt 仍必须提供 task-specific goal、scope、allowed files、validation 和 commit message。
- 本 skill 不用于开放式 brainstorming，不负责生成 Codex prompt，也不定义最终架构。

## Required inputs

- 本次任务目标、允许修改范围和实际 changed files。
- 涉及的 session、event、JSON、API、库存或 demo evidence 路径。
- 已运行或计划运行的 validation/replay/test 命令。

## Required reading

- `docs/project_baseline.md`
- 相关模块 README
- 相关 source files
- 涉及接口时读取 `docs/backend_api_cpp_only.md`
- 涉及模型或推理后端时读取 `models/README.md`

## Workflow

1. 标出任务触碰的闭环阶段：event detection -> before/after keyframe -> YOLO coarse classification/counting -> event judgment -> inventory update -> local display/API -> manual confirmation -> log/evidence。
2. 检查 upstream input：输入由谁产生、存在哪里、是真实输入、mock、debug 还是 replay。
3. 检查 downstream consumer：哪个 module、file、API 或 UI 消费输出。
4. 检查 event semantics：`operation`、coarse class、count delta、bbox movement、area change、`uncertain`、`partial_take_out_candidate` 是否一致。
5. 检查 inventory impact：add/remove/rearrange/partial candidate 是否能进入库存更新或 pending review。
6. 检查 fallback：low-confidence、`uncertain` 或不支持场景是否有 manual confirmation / pending review 路径。
7. 检查 evidence：是否有 logs、JSON、screenshots/crops、replay files 或 tests 证明链路没有断。

## Checks

- 上游输出字段是否满足下游最小输入要求。
- `partial_take_out_candidate` 仍限定在 fruit/vegetable 场景。
- mock/debug 路径没有被描述成真实 ONNX inference。
- `event.json` 仍作为调试、追溯、回放和演示证据，不作为 Python backend import pipeline。
- 变更是否需要同步 module README 或 `docs/project_baseline.md`。

## Required output

- Closure stage touched
- Upstream input
- Downstream consumer
- Changed or affected files
- Closure risks
- Missing links
- Required validation/evidence
- Whether `docs/project_baseline.md` or module README should be updated

## Validation evidence

- 优先使用现有 tests、session replay、stage2/final JSON、event JSON、crop/overlay artifacts、local service response 或人工演示记录。
- 明确标注 evidence level：mock/debug、replay、local inference、real camera、board-side run、human hardware verification。

## Documentation updates

- 若变更影响模块完成状态、当前限制、P0/P1/P2、构建/测试路径或主链路接入状态，更新 `docs/project_baseline.md`。
- 若变更影响具体模块行为或输入输出，更新对应 module README。
