# GPT 生成 Codex 指令工作流

## 1. 文档定位

本文档规定本 Project 中 GPT 如何分析任务、向用户输出执行前提示，以及如何生成可复制给 Codex 的任务指令。

本文档面向 ChatGPT Project 与人类协作者，是仓库维护的工作流说明；它不是 Project instructions，也不是 Codex 的仓库级执行规范。Project instructions 提供顶层硬约束和引用入口；本文档展开说明 GPT 如何把 Human + GPT 已同意的计划转化为 Codex execution prompt。Codex 的仓库级执行规范后续应放在 `AGENTS.md`。Codex 的可安装工作流包后续应放在 `.agents/skills/*/SKILL.md`。

开放式方案讨论、可行性判断和 Codex 执行前的工程收口应先使用 `docs/gpt_solution_design_workflow.md`。本文档从 Human + GPT 已达成 actionable plan 之后开始，用于生成 Codex prompt 并审查 Codex pushed result。

Project instructions 可以引用本文档，但硬约束优先级高于本文档。如果两者冲突，应以 Project instructions 和用户当前明确要求为准。

当前工程状态入口为 `docs/project_baseline.md`。最终方案设计入口为 `docs/system_final_design_cpp_only.md`。

## 2. 核心原则

1. 本 Project 默认采用“GPT + 人类思考，Codex 实操”的协作模式。
2. GPT 负责需求澄清、方案判断、技术路线收口、任务拆解、风险识别、Codex prompt 生成。
3. 人类负责确认任务目标、决定是否新开 Codex 任务、粘贴 Codex prompt、审查变更、运行真实硬件测试。
4. Codex 负责读取仓库、修改文件、运行测试、在安全时提交并推送、汇报变更。
5. GPT 生成 Codex 指令前，必须先根据已同意的计划给出“执行前提示”。
6. 模型和思考程度建议应由任务风险、影响范围、验证难度和是否需要真实硬件决定；不应机械套用固定表格。
7. Codex skills 是例外：如果仓库存在相关 `.agents/skills/<skill-name>/SKILL.md`，GPT 应在 Codex prompt 中显式要求 Codex 使用对应 skill。

## 3. GPT 输出给人类的执行前提示

GPT 在输出 Codex prompt 前，应先输出以下执行前提示。该部分是给人类看的极简建议，不是 Codex UI 字段，也不是给 Codex 执行的任务 prompt。

```markdown
## 执行前提示

- 推荐模型：
- 推荐思考程度：
```

执行前提示的写法规则：

- 执行前提示默认只保留推荐模型和推荐思考程度。
- 推荐模型只写能力等级，例如 `fast coding model`、`latest Codex-capable coding model`、`strongest available Codex coding model`，不要绑定不可保证的具体模型名称。
- 推荐思考程度使用 `Low`、`Medium`、`High`。
- 其它信息只在必要时作为普通说明文字补充，不作为固定 checklist 字段。
- commit/push policy 属于 Codex prompt、`AGENTS.md` 和 direct-main workflow rules。
- work directory、branch policy、validation commands 和 changed-file constraints 应写入 Codex prompt body。
- Codex skills 不是人类侧设置项；如果相关 skill 存在，必须在 Codex prompt 的 `Skills to use` 部分说明。
- 除非用户明确确认当前环境存在真实可选的 Codex plugin toggles，不要增加 `recommended plugins` 字段。

## 4. Codex Prompt 标准结构

GPT 生成给 Codex 的任务 prompt 必须包含以下部分：

1. Task title
2. Working context
3. Required reading
4. Goal
5. Scope
6. Constraints
7. Skills to use
8. Implementation requirements
9. Validation
10. Done when
11. Final report requirements

其中：

