# Shadow Channel 原型记录

## 目标

当光源与接收物体使用不同的 Lighting Channel、但使用相同的 Shadow Channel 时：

- 光源不参与接收物体的直接照明；
- 接收物体仍读取该光源产生的原生阴影；
- 阴影区域使用独立的减法结果，供后续卡通颜色映射使用。

## 阶段结论（2026-10-04）

Shadow Channel 的第一阶段已经完成：光源和几何体现在可以分别设置 Lighting Channel 与 Shadow Channel。光照通道不匹配时，光源不会给物体提供普通直接光；Shadow Channel 匹配时，物体仍可读取该光源生成的 UE 原生阴影，并通过独立减法得到阴影结果。

本阶段已经覆盖光源属性、Primitive 接收属性、CustomDepth/Stencil 标记、Deferred Light Shader、减法强度、CurveFloat 映射和曲线编辑自动刷新。当前目标是验证“光照与阴影解耦”的中间结果，后续仍可在更高层将结果写入独立的阴影等级 Buffer，再交给卡通颜色映射。

## 当前数据流

1. `FDeferredLightUniformStruct` 上传光源的 `LightingChannelMask` 和 `ShadowReceiverChannelMask`。
2. 延迟光照像素着色器读取 GBuffer 的 `LightingChannels` 和 `CustomDepth/Stencil`。
3. 当光照通道不匹配、阴影接收通道匹配时，得到 `bShadowChannelOnly`。
4. `GetLightAttenuationFromShadow` 继续读取 UE 原生 VSM 阴影可见度。
5. 阴影通道专用路径将可见度转换为 `1 - saturate(LightAttenuation)`，直接减到光照输出中。

## LightingBuffer 与 ShadowBuffer（第一阶段）

第一阶段把延迟光照的两个中间结果分开保存，供后续盒体颜色映射读取：

- `LightingBuffer`（`PF_FloatRGBA`）的 RGB 保存未经过原生阴影可见度调制的 HDR 直接光照，A 单独保存 Shadow Channel 减法量。RGB 保留局部光源范围、距离衰减、材质响应、BRDF 和光源颜色；Lighting Channel 不匹配时 RGB 保持黑色。
- `ShadowBuffer`（`PF_R16F`）只保存 UE 原生阴影的正值遮挡：`(1 - ShadowVisibility) * LocalLightInfluence`。Shadow Channel-only 光源不会写入该通道，而是写入 `LightingBuffer.A`，其值包含接收匹配、减法强度和 CurveFloat 因子。多光源可以叠加并超过 `1`，盒体映射阶段再处理范围。

两张纹理在每个 view 的 Deferred Light 开始前清零，之后每个贡献光源使用 `ELoad` 加法 MRT。它们和已有 `ToonShadow` 分开：Toon Main Light 的 `ToonShadow` 仍使用 `R/G/B = Surface/Transmission/Hair` 编码。

启用 `r.Toon.LightingShadowBuffers 1` 时的 MRT 约定如下：

| Slot | 目标 |
| --- | --- |
| `Target0` | SceneColor（Hair pass 为 sample-lighting target） |
| `Target1` | LightingBuffer（`ELoad`、加法；RGB 直接光照，A Shadow Channel 减法） |
| `Target2` | ShadowBuffer（`ELoad`、加法） |
| `Target3` | ToonShadow；无 Toon Main Light 时为 Substrate opaque separated color |
| `Target4` | Toon 开启时为 Substrate opaque，否则为 Substrate subsurface separated color |
| `Target5` | Toon 开启时为 Substrate subsurface separated color |

功能关闭时保留原先 Toon/Substrate 的 target 排布，不创建或写入新目标。为保证当前参数能被标准 deferred shader 读取，启用功能的光源暂时退出 Clustered Deferred 和 MegaLights；移动、前向、功能关闭时 uniform 绑定黑色 fallback。

### 调试命令

