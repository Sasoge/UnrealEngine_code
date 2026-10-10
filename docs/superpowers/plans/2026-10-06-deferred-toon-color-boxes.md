# Deferred Toon Color Boxes

## Goal

在 UE 的 Deferred、Tonemap 前阶段加入可编辑的多盒体风格化颜色映射。每个盒体独立控制边缘衰减、阴影颜色体积纹理、光照曲线、阴影曲线、权重和优先级；盒体作用于盒体包围的场景几何，而不是渲染盒体本身。输出继续保持 HDR/线性空间，随后进入现有 Bloom、Lens Flare、曝光和 Tonemap 链路。

第一版目标是让以下流程完整可用：

1. 在关卡中放置多个 Deferred Toon Volume。
2. 使用 UE 的 `UCurveFloat`、`UCurveLinearColor` 和 `UVolumeTexture` 资产调整每个盒体。
3. 通过 `LightingBuffer`、`ShadowBuffer` 和 GBuffer/SceneDepth 重建结果，并按优先级合成重叠盒体。
4. 在编辑器中修改曲线或 3D 纹理引用后，运行时立即刷新渲染代理。
5. 通过控制台开关和 debug view 检查盒体范围、纹理采样和最终结果。

## Architecture

- **组件层**：新增 `ADeferredToonVolume` 和 `UDeferredToonVolumeComponent`。组件保存盒体参数，使用 `UBoxComponent` 作为编辑器可视化辅助，不把辅助盒体作为场景几何送进主渲染。
- **场景镜像层**：新增 `FDeferredToonVolumeSceneProxy`，在游戏线程复制变换、曲线采样 LUT、纹理引用和优先级；通过 `FSceneInterface` 注册到渲染线程。曲线和引用变更走 `MarkRenderStateDirty`，变换走 render command 更新。
- **视图数据层**：每帧从当前 `FScene` 收集可见盒体，按 `Priority` 降序、稳定对象 ID 升序排序，并限制在一个可配置的可见盒体上限内。高优先级盒体覆盖低优先级盒体；软边缘区域按 `BlendWeight` 允许低优先级结果继续参与。
- **Deferred pass**：新增 `PostProcessDeferredToonVolumes`。在 `PostProcessing.cpp` 中放在运动模糊/后运动模糊半透明合成之后、Bloom setup/downsample 之前。pass 读取 SceneColor、SceneDepth、GBuffer、`LightingBuffer` 和 `ShadowBuffer`，重建世界坐标并判断盒体局部坐标。
- **纹理绑定策略**：第一版每个盒体执行一个可合并的 fullscreen RDG pass，单独绑定该盒体的 `UVolumeTexture`，并通过 ping-pong SceneColor 处理多个盒体。这样避免在 shader 中使用不可移植的动态 Texture3D 数组，同时保证每个盒体都能引用独立资产；可见盒体上限和开销通过 CVar 控制。
- **映射规则**：
  - `BoxLocalPosition = WorldToLocal(WorldPosition)`，归一化到 `[0,1]` 后三线性采样 `ShadowColorVolume`。
  - 没有 `ShadowColorVolume` 时使用 `ShadowColorCurve` 的默认颜色。
  - `LightingBuffer` 的亮度经过 `LightingColorCurve` 映射；`ShadowBuffer` 的值经过 `ShadowColorCurve` 映射。
  - `ShadowBuffer` 只作为阴影权重，不能独立把盒体外的像素染色；盒体权重由局部坐标和 `EdgeFalloff` 共同决定。
  - 每个盒体最终颜色按 `BlendWeight` 与盒体权重混合到当前 SceneColor，保持线性 HDR。
- **路径限制**：第一版只支持 Deferred/SM5；Forward、Mobile、无有效 SceneDepth 或无有效 SceneTextures 时跳过该 pass，保留原始 SceneColor。

## Tech Stack

- C++ UE Runtime/Renderer、RDG、`FScreenPassTextureSlice`、`FSceneInterface`/`FScene`。
- HLSL global pixel shader，使用 `SceneTextureParameters`、GBuffer、SceneDepth 和 3D texture SRV。
- UE asset types：`UCurveFloat`、`UCurveLinearColor`、`UVolumeTexture`。
- Python contract tests，覆盖源码接口、插入顺序和 shader 参数；编辑器编译作为集成验证。

## Spec

### Public component and actor

新增：

- `Engine/Source/Runtime/Engine/Classes/Components/DeferredToonVolumeComponent.h`
- `Engine/Source/Runtime/Engine/Private/Components/DeferredToonVolumeComponent.cpp`
- `Engine/Source/Runtime/Engine/Classes/Engine/DeferredToonVolume.h`
- `Engine/Source/Runtime/Engine/Private/DeferredToonVolume.cpp`