- `Working context` 应包含 repository、local path、current branch 等必要上下文，并明确要求 Codex follow `AGENTS.md` for repository-level execution rules。
- `Required reading` 通常应先列 `AGENTS.md`，再列本任务相关的 docs/source files；不要逐条复制 `AGENTS.md` 的完整阅读顺序，除非任务需要特殊阅读路径。
- `Constraints` 应写本任务特有约束，不应重复 `AGENTS.md` 的每一条通用禁止事项；高风险任务可以显式重申相关关键边界。
- `Validation` 必须说明本任务需要运行哪些检查、构建或测试；也可以写明同时遵循 `AGENTS.md` validation and push policy。除非任务要求精确命令，不要复制整个 `AGENTS.md` build/test section。
- `Done when` 必须说明任务级完成条件；可以引用 `AGENTS.md` direct-main policy，而不是复制全部 push rules。
- `Final report requirements` 默认使用 `AGENTS.md` final report format；只有任务需要额外信息时才追加 task-specific reporting items。

`Skills to use` 的写法规则：

- 如果相关 skill 已存在，写：`Use the <skill-name> skill for this task.`
- 如果不确定 skill 是否存在，写：`If .agents/skills/<skill-name>/SKILL.md exists, use the <skill-name> skill for this task; otherwise continue with the required reading and workflow below.`
- 如果本轮不需要 skill，写：`No Codex skill is required for this task.`

注意：执行前提示属于人类决策说明，不应混入 Codex prompt 主体；Codex skill 调用要求属于任务执行要求，可以写入 Codex prompt。

### AGENTS.md 与 Codex prompt 的职能边界

- `AGENTS.md` 是仓库级 standing rule file，定义 Codex 在本仓库内的默认执行规则。
- `AGENTS.md` 覆盖默认阅读顺序、项目硬约束、禁止事项、验证纪律、文档同步、direct-main commit/push policy 和 final report format。
- GPT 生成的 Codex prompt 是 task-specific overlay，应要求 Codex 读取并遵循 `AGENTS.md`，但不应把 `AGENTS.md` 的通用规则逐条复制进每个 prompt。
- Codex prompt 应聚焦本任务的 exact goal、affected files、allowed scope、task-specific constraints、implementation details、validation checks、commit message，以及必要的 additional report items。
- 如果任务高风险，prompt 可以显式重申与本任务直接相关的关键 `AGENTS.md` 规则，但不应复制整套规则。
- 未来 Codex skills 也不应重复 `AGENTS.md`；skill 应定义 recurring complex task type 的 specialized workflow，并让仓库级默认规则继续由 `AGENTS.md` 承担。

### 推荐 prompt pattern

下面是推荐的引用式写法示例。它展示结构和边界，不是必须逐字复制的长模板。

Task title:
  ...

Working context:
  Work in the local repository ...
  Follow AGENTS.md for repository-level execution rules.

Required reading:
  - AGENTS.md
  - docs/project_baseline.md
  - task-relevant file(s)

Goal:
  ...

Scope:
  ...

Task-specific constraints:
  Only this task's extra constraints.

Skills to use:
  ...

Implementation requirements:
  ...

Validation:
  Task-specific checks.
  Also follow AGENTS.md validation and push policy.

Done when:
  ...

Commit and push:
  Use AGENTS.md direct-main policy.
  Commit message: ...

Final report:
  Use AGENTS.md final report format.
  Additionally mention ...

### Codex prompt 输出格式规则

GPT 生成的 Codex prompt 应该易于作为一个稳定文本块复制，避免 Markdown fence 嵌套、UI 泄漏或视觉截断。

