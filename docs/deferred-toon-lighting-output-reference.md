# 盒体光照独立输出总览：来源、提取与合成

更新：2026-10-26。光照代码基线：`e5bbb06`（材质自发光拆分）；本文调试编号已更新为分组后的公开索引，见[完整索引与旧编号对照](deferred-toon-debug-view-indices.md)。本文记录当前实现，不把规划中的拆分或历史方案当成现有能力。

本文中的“原生结果”指**当前修改版引擎实际参与渲染的结果**，包含已有的 Toon Shading Model、Shadow Channel 等改动，不等于未经修改的 Epic 引擎。

## 1. 已经拆出了什么

可独立处理的主要颜色分量是：**有效直接漫反射、直接镜面高光、已接入的间接漫反射、环境镜面反射、材质自发光**。此外还保存了受光选区数据、阴影诊断量，以及两条间接漫反射遮蔽路径。

需要区分三类数据：

- **光照颜色**：有 RGB 能量，可在明确的合成规则下加回画面。
- **遮罩／可见度／受光响应**：用于选择和加工，不是另一份可以相加的光照。
- **有效性标记**：表示是否有受支持的数据；黑色光照不等于无数据。

| 部分 | 当前物理资源／通道 | 来源 Pass 与提取点 | 获取方式 | 调试视图 |
|---|---|---|---|---|
| 有效直接漫反射 | `EffectiveLightBuffer.rgb` | 延迟逐灯光照，`VisibleLighting.DiffuseLighting` | 复用原生遮挡后积分，MRT 逐灯加法累积 | 20 |
| 材质无关的有效受光响应 | `EffectiveLightBuffer.a` | 同一逐灯 Pass，`GetDeferredToonEffectiveResponse` | 额外评估单位白参考表面，取亮度并逐灯相加 | 21；22 为加工后的档位 |
| 未遮挡直接漫反射 | `LightingBuffer.rgb` | 同一逐灯 Pass，`UnshadowedLighting.DiffuseLighting` | 中和阴影输入后额外计算，MRT 加法累积 | 26 |
| 原生直接光遮挡诊断 | `ShadowBuffer.r` | 同一逐灯 Pass，`GetShadowTerms` | 导出遮挡量，逐灯累积 | 100 |
| 艺术减法阴影 | `LightingBuffer.a` | 同一逐灯 Pass，Shadow Channel 分支 | 接收通道、曲线、强度处理后的独立遮罩 | 101 |
| 直接镜面高光 | `SpecularBuffer.rgb` | 同一逐灯 Pass，`VisibleLighting.SpecularLighting` | 复用原生遮挡后镜面积分，MRT 加法累积 | 30；31/32 为风格化诊断 |
| 表面朝向诊断 | `SpecularBuffer.a` | 同一逐灯 Pass，`GetDeferredToonFacing` | 有效光源范围内的 `saturate(N·L)`，跨灯取最大值 | 27 |
| Lumen 间接漫反射 | `ToonIndirectDiffuse.rgb/a` | `DiffuseIndirectComposite`，`IndirectLighting.Diffuse` | 原生合成阶段写独立 UAV；A 为有效性 | 40/41 |
| 普通动态天光漫反射 | 同一张 `ToonIndirectDiffuse` | `ReflectionEnvironmentAndSky`，`SkyLightDiffuse` 的最终结果 | 另一条提供路径写同一接口；不含该 Pass 的镜面部分 | 40/41 |
| 普通天光 AO 前漫反射＋综合遮蔽 | `ToonSkyDiffuseBeforeAO.rgb/a` | `SkyLightDiffuse` 的可选输出，在 `ReflectionEnvironmentAndSky` 导出 | RGB 保存去局部 AO 的天光，A 保存综合可见度 | 60/61/62 |
| Lumen 短程 AO 前漫反射＋短程遮蔽 | `ToonLumenDiffuseBeforeShortAO.rgb/a` | `DiffuseIndirectComposite` 的短程 AO 应用位置 | RGB 在乘 AO 前保存，A 解码原生 `ShortRangeAOTexture` | 70/71/72 |
| 环境镜面反射 | `ToonIndirectSpecular.rgb/a` | Lumen 合成中的 `IndirectLighting.Specular`，或 `ReflectionEnvironmentAndSky` 中的 `ReflectionEnvironment(...)` | 按实际渲染路径导出最终镜面分量，写 UAV | 80/81 |
| 材质自发光 | `ToonEmissive.rgb/a` | Base Pass，`GetMaterialEmissive(PixelMaterialInputs)` | 附加 MRT，继承原生深度／遮罩覆盖；A 为有效性 | 90/91 |
| 自动综合遮蔽与二分 AO | 复用上述 AO 输入；另有自定义滤波／历史纹理 | `DeferredToon AO Stage ...`、`DeferredToonVolume` | 选择有效提供方，再滤波、时域处理和二分 | 50～54、42 |

