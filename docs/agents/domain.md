# 领域文档

工程技能在探索代码库时应如何使用此仓库的领域文档。

## 在探索之前，阅读这些

- 仓库根目录的 **`CONTEXT.md`**，或
- 如果存在，仓库根目录的 **`CONTEXT-MAP.md`**——它指向每个上下文的一个 `CONTEXT.md`。阅读与主题相关的每一个。
- **`docs/adr/`**——阅读触及你即将工作区域的 ADR。在多上下文仓库中，还要检查 `src/<context>/docs/adr/` 以获取上下文范围的决策。

如果这些文件中的任何一个不存在，**静默继续**。不要标记它们的缺失；不要建议提前创建它们。生产者技能（`/grill-with-docs`）在术语或决策实际解决时惰性创建它们。

## 文件结构

单上下文仓库（大多数仓库）：

```
/
├── CONTEXT.md
├── docs/adr/
│   ├── 0001-event-sourced-orders.md
│   └── 0002-postgres-for-write-model.md
└── src/
```

多上下文仓库（根目录存在 `CONTEXT-MAP.md`）：

```
/
├── CONTEXT-MAP.md
├── docs/adr/                          ← 系统级决策
└── src/
    ├── ordering/
    │   ├── CONTEXT.md
    │   └── docs/adr/                  ← 上下文特定决策
    └── billing/
        ├── CONTEXT.md
        └── docs/adr/
```

OpenTune 使用**单上下文**布局：根目录 `CONTEXT.md` + `docs/adr/`。项目级构建与架构说明见根目录 `AGENTS.md`。

## 使用术语表的词汇

当你的输出命名领域概念时（在 issue 标题、重构提案、假设、测试名称中），使用 `CONTEXT.md` 中定义的术语。不要偏离术语表明确避免的同义词。

如果你需要的概念尚未在术语表中，那是一个信号——要么你在发明项目不使用的语言（重新考虑），要么存在真正的差距（为 `/grill-with-docs` 注意它）。

## 标记 ADR 冲突

如果你的输出与现有 ADR 矛盾，明确揭示它而不是静默覆盖：

> _与 ADR-0007（事件源订单）矛盾——但由于……值得重新开放_
