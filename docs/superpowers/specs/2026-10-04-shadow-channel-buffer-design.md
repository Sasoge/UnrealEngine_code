# 光照与阴影分离 Buffer 第一阶段设计

状态：方案已确认；Task 1–3 已完成源码落地，Task 4 已加入可逆灰度调试入口；原生阴影与 Shadow Channel 已拆分为独立遮罩并统一支持盒体映射；完整 UE 编译和运行时截帧仍待验证

第一阶段纳入规则：启用光照与阴影分离时，参与直接光照的 Deferred 光源写入 `LightingBuffer`；其中投射阴影的光源同时写入 `ShadowBuffer`。Shadow Channel 光源在普通阴影贡献之外，继续应用 Shadow Channel 接收匹配、减法强度和 CurveFloat 权重。

为了先保证结果完整，第一阶段在分离功能开启时，会把需要写入这两张 buffer 的 Deferred 光源安排到标准非 clustered Deferred 路径，并排除 MegaLights。这样可以复用同一套 MRT 输出；后续再为 Clustered Deferred 和 MegaLights 增加双 buffer 输出能力。

## 背景

当前 Shadow Channel 在 `DeferredLightPixelShaders.usf` 中直接修改 `SceneColor`：当光照通道不匹配、Shadow Channel 匹配并且原生阴影可见度小于 1 时，计算减法阴影并从 HDR 场景颜色中扣除。UE 原生光源的阴影也会在各自的光照 pass 中直接调制该光源贡献。

这种方式可以完成当前的减法阴影，但后处理阶段只能看到混合后的最终颜色，无法把直接光照和阴影作为两种独立输入处理。为了逐步接近 Hi-Fi RUSH 的 Toon Post Process 架构，第一阶段增加两张屏幕空间 buffer：`LightingBuffer` 保存未被阴影可见度调制的直接光照贡献，`ShadowBuffer` 保存阴影遮挡贡献。

## 第一阶段目标

第一阶段只完成以下功能：

1. 为直接光照创建独立的 RDG `LightingBuffer`。
2. 为原生阴影和符合条件的 Shadow Channel 阴影创建独立的 RDG `ShadowBuffer`。
3. 在延迟光照 pass 中同时写入两张 buffer。
4. 保留当前 SceneColor 的减法结果，确保现有效果不被改变。
5. 将两张 texture 作为场景纹理输入传递到后处理和调试视图。
6. 增加调试显示，确认 buffer 的范围、接收者过滤、光源范围和多光源叠加是否正确。

第一阶段不实现盒体颜色映射，也不拆分环境光层或每盏灯的独立 buffer。所有直接光照先汇总到 `LightingBuffer`，所有参与卡通阴影映射的阴影先汇总到 `ShadowBuffer`。

## Buffer 语义

### LightingBuffer

`LightingBuffer` 保存当前光源在没有阴影遮挡时的直接光照贡献，包含材质响应、光源颜色和局部光源范围衰减，但不包含原生 Shadow Map/VSM 的可见度调制，也不包含 Shadow Channel-only 的减法。

```text
LightingContribution =
    EvaluateDirectLighting(
        ShadowVisibility = 1,
        LocalLightInfluence = native
    )
```

它使用 `PF_FloatRGBA`，保留 HDR 直接光照颜色。光照通道不匹配的 Shadow Channel-only 光源不写入 `LightingBuffer`，因为它不应该照亮该接收物体。

### ShadowBuffer

`ShadowBuffer` 保存阴影贡献，而不是最终颜色：

```text
ShadowContribution =
    (1 - ShadowVisibility)
    * LightInfluence
    * ReceiverFilter
    * ShadowWeight
```

其中：

- `ShadowVisibility` 来自 UE 原生 Shadow Map、VSM 或光追阴影；
- `LightInfluence` 使用原生光源范围、距离衰减或方向光有效范围，避免灯光范围外被错误压暗；
- `ReceiverFilter` 表示普通阴影或 Shadow Channel 接收规则是否允许写入；
- `ShadowWeight` 对普通阴影默认为 1，对 Shadow Channel 阴影使用减法强度和 CurveFloat 映射值。