上表的资源均指 `FSceneTextures` 中的自定义资源，最后一行除外。`ShadowBuffer`、`EffectiveLightBuffer` 等名称不是 UE 原本就提供的标准光照分层。

## 2. 关键源码入口与时序

源码路径以引擎仓库根目录为起点；函数名比行号更适合后续定位。

| 职责 | 源码入口 |
|---|---|
| 直接光拆分、参考表面、阴影与朝向输出 | `Engine/Shaders/Private/DeferredLightPixelShaders.usf` |
| 原生直接漫反射／镜面积分拆分 | `Engine/Shaders/Private/DeferredLightingCommon.ush::GetDynamicLightingSplit` |
| 直接光 MRT 绑定、混合、标准／Simple／Hair 路径 | `Engine/Source/Runtime/Renderer/Private/LightRendering.cpp`，`BindLightingShadowBufferMRT`、`InternalRenderLight` 等 |
| 直接光缓冲的帧内清理 | `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp` 中的 LightingShadowBuffers 清理分支 |
| Lumen 与天光／反射导出资源及 Pass 调度 | `Engine/Source/Runtime/Renderer/Private/IndirectLightRendering.cpp`，`RenderDiffuseIndirectAndAmbientOcclusion`、`RenderDeferredReflectionsAndSkyLighting` |
| Lumen 漫反射、短程 AO、环境反射提取 | `Engine/Shaders/Private/DiffuseIndirectComposite.usf` |
| 普通天光和环境镜面输出 | `Engine/Shaders/Private/ReflectionEnvironmentPixelShader.usf` |
| 天光遮蔽合并与 AO 前照明求值 | `Engine/Shaders/Private/SkyLightingDiffuseShared.ush::GetSkyLightVisibilityData`、`SkyLightDiffuse` |
| 自发光提取 | `Engine/Shaders/Private/BasePassPixelShader.usf::FPixelShaderInOut_MainPS` |
| 自发光 MRT 布局和编译输出声明 | `Engine/Source/Runtime/RenderCore/Private/GBufferInfo.cpp::GetToonEmissiveBinding`；`Engine/Source/Runtime/Engine/Private/ShaderCompiler/ShaderGenerationUtil.cpp::ApplyDerivedDefines` |
| 资源定义、创建及场景纹理绑定 | `Engine/Source/Runtime/Renderer/Internal/SceneTextures.h`；`Engine/Source/Runtime/Renderer/Private/SceneTextures.cpp` |
| 盒体读取、AO Pass 调度和历史维护 | `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessDeferredToonVolumes.cpp` |
| AO 空间／时域处理 | `Engine/Shaders/Private/DeferredToonAOFilter.usf` |
| 盒体分档、调色、分量扣除与加回 | `Engine/Shaders/Private/PostProcessDeferredToonVolumes.usf::MainPS` |
| 盒体插入后处理的位置 | `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp::AddDeferredToonVolumePass` 调用点 |

提取发生在各自负责的阶段，而不是等最终画面完成后再按颜色猜测：

```text
Base Pass 的材质求值 ──────────────→ 自发光 MRT
延迟逐灯积分 ────────────────────→ 直接漫反射／高光／参考响应／阴影诊断
Lumen 间接合成 ──────────────────→ 间接漫反射／短程 AO 前后数据／环境反射
普通天光与反射合成 ───────────────→ 天光漫反射／天光 AO 前后数据／环境反射
                                         ↓
                              盒体采样和独立加工
                                         ↓
                              后续 TAA/TSR、Bloom、曝光与色调映射
```

