# DirectLightMask 改为表面朝向判定

> 历史阶段记录。2026-10-15 起，N·L 仅作为调试／艺术输入保留，不再决定生产明暗选区。当前实现见 [漫反射映射设计](deferred-toon-diffuse-mapping-design.md)。

## 目的

旧遮罩对 LightingBuffer 的漫反射 RGB 亮度做阈值判定。漫反射结果已经包含材质漫反射颜色和光源能量，深色材质可能在相同朝向下被误分为暗部。去除预曝光不能消除材质颜色的影响。

本次将明暗判定与辐射能量分开。LightingBuffer RGB 和 SpecularBuffer RGB 保留原本的漫反射、高光输入用途。

## 输入与合并规则

- 新语义 `DirectLightFacing`，稳定 ID 为 12，范围为 0～1。
- 每盏实际参与 deferred lighting 的光源计算 `saturate(dot(normalize(N), normalize(L)))`。
- 点光、聚光和矩形光使用逐像素指向光源的方向。局部光作用范围/锥角仅作有效性判断，不将衰减乘入朝向值。矩形光这里采用中心方向，不积分面光源角度。
- 只匹配 Shadow Channel、不匹配 Lighting Channel 的减法灯不写入朝向值。
- 多灯取最大值，不累加：重复同方向的灯不会把原本低于阈值的表面推入亮部。
- 使用当前着色法线；法线贴图仍会改变分界。Substrate 路径使用顶层法线。
- 参数仅控制已被 UE 提交的有效灯；关闭、隐藏、强度为零而被剔除的灯不保证参与。

物理存储使用现有 PF_FloatRGBA SpecularBuffer 的 Alpha，RGB 仍保存高光。RT 每帧清为透明黑，Alpha 不预曝光。普通、Hair 和 Simple Light 的 MRT3 均使用 RGB Add / Alpha Max 混合。SpecularDirectLight 语义屏蔽其 Alpha，防止把朝向误读为高光。

## 盒体读取

```hlsl
DirectLightFacing = max_per_light(positive_NdotL);
DirectLightMask = DirectLightFacing > 0
    ? step(DirectLightMaskThreshold, DirectLightFacing) : 0;
ToonUnionShadow = 1 - DirectLightMask * (1 - CombinedShadowMask);
ToonBinaryShadowMask = step(ToonShadowThreshold, ToonUnionShadow);
```

朝向遮罩直接读取其提供者，不依赖 Style Profile 的层顺序。Profile/Inline Layers 也可通过新语义选取朝向值，不自动改动原有配表。光照 RGB 的曲线映射仍可使用原来的能量输入；只有明暗分类改变。

- Debug 14：严格二分的朝向遮罩，白为亮部。
- Debug 19：朝向暗部与现有遮挡合并后的阴影遮罩。
- Debug 20：ToonRecompose 最终二分阴影遮罩，白为暗部。
- Debug 21：未经二分的朝向因子，便于设置阈值。
- Direct Light Mask Threshold 现在是角度余弦阈值。0 接受所有正向受光面；0.5 对应夹角不超过约 60 度；背面、无灯和无效输入保持暗部。

## 保留的边界

本次只替换朝向判定来源，不改已有 ShadowBuffer/ShadowChannel 的合并策略、Pass 顺序或时间抗锯齿。现有原生阴影仍是合并遮挡；多灯场景中不等价于“逐灯朝向与该灯可见性相乘后，再求并集”。若后续需要一盏灯填亮另一盏灯的阴影，应单独重构逐灯可见性组合，不能从当前合并 RT 完整还原。

ToonRecompose 输出仍在 HDR 管线中经过原生曝光、TAA/TSR 和色调映射；本次保证分类不依赖预曝光，不取消最终显示处理。

## 验证

自动数值测试覆盖法线/方向归一化、背面、无效法线、局部光范围、光照通道、阈值零边界、材质/光照亮度独立性以及三条绘制路径的 Alpha Max 混合。C++ 编译与 D3D 着色器运行验证结果随本次交付说明记录；数值测试不替代 GPU 渲染验收。

2026-10-15 实际验证：

- UnrealEditor Win64 Development 编译成功。
- 工作区 Python 自动检查共 87 项通过。
- D3D12 / PCD3D_SM6 全局着色器编译与编辑器运行完成，无新增 Shader/PSO 编译错误。日志中的两个启动期 `LogAutomationTest: Condition failed` 在先前基线日志也存在，不作为本次渲染验证通过的依据。
- 独立编辑器验证进程显式临时开启 `r.Toon.LightingShadowBuffers 1`。该开关默认关闭，未开启时黑色预览只是无输入回退，不能用于验证朝向计算。
- 捕获 Debug 4、21、14、20：Debug 4 可见地面棋盘格；Debug 21/14 不再出现棋盘格分区，球体保留朝向变化；Debug 20 可见合并的投射阴影。
- 验证脚本结束后恢复盒体参数、视角和 RT 开关，未保存场景或资产。未执行 SM5、启用 Substrate 或 Hair 场景的独立 GPU 验收。

验证日志：`C:/Users/ASUS/Documents/Codex/2026-10-02/qin/outputs/direct-light-facing-d3d12-enabled.log`。
验证截图：同目录 `direct-light-facing-view-4.png`、`direct-light-facing-view-21.png`、`direct-light-facing-view-14.png`、`direct-light-facing-view-20.png`。