`r.LightingShadowBuffers.DebugView` 是可逆的临时灰度视图，插在最终输出阶段，也就是 tone mapping 和屏幕百分比放大之后：

```text
r.Toon.LightingShadowBuffers 1
r.LightingShadowBuffers.DebugView 1   // LightingBuffer 灰度
r.LightingShadowBuffers.DebugView 2   // ShadowBuffer 灰度
r.LightingShadowBuffers.DebugView 0   // 关闭，恢复正常 SceneColor
r.LightingShadowBuffers.DebugScale 1  // 可选缩放，HDR 值默认 saturate 映射
```

该视图只对 SM5 Deferred view 生效，并不会修改两个累加纹理。累加纹理仍保持 primary scene extent，调试图在最终 view-space 输出中读取它们，避免被 TSR/屏幕百分比链路再次缩放。也可以使用现有 `VisualizeTexture` 视口工具检查资源（`Vis LightingBuffer` 或 `Vis ShadowBuffer`）；该命令适合查看原始纹理、版本和 viewport，灰度入口则用于快速确认贡献范围。盒体 pass 会同时读取 `ShadowBuffer.R` 和 `LightingBuffer.A`，两者使用同一个盒体权重，再按联合遮挡量进行颜色映射。

### 当前风险与限制

- 尚未完成 Unreal C++/Shader 编译和 RenderDoc 运行时验证；Substrate、Hair、六目标 MRT 的平台支持以及多视图 clear 仍需验证。
- 灰度 pass 是临时调试替换，强度通过 `DebugScale` 和 saturate 映射，不能用于判断最终 HDR 盒体映射的色彩范围。
- LightingBuffer/ShadowBuffer 只覆盖 Deferred 光源；移动、前向、透明和其他不参与该 MRT 的路径读取黑色 fallback。
- 启用分离功能会暂时放弃 Clustered Deferred/MegaLights 的批处理收益，直到这些路径具备相同的双 buffer 输出。

## 本次修改

文件：`Engine/Shaders/Private/DeferredLightPixelShaders.usf`

- Shadow Channel-only 光源不再进入普通 BRDF 的负光照路径，避免被点光源距离衰减、法线夹角和材质响应压缩到几乎不可见。
- 在 Substrate 和非 Substrate 两条延迟光照路径中，直接使用阴影可见度反值执行减法预览。
- 减法结果现在乘以原生局部光源范围衰减 `LightMask`；点光源、聚光灯和矩形灯范围之外不会再因为 ShadowMask 的默认值而被误减黑。
- 所有新增和修改代码均使用 `[ShadowChannelPrototype]` 注释标记。

## RenderDoc 截帧结论

截帧：`Project/test/Saved/RenderDocCaptures/2026.10.04-01.17.20_capture_3.rdc`

- 渲染路径是 D3D12 延迟渲染，点光源事件为 EID 5598。
- 点光源常量：`LightingChannelMask = 2`（Lighting Channel 1），`ShadowReceiverChannelMask = 1`（Shadow Channel 0）。
- CustomDepth 的 Stencil 已正确写入，平面像素为 1。
- `Shadow.Virtual.ShadowMask` 已包含球体产生的阴影，说明阴影生成和采样链路正常。
- 原实现的负值只有约 `-1.1e-5`，原因是它仍经过普通点光源 BRDF 和距离衰减；这次改为独立减法后应能看到明确压暗。

## 验证方式

测试场景：

- 点光源：Lighting Channel 1，Shadow Channel 0；
- 平面：Lighting Channel 0，Shadow Channel 0；
- 项目设置开启 Custom Depth-Stencil；
- 保留球体投射阴影。

预期结果：平面不接收点光源直接照明，但球体在平面上的阴影位置会产生独立的减法压暗。减法强度可以通过光源上的 `Shadow Channel Subtraction Strength` 调整，也可以使用 `CurveFloat` 按局部光源距离映射。