这是数据依赖图，不保证所有平台上 Lumen、直接光和天光 Pass 的调度顺序完全一致。RenderDoc 中可搜索 `Light::StandardDeferred`、`DiffuseIndirectComposite(...)`、`ReflectionEnvironmentAndSky(...)`、`DeferredToon AO Stage`、`DeferredToonVolume`；实际事件名可能带尺寸、材质类型或灯名后缀。

## 3. 直接漫反射、选区与阴影

### 3.1 有效直接漫反射：颜色输入

普通 GBuffer 路径复用 `GetDynamicLightingSplit` 的 `VisibleLighting`，把 `DiffuseLighting.rgb` 写入 `EffectiveLightContribution.rgb`，对应逐灯 MRT Target4。所有灯的结果加法累积到 `EffectiveLightBuffer`。

它保留实际材质响应、法线、灯光颜色与强度、光源形状与距离衰减、原生遮挡，以及当前引擎的光照／阴影接收通道规则。有效漫反射不再包含被分开的直接镜面高光。

它是**遮挡后结果**：多光源已经先各自计算再相加。因此某个弱点光源被挡住，不会自动把方向光已经照亮的位置判成全暗。不能再给这份 RGB 乘一次 `ShadowBuffer`。

已烘焙进 Lightmap 等路径的贡献不会被追溯还原为这里的某盏灯。直接光拆分有 Substrate、Hair 分支，但不表示整个盒体、AO、自发光及反射接口都完整支持这些材质。

### 3.2 有效受光响应：分档输入

`GetDeferredToonEffectiveResponse` 保留真实表面法线、位置与灯光输入，将参考材质固定为 Default Lit、白色漫反射、非金属、粗糙度 1、镜面为 0；同时关闭参考计算中的自定义 Toon BRDF 控制。额外调用原生积分，取漫反射亮度，累积到 `EffectiveLightBuffer.a`。

因此它不依赖真实材质的底色、金属度、粗糙度，也不通过除以底色恢复光照；**仍依赖法线、灯光强度／颜色、衰减、光源形状和遮挡**。它不是纯 `N·L`，不是严格物理照度单位，也不保证落在 0～1。原生材质响应复用已有计算，参考表面计算则有额外成本。

盒体以去预曝光后的 A 做二分或多档选择。21 的显示采用压缩映射，不能把截图灰度直接当作原始受光数值；22 是选择后的档位值，也不是新光照层。23、24、25 是二分参考／派生遮罩，不能代替当前多档颜色结果。

### 3.3 未遮挡直接漫反射与纯朝向

`LightingBuffer.rgb` 在额外积分前把预计算阴影、传入阴影衰减及接触阴影输入中和，但仍保留材质、光源和原生积分的其他输入。它是当前定义下的“未遮挡直接漫反射”诊断量，**不是完全无材质、无 AO、无衰减的照度**，也不是当前明暗分档的主输入。

`SpecularBuffer.a` 单独保存朝向诊断：灯光通道匹配且处于灯的影响范围时计算 `saturate(N·L)`，跨灯用 MAX 混合。RGB 高光仍用加法混合。这个 A 不保存光强，也不保存阴影可见度；不能把最大朝向与所有灯的阴影并集拼成正确的多灯受光。

### 3.4 原生遮挡与艺术 Shadow Channel

- `ShadowBuffer.r`：`GetShadowTerms` 得到表面可见度后转成遮挡量，结合灯光影响范围；接收阴影通道不匹配或属于专用减法灯时不写普通遮挡。为减少诊断图抖动，这条导出关闭接触阴影并使用固定 dither；实际有效光仍走自己的原生可见性计算。
- `LightingBuffer.a`：只承载专用 Shadow Channel 的艺术减法量，包含接收通道过滤、阴影曲线和减法强度。它不随预曝光缩放，也不代表一份额外 RGB 光照。

普通遮挡逐灯累积，没有保留逐灯能量配对，可能超过 1；预览时会饱和。它适合诊断，不能作为所有灯最终照明的统一乘数。艺术减法层则按当前盒体规则覆盖明暗选择，与物理受光分开解释。

