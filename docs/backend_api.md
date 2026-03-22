# 后端接口说明

## 目标

Python 后端负责接收视觉侧输出的事件 JSON，保存事件历史，更新 SQLite 库存状态，并提供最小可用的本地 Web/API 界面。

## 路由

### `GET /health`

返回服务健康状态以及当前记录数量。

### `GET /events`

按时间倒序返回最近事件列表。

### `GET /inventory`

返回当前库存项以及待确认列表。

### `POST /confirm`

用于处理以下操作：

- 局部取出候选事件的确认
- 待确认项的驳回
- 手动库存修正

支持的表单或 JSON 字段包括：

- `action`
- `session_id`
- `item_name`
- `category`
- `count_delta`
- `remain_level`
- `note`

### `GET /`

渲染本地演示页面。

## 同步行为

在每次访问读取型接口或页面渲染前，后端都会扫描 `data/sessions/**/event.json`，并导入尚未处理过的事件。

这样可以保持第一阶段演示流程足够简单：

- 先运行 C++ 事件检测程序
- 再刷新本地 Web 页面
- 即可看到库存状态更新
