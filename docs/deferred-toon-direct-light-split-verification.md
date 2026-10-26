# Deferred Toon DirectLight 拆分验证记录

> 调试编号说明（2026-10-26）：本文保留当时的旧编号，运行命令和界面请按[新版索引对照表](deferred-toon-debug-view-indices.md)换算；`AOFilterDebug` 子模式编号不变。

日期：2026-10-13  
工程：`D:\UnrealEngine-release`  
测试工程：`D:\UnrealEngine-release\Project\test`

## 目标

将盒体的直接光照输入拆成漫反射和高光两个独立 RT，避免材质 Roughness 改变时 `DirectLightMask` 被高光能量污染。

## 输出契约

| MRT | 内容 | 盒体语义 |
| --- | --- | --- |
| Target0 | SceneColor | 原生场景颜色 |
| Target1 | `LightingBuffer.RGB`，未受阴影调制的直接漫反射 | `DirectLight` |
| Target2 | `ShadowBuffer.R`，UE 原生阴影遮挡 | `NativeShadow` |
| Target3 | `SpecularBuffer.RGB`，未受阴影调制的直接高光 | `SpecularDirectLight` |
| Target1 Alpha | Shadow Channel 减法阴影 | `ShadowChannel` |

Substrate 路径使用 `TotalDiffuseLighting` / `TotalSpecularLighting`；普通 GBuffer 和 Hair 路径使用 `GetDynamicLightingSplit`。前 3 个旧槽位保持不变，Toon/Substrate 输出顺延到 4–6。

## 盒体使用

- `DirectLight` 只读取 `LightingBuffer.RGB`，其 mask 不再包含高光。
- `SpecularDirectLight` 读取 `SpecularBuffer.RGB`，可在数字资产中单独 Multiply/Add/Replace。
- `DebugView 18` 显示高光 RT；14–17 的既有阴影 mask 视图保持原定义。
- 新增源码均标记 `[DeferredToon]` 或 `[LightingShadowBuffer]`，便于后续回溯和删改。

## 修改位置

- `Engine/Source/Runtime/Engine/Public/SceneTexturesConfig.h`
- `Engine/Source/Runtime/Renderer/Internal/SceneTextures.h`
- `Engine/Source/Runtime/Renderer/Public/SceneRenderTargetParameters.h`
- `Engine/Source/Runtime/Renderer/Private/SceneTextures.cpp`
- `Engine/Source/Runtime/Renderer/Private/LightRendering.h`
- `Engine/Source/Runtime/Renderer/Private/LightRendering.cpp`
- `Engine/Shaders/Private/DeferredLightPixelShaders.usf`
- `Engine/Source/Runtime/Engine/Classes/Engine/DeferredToonInputRegistry.h`
- `Engine/Source/Runtime/Engine/Private/DeferredToonInputRegistry.cpp`
- `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessDeferredToonVolumes.cpp`
- `Engine/Shaders/Private/PostProcessDeferredToonVolumes.usf`
- `Engine/Source/Runtime/Engine/Classes/Components/DeferredToonVolumeComponent.h`

## 验证结果

~~~text
python -m unittest discover -s tests -p "test_*.py"
Ran 74 tests ... OK

Build.bat UnrealEditor Win64 Development -NoHotReload -NoHotReloadFromIDE -NoUBTMakefiles
Result: Succeeded
~~~

`Project/test` 以 NullRHI 启动并完成 Engine Initialization，日志未见 Shader compilation failure、PipelineState failure 或 Fatal error。建议在编辑器中运行：

~~~text
r.Toon.LightingShadowBuffers 1
r.Toon.DeferredVolumes.DebugView 4
r.Toon.DeferredVolumes.DebugView 18
~~~

## Hair permutation 编译修正（2026-10-14）

全局着色器编译暴露了 Hair lighting permutation 的输出签名遗漏：Hair 分支已经写入独立高光 MRT `SpecularContribution`，但函数参数没有声明 `SV_Target3`，因此在 `FDeferredLightPS` 的 Hair permutation 中会报 `use of undeclared identifier 'SpecularContribution'`。

已在 `Engine/Shaders/Private/DeferredLightPixelShaders.usf` 的 Hair 输出签名中补齐 `SpecularContribution : SV_Target3`，并在所有提前返回前初始化为零。该修改只在 `LIGHTING_SHADOW_BUFFER_OUTPUT` 分支生效，不改变未启用分离 buffer 的 permutation。

修正后的验证：

~~~text
python -m unittest discover -s tests -p "test_*.py"  # 74/74 OK
Build.bat UnrealEditor Win64 Development ...          # Result: Succeeded
~~~