- 当 GPT 向 human 输出较长 Codex prompt 时，应将整个 prompt 包在外层 four-backtick `text` fence 中。
- 外层可复制块内部避免使用 triple-backtick Markdown code fences。
- 如果 Codex prompt 内需要 command examples，应写成普通缩进命令行或 bullet-list command lines，不使用 fenced code blocks。
- 如果必须在 Codex prompt 内展示 literal code fence，应使用不同于外层的 fence length，并确保不会关闭外层 fence。
- Codex prompts 优先使用 plain text sections 和 indented commands。
- Codex prompt 应保持为一个 copyable unit，除非用户明确要求多个 prompts。
- prompt block 开始后，避免在其外部继续混入普通说明文字；human-facing notes 应放在 prompt block 之前。
- 编辑 `docs/gpt_codex_workflow.md` 本身时，Codex 应检查 malformed Markdown fences、unclosed code blocks 和可能破坏渲染的 nested triple-backtick fences。

## 5. 动态模型与思考程度建议

GPT 应根据已同意的计划动态推荐模型和思考程度，而不是按固定任务类型表机械选择。

判断维度：

- 影响范围：只读、单文档、单模块、多模块、跨系统链路。
- 风险级别：是否触碰源码、构建系统、数据持久化、HTTP server、小程序接口、模型推理、硬件运行。
- 验证难度：是否只需文档 diff，是否需要单元测试、CMake build、真实摄像头、板端运行或人工硬件验证。
- 上下文复杂度：是否需要理解最终方案、当前工程状态、历史未完成项和多个 README。
- 外部依赖：是否涉及官方文档、库版本、SDK、云 provider、GitHub remote 状态。

推荐规则：

- `Low`：只读总结、简单定位、非常小的文字修正，且不需要跨文档推理。
- `Medium`：文档小改、工程状态文档更新、单模块低风险实现、常规测试补充。
- `High`：跨模块代码修改、架构重构、构建/测试失败排查、Module 1 -> 2 联调、SQLite / HTTP server / 小程序接口联调、板端部署、可能影响远端 `main` 的复杂任务。

模型建议：

- 简单只读或小文档任务：`fast coding model` 或 `latest Codex-capable coding model`。
- 普通文档维护和单模块低风险代码任务：`latest Codex-capable coding model`。
- 跨模块、调试、架构、构建、硬件或持久化相关任务：`strongest available Codex coding model`。

如果任务需要真实硬件、私有密钥、外部 SDK 或远端 GitHub 状态，GPT 应在执行前提示中明确这些前置条件，并在 Codex prompt 中要求 Codex 如实报告无法验证的部分。

## 6. 仓库读取规则

Codex prompt 必须要求 Codex 先读取并遵循：

- `AGENTS.md`

然后根据任务需要读取：

- `docs/project_baseline.md`

如果涉及最终方案、架构、答辩、赛题得分点，再读：

- `docs/system_final_design_cpp_only.md`

如果涉及模块实现，再读：

- 对应模块 README
- `cpp/CMakeLists.txt`
- `configs`
- 相关源码

除非任务需要特殊阅读路径，prompt 不应重复 `AGENTS.md` 的完整默认阅读顺序；只列本任务相关的额外 docs/source files 即可。如果仓库中存在相关 skill，Codex prompt 必须显式要求使用该 skill。

## 7. 禁止事项

GPT 生成 Codex 指令时不得：

- 让 Codex 默认新建分支，除非用户明确要求。
- 让 Codex 修改模型文件、数据文件、小程序结构，除非任务明确要求。
- 让 Codex 恢复 Python/Flask 后端作为最终架构。
- 让 Codex 把目标态误写成当前已实现。
- 让 Codex 跳过文档和源码读取直接修改。
- 让 Codex 在未验证的情况下声称构建或测试通过。
- 让 Codex 忽略 `docs/project_baseline.md` 中的未完成状态。
- 把“执行前提示”混入 Codex prompt 主体。

## 8. Codex 完成后的回填规则

如果 Codex 任务影响以下内容，必须要求同步更新 `docs/project_baseline.md`：

- 模块完成状态
- 未完成清单
- P0/P1/P2 优先级
- 构建/测试方式
- 推理后端状态
- 库存持久化状态
- HTTP server / 小程序联调状态
- Module 3 接入状态