### 3.5 直接光路径与资源开关

`r.Toon.LightingShadowBuffers` 控制四张直接光缓冲的分配与写入，关闭或不受支持时使用黑色回退。启用时标准延迟光、Simple Lights 和 Hair 对应路径绑定这些输出；灯光调度会避开相关 Clustered／MegaLights 路径，使贡献走已接入的导出路径。这不仅是增加几次纹理写入，也可能改变光照路径的性能特征。

四张纹理在照明前清零，再逐灯累积，**没有我们额外添加的跨帧直接光历史**。完整 extent／ViewRect 清理不可省略，否则可能重新出现视口缩放后的残留。

## 4. 直接镜面高光

`SpecularBuffer.rgb` 对应逐灯 MRT Target3，取自有效遮挡后的 `VisibleLighting.SpecularLighting.rgb`。它已经包含材质镜面响应、F0／菲涅尔、粗糙度、灯光颜色强度和遮挡。

这层代表直接光源产生的镜面高光，不包含镜子里的环境倒影、SSR 或 Lumen 场景反射。使用时不能再次乘 F0 或材质底色。

当前硬边单档风格化在盒体中对去预曝光的有效高光亮度做阈值选择，颜色可保留原生 RGB 比例并染色，也可使用固定替换色。材质中的细碎法线、粗糙度等变化已经进入原生高光，二分会把阈值附近的细节放大成颗粒；本阶段没有自动重建风格化法线。

30 显示原生直接高光，31 显示硬边选区，32 显示加工后的高光。只有启用硬边模式时，它才从“其他光照”中独立扣出并加回；选择原生模式时仍按剩余贡献的原规则保留。

## 5. 间接漫反射：两条提供路径

### 5.1 Lumen

CPU 在 `RenderDiffuseIndirectAndAmbientOcclusion` 中，为正常 Lumen 合成、有效输入纹理、传统 GBuffer 且存在盒体的视图开启导出。`FDiffuseIndirectCompositePS` 在 `DiffuseIndirectComposite.usf` 中将 `IndirectLighting.Diffuse` 写入 `ToonIndirectDiffuseOutput`。

位置在原生材质响应、遮蔽和已有 Toon Lumen 加工之后，加入 SceneColor 之前。RGB 已包含材质颜色、环境补光／反弹及 GI 内部的几何遮挡，不是原始入射光场。Lumen 内的天空贡献与其他 GI 在这里已合并，**没有进一步拆成“天空一张、反弹一张”**。

### 5.2 非 Lumen 普通动态天光

`FReflectionEnvironmentSkyLightingPS` 的 `ReflectionEnvironmentAndSky` Pass 调用 `SkyLightDiffuse`，把实际天光漫反射结果写入同一张 `ToonIndirectDiffuse`。提取位于加入 `ReflectionEnvironment(...)` 镜面部分之前，保留云遮蔽等该路径实际使用的因素。

所以 GI 方法为 None 时，该 RT 仍可能有动态 SkyLight 漫反射。启用 Lumen 后通常由 Lumen 提供接口，普通动态天光导出不重复执行；不要把两条路径的结果再加一次。

### 5.3 共同边界

RGB 是预曝光颜色，A=1 表示有效，A=0 表示未提供。需要后续独立散射的 SSS 被排除；材质有效性和各分支检查以代码为准，不能仅因为 RT 已分配就判断每个像素可用。

目前不完整覆盖独立 SSGI、Lightmap、Volumetric Lightmap、Indirect Lighting Cache、Ambient Cubemap 等来源。即便这些效果在最终画面中存在，也不能宣称它们都在视图 40 中。透明／水等专用路径也不是这个接口的完整承诺。

这里没有自定义跨帧累计；读取的 Lumen 结果本身可能已经经过其原生空间／时间处理。之后的盒体 AO 历史是另一套处理。

## 6. 间接漫反射遮蔽

### 6.1 普通天光综合遮蔽

在 `SkyLightingDiffuseShared.ush::SkyLightDiffuse` 中追加 `BeforeAO`、`Visibility` 输出：

