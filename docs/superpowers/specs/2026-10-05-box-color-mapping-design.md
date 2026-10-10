# 后处理盒体颜色映射设计

日期：2026-10-05
状态：待评审

## 目标

增加多个独立的 Deferred 盒体，用于在各自范围内读取 GBuffer、`LightingBuffer` 和 `ShadowBuffer`，对场景光照和阴影做局部风格化合成。第一版在 Tonemap 之前运行，保留 Tonemap 后的最终调色作为后续扩展。

成功标准：

- 多个盒体可以在编辑器中创建、移动、旋转和缩放。
- 只有盒体范围内的屏幕像素受到影响。
- 盒体边缘可以通过曲线产生连续软过渡，不出现硬切边。
- 修改曲线资产后，运行中的渲染结果可以刷新，不需要重新给灯光或盒体重新赋值。
- 盒体关闭或曲线无效时，最终画面与未启用盒体时一致。
- 盒体 pass 不改变现有 ShadowBuffer 和 LightingBuffer 的写入逻辑。
- 盒体重叠时按照明确的优先级和混合规则得到稳定结果。
- 盒体对光照和阴影的修改会进入后续 Bloom、Lens Flare 和 Tonemap 流程。

## 方案选择

采用独立的盒体组件和独立的 renderer pass，不把新参数直接加入 UE 原生 `FPostProcessSettings`。

原因：

- 原生后处理体积已有自己的优先级、混合和序列化逻辑，直接扩展会影响所有后处理体积和多个渲染路径。
- 独立组件可以将盒体数据限制在自定义渲染结构中，减少对原生后处理设置和移动端路径的影响。
- 盒体直接使用现有 `LightingBuffer`、`ShadowBuffer` 和 GBuffer，不需要把所有结果先合并成最终 SceneColor。

## 运行时结构

### 盒体对象

新增一个独立的盒体对象，包含一个可视化的 `UBoxComponent` 和颜色映射设置。场景可以注册多个活动盒体；对象数据进入渲染线程时使用只读快照，避免 shader 直接访问 UObject。

建议参数：

- `bEnabled`：是否启用。
- `BoxExtent`：盒体半尺寸由 `UBoxComponent` 提供。
- `EdgeFalloff`：软边缘宽度，范围为 0 到 1。
- `FalloffCurve`：`UCurveFloat`，输入为盒体边缘到中心的归一化距离，输出为影响权重。
- `ShadowColorVolume`：`UVolumeTexture`，使用盒体局部坐标采样复杂的阴影颜色。
- `ShadowColorVolumeWeight`：阴影颜色纹理的混合强度。
- `LightingColorCurve`：`UCurveLinearColor`，根据局部光照强度产生风格化光照颜色。
- `ShadowColorCurve`：`UCurveLinearColor`，根据阴影量产生风格化阴影颜色。
- `BlendWeight`：最终颜色映射强度。
- `Priority`：重叠盒体的处理优先级。

第一版不加入完整三维颜色 LUT、Lens Flare 参数和 Tonemap 后最终调色参数。`ShadowColorVolume` 是第一版的空间颜色输入；没有指定纹理时回退到 `ShadowColorCurve` 和默认阴影色。多个盒体的排序和重叠合成属于第一版必须明确并实现的行为。

### 曲线资产和 GPU 数据

`UCurveFloat` 与 `UCurveLinearColor` 只作为编辑器资产和源数据。渲染线程不调用曲线 UObject，而是在资源或参数变化时将曲线均匀采样到小型 1D LUT：

- `FalloffCurve` 输出一个标量 LUT。
- `ShadowColorVolume` 作为渲染资源绑定到盒体代理，并使用局部坐标进行三线性采样。
- `LightingColorCurve` 和 `ShadowColorCurve` 各输出一个 RGBA LUT。

盒体代理保存 LUT 版本和采样数据。曲线资产修改时，通过编辑器属性变更和资源更新通知刷新代理；不要求用户重新赋值资产。

## 像素处理

盒体使用世界空间到盒体局部空间的变换。后处理像素通过场景深度重建世界位置，再转换到盒体局部空间：

1. 判断局部位置是否在盒体边界内。
2. 根据局部位置到六个面的最小距离计算归一化边缘距离。
3. 使用 `FalloffCurve` 查表得到盒体影响权重。
4. 读取 `LightingBuffer` 的直接光照和 `ShadowBuffer` 的正向阴影累加值。
5. 使用盒体局部坐标采样 `ShadowColorVolume`；纹理缺失时使用阴影曲线查表。
6. 使用光照曲线查表得到风格化光照颜色。
7. 在盒体影响权重、纹理权重和 `BlendWeight` 下，重新合成局部光照、阴影和基础颜色。
8. 盒体外或深度无效的像素保留原有 Deferred 光照结果。