`LightingBuffer` 使用 `PF_FloatRGBA`，`ShadowBuffer` 使用 `PF_R16F`。多个光源的阴影贡献可以叠加并超过 `1`，后续盒体映射阶段再决定是否 Clamp、分级或映射为颜色。两个 buffer 的 0 分别表示没有直接光照贡献和没有参与 Toon 映射的阴影贡献。

普通阴影按 UE 原生光源的接收规则写入。Shadow Channel 阴影仍然使用现有的 CustomDepth/Stencil 标记和三通道匹配逻辑：光照通道可以不匹配，但 Shadow Channel 必须匹配。投射者不需要写入这张 buffer，仍然通过 UE 原生 Shadow Map、VSM 或光追路径产生遮挡。

## 现有 `ToonShadow` 的关系

工程已经存在 `SceneTextures.ToonShadow`。它由 `IS_TOON_MAINLIGHT` pass 写入，格式为 `PF_R8G8B8A8`，目前编码的是：

```text
R = SurfaceShadow
G = TransmissionShadow
B = HairShadow
```

它的用途和新建的两张 buffer 不同，且只绑定到 Toon Main Light。第一阶段保留三者独立：`ToonShadow` 继续提供 Toon Main Light 的 Surface/Transmission/Hair 细节，`LightingBuffer` 提供直接光照，`ShadowBuffer` 提供盒体颜色映射需要的汇总阴影量。

## 渲染阶段设计

### 1. 创建 texture

在 deferred scene textures 初始化阶段创建 `LightingBuffer` 和 `ShadowBuffer` texture：

```text
Extent              = SceneTexturesConfig.Extent
Lighting format     = PF_FloatRGBA
Shadow format       = PF_R16F
Flags               = RenderTargetable | ShaderResource
Clear               = 0
Debug names         = LightingBuffer / ShadowBuffer
```

这两张 texture 只在 Deferred Shading Path 创建。移动路径和前向路径继续使用黑色 fallback。为了避免普通场景无条件增加 MRT，第一阶段由 view/cvar 级开关控制是否生成和写入。

### 2. 延迟光照 MRT 输出

需要写入两张 buffer 的光源在第一阶段统一安排到标准非 clustered Deferred 路径，因此可以在同一个光源 pass 中保留当前的 SceneColor 输出，并增加 LightingBuffer 与 ShadowBuffer MRT 输出。普通直接光照光源和 Shadow Channel 光源都使用这套路径；没有直接光照贡献的特殊路径不写入这两张 buffer。

Shader 需要增加一个独立的 `LIGHTING_SHADOW_BUFFER_OUTPUT` permutation：

```text
Target0 = SceneColor
TargetN = LightingContribution
TargetM = ShadowContribution
```

这里的 `N/M` 必须和现有 ToonShadow、Substrate 输出进行统一的 MRT slot 规划，不能直接假设 target 1/2 永远空闲。特别要处理一个光源同时是 Toon Main Light、Shadow Channel Light 和 Substrate 的情况。

当光照与阴影分离功能关闭时，所有光源都不绑定这两张 MRT。当功能开启时，参与直接光照的 Deferred 光源进入这套 permutation；投射阴影的光源额外写入 ShadowBuffer。

LightingBuffer output 写入未经过阴影可见度调制的直接光照。ShadowBuffer output 只写接收规则允许、原生阴影处于遮挡状态并且处于光源有效范围内的贡献。SceneColor 的现有直接光照和减法路径保持不变，作为第一阶段的行为基线。

### 3. 场景纹理和后处理输入

将 `LightingBuffer` 和 `ShadowBuffer` 加入 `FSceneTextures` 及其 uniform setup，分别提供黑色 fallback。后处理阶段可以通过场景纹理参数读取它们；第一阶段不改变现有后处理合成，只增加调试模式或临时可视化入口。

后续盒体 pass 将读取：

```text
SceneColor
SceneDepth / GBuffer
LightingBufferTexture
ShadowBufferTexture
```