- `Visibility = saturate(SkyVisData.SkyDiffuseLookUpMul)`。其来源包含该天光路径使用的 Bent Normal／DFAO 控制、材质 AO、屏幕 AO 的合并，以及方向几何因子；具体合并使用原生 min 或乘法模式。
- `BeforeAO` 使用 AO 修改方向前的天光法线重新求 SH 漫反射，保留材质、能量守恒、天光颜色与预曝光，去掉这里的局部遮蔽和 Occlusion Tint。
- 在 `ReflectionEnvironmentPixelShader.usf` 导出时，AO 前后都保留云遮蔽。

结果写入 `ToonSkyDiffuseBeforeAO`：RGB=AO 前漫反射，A=综合可见度；A=-1 表示不支持，A=0 是有效的完全遮挡。原始最终 RGB 仍在 `ToonIndirectDiffuse`。

它不是纯 SSAO，也不是纯 DFAO，更不是“整个 UE 的所有 AO”。改变采样方向、颜色补偿等会改变 RGB，所以不能简单用最终 RGB 除以这个 A 来恢复无 AO 照明。天空、Unlit、Hair、特殊 SSS、自定义 Toon 禁用天光等分支不提供完整的这组拆分。

### 6.2 Lumen 短程 AO

在 `DiffuseIndirectComposite.usf` 真正应用短程 AO 的位置，先保存 `ToonDiffuseBeforeShortAO`，再保留原生应用遮蔽后的最终漫反射。

可见度来自原生 `ShortRangeAOTexture`：标量模式直接读取；Bent Normal 模式先解码，再取向量长度。原生实际 RGB 遮蔽通过 `DistantIlluminationRescale` 等进行材质相关的多次反弹补偿，**导出的标量可见度并不等于最终 RGB 的逐通道乘数**。

`ToonLumenDiffuseBeforeShortAO` 的 RGB 保存短程 AO 前照明，A 保存可见度，A=-1 表示不可用。短程 AO 关闭、提前在积分阶段应用而没有可分离输入，或者材质／路径不受支持时，不伪造有效数据。

移除这层只移除该处独立短程遮蔽，仍保留 Lumen 传播／追踪形成的大范围 GI 暗部、材质响应及已有 Toon GI 处理。这里没有完整拆出 Lumen 材质 AO，也没有拆出反射遮蔽。

### 6.3 自动汇总、二分与历史

`PostProcessDeferredToonVolumes.usf::ResolveDeferredToonOcclusion` 逐像素选择：有效 Lumen 短程 AO 优先，否则选择有效普通天光综合遮蔽，否则无提供方。它复用已有纹理，**不是再创建一张包含所有 AO 的原生总缓冲，也不是把两路 AO 相乘**。

| 视图 | 含义 |
|---|---|
| 50 | 自动选择的原始可见度；不可用时也可能显示中性白，应同时看 53 |
| 52 | 选中提供方的 AO 前漫反射 |
| 53 | 提供方数据有效性 |
| 54 | 青=Lumen，橙=普通天光，黑=无数据 |
| 42 | 整层间接光的二分选区，不是 AO 二分 |
| 51 | 经自定义处理后的 AO 二分遮罩，白为无遮挡 |

`DeferredToonAOFilter.usf` 与 C++ 调度负责空间过滤、同表面检查、连续可见度历史、重投影、二分滞回及暗部连续确认。原始 50 不会被这些历史处理改写；51 才是加工后的结果。处理使用自己的临时和历史纹理，不修改 UE 原生 AO 算法。

当前二分 AO 的合成顺序是：**恢复所保存的 AO 前漫反射 → 间接光调色／分档 → 应用二分 AO → 与其他层组合**。不能在已受原生 AO 影响的颜色上再乘一次二分 AO，也不能先乘完 AO 再用固定色把暗部填回去。它只调整间接漫反射，不压暗直接光、镜面反射或自发光。

历史、确认延迟、细线删除和运动边界仍有取舍，详细优化过程见 [AO 降噪记录](deferred-toon-ao-edge-filter.md)。关闭间接独立调色、只使用 AO 调整时，这层间接光仍受“保留其他光照”控制。

### 6.4 屏幕 AO 和材质 AO 的位置

