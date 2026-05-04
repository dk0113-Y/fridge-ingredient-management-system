---
name: fridge-interface-contract-guard
description: Protect cross-module smart-fridge contracts such as JSON fields, event outputs, stage2/final artifacts, local service responses, config keys, replay/session formats, and module boundary assumptions. Use when a task affects event.json, stage2/final output, Module 1 to Module 2 handoff, Module 2 to Module 4 event fields, Module 4 inventory/pending structures, Module 5 local service JSON, mini-program-facing API data, config keys, or replay/session formats.
---

# fridge-interface-contract-guard

## Purpose

保护跨模块 contract，避免 JSON fields、event output、stage2/final artifacts、local service responses、config keys 或 module boundary assumptions 被局部改动破坏。

## When to use

- 任务影响 `event.json`、stage2/final output、session replay format。
- 任务影响 Module 1 -> Module 2 handoff、Module 2 -> Module 4 event fields。
- 任务影响 Module 4 inventory/pending review structures。
- 任务影响 Module 5 local service JSON responses 或 mini-program-facing API data。
- 任务影响 config keys、class interface 或 module output。

## Do not use for

- 没有 external/module-visible contract 的内部实现修改。
- 一般 style cleanup。
- 开放式架构讨论。

## Assumptions

- `AGENTS.md` 仍是 repository-level rule source。
- 本 skill 只添加 contract guard 专项流程。
- GPT/Codex prompt 仍必须提供 task-specific goal、scope、allowed files、validation 和 commit message。
- 本 skill 不用于开放式 brainstorming，不负责生成 Codex prompt，也不定义最终架构。

## Required inputs

- 被修改或被依赖的 contract 名称、文件、API、config key 或 class interface。
- producer 和 consumer 的相关文件路径。
- 兼容性要求、样例 JSON、replay/session 或测试输入。

## Required reading

- `docs/project_baseline.md`
- 相关 module README
- 相关 source files
- 涉及服务/API 时读取 `docs/backend_api_cpp_only.md`
- 涉及模型输出字段时读取 `models/README.md`

## Workflow

1. 识别 contract：file format、JSON schema、API response、config key、class interface 或 module output。
2. 识别 producer：创建该 contract 的 module/file/function。
3. 识别 consumer：读取该 contract 的 module/file/function/UI/service。
4. 检查 compatibility：old fields、new fields、optional fields、default values、missing-field behavior。
5. 检查 semantic consistency：operation names、class names、confidence values、bbox fields、`partial_take_out_candidate`、`uncertain`、inventory IDs、pending IDs。
6. 检查 documentation：README、backend API docs、module docs 或 `docs/project_baseline.md` 是否需要更新。
7. 检查 tests/replay：sample JSON、session replay、unit tests 或 manual validation 是否需要更新。

## Checks

- 字段 rename/remove 是否会破坏旧 consumer。
- 新字段是否有 default 或 optional 语义。
- `operation` / event type 与 inventory mutation 语义是否一致。
- bbox 坐标、confidence、class name 和 count delta 是否保持同一含义。
- service response 是否仍满足 mini-program-facing data 预期。

## Required output

- Contract name/type
- Producer
- Consumer
- Field/interface changes
- Compatibility risks
- Required docs update
- Required validation/replay/test
- Whether downstream modules may break

## Validation evidence

- 使用 sample JSON、session replay、unit tests、API response diff 或 manual validation 证明 producer/consumer 仍兼容。
- 对无法运行的下游验证，明确列出需要 human 或后续任务确认的 consumer。

## Documentation updates

- Contract 可见行为改变时，更新对应 module README 或 `docs/backend_api_cpp_only.md`。
- 若影响项目状态、当前限制或阅读入口，再更新 `docs/project_baseline.md`。
