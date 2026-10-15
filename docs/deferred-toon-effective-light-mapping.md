# 有效直接光与盒体调色、阴影纹理

> 历史实现记录。2026-10-15 清理后以 [当前漫反射映射设计](deferred-toon-diffuse-mapping-design.md) 为准：Use Effective Lighting 切换和旧角度选区已退役；当前统一使用有效光选区。本文件下述开关与兼容模式描述仅记录清理前状态。

日期：2026-10-15。

## 目的

先按每盏灯自己的可见性、通道、颜色、强度和衰减计算贡献，再累加，最后由盒体二分与映射。弱点光源被挡住不应抹掉方向光已经提供的照明。原有“最大朝向 + 所有灯的遮挡并集”丢失了灯与遮挡的对应关系，只作为可选旧模式保留。

## 输入契约

新增 `EffectiveLightBuffer`，与主视口相同分辨率，`PF_FloatRGBA`，每帧在照明前清零、逐灯加法累积，无自定义历史。

- RGB：复用原生 `GetDynamicLightingSplit` 的遮挡后漫反射结果；包含材质响应，不含直接高光。Substrate 复用原生 diffuse totals。
- Alpha：用原生光照积分函数评估单位白色、非金属、固定粗糙度、无镜面反射的参考表面，保留实际表面法线、光源形状/距离/范围、灯光颜色强度、Light Function、阴影和接收通道。取亮度后加法累积。
- Alpha 是“参考白色漫反射响应”，不是严格照度单位，也不是阴影概率或归一化 N·L。它可以大于 1；阈值会随真实光源强度和颜色而变化，但不会随材质底色、金属度、粗糙度或显示曝光变化。
- RGB 与 Alpha 都在原生灯光输出时乘预曝光，盒体采样后统一乘 `OneOverPreExposure` 恢复场景线性值。
- 参考表面额外执行一次原生积分，避免除以材质底色的数值不稳定；不是免费的拷贝。原生 RGB 则复用已有计算，没有再算一遍材质着色。后续可在性能分析后进一步共享积分中间量。
- 普通灯的原生遮挡不再二次乘到有效光结果上。减法专用阴影仍作为独立的艺术层，通过 LightingBuffer Alpha 合入最终选区，不宣称是物理光源。

原来的 LightingBuffer、ShadowBuffer、SpecularBuffer 语义保留。新增稳定语义 ID 13 `EffectiveDirectLight`、14 `EffectiveLightResponse`，可用于 Inline Layers / Style Profile；每个 Profile 的 12 层容量不变。新输入采用显式纹理绑定，不扩大全局 SceneTexture uniform。

## 操作

在 DeferredToonVolumeComponent 中：

1. **Selection → Use Effective Lighting**：默认开启。关闭时恢复旧朝向/遮挡并集供对照。
2. **Effective Light Threshold**：参考白色漫反射的场景线性阈值，默认 0.1。越高亮区越少。先观察 Debug 23，再调整阈值；阈值 0 仍不把零受光像素判亮。
3. **Output Mode → Direct Light Grade**：对原生遮挡后的直接漫反射进行选区调色/替换。
4. `Lighting Color Curve` / `Shadow Color Curve` 的时间 1 端点分别是亮区、暗区的颜色。`Lit Color Replace Weight` / `Shadow Color Replace Weight` 为 0 时乘色，为 1 时替换，中间值混合。替换可给完全黑的暗部赋色。
5. **Shadow Texture**：选择普通 Texture2D。纹理 RGB 与暗部目标色混合，权重为 Texture Weight × 纹理 Alpha；保留纹理资产自己的 sRGB 解码。Tiling / Offset 控制重复与位移。
6. **Base Color Weight**：控制替换颜色/纹理是否乘材质底色。不会把底色再次乘到原生漫反射上。
7. **Other Lighting Weight**：仅 Direct Light Grade 使用。1 保留原画面扣除直接漫反射后的剩余贡献，0 仅输出加工后的直接层。它是 SceneColor 残差，不是分别提取的 GI / 反射 RT；有雾、SSS、其他非线性合成的场景需单独验证。

`Toon Recompose` 继续支持纯色严格二分，现在同样可使用有效光选区、Shadow Texture 与已有 Shadow Color Volume。`Direct Light Grade` 的**选区**严格二分，但在乘色模式下区内仍保留原生连续的照明变化；要纯色两档，应使用 Toon Recompose 或将两区替换权重都设为 1，同时关闭其他光照残差。

纹理采用盒体局部三平面映射，投影权重取几何导数；不会使用屏幕 UV 随相机滑动。盒体旋转/缩放会改变映射；移动物体穿过固定盒体时纹理会经过物体表面，这是空间投影的预期行为。三平面之间允许混合，但不会软化明暗选区。纹理、底色和盒体边界仍可包含渐变，最终 TAA/TSR 也会抗锯齿。

## 调试

- 14：当前选择策略的受光二分。
- 20：最终二分阴影选区（含艺术减法层）。
- 21：原始最大 N·L，继续独立保留。
- 22：有效直接漫反射 RGB。
- 23：参考白色受光量，显示压缩为 `E/(1+E)`，避免 HDR 数值直接截白。

盒体运行于 TAA/TSR 前，不新增时序累计。新增 MRT 位于 Target4；Toon / Substrate 后移至 5/6/7，运行时绑定、Hair/Simple/Standard 混合与 PSO 预缓存格式同步更新。最复杂组合使用 8 个 MRT。

多个盒体按既有优先级混合；Direct Light Grade 的残差使用进入盒体系统前的 SceneColor，避免重叠盒体反复扣除同一份直接光。

本阶段覆盖延迟灯光通道中的直接光。已烘焙并混入其他照明层的光不自动还原到新输入中。Hair 和 Substrate 也接入导出分支，但本次视觉回归以普通不透明场景为主；参考表面选区不模拟头发透射或多层材质自身的复杂散射。

## 验证

验证日志与图片保存在 `C:/Users/ASUS/Documents/Codex/2026-10-02/qin/outputs`，测试场景仅在独立离屏进程中临时修改，未保存工程资产。

- C++ 一致性重编译成功，343 个构建步骤，日志 `effective-light-build-consistent.log`。中途一次运行因新旧 SceneTexture 参数布局混用崩溃，完成全部相关模块重编译后消除；最终没有扩大 SceneTexture uniform。
- 95 项回归检查全部通过，日志 `effective-light-tests-final.log`。
- D3D12 / SM6 全局着色器与实际离屏渲染通过，15 个截图阶段完成并正常退出，日志 `effective-light-d3d12-final.log`，标记 `EFFECTIVE_LIGHT_GPU_PASS`。本轮未单独验证 SM5 运行。
- 截图数值检查通过：点光源确实产生 23,201 个额外遮挡像素，但加入该灯后最终二分阴影仅有 31 个像素差异（边缘容差内），没有把该灯的遮挡并集盖到方向光结果上。
- 黑白材质切换、预曝光切换后的参考响应平均误差均约 0.134 / 255；可见漫反射仍随材质底色明显改变。预曝光切换的二分差异为 77 个边缘像素。
- 阴影通道不匹配时，测试区域的原生遮挡像素为 0。暗部纹理输出与无纹理输出产生可见差异，并已检查截图。

上述截图是固定视角的集成回归，不替代复杂项目中的移动视角、Hair / Substrate、雾与 SSS 专项视觉验收。