视图 55 直接读取原生 `SceneTexturesStruct.ScreenSpaceAOTexture`，没有为它新建另一张独立 AO RT。它不是 Lumen 短程 AO 的同义词，Lumen 路径下可能没有这张屏幕 AO 的有效信息。

材质 AO 是 GBuffer／原生材质输入的一部分，目前没有一套独立的材质 AO RGB 光照层。它是否进入某张综合遮蔽，应按提供路径判断：普通天光合并逻辑会使用 `GBufferAO`，Lumen 短程 AO 导出的 A 则取自 `ShortRangeAOTexture`，不能称作完整材质 AO 汇总。

## 7. 环境镜面反射

两条导出位置都保留原生材质／F0、粗糙度、遮蔽和反射颜色：

1. **Lumen GI 合成路径**：在 `DiffuseIndirectComposite.usf` 中，`CombineRoughSpecular` 等完成粗糙／追踪反射合成、SSS 棋盘处理及自定义 Toon 屏蔽后，取 `IndirectLighting.Specular`。
2. **普通反射环境或独立反射路径**：在 `ReflectionEnvironmentPixelShader.usf` 中取 `ReflectionEnvironment(...)` 返回的 `NativeIndirectSpecular`。它可包含当前实际使用的 SSR、Reflection Capture、天空镜面和独立 Lumen 反射输入。

分别写入 `ToonIndirectSpecular`，由实际渲染路径选择提供方。普通反射导出不要求动态 SkyLight 存在，所以移除天光后仍可有 SSR。这里没有进一步把 SSR、反射捕获、粗糙反射等分别导出；它们仍是一份最终环境镜面贡献。

RGB 是预曝光颜色，A 是有效性。当前自定义 `SHADINGMODELID_TOON` 的 Lumen 镜面屏蔽保留在导出之前：可以出现“80 黑、81 白”，表示有效的零贡献，而不是导出丢失。独立强度开关不能恢复原生材质路径主动屏蔽的反射。

Hair、需独立散射的 SSS、SingleLayerWater、透明和 Substrate 不作为这层完整支持范围。水面倒影不能仅凭 80 判断是否已完整拆出。

## 8. 材质自发光

Base Pass 直接从 `GetMaterialEmissive(PixelMaterialInputs)` 保存材质 Emissive，乘当前视图预曝光并按输出范围限制后，写入附加 MRT `ToonEmissive`。**不是把 Base Pass 的整个 SceneColor 复制出来**，因为其中也可能有烘焙／间接光等贡献。

MRT 写入继承实际深度测试、Opacity Mask 和材质求值，支持已验证的传统延迟 Lit、Unlit、Masked 以及 Nanite 场景。RGB 是非负预曝光自发光，A=1 有效；有效但不发光的材质仍是 `(0,0,0,1)`。Unlit 没有有效法线时，开启独立自发光可凭有效导出参与盒体，并跳过直接漫反射色板替换。

透明、SingleLayerWater、天空、Hair、独立 SSS、Substrate、移动和 Forward 未作为这次完整支持范围；后续自发光 Decal、透明粒子及负 Emissive 特殊用法也不等于已完整分离。水体另有 MRT 用途，绑定／PSO 排除自发光槽，消费侧也拒绝水面覆盖后的旧像素。

自发光强度仅调整当前可见表面的贡献，不改材质表达式、Lumen Surface Cache 或反射场景，因此调到 0 不表示该物体停止照亮别处或从倒影中消失。Bloom 尚未独立拆层，后续仍可能对加回的发光产生泛光。

后续改动 MRT 位置时，必须同步编译器输出声明／格式、运行时绑定、清理、PSO 和 Nanite 输出，并更新相应 DDC 版本。当前 `TOON_EMISSIVE_MRT_INDEX` 只在 Base Pass Shader 环境设置；不要在共享材质编译环境放一个禁用默认值，否则后合并的共享环境会覆盖有效槽号，导致“资源已产生但全黑”。

## 9. 数据空间、有效性与成本