## 当前验证状态

Shader 源码修改已经写入，但当前运行中的编辑器日志只记录到了 `FPostProcessMaterialPS` 的编译，尚未看到 `DeferredLightPixelShaders.usf` 的重新编译。因此 RenderDoc 中的旧截帧还不能用来判断这次减法修正是否已经生效。

在编辑器控制台执行：

```text
recompileshaders changed
```

等待 Shader 编译完成后重新截帧。验证点是点光源的 Deferred Light 输出：球体投影区域应出现明显的负值或明显压暗；如果该阶段已有负值但最终画面没有压暗，再继续检查后续合成或色调映射阶段。

## 投射者与接收者

Primitive 的 `Shadow Channel Mask` 当前表示“是否接收 Shadow Channel 减法”。需要投射阴影的球体、树叶等几何体不需要勾选 Shadow Channel；它们仍会进入 VSM 阴影深度。只有希望被该减法结果影响的地面或其他接收物体才勾选对应通道。

为避免和 UE 原生 CustomDepth/Stencil 混淆，Shadow Channel 接收者写入 stencil 时会附加 bit 7 标记，Shader 只有看到该标记才读取低三位通道。

如果投射阴影的球体同时勾选了通道 0，它也会写入 Custom Stencil 并接收自己的 VSM 自遮挡，可能在球面上出现细的暗线。测试时应保持球体的 `Shadow Channel Mask` 为空，只在地面上勾选通道 0。

## 2026-10-04 黑线问题修复

### 现象

相机拉远后，球体表面出现细的黑线。检查编辑器属性后确认球体的 `Shadow Channel Mask` 为空，但球体仍可能开启普通 CustomDepth，并拥有普通 CustomStencil 值。

### 原因

此前 Shader 直接将 `CalcSceneCustomStencil()` 的低三位与光源的 `ShadowReceiverChannelMask` 比较。这样会把普通 CustomDepth 的 stencil 值（例如值 1）误判为 Shadow Channel 0，导致球体、天空球等普通 CustomDepth 几何体进入 Shadow Channel 减法路径。球体自身的 VSM 自遮挡因此被放大成黑线。

### 修复

- 在 `CustomDepthRendering.cpp` 和 `PrimitiveSceneProxy.cpp` 中，真正启用 Shadow Channel 的几何体写入 stencil 时附加 bit 7 标记 `0x80`。
- 在 `DeferredLightPixelShaders.usf` 和 `ClusteredDeferredShadingPixelShader.usf` 中，只有检测到 bit 7 后才读取低三位 Shadow Channel。
- 普通 CustomDepth/CustomStencil 保持原有行为，不再自动参与 Shadow Channel 减法。
- 保留原有 Shadow Channel 0/1/2 的三通道设计。

### 重新编译

该修复包含 C++ 和 Shader 两部分，必须先在 Rider 中重新编译 `UnrealEditor`，然后在编辑器控制台执行：

```text
recompileshaders changed
```

重新截帧时应确认：球体可以继续投射 VSM 阴影，只有明确勾选 Shadow Channel 的地面接收减法，普通 CustomDepth 不会再造成球面黑线。

## 当前暂缓问题：球面细黑线

重新测试后，球面上的细黑线仍然存在；提高平行光亮度后，黑线的视觉对比度明显降低，基本不可见。当前暂不继续修改渲染路径，保留现有实现。

后续如果需要处理，应单独确认该线来自 VSM 自遮挡/阴影偏差、阴影采样边界，还是场景光照对比度对减法结果的放大，再决定是否调整 Shadow Channel 逻辑。

## 减法导致的颜色异常

测试中观察到：平行光较弱时，阴影区域及其边界出现黑色、红色或其他异常色；提高平行光亮度后颜色明显正常。

这与当前减法路径的计算方式一致。Shadow Channel 减法发生在 HDR 场景颜色和曝光处理之前，当前预览强度固定为 `1.0`：