`UDeferredToonVolumeComponent` 继承 `USceneComponent`，公开以下属性和 Blueprint setter：

- `bEnabled`。
- `EdgeFalloff`，以盒体局部空间边缘距离控制软边。
- `UCurveFloat* FalloffCurve`，输入归一化局部边缘距离，输出盒体权重。
- `UVolumeTexture* ShadowColorVolume`，RGB 为阴影颜色，A 为可选额外权重。
- `float ShadowColorVolumeWeight`。
- `UCurveLinearColor* LightingColorCurve`。
- `UCurveLinearColor* ShadowColorCurve`。
- `float BlendWeight`。
- `int32 Priority`。

组件需要实现 `CreateRenderState_Concurrent`、`SendRenderTransform_Concurrent`、`DestroyRenderState_Concurrent`、`CreateSceneProxy` 和编辑器 `PostEditChangeProperty`。资产或曲线属性变化必须刷新渲染状态；仅变换变化使用 render command 更新代理。组件使用 `UBoxComponent` 作为 editor-only 子组件显示盒体范围，并保证该辅助组件不参与可见场景绘制。

`ADeferredToonVolume` 负责放置组件、设置默认根组件和编辑器图标，不重复保存渲染参数。

### Scene registration and render proxy

新增：

- `Engine/Source/Runtime/Engine/Public/DeferredToonVolumeSceneProxy.h`
- `Engine/Source/Runtime/Engine/Private/DeferredToonVolumeSceneProxy.cpp`

修改：

- `Engine/Source/Runtime/Engine/Public/SceneInterface.h`
- `Engine/Source/Runtime/Renderer/Private/ScenePrivate.h`
- `Engine/Source/Runtime/Renderer/Private/RendererScene.cpp`
- 新增或扩展 `Engine/Source/Runtime/Renderer/Private/DeferredToonVolumeRendering.cpp`

`FDeferredToonVolumeSceneProxy` 保存：

- 世界变换和逆变换、世界包围盒。
- 采样后的固定大小 Falloff/Lighting/Shadow LUT，避免在像素 shader 中读取 UObject 曲线。
- `UVolumeTexture` 对应的 `FTextureResource`/RHI 引用和纹理有效标记。
- `bEnabled`、`BlendWeight`、`Priority`、稳定对象 ID。

向 `FSceneInterface` 增加 Add/Remove/Update 方法；`FScene` 保存代理数组并提供线程安全的注册/删除命令。为无渲染场景实现空实现，保持与 `RendererScene.cpp` 中现有 volume 接口一致。收集函数只返回启用且与视图包围盒相交的盒体。

### RDG pass and shader

新增：

- `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessDeferredToonVolumes.h`
- `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessDeferredToonVolumes.cpp`
- `Engine/Shaders/Private/PostProcessDeferredToonVolumes.usf`

pass 接口至少包含：

- 输入：SceneColor、SceneDepth、SceneTextures/GBuffer、`LightingBuffer`、`ShadowBuffer`。
- 当前视图 uniform buffer 和 view rect。
- 单个盒体的 world-to-local、局部尺寸、边缘衰减、BlendWeight、Priority、曲线 LUT、ShadowColorVolume SRV。
- 默认纹理/默认 LUT，确保资产为空或资源尚未加载时 shader 不读空 SRV。

像素流程：

1. 读取 SceneDepth，重建世界坐标；无几何像素直接复制 SceneColor。
2. 变换到盒体局部坐标，计算 `[0,1]` 采样坐标和到盒体边缘的归一化距离。
3. 通过 `FalloffCurve` 和 `EdgeFalloff` 得到盒体权重；盒体外权重为零。
4. 读取当前像素的光照/阴影输入，分别使用 Lighting/Shadow 曲线求得映射颜色与阴影权重。
5. 若绑定 `ShadowColorVolume`，三线性采样并按 `ShadowColorVolumeWeight` 与曲线颜色混合；没有纹理时直接使用曲线颜色。
6. 将结果按盒体权重和 `BlendWeight` 混合回 SceneColor，保持 HDR 线性值，不做 Tonemap/曝光。

`PostProcessing.cpp` 的顺序必须满足：Deferred Toon Volume pass → pre-bloom post-process material/downsample → Bloom/Lens Flare → `SceneColorBeforeTonemapSlice` → Tonemap。pass 需要在没有盒体或 CVar 关闭时完全跳过，不能改变现有 LightingBuffer/ShadowBuffer debug view。

### Console variables and debug

新增渲染线程安全 CVar：