| 数据 | 资源格式 | 曝光契约 | 有效性／累积 |
|---|---|---|---|
| `EffectiveLightBuffer` | RGBA16F | **RGBA 都有预曝光** | RGBA 逐灯加法；A 不是有效性 |
| `LightingBuffer` | RGBA16F | RGB 有预曝光，A 无 | RGB／艺术遮罩累积 |
| `ShadowBuffer` | R16F | 无曝光 | 遮挡量累积，预览通常饱和 |
| `SpecularBuffer` | RGBA16F | RGB 有预曝光，A 无 | RGB 加法，A 最大朝向 |
| `ToonIndirectDiffuse`、`ToonIndirectSpecular`、`ToonEmissive` | RGBA16F | RGB 有预曝光，A 无 | A=1 有效，A=0 缺失 |
| `ToonSkyDiffuseBeforeAO`、`ToonLumenDiffuseBeforeShortAO` | RGBA16F | RGB 有预曝光，A 无 | A 为可见度；-1 缺失，0 仍有效 |

盒体进行阈值、调色等场景线性计算时对对应 RGB／有效受光量乘 `OneOverPreExposure`，加回 SceneColor 时恢复预曝光。不能对所有纹理的 A 一律做曝光转换。截图还经过后续显示处理，不能直接当作 RT 数值读回。

所有基础分量按帧生成；同一帧的盒体共享基础 RT，不是每个盒体复制一整套。AO 的按视图／活跃盒体维护的历史另算。

基础导出跟随主渲染缓冲的 extent，消费时使用对应 ViewRect／缓冲 UV；这不一定等于最终窗口分辨率。屏幕比例、TSR、编辑器多视口和窗口缩放时，不可直接拿最终显示像素坐标采样，也不可只检查资源是否非空而跳过是否已写入的判断。

- 直接光四张纹理由直接光缓冲开关控制。
- 间接漫反射、环境镜面和天光 AO 前纹理在存在盒体消费者、受支持路径下分配并清理；短程 AO 前纹理还要求有对应 Lumen 输入。目前天光 AO 前纹理可能在 Lumen 场景中分配但无有效像素，不可把“有资源”当成“有数据”。
- 自发光采用固定 Base Pass 布局，受支持配置中即使没有启用盒体也会分配／写入，不能宣称零额外成本。
- 单张全分辨率 RGBA16F 在 1920×1080 约 15.8 MiB，R16F 约 4.0 MiB；还有清理、写入和读取带宽。AO 历史另有成本，未做完整 GPU 时间基准。

MRT 槽位限制针对**同一次绘制的颜色输出**，不是整个工程只能有 8 张 RT。Base Pass 的自发光、逐灯直接光输出，以及间接合成的 UAV 导出属于不同 Pass。自发光在最复杂原生七槽布局后追加至八槽；SM5 开启虚拟纹理时还预留像素 UAV 反馈槽，槽不足则不启用该导出。

## 10. 当前最终合成顺序

以下描述 `DirectLightGrade`，其他输出模式不能套用同一控制语义。基础 RT 的获取来自上述原生计算；**SceneColor 残差仅用于限制可扣除的预算，不用于猜测某层的来源**。

都在预曝光颜色空间，按每个颜色通道计算：

```text
O  = 进入盒体系统前的 OriginalSceneColor
D  = 有效直接漫反射
R0 = max(O - D, 0)

E  = 自发光独立开启且有效 ? min(EmissiveRT, R0) : 0
R1 = R0 - E
S  = 直接高光硬边模式且可用 ? min(DirectSpecularRT, R1) : 0
R2 = R1 - S
F  = 环境镜面独立开启且有效 ? min(IndirectSpecularRT, R2) : 0
R3 = R2 - F
I  = 需要处理且有效的间接漫反射，限制在 R3 的可扣除预算内

目标色 = 加工后的直接漫反射
       + E * 自发光强度
       + 风格化直接高光（启用硬边模式时）
       + F * 环境反射强度
       + 加工后的间接漫反射（包含需要的 AO 处理）
       + 剩余未拆分贡献 * 保留其他光照

最终像素 = 按盒体空间范围、效果强度等权重混合当前画面和目标色
```

公式省略每步的非负钳制和无输入回退；实际代码位于 `PostProcessDeferredToonVolumes.usf` 的 `OutputMode == 4u` 分支。只启用 AO、没有启用间接独立调色时，间接层仍随 Other Lighting Weight 缩放。已独立启用的自发光、环境镜面、硬边直接高光和独立间接漫反射不再受这一剩余权重控制。