```text
SceneColor -= (1 - ShadowMask) * LightMask
```

当原始场景颜色小于减法值时，RGB 会出现负数。后续曝光、色调映射和颜色合成会将这些负值压暗或裁剪，并可能在边界产生色偏。提高平行光亮度只是提高了减法前的场景颜色基线，并没有改变 ShadowMask 本身。

后续若要稳定用于卡通颜色映射，应增加可调的减法强度，并考虑在进入最终颜色合成前将减法结果限制到合理范围，或直接输出独立的阴影等级，而不是长期依赖负 HDR 颜色作为中间结果。

## 2026-10-04 可调减法强度与 CurveFloat 映射

本次已加入两个光源参数，用于控制 Shadow Channel-only 光源的减法：

- `Shadow Channel Subtraction Strength`：独立的标量强度，默认值为 `1.0`。不需要曲线、只想调整整体压暗量时直接使用它；负值会在渲染代理中钳制为 `0`，大于 `1` 的值可用于风格化加强。
- `Shadow Channel Falloff Curve`：可选的 UE `CurveFloat` 资产。曲线输入是局部光源归一化距离，`0` 表示光源中心，`1` 表示光源半径边界；曲线输出作为减法乘数。未指定曲线时乘数恒为 `1`，仍使用 UE 原生的局部光源范围衰减。

实际减法公式变为：

```text
Subtraction = (1 - ShadowVisibility)
            * NativeLocalLightMask
            * ShadowChannelSubtractionStrength
            * CurveFloat(normalizedDistance)
```

`CurveFloat` 不会在每个像素中进行资产查找。`FLightSceneProxy` 创建时把曲线均匀采样为 16 个浮点值，按四个 `float4` 写入 `FDeferredLightUniformStruct`；Shader 中只做一次线性插值。因此每个光源增加 64 字节左右的常量数据和少量 ALU，不增加纹理采样。曲线输出小于 `0` 会被钳制为 `0`，大于 `1` 会保留。

为了保证曲线数据在延迟光照 Shader 中可用，启用 Shadow Channel 的光源会被安排到标准的非 clustered deferred 光照路径；这会牺牲这类光源的批处理收益，但不会改变普通光源的路径。MegaLights 也继续被排除，因为它没有接收者 stencil 输入。

本次涉及：

- `Engine/Source/Runtime/Engine/Classes/Components/PrimitiveComponent.h`
- `Engine/Source/Runtime/Engine/Private/Components/PrimitiveComponent.cpp`
- `Engine/Source/Runtime/Engine/Public/PrimitiveSceneProxy.h`
- `Engine/Source/Runtime/Engine/Private/PrimitiveSceneProxy.cpp`
- `Engine/Source/Runtime/Renderer/Private/CustomDepthRendering.cpp`
- `Engine/Source/Runtime/Renderer/Private/LightSceneInfo.cpp`
- `Engine/Source/Runtime/Engine/Classes/Components/LightComponent.h`
- `Engine/Source/Runtime/Engine/Private/Components/LightComponent.cpp`
- `Engine/Source/Runtime/Engine/Public/LightSceneProxy.h`
- `Engine/Source/Runtime/Engine/Private/LightSceneProxy.cpp`
- `Engine/Source/Runtime/Renderer/Private/LightRendering.h`
- `Engine/Source/Runtime/Renderer/Private/LightRendering.cpp`
- `Engine/Shaders/Private/DeferredLightPixelShaders.usf`
- `Engine/Shaders/Private/ClusteredDeferredShadingPixelShader.usf`

新增和修改处均保留 `[ShadowChannelPrototype]` 注释。修改后需要重新编译 `UnrealEditor`，再执行 `recompileshaders changed`。已分配曲线资产的控制点现在会通过 `UCurveBase::OnUpdateCurve` 自动触发光源 Render State 刷新并重新采样 16 点 LUT。

