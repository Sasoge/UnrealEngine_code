# Deferred Toon 直接光照漫反射/高光拆分设计

日期：2026-10-13

## 目标

将当前 `LightingBuffer` 中混合的直接漫反射和直接高光拆分为两个可独立消费的输入，使 `DirectLightMask` 只反映漫反射受光强度，不再因为材质 Roughness 或直接高光变化而改变。同时保留现有 `ShadowBuffer` 和 `LightingBuffer.a` 的阴影语义，避免重新引入时序残影和盒体边缘问题。

## 现状

Deferred Light Pass 当前将未受阴影影响的完整直接光照写入 `LightingBuffer.rgb`。非 Substrate 路径通过 `GetDynamicLighting` 返回漫反射与高光之和；Substrate 路径通过 `SubstrateDeferredLighting(...).SceneColor.rgb` 返回同样的混合结果。`LightingBuffer.a` 保存 Shadow Channel 减法量，`ShadowBuffer.r` 保存原生阴影量。

盒体 pass 当前从 `LightingBuffer.rgb` 的亮度生成 `DirectLightMask`，因此高光会改变这个 mask。

## 方案

### 资源和 MRT

- 保留现有 `LightingBuffer` 资源名和 RGB/A 语义位置，但将 RGB 改为未受阴影影响的直接漫反射。
- 新增 `SpecularBuffer`，格式使用与 `LightingBuffer` 一致的 `PF_FloatRGBA`，RGB 保存未受阴影影响的直接高光，Alpha 预留为 0。
- `ShadowBuffer` 保持 `PF_R16F` 和现有语义不变。
- Deferred Light MRT 增加一个目标，保持 SceneColor、LightingBuffer、ShadowBuffer 的既有槽位，新增 SpecularBuffer 后再放置 Toon/Substrate 输出，避免改变已有 ShadowBuffer 绑定。

### 光照写入

- 非 Substrate 路径使用 UE 已有的 `GetDynamicLightingSplit`，分别写入 `DiffuseLighting` 和 `SpecularLighting`。
- Substrate 路径使用 `FSubstrateDeferredLighting.TotalDiffuseLighting` 和 `TotalSpecularLighting`，不从最终 SceneColor 近似拆分。
- Hair direct-light buffer 路径使用同一拆分语义；大气透射、光照配置、coverage 和 pre-exposure 对两个输出使用相同的缩放。
- 原生阴影项、接触阴影项仍从 Lighting/Specular 两个光照输出中排除；它们继续由 ShadowBuffer 和原生 SceneColor 路径负责。
- 所有新增和修改的 UE 源码行使用 `[DeferredToon]` 或 `[LightingShadowBuffer]` 注释标记。

### SceneTextures 和 shader 输入

- `FSceneTextures` 新增 `SpecularBuffer`。
- `ESceneTexture` 和 `ESceneTextureSetupMode` 增加 SpecularBuffer 条目。
- `FSceneTextureUniformParameters` 增加 `SpecularBufferTexture`，并在可用/不可用路径分别绑定实际资源或黑色 fallback。
- Deferred Toon Volume pass 增加 `SpecularBufferTexture` 参数。
- 输入语义层增加 `SpecularDirectLight`，现有 `DirectLight` 语义改为读取漫反射 `LightingBuffer.rgb`；后续扩展仍通过语义 ID 和固定层表管理。

### 盒体和调试视图

- `DirectLightMask` 继续由 `LightingBuffer.rgb` 的亮度经过 Threshold/Softness 生成，此时它表示漫反射直接受光区域。
- 增加 `SpecularDirectLight` 输入，先只作为独立调试和后续风格化输入，不改变当前最终合成颜色。
- 保留 DebugView 14–17 的四个独立 mask。
- 增加 SpecularBuffer/SpecularDirectLight 调试视图，具体编号接在现有视图之后，并同步组件枚举、CVar 范围、帮助文本和测试契约。
- 增加源 RT 视图说明，避免把总直接光、漫反射、高光和阴影 mask 混为一谈。

## 数据流

```text
每个 Deferred Light
    ├─ unshadowed diffuse → LightingBuffer.rgb
    ├─ unshadowed specular → SpecularBuffer.rgb
    ├─ native occlusion → ShadowBuffer.r
    └─ Shadow Channel subtraction → LightingBuffer.a

Deferred Toon Volume
    ├─ LightingBuffer.rgb → DirectLightMask
    ├─ SpecularBuffer.rgb → SpecularDirectLight
    ├─ ShadowBuffer.r → NativeShadowMask
    └─ LightingBuffer.a → ShadowChannelMask
```

## 兼容性和风险

- `ShadowBuffer` 的格式、清理方式、采样方式和当前帧语义不变。
- 增加一个 RGB HDR MRT 会增加显存带宽和 Deferred Light 写入成本；功能开关 `r.Toon.LightingShadowBuffers` 继续控制全部自定义缓冲分配。
- 集群 Deferred 路径在自定义 Lighting/Shadow buffer 开启时保持现有禁用策略，避免遗漏 SpecularBuffer 写入。
- 如果目标平台的 MRT 数量不足，编译或运行时应沿用现有 feature-off fallback，所有新输入绑定黑色纹理。
- 直接光拆分只覆盖 Deferred Light 产生的直接光，不包含间接光、反射环境或后处理反射。

## 验证标准

1. 引擎增量编译成功，且没有新的 Shader/Pipeline Fatal。
2. 现有 71 项契约测试全部通过，并增加资源、MRT、输入语义和 DebugView 的检查。
3. `DebugView 14` 在改变地面 Roughness 时基本不再随高光变化。
4. Specular 调试视图会随 Roughness 和光源角度变化。
5. `DebugView 15–17` 与拆分前保持相同的阴影语义，移动相机不重新出现 ShadowBuffer 残影或矩形块。
6. `NewMap` 能正常启动，`r.Toon.LightingShadowBuffers 1` 和盒体调试视图可用。
