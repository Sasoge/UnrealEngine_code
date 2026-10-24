# 直接光高光：硬边单档

日期：2026-10-24。间接光与 AO 暂不继续扩展。本轮只处理原生 Deferred Light 的直接镜面高光。

## 入口与表现

输出使用「直接光调色 / Direct Light Grade」，展开「风格化 | 直接光高光」：

- 高光方式：默认「原生」，保持旧资产的最终画面。选择「硬边单档」后才提取并替换直接高光。
- 高光范围阈值：对去预曝光后的原生可见高光亮度进行硬阈值判断。越高范围越小；零能量始终不生成高光，即使阈值为零。
- 高光颜色：区域内固定的场景线性颜色；不再乘材质底色或灯光颜色。
- 高光强度：只改变输出颜色强度，不改变选区。0 移除已提取的直接高光。

没有软过渡参数，没有新增纹理、曲线或分档数组。材质粗糙度、着色法线、视线、灯光强度和灯光色仍影响原生高光及选区；这里没有替换 BRDF，也没有移除法线贴图。多光源先合计实际可见高光，再统一做一次硬阈值。

## 原生输入修正

旧 `SpecularBuffer.rgb` 是未遮挡直接高光，仅用于调试/实验语义输入，不能直接用于最终高光重组。现在沿用同一个 MRT，RGB 改为原生实际可见高光：

- 普通 Deferred：`VisibleLighting.SpecularLighting`。
- Substrate：`SubstrateLighting.TotalSpecularLighting`。
- Hair：`VisibleHairLighting.SpecularLighting`，保留原生透射、覆盖、IES 与曝光缩放。
- Shadow-only 光源不添加高光；保留每盏灯原有照明通道和阴影接收通道策略。自定义 Toon MainLight 分支如果清除了原生 SceneColor，也清除该分支高光 RGB。

RGB 保留原生预曝光；Alpha 仍为既有的朝向诊断值，仍采用原有的独立混合规则。没有增加 MRT、修改槽位、RT 格式或新增历史。不会把所有灯的总高光乘上某一张合并阴影遮罩。

兼容性：调试 18 与实验输入 `SpecularDirectLight`（ID 11）现在表示**有效直接高光**，不再表示未遮挡高光；使用这个实验输入的自定义分层资产可能看到阴影内高光减少。历史拆分设计文档描述的是当时的输入语义，以本更新为准。其他光照输入的含义不变。

## 硬边与合成

```text
SpecularSceneLinear = max(SpecularBuffer.rgb / PreExposure, 0)
Response = luminance(SpecularSceneLinear)
Mask = Response > 0 && Response >= Threshold ? 1 : 0
StyledSpecular = Mask * Color * Intensity * PreExposure
```

Direct Light Grade 中的分解顺序：

```text
原场景 - 有效直接漫反射 = 原残差
可提取直接高光 = min(有效直接高光, 原残差)  // 按 RGB 通道限定
剩余预算 = 原残差 - 可提取直接高光
剩余预算再按现有规则分离/处理间接漫反射
最终 = 风格化直接漫反射 + 风格化直接高光 + 已处理间接光及保留的其余残差
```

直接高光只扣一次，再加入硬边结果，避免原生软高光与硬边高光叠加。开启硬边后该层独立于 `Other Lighting Weight`；原生模式仍由这个旧权重管理。间接光与 AO 的参数/算法不改，视图 37 的预算同步扣除已独立提取的高光以对应生产输出。多个盒体仍使用不可变原场景作为各自的分解来源，不反复从前一个盒体结果扣光。

直接漫反射分档、独立 AO、环境反射、SSR、Lumen 镜面反射和反射捕获不使用该高光遮罩。静态烘焙、透明、前向和未由这个 Deferred MRT 导出的高光仍留在残差中。没有有效缓冲时保留原有回退行为，不凭空生成高光。

## 调试与边界

| 视图 | 内容 |
| --- | --- |
| 18 | 原生有效直接高光，包含每盏灯的遮挡和衰减 |
| 39 | 高光硬阈值遮罩；不受作者颜色、强度影响 |
| 40 | 当前高光方式的输出：原生或硬边颜色 |

视图 39 可以在原生模式下独立预览阈值选区；看见该遮罩不代表最终输出已启用硬边高光。最终合成须选择「硬边单档」。

