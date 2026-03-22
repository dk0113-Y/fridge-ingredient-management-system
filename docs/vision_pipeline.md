# 视觉流程说明

## 目标

C++ 视觉主链当前遵循“先做事件识别，再做物体存在性判断，最后再接细粒度分类”的原则。

## 第一阶段状态机

1. 从本地输入源读取帧。
2. 计算相邻帧之间的运动摘要。
3. 围绕最强交互窗口，选择一张稳定的 `before` 帧和一张稳定的 `after` 帧。
4. 在冰箱 ROI 内比较这两张关键帧。
5. 输出以下四类之一：
   - `no_change`
   - `put_in`
   - `take_out`
   - `partial_take_out_candidate`

## 启发式指标

- `changed_ratio`：超过像素阈值后的 ROI 变化占比
- `mean_delta`：变化区域内的平均有符号亮度差
- `peak_transition`：整段序列中最强的交互转折点
- `stable_frame_score`：前后关键帧稳定性评分，越低越好

## 当前阈值含义

- `changed_ratio` 很低时，判为 `no_change`。
- 在有效变化下，`mean_delta` 显著为负时，倾向判为 `put_in`。
- 在有效变化下，`mean_delta` 显著为正时，倾向判为 `take_out`。
- 当变化明显但符号证据不足时，判为 `partial_take_out_candidate`。

这些阈值目前故意保持简单，后续需要结合以下场景继续调参：

- 手部干扰
- 短时遮挡
- 整理食材但库存无变化
- 低照度场景

## 输入与输出

### 输入

- 第一阶段标准输入：本地视频文件
- 调试退路输入：本地帧目录

### 输出

- `data/sessions/<session_id>/before.jpg`
- `data/sessions/<session_id>/after.jpg`
- `data/sessions/<session_id>/diff.jpg`
- `data/sessions/<session_id>/event.json`

如果当前构建未启用 OpenCV，调试图像会回退为 `.pgm` 文件，但事件 JSON 协议保持不变。

## 已知 TODO

- TODO：在事件检测稳定后接入真实分类模型。
- TODO：将文件输入替换为实时摄像头视频流。
- TODO：补齐板端视频解码与运行时适配。
