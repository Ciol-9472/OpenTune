# OpenTune — 领域上下文

## 术语表

| 术语 | 含义 |
|------|------|
| **Clip** | 轨道上的音频片段，含 `PitchCurve`、`RenderCache`、`notes` |
| **PitchCurve** | RMVPE 原始 F0 + 用户修正段；COW 快照含 `renderGeneration` |
| **Clip 分块渲染** | 按 silent gap 切 chunk，mel + vocoder 合成后写入 `RenderCache` |
| **渲染失效** | 编辑提交后 `ClipRenderInvalidation::invalidateByClipId`，经 processor 的 `invalidateClipRender` 将 `renderGeneration` 同步到 chunk 的 `desiredRevision` |
| **desiredRevision** | `RenderCache` 分块目标版本，与 `PitchCurve` 的 `renderGeneration` 对齐（B1） |

## 架构接缝（编辑 → 播放）

1. UI：`PianoRollComponent` → `Listener::pitchCurveEdited(clipId, …)`（仅 Editor）
2. `ClipRenderInvalidation` → `OpenTuneAudioProcessor::enqueuePartialRender`
3. `ClipChunkRenderPipeline::runOneIteration`（C-借用 worker 线程）
4. `VocoderChunkSynthesizer::submitChunkSynthesis`（ONNX + 写 cache）