阈值作用于场景线性 HDR 亮度，不是归一化范围；通过 OneOverPreExposure 消除预曝光存储缩放，不消除灯光能量、材质和视角的真实变化。最终仍经过 UE 曝光、Tonemapper、TAA/TSR、泛光等。掩码严格 0/1，不表示屏幕边缘没有抗锯齿或泛光；本轮没有添加高光专用时域稳定器，细小法线和移动视角可能造成选区跳变。

残差提取在雾、SSS、后续合成等场景下不保证精确分解所有贡献，因此保留非负预算上限。不能把截图内所有亮点都视为本功能可处理的直接高光。

## 验证

- `UnrealEditor Win64 Development` 编译成功，101 项构建动作，约 307 秒。
- 原有回归测试：工作区 99 项 + 引擎分档测试 7 项，通过。
- D3D12 / SM6：`verify_direct_specular_editor.py` 的 32 组 960×540 离屏截图已完成；原生模式保真、缓冲缺失回退、两类光源参与、多光源累加、粗糙度响应、阈值收缩、零光照不生成高光、作者颜色/强度不改变选区、预曝光对照、独立高光和最终叠加检查通过。
- 阈值 0.02 / 0.1 / 1 的盒体内白色覆盖率分别约 1.2166% / 0.4589% / 0.0382%。无抗锯齿时选区为二值。
- 作者颜色 `(0.3, 0.08, 0.015)` 输出像素为 `(76, 20, 4)`；强度减半后为 `(38, 10, 2)`。固定颜色平均线性误差约 0.001405，最终“漫反射 + 高光”叠加误差约 0.000112。
- 检查脚本初版错误地将关闭 Tonemapper 的截图当作 sRGB 解码，导致固定颜色断言失败。对照实际固定 RGB 后，改用线性 UNORM 读取，通过原有误差上限；没有为通过测试而修改渲染或放宽容差。
- 基础场景的 `shadow-ignored` 与 `shadow-matched` 差异为零，不能据此认定阴影通道视觉验证通过。补充 `verify_direct_specular_shadow_editor.py` 的四组专用截图：不可见双面遮挡物包住点光源后，有效高光能量降为 0；将灯的阴影通道改为与接收面不匹配后，恢复无遮挡结果（RGB 平均误差 0）；关闭遮挡物投影同样恢复原结果。检查脚本要求遮挡前存在有效高光，避免全黑画面假通过。
- D3D11 / SM5：完成全局着色器编译及五组运行截图（原生、双光源输入、硬边遮罩、固定颜色、最终合成）；二值性、固定线性颜色与有效输入检查通过。日志确认使用 `PCD3D_SM5`，未发生用户之前遇到的 Hair 分支 `SpecularContribution` 未声明错误。
- 合计 41 组截图。`check_direct_specular_images.py` 最终输出 `DIRECT_SPECULAR_IMAGE_PASS`、`DIRECT_SPECULAR_SHADOW_PASS`、`DIRECT_SPECULAR_SM5_PASS`。

运行脚本只做临时场景/材质修改，不保存关卡或材质。输出位于测试项目 `Saved/Automation/DeferredToonDirectSpecular*`。测试日志的完成标识沿用共享回调的 `INDIRECT_DIFFUSE_GPU_PASS`，具体用例由本高光脚本定义。

启动阶段日志有两条已有的 `LogAutomationTest: Error: Condition failed`，出现在测试脚本执行前；本轮未见 Shader 编译失败。上述图像验证针对普通 Deferred 材质，不能替代 Hair、Substrate、雾/SSS、透明、VR、多视图等专门场景验收。TSR 用例只确认可运行、输出尺寸正确，没有宣称完成移动视角稳定性验证。

复现入口：

```powershell
# 在 UnrealEditor 中以 -ExecutePythonScript= 指定脚本。
# 主测试用 -d3d12，SM5 脚本用 -d3d11 -sm5；其余使用
# -RenderOffscreen -unattended -nop4 -nosplash -NoSound
Tests/DeferredToon/verify_direct_specular_editor.py
Tests/DeferredToon/verify_direct_specular_shadow_editor.py
Tests/DeferredToon/verify_direct_specular_sm5_editor.py

# 使用安装了 numpy、Pillow 的 Python：
python Tests/DeferredToon/check_direct_specular_images.py <主测试目录> <遮挡测试目录> <SM5目录>
```