如果任务新增 `AGENTS.md` 或 `.agents/skills`，也应更新 `docs/project_baseline.md` 的阅读入口或使用规则。

## 9. End-to-end collaboration loop

本 Project 的默认工作流是：

1. Human 和 GPT 讨论需求，确认目标、范围、约束和验收标准。
2. GPT 识别任务类型、风险、影响模块和可能的验证方式。
3. GPT 输出给人类看的执行前提示。
4. GPT 输出可复制给 Codex 的任务 prompt。
5. Human 决定是否新开 Codex 任务、是否允许 commit + push、是否需要准备本地资源，然后粘贴 prompt。
6. Codex 读取 required docs、必要源码和当前 git 状态。
7. Codex 在当前 `main` 分支执行任务。
8. Codex 按 prompt 要求验证变更。
9. Codex 在安全时 commit 并 push 到 `origin/main`。
10. Codex 报告 commit hash、changed files、validation results 和 unresolved risks。
11. Human 将 Codex report 转发给 GPT。
12. GPT 通过 GitHub connector 或远程仓库检查 pushed commit / remote `main`，并给出 acceptance review 或 follow-up Codex prompt。

该闭环不引入默认分支工作流。除非用户明确要求，GPT 不应建议 Codex 新建分支或打开 PR。

## 10. Commit and push policy

### Default policy

- 对 documentation-only tasks 和 small safe code tasks，Codex 应在验证后 commit 并 push 到 `origin/main`。
- 对 code tasks，Codex 只有在运行相关 build/tests 后，才应 commit 并 push；如果测试无法运行，Codex 必须清楚报告原因，并且只有在任务明确允许继续时才可 push。
- Push target 应为 `origin/main`，因为当前项目工作流避免多分支。

### Required before push

Codex push 前必须：

- 运行 `git status`。
- 运行 `git diff` 或 `git diff --name-only`。
- 确认 changed files 都在任务 scope 内。
- 按任务要求运行 required validation。
- 使用简洁 commit message。

### Do not push when

Codex 不得 push 的情况：

- Tests fail，并且失败由当前 changes 引起。
- 任务 scope 已经超出 prompt。
- 任务会修改 model files、data files、generated binaries 或 mini-program structure，但没有明确 permission。
- 存在 unrelated uncommitted changes，且无法安全区分本次任务变更。
- Codex 不确定 implementation 是否符合 Human + GPT 已同意的 plan。
- User explicitly asks not to push。

### If Codex does not push

如果 Codex 不 push，必须：

- 清楚说明原因。
- 列出 changed files。
- 提供 human 下一步需要执行的 exact command 或 decision。

## 11. Codex final report format

Codex 完成任务后必须按以下格式汇报：

```markdown
- Branch:
- Commit pushed:
- Commit hash:
- Changed files:
- Summary of changes:
- Validation run:
- Validation result:
- Files intentionally not changed:
- Remaining risks / unverified items:
- Whether docs/project_baseline.md was updated:
- Recommended next step:
```

如果没有 push，`Commit pushed` 应写 `No`，`Commit hash` 应写 `N/A`，并在 `Remaining risks / unverified items` 或 `Recommended next step` 中说明原因和下一步。

## 12. GPT acceptance review after Codex push

当 Human 转发 Codex report 后，GPT 应执行 acceptance review：

- 在可用时通过 GitHub connector 检查 commit 或 remote `main`。
- 对照 Codex report 和实际 changed files。
- 验证变更是否符合 Human + GPT 已同意的 plan。
- 检查本次任务是否应同步更新 `docs/project_baseline.md`。
- 识别 missing tests、risky changes 或 documentation drift。
- 给出 acceptance，或生成 follow-up Codex prompt。

GPT 不应仅凭 Codex report 宣称验收通过；如果无法访问远程仓库或无法验证关键事实，应明确说明限制，并给出人工检查项。
