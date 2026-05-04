# GPT 方案讨论与工程收口工作流

## 1. 文档定位

本文档定义 GPT 在 Codex 执行前如何与人类协作者讨论工程想法、判断可行性、收口实施方案，并决定是否需要交给 Codex。

本文档是仓库内保存的 GPT 侧 class-skill workflow，用于方案讨论和工程判断。它不是 Project instructions，不是 `AGENTS.md`，不是 Codex skill，也不是 Codex prompt template。它与 `docs/gpt_codex_workflow.md` 互补：本工作流用于 Codex 介入前；当方案已经清晰、可执行并得到确认后，再转入 `docs/gpt_codex_workflow.md` 生成 Codex execution prompt。

如果方案尚不清楚，应继续停留在 Human + GPT 讨论阶段；如果方案已经明确、范围可控、验收方式清楚，再决定是否交给 Codex。

## 2. 与其它文档的职能边界

| Document | Role | Used when | Should not do |
|---|---|---|---|
| Project instructions | ChatGPT Project 顶层硬约束和引用入口 | 所有 GPT 讨论和 Codex prompt 生成前 | 展开具体仓库执行细节 |
| `docs/project_baseline.md` | 当前工程状态、阅读入口和推进优先级索引 | 对齐当前实现、未完成项和下一步优先级 | 替代最终架构或源码事实 |
| `docs/system_final_design_cpp_only.md` | 全 C/C++ 目标架构和答辩口径基线 | 讨论最终方案、赛题得分点、模块职责和目标设计 | 把目标态写成当前已完成状态 |
| `docs/gpt_solution_design_workflow.md` | Human + GPT 方案讨论、可行性判断和工程收口 workflow | Codex 介入前讨论新方案、路线选择和是否交给 Codex | 生成 Codex prompt 或定义 Codex 仓库执行规则 |
| `docs/gpt_codex_workflow.md` | 将已收口方案转换为 Codex prompt，并审查 Codex push 结果 | Human + GPT 已达成可执行计划后 | 替代开放式方案讨论 |
| `AGENTS.md` | Codex repository-level execution rules | Codex 进入仓库执行任务后 | 定义 GPT 侧头脑风暴或最终系统架构 |
| future `.agents/skills/*/SKILL.md` | 复用的专项 Codex workflow | 某类复杂 Codex 任务反复出现时 | 复制 `AGENTS.md` 或承担开放式 brainstorming |

## 3. 适用场景

本工作流适用于：

- 用户提出新功能或工程改进想法。
- 用户询问某条技术路线是否值得做。
- 用户询问如何改进识别、库存、部分取出、部署、测试、演示、报告或答辩。
- 用户询问某个任务是否应交给 Codex。
- 用户要求比较多种实现路径。
- 用户要求基于真实工程状态组织论文、报告或答辩技术叙事。

对于简单事实问题，或计划已经明确、只需要直接生成 Codex prompt / 命令的任务，可以不使用完整方案讨论流程。

## 4. 方案讨论流程

GPT 讨论方案时应按需要覆盖以下阶段：

1. 复述用户真实需求，确认要解决的问题和期望结果。
2. 对齐当前项目状态，区分已实现、baseline、目标态和未验证项。
3. 映射到研电赛评分点，判断对成绩或展示的实际贡献。
4. 检查端侧部署可行性，避免超出 embedded/edge AIoT 约束。
5. 检查工程成本和当前代码成熟度，判断是否适合现在做。
6. 考虑冰箱场景鲁棒性，包括遮挡、反光、光照、摆放变化和误检。
7. 分离基础必做方案、稳妥增强方案和暂不建议方案。
8. 识别主要风险、回退路径和无法由 Codex 单独验证的部分。
9. 定义验收标准和证据形式，例如日志、截图、测试、演示链路或人工硬件验证。
10. 决定继续讨论，或在用户同意后转入 `docs/gpt_codex_workflow.md`。

## 5. 研电赛评审映射

当提案与比赛展示或答辩相关时，GPT 应说明它如何影响以下维度：

- system completeness
- real event recognition
- object classification/counting
- inventory management/update strategy
- partial take-out handling
- demo stability
- documentation and testing evidence

GPT 应明确说明 proposed change 如何提升评分、支撑演示或增强证据链；如果收益低、风险高或与当前阶段不匹配，也应说明为什么暂不值得做。

## 6. 端侧与工程可落地性检查

GPT 在推荐方案前应检查：

- 是否符合 embedded/edge AIoT 部署方向。
- 是否保持在 1 TOPS 以内，理想目标约 0.5 TOPS。
- 是否避免把 heavy cloud-first model 作为核心 pipeline 依赖。
- 是否保留“低成本事件检测 + YOLO 粗分类 + 规则/库存 + 可选细粒度增强”的分层识别思路。
- 当前团队是否能稳定演示、调试和解释该方案。
- 是否需要额外硬件、摄像头、SDK、模型导出、数据准备或人工验证。

如果某方案只能在云端、强算力或未准备的数据条件下成立，应作为高风险或暂不建议方案处理。

## 7. 方案分层输出格式

GPT 在方案讨论中优先使用以下 planning checklist，但不必每次机械完整展开：

- 推荐结论
- 当前工程依据
- 基础必做方案
- 稳妥增强方案
- 暂不建议方案
- 影响模块
- 预计修改范围
- 验收标准
- 是否建议交给 Codex
- 若交给 Codex，下一步应进入 `docs/gpt_codex_workflow.md`

输出应服务于工程决策：先给结论，再说明依据、风险、验证方式和下一步。

## 8. 转入 Codex 的判定标准

只有同时满足以下条件时，GPT 才应转入 Codex prompt generation：

- goal 清楚。
- affected files 或 modules reasonably bounded。
- success criteria 已定义。
- validation method 已知。
- risk boundaries 已理解。
- user agrees to proceed。

如果任一条件不满足，应继续 Human + GPT 讨论，不要生成 Codex prompt，也不要让 Codex 做开放式 brainstorming。

## 9. 禁止事项

GPT 不应：

- 对不清楚的想法直接跳到 Codex。
- 让 Codex 承担开放式 brainstorming。
- 推荐 heavy cloud-first models 作为核心 pipeline。
- 把 target architecture 当作 current implementation。
- 宣称未验证能力已经完成。
- 讨论当前状态时忽略 `docs/project_baseline.md`。
- 讨论最终架构时忽略 `docs/system_final_design_cpp_only.md`。
- 生成超过真实实现和测试证据的论文、报告或答辩表述。

## 10. 与论文/答辩的关系

本工作流也适用于论文、报告、答辩和演示规划。GPT 应把工程事实连接到比赛评分点和证据链，同时严格区分：

- implemented items
- baseline items
- target design
- unverified items

论文和答辩表达应基于当前仓库事实、验证结果和可展示证据，不应把规划中的目标能力写成已经完成。
