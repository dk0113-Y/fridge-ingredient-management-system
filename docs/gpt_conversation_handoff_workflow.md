# GPT 对话交接提示词工作流

## 1. 文档定位

本文档定义 GPT 在当前 chat context 过长，或用户希望新开对话继续同一 Project 时，如何生成可复制给新 GPT 对话的 continuation prompt。

本文档是 GPT 侧 class-skill workflow，用于同一 Project 内的 chat-to-chat continuity。它不是 Project instructions，不是 `AGENTS.md`，不是 Codex skill，不是 Codex prompt template，也不是实际 handoff records 的存放位置。

本文档与以下文档互补：

- `docs/project_baseline.md`：稳定项目状态与文档入口。
- `docs/gpt_solution_design_workflow.md`：开放式方案讨论与工程收口。
- `docs/gpt_codex_workflow.md`：Codex prompt 生成与 post-push review。

当用户要求 GPT 为新对话生成提示词、转移上下文或让另一个 conversation 继续当前工作时，使用本文档。输出应是 human-copyable continuation prompt for a new GPT conversation，不是 Codex task prompt。

## 2. 与其它文档的职能边界

| Document | Role | Used when | Should not do |
|---|---|---|---|
| Project instructions | ChatGPT Project 顶层硬约束和引用入口 | 所有 GPT 对话和任务前 | 存放具体交接记录 |
| `docs/project_baseline.md` | 稳定项目状态、阅读入口和推进索引 | 新对话需要快速对齐当前工程事实 | 替代 handoff prompt 或展开完整对话历史 |
| `docs/gpt_solution_design_workflow.md` | Human + GPT 方案讨论、可行性判断和工程收口 | 新对话继续开放式规划时 | 生成 Codex prompt |
| `docs/gpt_codex_workflow.md` | 已收口方案的 Codex prompt 生成与 post-push review | 新对话继续 Codex 执行或验收时 | 存放聊天交接记录 |
| `AGENTS.md` | Codex repository-level execution rules | Codex 进入仓库执行任务后 | 指导 GPT 新对话交接 |
| `.agents/skills/*/SKILL.md` | 专项 Codex execution workflows | Codex prompt 明确调用 skill 时 | 承担 GPT 对话交接或 brainstorming |
| `docs/gpt_conversation_handoff_workflow.md` | 定义 GPT 如何生成新对话 continuation prompt | 当前对话需要迁移到新 GPT chat 时 | 替代项目状态文档、Codex prompt 或实际 handoff archive |

## 3. 适用场景

本 workflow 适用于：

- 当前 chat context 过长。
- 用户希望新开 Project chat 但继续同一任务。
- 一个重要 milestone 或 Codex commit 刚完成。
- 用户希望新对话继续 implementation planning、Codex prompt generation、commit acceptance review、paper/report/defense writing 或 hardware testing。
- 用户希望只转移有用工作上下文，而不是复制整段对话。

短小、自包含的问题不需要使用本文档。交接提示词不应总结 private chain-of-thought；只总结 decisions、facts、open issues 和 next actions。

## 4. 交接提示词必须包含的内容

生成 continuation prompt 时应包含：

- Project identity and goal。
- 当前 repository 和重要 document entry points。
- 当前 task or discussion focus。
- 新对话必须保留的 stable project facts。
- 当前对话中形成的 recent decisions。
- 最近 Codex commits 或 repository changes，如果有：commit hash、changed files、validation result、GPT acceptance conclusion。
- Open issues and unresolved risks。
- Next recommended action。
- 新对话应优先进入哪个 workflow：`docs/gpt_solution_design_workflow.md`、`docs/gpt_codex_workflow.md`，或 GitHub commit review for Codex acceptance。
- Important prohibitions：不要把 target design 当作 implemented；不要忽略 `docs/project_baseline.md`；方案未收口前不要生成 Codex prompt；不要把未验证 hardware/camera/board 结果写成已验证。

## 5. 交接提示词不应包含的内容

交接提示词不应：

- 复制完整 conversation。
- 复制完整 `docs/project_baseline.md`、`AGENTS.md` 或 workflow docs。
- 包含 secrets、tokens、API keys、private credentials 或 personal sensitive data。
- 把 unsupported speculation 写成 fact。
- 包含 private chain-of-thought。
- 把 unverified target features 写成 implemented。
- 用长叙事淹没 next action。

## 6. 推荐交接提示词结构

以下是推荐结构。实际输出应使用 plain text，并保持可复制。

你正在接手一个研电赛冰箱食材识别与管理系统 Project 的延续对话。

请先读取：
- `docs/project_baseline.md`
- `docs/gpt_solution_design_workflow.md`
- `docs/gpt_codex_workflow.md`
- `AGENTS.md`
必要时再读取相关模块 README、源码、commit 或 Codex skills。

当前任务背景：
...

当前已确认的关键结论：
1. ...
2. ...

最近相关仓库变更 / Codex commit：
- commit hash:
- changed files:
- validation:
- GPT acceptance:

当前未完成问题：
1. ...
2. ...

下一步建议：
...

请遵守：
- 如果我提出开放式方案问题，先按 `docs/gpt_solution_design_workflow.md` 讨论，不要直接生成 Codex prompt。
- 如果我说方案已定，请按 `docs/gpt_codex_workflow.md` 生成 Codex prompt。
- 如果我转发 Codex report，请通过 GitHub connector 检查 commit 后验收。
- 不要把目标态写成已实现，不要把 mock/debug 当作真实部署验证。

## 7. 交接提示词生成流程

GPT 生成交接提示词时应：

1. 识别为什么需要 handoff。
2. 读取或引用 `docs/project_baseline.md` 对齐稳定项目状态。
3. 只提取当前 conversation 中 durable context：decisions、user preferences、repository changes、accepted/rejected plans、open risks、next actions。
4. 区分 stable project facts、current conversation decisions、unverified assumptions 和 next action。
5. 决定新对话应优先进入哪个 workflow。
6. 输出一个 copyable continuation prompt。
7. 保持足够简洁，让新对话能直接使用。

## 8. 新对话接收后的预期行为

新 GPT chat 应先读取 `docs/project_baseline.md`。如果第一项任务是 planning，使用 `docs/gpt_solution_design_workflow.md`；如果第一项任务是 Codex execution，使用 `docs/gpt_codex_workflow.md`；如果第一项任务是 Codex result acceptance，通过 GitHub connector 检查 commit。

如果 repository state 与 handoff text 不一致，应信任 repository state，并报告冲突。

## 9. 禁止事项

GPT 不应：

- 生成没有 next action 的模糊 handoff prompt。
- 包含完整 chat history。
- 包含 private chain-of-thought。
- 夸大未验证工作。
- 跳过当前 repository state。
- 混合 Codex task prompt 和 GPT continuation prompt。
- 在 repository state 已变化时，用 handoff prompt 替代 `docs/project_baseline.md` 更新。

## 10. 与论文/答辩/交付的关系

Handoff prompts 可以帮助新对话继续 paper、report、defense 和 demo 工作。提示词必须保留 implemented、baseline、target、unverified 的区分，指向 evidence sources，并提醒新对话避免 unsupported claims。