- `r.Toon.DeferredVolumes`：总开关，默认 1。
- `r.Toon.DeferredVolumes.MaxVisible`：每视图最大盒体数，默认 8。
- `r.Toon.DeferredVolumes.DebugView`：0 正常、1 显示盒体权重、2 显示局部 3D 纹理采样颜色、3 显示优先级/重叠结果。

保留现有 `r.Toon.LightingShadowBuffers` 和 `r.LightingShadowBuffers.DebugView` 的语义；Deferred Toon pass 不能把 debug view 的资源替换成临时 SceneColor。

## Global Constraints

- 不改变 UE 原生光照通道和 Shadow Channel 的匹配规则；盒体只消费已经产生的 lighting/shadow 输入。
- 不把盒体辅助几何写入 GBuffer、CustomDepth 或 ShadowBuffer。
- 不在 Tonemap 后修改颜色；所有盒体映射必须发生在 HDR/线性空间。
- 不依赖编辑器重新保存资产才能刷新；属性、曲线引用和纹理引用修改都必须触发渲染代理更新。
- 保持 Forward/Mobile 的编译与运行路径不受影响。
- 所有新增 UObject/组件接口使用 UE 反射宏和生成头，避免在 Renderer 模块直接持有 UObject。
- 先完成最小可运行版本，再考虑 tile/cluster culling、单 pass Texture3D 数组或 stencil overlap 优化，不在第一版混入这些优化。
- 不修改现有 LightingBuffer/ShadowBuffer 的写入公式和调试显示逻辑，除非编译错误明确要求接口适配。

## Review Focus

- 是否确实在 Bloom/Lens Flare/Tonemap 之前，并且输出仍是 HDR 线性 SceneColor。
- 盒体外像素是否保持原色，边缘权重是否只由局部盒体坐标控制。
- 多盒体优先级、同优先级权重和稳定排序是否确定且可复现。
- `UVolumeTexture` 是否三线性采样、随盒体变换正确映射，并在无纹理时安全回退到曲线颜色。
- 曲线或纹理修改后是否无需重新赋值即可刷新渲染线程资源。
- empty scene、无深度、Forward/Mobile 和超过最大盒体数时是否安全跳过或截断。
- 是否保留已有 LightingShadowBuffers debug view，并避免编辑器 viewport 的 RDG 输出尺寸问题再次出现。

## Implementation Tasks

### 1. Add the volume asset-facing component and actor

- Files: the four component/actor files listed above.
- Define reflected properties, editor categories, setters and editor-only box visualization.
- Mirror the `ULocalFogVolumeComponent` render-state lifecycle and make all asset/property edits mark render state dirty.
- Add a deterministic stable ID for sorting and a default fallback LUT/volume color.

### 2. Add render-thread proxy and scene registration

- Files: proxy files, `SceneInterface.h`, `ScenePrivate.h`, `RendererScene.cpp`, volume rendering source.
- Copy curve values into fixed LUT arrays on the game thread.
- Register, remove and transform-update proxies through render commands.
- Add view intersection filtering, priority sorting and max-visible truncation.

### 3. Implement the pre-Tonemap RDG pass

- Files: new post-process C++/HLSL files and `PostProcessing.cpp`.
- Add CVar gating and deferred/SM5 checks.
- Implement per-volume ping-pong SceneColor passes so each volume binds its own 3D texture resource.
- Reconstruct world position from SceneDepth, sample GBuffer/LightingBuffer/ShadowBuffer, evaluate curves, sample `ShadowColorVolume`, and blend in linear HDR.
- Insert the pass before pre-bloom work and preserve existing debug paths.

### 4. Add debug output and safe fallbacks

- Expose debug CVar modes for weight, volume texture and overlap priority.
- Add black/white/default dummy resources for missing curves or volume textures.
- Ensure no invalid texture resource or stale proxy pointer can reach the render thread.

### 5. Add contract tests

- Add `tests/test_deferred_toon_volume_contract.py`.
- Assert component/proxy/scene APIs, reflected property names, curve/volume fallback, multi-box ordering, pre-bloom insertion, deferred-only guards, and debug CVar wiring.
- Keep existing `tests/test_lighting_shadow_buffers_contract.py` passing unchanged.

### 6. Build and verify

- Run the focused Python contract tests.
- Build the affected UE editor modules with the existing Rider/UBT configuration.
- In a test map, place two overlapping boxes with different priorities and volume textures; verify camera movement, box transforms, curve edits, texture edits, no-texture fallback, and Bloom/Tonemap behavior.
- Capture RenderDoc/viewport frames for normal output and each debug mode; verify that only the intended scene geometry is affected.