并使用世界坐标到盒体局部坐标的变换采样颜色映射数据。

Task 4 提供了一个可逆的 final-output 调试入口：`r.LightingShadowBuffers.DebugView 1` 显示 `LightingBuffer` 灰度，`2` 显示 `ShadowBuffer` 灰度，`0` 关闭；`r.LightingShadowBuffers.DebugScale` 控制进入 saturate 映射前的缩放。该入口仅在 SM5 Deferred view 生效，读取同一组 `SceneTextureUniformParameters`，不会覆盖或修改累加纹理。累加纹理保持 primary scene extent，调试替换在 tone mapping 和 screen-percentage upscaling 之后写入最终 view-space 输出，以避免后续链路重复缩放。普通 `Vis LightingBuffer` / `Vis ShadowBuffer` 仍可用于现有 VisualizeTexture viewport 捕获和原始纹理检查。移动、前向、禁用功能时统一读取黑色 fallback。

## 调试验证

第一阶段需要验证以下情况：

- 没有参与直接光照的光源时，LightingBuffer 对应像素为黑。
- 普通直接光照可以写入 LightingBuffer。
- 普通阴影可以写入 ShadowBuffer。
- 光照通道不匹配但 Shadow Channel 匹配时，LightingBuffer 不写入，Shadow Channel 阴影仍然可以写入 ShadowBuffer。
- 光源范围外不会产生贡献。
- 没有勾选接收 Shadow Channel 的物体不会产生贡献。
- 多个 Shadow Channel 光源可以正确累加。
- 普通阴影和 Shadow Channel 阴影可以在同一像素累加。
- LightingBuffer 与 ShadowBuffer 在同一像素可以同时有贡献。
- 关闭 CustomDepth/Stencil 接收标记时不会误写 buffer。
- 当前 SceneColor 的减法结果与改动前保持一致。

## 性能边界

第一阶段会增加一张全分辨率 `PF_FloatRGBA` LightingBuffer 和一张 `PF_R16F` ShadowBuffer，并对需要写入它们的光源启用额外 MRT。光照与阴影分离开启时，直接光照光源会暂时退出 Clustered Deferred 和 MegaLights，因此这些光源的批处理收益会降低。

主要成本是每个参与光照的光源每次光照 pass 的两张额外 Render Target 写带宽，以及后处理读取两张纹理。暂时不增加新的 Shadow Map，也不重复执行阴影采样。

## 后续扩展

第一阶段完成后，再按以下顺序扩展：

1. 增加世界空间盒体组件和盒体局部坐标计算。
2. 用盒体范围和 3D 纹理分别映射 `LightingContribution` 与 `ShadowContribution` 的颜色。
3. 再评估 clustered deferred、Lumen、透明和前向路径的兼容方案。

## 主要涉及文件

预计第一阶段涉及：

```text
Engine/Source/Runtime/Renderer/Internal/SceneTextures.h
Engine/Source/Runtime/Renderer/Private/SceneTextures.cpp
Engine/Source/Runtime/Renderer/Private/LightRendering.cpp
Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessInputs.h
Engine/Shaders/Private/DeferredLightPixelShaders.usf
```

是否需要修改 `SceneTextureUniformParameters`、MRT slot 排布和后处理输入结构，需在实现前根据当前 ToonShadow/Substrate 的实际 permutation 进一步确认。

## 2026-10-07 盒体映射语义补充

盒体阶段现已采用双遮罩输入：`ShadowBuffer.R` 保存原生 UE 阴影遮挡，`LightingBuffer.A` 保存 Shadow Channel 减法量，`LightingBuffer.RGB` 仍保存未受阴影调制的直接光照。两种遮罩都进入同一个盒体权重；后处理使用 `Native + Channel * (1 - Native)` 计算联合遮挡，并对两种颜色来源做贡献加权，避免重复连乘。

因此，原生阴影跨过盒体边界时出现映射过渡是设计结果，不应再把这类边界变化与纹理池残留矩形混为一谈。纹理池残留、视口外旧像素和接触阴影逐帧抖动仍按本文前述清理及固定导出 Dither 的规则处理。