AO 前 RGB 用来重建这一份间接光，不作为额外光照再加一次。所谓中性设置也要按功能区分：光照独立控制的强度 1 可保留原贡献；AO 二分或直接高光硬边模式本身就是艺术改造，不能视为中性操作。

导出与盒体合成之间还可能发生雾、散射、透明等处理，故目前的分解不是对任意最终像素都严格可逆。预算限制避免负残差和重复扣除；若后续要精确控制雾／透明，应在其真实合成位置增加接口，不能靠继续减亮度猜测。

## 11. 尚未完整拆分的部分

| 部分 | 当前状态 |
|---|---|
| Lumen 内天空贡献、各次反弹、GI 传播中的宽范围遮挡 | 仍混合在 Lumen 间接光结果中；短程 AO 不能代替它们 |
| 材质 AO、DFAO、屏幕 AO 各自完整独立控制 | 有原生输入或局部综合入口，但未全部拆成独立可重组接口 |
| 反射遮蔽、SSR／Reflection Capture／Lumen 反射各来源 | 保留在环境镜面最终结果中，未进一步分开 |
| SSGI、Lightmap、VLM、ILC、Ambient Cubemap | 不属于目前间接漫反射接口的完整覆盖范围 |
| 雾、体积光、云等最终合成 | 未做通用独立分层；已包含于某输入的云遮蔽仍保留 |
| 透明、折射、SingleLayerWater、Hair、独立 SSS、Substrate | 各导出支持范围不同，不能宣称全链路完整拆分 |
| Bloom、曝光、色调映射和颜色分级 | 后续显示／后处理阶段，未拆成独立光照输入 |
| 后续 Emissive Decal、透明粒子发光 | 不在当前 Base Pass 材质自发光完整范围内 |

“保留其他光照”是未单独控制内容的剩余预算，不是一张命名准确、成分固定的原生光照层。其实际内容随启用的独立功能和渲染路径变化。

## 12. 排查与后续维护

1. 先确定实际提供路径：Lumen 还是普通天光；使用 41、62、72、53、54、81、91 检查有效性，不只看 RGB。
2. 在 RenderDoc 中定位真实 producer Pass，核对其 UAV／MRT、资源格式、ViewRect、清理和曝光空间，再检查盒体消费。
3. 直接光先看 20／21，镜面分别看 30 和 80，自发光看 90；不要把镜面倒影归入间接漫反射，也不要把材质亮部当成受光选区。
4. 原始 AO 看 61／71／50，加工后看 51。时域稳定问题同时检查重投影接受率、提供方切换与历史重置，不只加大模糊。
5. 新加分量需明确：来源、格式、有效性哨兵、曝光、无输入回退、支持材质、是否需要历史、独立参数默认值，以及从剩余预算中何时扣除。
6. 保持已发布的公开索引和内部序列化 ID 稳定；两者不同，参见索引表。面板参数和提示同步中英文。不要因优化重新混合已拆出的漫反射、镜面或自发光。

### 相关专题与验证边界

- [漫反射映射设计](deferred-toon-diffuse-mapping-design.md)
- [直接光高光](deferred-toon-direct-specular.md)
- [间接漫反射](deferred-toon-indirect-diffuse.md)
- [天光综合遮蔽](deferred-toon-sky-occlusion.md)、[Lumen 短程 AO](deferred-toon-lumen-short-range-ao.md)
- [统一遮蔽接口](deferred-toon-unified-occlusion.md)、[AO 二分](deferred-toon-ao-two-tone.md)、[逐轮降噪记录](deferred-toon-ao-edge-filter.md)
- [环境反射拆分与独立控制](deferred-toon-indirect-light-split.md)
- [自发光拆分](deferred-toon-emissive.md)

各专题记录自身的验证场景、脚本和限制，不能将某一轮 SM5／SM6 通过扩展成所有平台、所有材质都通过。本文件本轮只进行源码对照和文档检查，不修改渲染行为，不新增运行验收。最近自发光的“静态光照＋Base Pass 速度输出”八槽组合与 SM5 虚拟纹理回退尚未完成专项运行验证，详见自发光文档。