第一版采用盒体参数数组和每个盒体的 LUT。渲染 pass 对视图可见的盒体执行有限循环；后续盒体数量较多时可以扩展为结构化缓冲、裁剪列表或实例化 pass。

### 输入与输出

第一版输入包括 SceneDepth、GBuffer、`LightingBuffer`、`ShadowBuffer` 和当前 Deferred 光照结果。SceneDepth 用于重建世界位置和判断盒体影响范围；LightingBuffer 与 ShadowBuffer 分别提供光照和阴影信息。盒体 pass 输出仍处于 HDR/线性场景颜色空间，结果继续进入 Bloom、Lens Flare、曝光和 Tonemap。

`FalloffCurve` 将盒体空间位置转换为影响权重；`ShadowColorVolume` 根据盒体内部的三维位置提供手绘式阴影颜色；`LightingColorCurve` 和 `ShadowColorCurve` 分别将光照强度和阴影量映射为备用或补充颜色。后续如果需要同时对光照和阴影进行三维手绘映射，可以增加独立的 Lighting Color Volume。

### 重叠规则

盒体按 `Priority` 从高到低排序。高优先级盒体先参与合成，低优先级盒体只在高优先级盒体的软边缘或影响权重不足时继续贡献；同优先级盒体按各自的 `BlendWeight` 做归一化混合。排序使用稳定的对象标识作为平局处理，保证相同参数下结果不会随注册顺序随机变化。

## 渲染位置

新增独立的 Deferred RDG pass，放在 Deferred 光照和盒体输入纹理准备完成之后、Bloom/Lens Flare 和 Tonemap 之前。该 pass 使用场景深度重建世界位置，并像 Deferred Light 一样将盒体参数应用到盒体覆盖的场景几何体，而不是给盒体自身的几何表面着色。

盒体 pass 的输出继续进入 UE 的 Bloom、Lens Flare、曝光和 Tonemap；编辑器的 ShadowBuffer DebugView 仍保持最终调试显示优先级。

第一版只在 deferred、SM5 或更高特性级别生效。Forward、Mobile 和无效场景深度路径直接跳过，保留原始 SceneColor。

## 性能和资源

- 每个像素只重建一次世界位置，然后对视图可见盒体执行局部空间测试、LightingBuffer/ShadowBuffer 读取和 LUT 采样。
- 盒体数量增加会带来线性像素开销，因此需要设置可见盒体上限，并在 CPU 侧按视锥和包围盒裁剪。
- LUT 采用低分辨率线性采样，避免每帧 CPU 评估曲线。
- pass 只在盒体启用且至少有一个有效曲线或非默认颜色参数时加入 RDG 图。
- 暂不申请新的持久化场景纹理，不影响现有 LightingBuffer/ShadowBuffer 的带宽。

## 编辑器更新和错误处理

- 盒体无效、曲线资产为空或 LUT 更新失败时使用默认线性衰减和白色目标颜色。
- 曲线编辑后刷新渲染代理；不依赖重新选择 Actor 或重新赋值资产。
- 盒体变换更新只刷新代理的变换和局部参数，不重新创建整个渲染管线。
- 非法的负边缘宽度、空 extent 和 NaN 参数在上传前钳制或回退到默认值。

## 验证计划

1. 创建多个盒体，确认移动、旋转、缩放能分别改变 Deferred 光照和阴影区域。
2. 使用线性 `FalloffCurve`，确认每个盒体内外和软边缘连续。
3. 使用带明显空间颜色块的 `ShadowColorVolume`，确认纹理跟随盒体变换并只影响阴影区域。
4. 使用阶梯曲线和反向曲线，确认每个盒体的 LUT 映射生效。
5. 在编辑器中直接修改曲线资产和替换 Volume Texture，确认无需重新赋值即可更新所有引用盒体。
6. 测试盒体重叠、优先级和纹理混合规则，确认结果稳定且可解释。
7. 确认盒体输出会影响后续 Bloom、Lens Flare 和 Tonemap。
8. 关闭所有盒体、移出摄像机视锥、关闭深度或切换到不支持路径，确认画面保持原样。
9. 开启 `r.LightingShadowBuffers.DebugView`，确认 ShadowBuffer/LightingBuffer 调试显示仍然可用。
10. 运行现有 Lighting/Shadow Buffer 合同测试，并增加多盒体 Deferred pass 的参数、裁剪、纹理绑定、路径和跳过条件测试。

## 后续扩展

完成第一版并验证后，再考虑：

- 独立的 Lighting Color Volume。
- Tonemap 后的最终局部调色。
- 盒体对 Lens Flare 强度、色散和星芒方向的调制。
- 与原生 Post Process Volume 的 BlendWeight、Priority 和 BlendRadius 统一。
