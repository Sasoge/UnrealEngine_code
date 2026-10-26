# 盒体调试视图：分组索引与兼容性

更新：2026-10-26。

组件“缓冲视图预览”和 `r.Toon.DeferredVolumes.DebugView` 使用下列**公开索引**。按功能分段，段内优先排列实际颜色、选区与有效性；保留空号供后续扩展。此次只整理选择入口，不改光照、遮蔽、滤波或合成算法。

## 常用入口

| 索引段 | 内容 | 常用视图 |
|---|---|---|
| 0～3 | 输出与盒体 | 0 当前输出；1 盒体范围；2 优先级 |
| 10～19 | 表面数据 | 10 底色；11 法线；12 深度；13 速度 |
| 20～29 | 直接漫反射 | 20 有效颜色；21 受光响应；22 明暗档位 |
| 30～39 | 直接镜面高光 | 30 原生高光；31 硬边遮罩；32 加工后颜色 |
| 40～49 | 间接漫反射 | 40 原生颜色；41 有效性；42 整层二分遮罩 |
| 50～59 | 综合遮蔽 | 50 原始遮蔽；51 AO 二分；53 有效性；54 来源；55 屏幕 AO |
| 60～69 | 非 Lumen 天光分路 | 60 AO 前漫反射；61 综合遮蔽；62 有效性 |
| 70～79 | Lumen 短程 AO 分路 | 70 短程 AO 前漫反射；71 短程 AO；72 有效性 |
| 80～89 | 环境镜面反射 | 80 颜色；81 有效性 |
| 90～99 | 材质自发光 | 90 颜色；91 有效性 |
| 100～109 | 阴影输入与旧版诊断 | 100 原生遮挡；101 艺术阴影通道；102 组合遮罩 |
| 110～119 | 实验分层 | 110 分层遮罩；111 分层颜色 |

表中范围是保留的功能段，不代表每个数字都已实现。**42 是整层间接漫反射二分，51 才是 AO 二分**，两者不能混用。

独立操作入口见[Toon 光照调试面板](deferred-toon-debugger-panel.md)，可以分类切图、搜索、收藏，并临时接管目标盒体。

## 使用方式

直接在组件下拉菜单选择相应条目；或者将组件设为“跟随全局 / 控制台”，再输入：

```text
r.Toon.DeferredVolumes.DebugView 80
```

上例预览环境镜面反射。恢复画面使用 `r.Toon.DeferredVolumes.DebugView 0`。组件选择了具体视图时，它会覆盖全局控制台设置；控制台改动不会强制改写组件资产。

- 空号、负数、越界数值及控制台输入 255 均回退至 0，避免静默夹到一个无关视图。
- 255 仅是组件内部“跟随全局”的哨兵，不是可预览图。
- `r.Toon.DeferredVolumes.AOFilterDebug` 是 **51 的子诊断开关**，自身 0～5 的编号不变。查看普通 AO 二分时设为 0；例如 4 仍表示历史接受情况。
- 数据有效性为黑不等于功能发生错误：可能是当前路径、材质或视图没有提供数据。来源和限制见[光照独立输出总览](deferred-toon-lighting-output-reference.md)。

## 新旧对照

“旧编号 / 内部 ID”是本次整理前的编号，也是继续保留的序列化和 Shader ID；“新索引”才是现在面板和控制台使用的数字。