### 编译修正

首次编译时 MSVC 将 `uint8 bShadowChannelCurveEnabled` 直接返回为 `bool` 的隐式转换视为错误（C4800）。已改为显式比较 `bShadowChannelCurveEnabled != 0`，不改变运行时逻辑。

### CurveFloat 编辑时自动刷新

如果只修改已分配的 `CurveFloat` 控制点，之前光源代理中的 16 点 LUT 不会自动重建，所以必须保存曲线并重新赋值才能看到变化。现在 `ULightComponent` 会订阅 `UCurveBase::OnUpdateCurve`：

- 曲线被赋给光源、替换或清空时重新绑定监听；
- 曲线资产的控制点在编辑器中变化时调用 `MarkRenderStateDirty()`；
- 光源代理重建并重新采样曲线，新的 LUT 会立即上传到 Deferred Light uniform；
- 组件加载、注册和销毁时分别补绑、兜底绑定和解除监听。

因此编辑曲线时不需要再保存后重新赋值。该自动刷新逻辑只存在于编辑器构建，不会给打包运行时增加委托开销。

## 与 UE 原生 Lighting Channels 的异同

### 相同点

- 两者都使用 3 个通道，并在光源和 Primitive 上分别保存通道掩码。
- 两者都会在 Scene Proxy 中转换为紧凑的位掩码，随后上传或打包到渲染线程数据。
- 两者都在光照阶段判断光源掩码与物体掩码是否匹配。
- 两者都可以让同一个场景中的不同光源只影响指定物体集合。
- 两者都不会创建新的 Shadow Map。Shadow Channel 继续使用 UE 原生 VSM、Shadow Map 或其他当前启用的阴影生成路径。

### 不同点

| 对比项 | UE 原生 Lighting Channels | Shadow Channel 原型 |
|---|---|---|
| 主要用途 | 决定光源是否给物体计算普通直接光 | 在不接收普通直接光的情况下，决定物体是否接收该光源的阴影减法 |
| 接收数据 | 物体的 Lighting Channel 数据，通常来自 Lighting Channels Buffer/GBuffer | 物体的 Shadow Channel Mask 写入 CustomDepth/Stencil，并附加 `0x80` 标记；低 3 位保存通道 |
| 光源数据 | `FLightSceneProxy::LightingChannelMask` 与 `FDeferredLightUniformStruct::LightingChannelMask` | `FLightSceneProxy::ShadowChannelMask` 与 `FDeferredLightUniformStruct::ShadowReceiverChannelMask` |
| 不匹配时 | 直接光照路径跳过或将光源颜色置零 | 只有 Lighting 不匹配且 Shadow 匹配时进入 `bShadowChannelOnly`，读取原生阴影可见度并执行减法 |
| 阴影生成 | 原生 Lighting Channel 主要参与光照相关筛选，不改变本次原型的阴影结果生成 | 投射者仍正常进入阴影深度；接收者通过 CustomDepth/Stencil 决定是否接收减法 |
| 阴影结果 | 没有额外的减法结果 | `1 - ShadowVisibility`，再乘原生局部光源范围衰减、强度和曲线值 |
| 曲线控制 | 没有本次新增的 Shadow Channel 曲线 | 光源可选 `CurveFloat`，代理创建时采样 16 点并在 Shader 中插值 |
| 渲染路径 | 可参与 UE 原生的 clustered/tiled/Deferred 路径 | 启用 Shadow Channel 的光源被安排到标准非 clustered Deferred 路径，保证能读取曲线 LUT；普通光源不受影响 |
| MegaLights | 可按 UE 原生规则参与 | 当前排除 Shadow Channel 光源，因为 MegaLights 路径没有接收者 stencil 输入 |

### 需要特别区分的地方

