# 盒体遮蔽阴影的接收通道过滤修复

日期：2026-10-15。

## 问题与原因

物体与光源的 Shadow Channels 不匹配时，普通原生着色路径会把该灯的遮挡衰减中和，但自定义 ShadowBuffer 使用的是此前保留的原始可见性。导出普通光源的 NativeShadowBufferAmount 时只判断了是否为减法专用光源，没有判断接收通道是否匹配。

结果是原生画面忽略的遮挡仍写进 ShadowBuffer，盒体合并后又把它显示成暗部。这发生在导出阶段，盒体无法从多灯累积后的标量恢复每盏灯的通道关系。

## 修改

在 `Engine/Shaders/Private/DeferredLightPixelShaders.usf` 的普通 GBuffer、Substrate 与 Hair 三个导出分支中增加相同的过滤：

```hlsl
NativeShadowBufferAmount = bShadowChannelOnly || !bShadowReceiverChannelMatch
    ? 0.0f
    : 原有遮挡贡献;
```

Hair 分支使用对应的 `bHairShadowChannelOnly` 和 `bHairShadowReceiverChannelMatch`。

所有新增修改均以 `[DeferredToon]` 注释标识。当前修复不修改 C++、RT 布局、Pass 顺序或时序累计。

## 行为边界

- Lighting Channels 匹配、Shadow Channels 不匹配：仍可参与朝向和直接光照，不写入该灯的原生遮挡。
- Shadow Channels 匹配：保留原有遮挡、范围衰减及 Hair 覆盖率。
- 减法专用光源：继续走 LightingBuffer Alpha 的 ShadowChannel 分支，不重复写入原生 ShadowBuffer。
- 光源的 Shadow Channels 全部关闭（mask 为 0）：沿用现有“不启用接收通道过滤”的语义，不改成“禁止所有阴影”。
- 背光面产生的朝向暗部不由 Shadow Channels 控制。即使通道不匹配，背光面仍可能显示暗色。
- 其他通道匹配的灯仍可写入遮挡，因此多灯场景不保证整个像素的总阴影变为零。

## 验证方式

`tests/test_shadow_receiver_export_behavior.py` 在工作区中读取实际 HLSL 导出表达式进行数值回归：验证不匹配时三条路径均输出零、匹配时保留原值、减法灯不会重复写入原生遮挡。连同已有测试共 90 项通过。

GPU 验证使用独立 D3D12 编辑器进程和 `outputs/verify_shadow_receiver_export.py`，临时开启自定义 RT，隔离一个方向光，依次检查匹配、不匹配、mask 为零时的原生阴影，以及匹配/不匹配时的朝向遮罩与最终阴影遮罩。脚本恢复参数和视角，不保存场景资产。

## 本次验证结果

- D3D12 / PCD3D_SM6 着色器编译与实际渲染完成，进程退出码 0；无新增着色器编译或 PSO 失败。
- 七张 960×540 截图检查通过。以灰度大于 32 计数，匹配时原生遮挡为 13,903 像素，不匹配时为 0；光源 mask 为零时为 13,897。
- 匹配/不匹配的朝向遮罩仅 5 个边缘像素不同，未改变朝向分类。
- 最终阴影遮罩从 74,961 像素降至 63,524；视觉检查确认地面投影消失，球体背光弧面及方块背光面保留。该全图计数包含天空，不能当作纯几何阴影面积。
- 日志：工作区 `outputs/shadow-receiver-export-d3d12.log`；截图：`outputs/shadow-receiver-{match-native,mismatch-native,wildcard-native,match-facing,mismatch-facing,match-final,mismatch-final}.png`。
- 日志中仍有此前已存在的两条启动 `LogAutomationTest: Error: Condition failed`，未将其计为本次新增故障。

用户原先运行的编辑器未关闭，需执行 `RecompileShaders Changed` 或重启编辑器加载更新。此次不需要重新编译 C++ DLL。
