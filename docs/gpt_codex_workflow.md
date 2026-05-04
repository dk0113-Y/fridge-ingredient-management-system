# GPT 生成 Codex 指令工作流

## 1. 文档定位

本文档规定本 Project 中 GPT 如何分析任务、向用户推荐 Codex 会话配置，以及如何生成可复制给 Codex 的任务指令。

本文档面向 ChatGPT Project 与人类协作者，不是 Codex 的仓库级执行规范。Codex 的仓库级执行规范后续应放在 `AGENTS.md`。Codex 的可安装工作流包后续应放在 `.agents/skills/*/SKILL.md`。

当前工程状态入口为 `docs/project_baseline.md`。最终方案设计入口为 `docs/system_final_design_cpp_only.md`。

## 2. 核心原则

1. 本 Project 默认采用“GPT + 人类思考，Codex 实操”的协作模式。
2. GPT 负责需求澄清、方案判断、技术路线收口、任务拆解、风险识别、Codex prompt 生成。
3. 人类负责确认任务目标、设置 Codex 会话配置、粘贴 Codex prompt、审查变更、运行真实硬件测试。
4. Codex 负责读取仓库、修改文件、运行测试、汇报变更。
5. GPT 生成 Codex 指令前，必须先根据任务类型给出“推荐 Codex 会话配置”。
6. 模型、思考程度、插件/工具开关、工作目录、分支策略属于人类在 Codex 界面/环境中设置的内容，应在 Codex prompt 之前单独告知用户，不应伪装成 Codex 能自动执行的 prompt 内容。
7. Codex skills 是例外：如果仓库存在相关 `.agents/skills/<skill-name>/SKILL.md`，GPT 应在 Codex prompt 中显式要求 Codex 使用对应 skill。

## 3. 推荐 Codex 会话配置格式

GPT 在输出 Codex prompt 前，应先输出以下配置建议。该部分是给人类看的会话设置，不是给 Codex 执行的任务 prompt。

```markdown
## 推荐 Codex 会话配置

- 工作目录：
- 分支策略：
- 推荐模型：
- 推荐思考程度：
- 需要开启的能力 / 插件：
- 是否需要终端：
- 是否需要构建 / 测试：
- 是否需要联网：
- 是否需要 GitHub 访问：
- 是否建议使用 Codex Skills：
- 备注：
```

配置判断规则：

- 文档小改：中等模型 / Medium reasoning 即可。
- 跨模块代码修改、架构重构、构建失败排查：推荐 strongest available Codex coding model / High reasoning。
- 只读分析任务：可以不启用文件写入。
- 需要实际修改代码：必须启用文件编辑。
- 需要验证构建测试：必须启用终端。
- 涉及仓库当前状态：必须启用 GitHub 或本地文件访问。
- 涉及外部依赖、官方文档、库版本：如 Codex 环境支持，应启用联网；否则让 Codex 明确无法验证外部信息。

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

`Skills to use` 的写法规则：

- 如果相关 skill 已存在，写：`Use the <skill-name> skill for this task.`
- 如果不确定 skill 是否存在，写：`If .agents/skills/<skill-name>/SKILL.md exists, use the <skill-name> skill for this task; otherwise continue with the required reading and workflow below.`
- 如果本轮不需要 skill，写：`No Codex skill is required for this task.`

注意：推荐 Codex 会话配置属于人类设置说明，不应混入 Codex prompt 主体；Codex skill 调用要求属于任务执行要求，可以写入 Codex prompt。

## 5. 任务类型与推荐配置

| 任务类型 | 推荐模型 / 思考程度 | 需要开启的能力 | 是否需要 skill |
|---|---|---|---|
| 纯文档小改 | fast coding model 或 latest Codex-capable coding model / Medium | 本地文件访问、文件编辑；通常不需要终端 | 通常不需要 |
| 工程状态文档更新 | latest Codex-capable coding model / Medium | 本地文件访问、文件编辑；必要时只读源码；通常不需要构建 | 通常不需要 |
| 生成/维护 `AGENTS.md` | strongest available Codex coding model / High | 本地文件访问、文件编辑；需要读取项目基线和模块 README | 可选；若已有文档治理 skill 则使用 |
| 新增/维护 Codex Skill | strongest available Codex coding model / High | 本地文件访问、文件编辑；必要时终端验证结构 | 需要；若已有 skill-creator 或相关 skill 则使用 |
| 单模块代码修改 | latest Codex-capable coding model / Medium 或 High | 本地文件访问、文件编辑、终端、构建/测试 | 若存在对应模块 skill 则使用 |
| 跨模块代码修改 | strongest available Codex coding model / High | 本地文件访问、文件编辑、终端、构建/测试 | 若存在对应模块或集成 skill 则使用 |
| 构建/测试失败排查 | strongest available Codex coding model / High | 本地文件访问、终端、构建/测试；必要时联网查官方文档 | 可选 |
| Module 1->2 联调 | strongest available Codex coding model / High | 本地文件访问、文件编辑、终端、构建/测试、读取数据路径 | 若存在视觉/联调 skill 则使用 |
| SQLite / HTTP server / 小程序接口联调 | strongest available Codex coding model / High | 本地文件访问、文件编辑、终端、构建/测试；必要时联网 | 若存在后端/接口联调 skill 则使用 |
| 答辩材料和演示文档生成 | latest Codex-capable coding model / Medium | 本地文件访问、文件编辑；如生成 PPT/文档可启用对应插件 | 若存在 presentations/documents skill 则使用 |

## 6. 仓库读取规则

Codex prompt 必须要求 Codex 优先读取：

- `docs/project_baseline.md`

如果涉及最终方案、架构、答辩、赛题得分点，再读：

- `docs/system_final_design_cpp_only.md`

如果涉及模块实现，再读：

- 对应模块 README
- `cpp/CMakeLists.txt`
- `configs`
- 相关源码

如果仓库中存在 `AGENTS.md`，Codex 必须先读 `AGENTS.md`。如果仓库中存在相关 skill，Codex prompt 必须显式要求使用该 skill。

## 7. 禁止事项

GPT 生成 Codex 指令时不得：

- 让 Codex 默认新建分支，除非用户明确要求。
- 让 Codex 修改模型文件、数据文件、小程序结构，除非任务明确要求。
- 让 Codex 恢复 Python/Flask 后端作为最终架构。
- 让 Codex 把目标态误写成当前已实现。
- 让 Codex 跳过文档和源码读取直接修改。
- 让 Codex 在未验证的情况下声称构建或测试通过。
- 让 Codex 忽略 `docs/project_baseline.md` 中的未完成状态。
- 把“推荐给人类的 Codex 会话配置”混入 Codex prompt 主体。

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