`Shadow Channel Mask` 不是“投射阴影的物体分组”。它表示接收者是否允许某个 Shadow Channel 的减法结果影响自身。球体、树叶和花朵等投射者不需要勾选 Shadow Channel，只需要继续参与 UE 原生阴影深度；地面或其他接收物体才勾选对应的 Shadow Channel。

`Shadow Channel` 也不是第二张独立 Shadow Map。当前实现只增加了“是否接收”和“如何把原生阴影可见度转换为减法”的控制。阴影形状、软硬程度和遮挡关系仍由 UE 当前使用的 VSM、Shadow Map 或光追阴影路径决定。

普通 CustomDepth/Stencil 值不会自动成为 Shadow Channel。只有带有 `0x80` 标记的 stencil 值才会被 Shadow Channel Shader 读取，这个标记用于避免普通 CustomDepth 与 Shadow Channel 接收数据混淆。

## 当前边界与后续方向

- 当前减法直接作用于 HDR 场景颜色，强度过大时可能产生负值和边界色偏；卡通项目最终更适合输出独立的阴影等级或在颜色映射前进行受控 Clamp。
- 当前曲线采样使用归一化局部光源距离，方向光不使用距离曲线，曲线因子固定为 `1`。
- 曲线使用 16 点 LUT，适合实时调整和低开销运行；如果后续需要更高精度，可增加采样点或改用独立纹理，但会增加常量和采样成本。
- 当前没有按树叶、花朵等投射者来源分别生成 Shadow Mask；如果需要分级叠加，应另建 Shadow Mask 烘焙或独立投射组流程。
- 球面细黑线问题仍暂缓处理，提高平行光亮度后视觉对比度会降低；后续应单独检查 VSM 自遮挡、阴影偏差和减法结果的 HDR 范围。

## Clustered Deferred 计算流程记录

Clustered Deferred 不是提前烘焙最终光照，而是每帧建立一个局部光源索引网格。UE 会把屏幕 XY 和摄像机深度方向划分为 3D Cluster，然后通过 `LightGridInjectionCS` 判断每个点光源、聚光灯和矩形灯会影响哪些 Cluster，并把光源索引写入：

```text
NumCulledLightsGrid
CulledLightDataGrid
```

对应的主要代码和 Shader 是：

```text
Engine/Source/Runtime/Renderer/Private/LightGridInjection.cpp
Engine/Shaders/Private/LightGridInjection.usf
Engine/Shaders/Private/ClusteredDeferredShadingPixelShader.usf
```

像素阶段根据屏幕坐标和场景深度找到当前像素所属的 Cluster，然后遍历这个 Cluster 的局部光源列表：

```text
像素坐标和深度
→ 找到所属 Cluster
→ 读取该 Cluster 的光源索引
→ 读取光源参数
→ 计算距离衰减和材质光照
→ 采样 VSM/Shadow Map 阴影可见度
→ 累加直接光照
```

Shadow Map 或 VSM 的生成是独立流程，Clustered Deferred 只负责在像素阶段读取阴影结果并参与光照计算。方向光也通常走单独的 Deferred Light 路径，不依赖局部光源 Cluster。

当前 Shadow Channel 光源暂时关闭 Clustered Deferred，原因是新增的 `ShadowChannelSubtractionStrength`、`ShadowChannelCurveEnabled` 和 `ShadowChannelCurveSamples[4]` 位于 `FDeferredLightUniformStruct`，而 Clustered Deferred 使用的 `FLocalLightData` 还没有这些参数。为了确保减法强度和 CurveFloat LUT 一定生效，Shadow Channel 光源先走标准的非 clustered Deferred Light Pass。

这只是当前参数传递方案的限制，并不代表 Clustered Deferred 无法支持 Shadow Channel。后续如果需要重新接入，可以将减法强度和曲线数据加入 `FLocalLightData`，或者提供独立的 GPU Structured Buffer，让 `ClusteredDeferredShadingPixelShader.usf` 也能读取同一套 Shadow Channel 参数。