| 新索引 | 中文名称 | 英文枚举名 | 旧编号 / 内部 ID |
|---|---|---|---|
| 0 | 当前输出 | `NativeComposite` | 0 |
| 1 | 盒体影响范围 | `BoxInfluence` | 1 |
| 2 | 优先级 | `Priority` | 3 |
| 3 | 合成阴影 | `ShadowComposite` | 2 |
| 10 | 材质底色 | `BaseColor` | 7 |
| 11 | 世界空间法线 | `WorldNormal` | 8 |
| 12 | 线性深度 | `Depth` | 12 |
| 13 | 运动速度 | `Velocity` | 13 |
| 20 | 有效直接漫反射 | `EffectiveDirectLight` | 22 |
| 21 | 有效受光响应 | `EffectiveLightResponse` | 23 |
| 22 | 所选明暗档位（0 暗 / 1 亮） | `LightBandValue` | 24 |
| 23 | 直接光遮罩 | `DirectLightMask` | 14 |
| 24 | 卡通阴影遮罩 | `ToonShadowMask` | 19 |
| 25 | 卡通二分阴影遮罩 | `ToonBinaryShadowMask` | 20 |
| 26 | 直接光 | `DirectLight` | 4 |
| 27 | 朝向受光（N·L） | `DirectLightFacing` | 21 |
| 30 | 有效直接光高光 | `SpecularDirectLight` | 18 |
| 31 | 直接高光硬边遮罩 | `DirectSpecularMask` | 39 |
| 32 | 风格化直接光高光 | `StyledDirectSpecular` | 40 |
| 40 | 间接漫反射 | `IndirectDiffuse` | 25 |
| 41 | 间接漫反射数据有效性 | `IndirectDiffuseCoverage` | 26 |
| 42 | 间接漫反射二分遮罩 | `IndirectDiffuseBand` | 37 |
| 50 | 间接漫反射综合遮蔽（自动） | `IndirectOcclusion` | 33 |
| 51 | 遮蔽二分遮罩（白为无遮挡） | `IndirectOcclusionBand` | 38 |
| 52 | 遮蔽前间接漫反射（自动） | `IndirectDiffuseBeforeOcclusion` | 34 |
| 53 | 综合遮蔽数据有效性 | `IndirectOcclusionCoverage` | 35 |
| 54 | 遮蔽来源（青=Lumen，橙=天光，黑=无） | `IndirectOcclusionSource` | 36 |
| 55 | 屏幕空间 AO | `AmbientOcclusion` | 9 |
| 60 | 天光漫反射（AO 前） | `SkyDiffuseBeforeAO` | 27 |
| 61 | 天光综合遮蔽（白为无遮挡） | `SkyOcclusion` | 28 |
| 62 | 天光遮蔽数据有效性 | `SkyOcclusionCoverage` | 29 |
| 70 | Lumen 漫反射（短程 AO 前） | `LumenDiffuseBeforeShortAO` | 31 |
| 71 | Lumen 短程 AO（白为无遮挡） | `LumenShortRangeAO` | 30 |
| 72 | Lumen 短程 AO 数据有效性 | `LumenShortRangeAOCoverage` | 32 |
| 80 | 间接镜面反射（环境反射） | `IndirectSpecular` | 41 |
| 81 | 间接镜面反射可用范围 | `IndirectSpecularCoverage` | 42 |
| 90 | 材质自发光 | `Emissive` | 43 |
| 91 | 材质自发光可用范围 | `EmissiveCoverage` | 44 |
| 100 | 原生阴影 | `NativeShadow` | 5 |
| 101 | 艺术阴影通道 | `ShadowChannel` | 6 |
| 102 | 组合阴影遮罩 | `CombinedShadowMask` | 17 |
| 103 | 原生阴影遮罩（旧版）；仅控制台，菜单继续隐藏 | `NativeShadowMask` | 15 |
| 104 | 艺术阴影通道遮罩（旧版）；仅控制台，菜单继续隐藏 | `ShadowChannelMask` | 16 |
| 110 | 分层遮罩 | `StyleMask` | 10 |
| 111 | 分层颜色 | `StyleColor` | 11 |

## 保存兼容性与维护规则

1. `EDeferredToonDebugView` 的枚举名称和显式数值保持原样，仅调整声明顺序与显示标签。已有场景、蓝图和组件保存的选择不需要重新设置；例如旧资产的 `IndirectSpecular = 41` 仍指向环境镜面反射，界面现在显示 80。
2. 控制台数值采用新索引，旧的控制台配置、快捷键和外部脚本需要按表迁移。例如旧 `DebugView 38` 现在应写 `DebugView 51`；旧 `DebugView 41` 应写 `DebugView 80`。不根据数值猜测新旧版本，避免同号歧义。
3. C++ / Python / Blueprint API 继续使用具名枚举（例如 `INDIRECT_SPECULAR`）。不要把新公开索引直接强制转换成组件枚举。枚举转整数得到的是稳定内部 ID。
4. `DeferredToonDebugViews.h` 是公开索引、具名枚举、控制台帮助和中文显示名称的共享表。Renderer 只在读取全局 CVar 时转换一次；组件直接传稳定 ID。Shader 分支、AO 历史判断和后处理调试路径仍使用稳定 ID。
5. UENUM 的英文标签需要与共享表同步；声明顺序按公开索引递增排列，隐藏项仍隐藏。不要为 UI 整理改动 Shader ID，不要按枚举的声明下标读写持久化值。
6. 新增视图选择对应功能段中的空号，并分配新的、不重复的内部 ID。同步枚举、共享表、渲染分支/上限、双语提示、测试和本文；不要覆盖已发布索引。

历史专题和 AO 逐次优化日志保留当时编号，以便理解旧截图、运行记录与提交；文首注明使用本表换算。当前光照总览已更新为新索引，仓库中的运行验证脚本也已迁移。

## 验证

- C++ 自动化 `Engine.DeferredToon.DebugViewIndices`：检查 45 个视图映射、全部旧 ID 保持、反射菜单顺序、标签前缀、隐藏项、空号与非法索引回退。
- 现有画面验证脚本的数值 CVar 选择同步换号；`AOFilterDebug` 子模式与具名枚举保持不变。
- `UnrealEditor Win64 Development` 编译成功。运行 `Engine.DeferredToon.DebugViewIndices` 通过，验证全部 45 个视图及兼容性。
- 现有工作区 99 项契约/行为测试与仓库 7 项明暗分档测试通过；其中三处旧的控制台上限断言改为公开索引转换入口。
- 静态比对确认 13 个画面验证脚本只迁移了对应视图选择，原动作与 `AOFilterDebug` 子模式保持一致；本轮没有重新执行全部 GPU 场景测试。45 个标签/表项与新旧对照一致，受影响文档链接已检查。
