# 直接光高光：硬边单档

日期：2026-10-24。间接光与 AO 暂不继续扩展。本轮只处理原生 Deferred Light 的直接镜面高光。

## 入口与表现

输出使用「直接光调色 / Direct Light Grade」，展开「风格化 | 直接光高光」：

- 高光方式：默认「原生」，保持旧资产的最终画面。选择「硬边单档」后才提取并替换直接高光。
- 高光范围阈值：对去预曝光后的原生可见高光亮度进行硬阈值判断。越高范围越小；零能量始终不生成高光，即使阈值为零。
- 高光配色来源：默认「材质与灯光颜色」，保留原生有效高光的 RGB 比例，只将亮度分档；可选「固定颜色替换」恢复第一版配色方式。
- 高光染色 / 替换色：原生配色下为线性乘色，白色为中性；固定替换下为指定的输出颜色。不会再次乘材质底色、F0 或灯光颜色。
- 高光强度：原生配色下为染色前的单档线性亮度；固定替换下缩放指定颜色。不改变选区，0 移除已提取的直接高光。

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
NativeChroma = SpecularSceneLinear / max(Response, 0.000001)
BandColor = ColorMode == FixedColor ? Color : NativeChroma * Color
StyledSpecular = Mask * BandColor * Intensity * PreExposure
```

材质与灯光颜色来自原生可见高光，不由后处理再次估计：非金属的 Specular/F0、金属由 Base Color 决定的 F0、Fresnel、各光源颜色及受遮挡后的相对贡献均已体现在输入 RGB 中。先累计多光源再归一化，可保留不同位置的混色；单档亮度不等于单一 RGB。默认白色染色下，有效选区的线性亮度等于高光强度；非白色染色会再次改变颜色及亮度。

不对归一化颜色逐通道 saturate，否则会破坏饱和灯光和金属色。零输入输出零；极弱输入使用 1e-6 分母下限，避免除零与过度放大数值误差，因此低于该下限的输入不保证严格单档亮度。遮罩的原生亮度阈值仍在染色、归一化及强度处理前计算。输出经过 UE 曝光与色调映射后，屏幕 RGB 仍可能变化。

兼容与使用：新增配色来源默认选「材质与灯光颜色」，因此之前已启用硬边模式的盒体也会使用新逻辑，旧颜色值保留并改作染色。若要看未经艺术染色的材质/灯光颜色，请将「高光染色 / 替换色」设为白色；若要复现第一版纯色高光，选择「固定颜色替换」。高光方式本身仍默认「原生」，未开启硬边模式的资产不受影响。

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
| 40 | 当前高光方式与配色来源的输出：原生、硬边原生配色或固定替换 |

视图 39 可以在原生模式下独立预览阈值选区；看见该遮罩不代表最终输出已启用硬边高光。最终合成须选择「硬边单档」。

阈值作用于场景线性 HDR 亮度，不是归一化范围；通过 OneOverPreExposure 消除预曝光存储缩放，不消除灯光能量、材质和视角的真实变化。最终仍经过 UE 曝光、Tonemapper、TAA/TSR、泛光等。掩码严格 0/1，不表示屏幕边缘没有抗锯齿或泛光；本轮没有添加高光专用时域稳定器，细小法线和移动视角可能造成选区跳变。

残差提取在雾、SSS、后续合成等场景下不保证精确分解所有贡献，因此保留非负预算上限。不能把截图内所有亮点都视为本功能可处理的直接高光。

## 第一版固定颜色验证（20f6cf7）

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

## 第二版：保留材质与灯光配色（2026-10-24）

用户反馈固定颜色抹掉了材质 F0 与灯光颜色，因此本次将配色与高光范围分开。只新增「高光配色来源」选项，复用现有染色、强度与阈值，不新增 RT、不重复评估 BRDF、不改变原生光源导出、遮罩范围和残差扣除顺序。法线细节引起的高光颗粒不在本次修改范围。

验证入口：`verify_direct_specular_chroma_editor.py`，正常使用 `-d3d12`；SM5 使用 `-d3d11 -sm5 -SpecularChromaSM5`。每个平台各 24 组截图：两种底色的非金属、彩色灯光、金色金属、蓝色金属、红蓝双光源混色，以及染色、强度减半、预曝光、独立合成、零光照、固定色回退。

`check_direct_specular_chroma_images.py <截图目录>` 从视图 18 的实际 RGB 推导参考色，检查视图 40 是否保留同一 RGB 比例，不用猜测材质最终高光色。比较时排除输入截图已截断的 HDR 像素与量化误差过大的低亮度像素，并要求有效样本数；白色染色下另外检查输出线性亮度是否保持单档。非金属改变 Base Color 不应染色高光，彩色金属与灯光必须确实产生颜色变化。

本轮发现测试条件需要明确显示 Gamma：全局调试视图在 `PostProcessing.cpp` 末尾直接显示色调映射前图像，而生产输出在关闭 Tonemapper 时仍有 Gamma-only 转换。不能仅凭 `ShowFlag.Tonemapper 0` 就断言所有截图是线性编码。现已在共享高光测试进程显式执行 `gamma 1`，统一比较空间；命令只改当前测试进程，不保存设置。另将盒体白区识别阈值统一为既有测试的 250/255，避免量化后的 `(253,253,251)` 白区被误排除。未为通过检查修改渲染或放宽颜色误差上限。

最终结果：

- C++ 编译成功，17 项构建动作，约 104 秒；既有 99 + 7 项回归测试通过。
- SM6 与 SM5 各 24 组新配色截图，均输出 `DIRECT_SPECULAR_CHROMA_PASS`。非金属红/蓝底色对原生高光的差异为 0；彩色灯、金色金属、蓝色金属、双光源混色相对原生 RGB 比例的平均线性误差约为 0.00164 / 0.00170 / 0.00196 / 0.00517，含 RT 与截图量化误差。
- 白色染色下有效区保持 0.12 的单档亮度（允许格式量化）；染色/强度不改变遮罩，强度减半、预曝光 1→4、生产合成与独立预览、强度 0、无光照及固定颜色回退均通过。
- 重新运行 32 组原固定颜色模式回归，通过 `DIRECT_SPECULAR_IMAGE_PASS`；颜色误差约 0.001405，最终漫反射与高光叠加误差约 0.0000166。该场景的普通阴影对照仍不具有有效覆盖，不重复宣称本轮验证了所有阴影路径；原生高光导出未变，专用遮挡验证见第一版记录。
- 本轮最终验收合计 80 组截图（不计排查 Gamma 时的首轮截图），不保存测试关卡或材质。启动阶段仍有已有的两条 AutomationTest 条件错误，脚本均完成，未见本轮 Shader 编译错误。此轮不包含移动镜头稳定性或城市场景法线简化验收。

## 原生间接镜面反射的 Toon 作用范围修复（2026-10-25）

现象：关闭盒体仍没有 Lumen 倒影；GI 与反射都为 Lumen 时无倒影，将 GI 改成无后反而可回退到 SSR。检查发现 `DiffuseIndirectComposite.usf` 在最终累加前无条件执行 `IndirectLighting.Specular = 0`。该行位于自定义 Toon 分支之外，基线提交 `9fdaae0` 中已存在，会清掉经过该非 Substrate 合成路径的普通材质镜面贡献；现有历史无法确定基线之前的作者意图。

用户确认希望只影响自定义 Toon Shading Model。修复是在原位置增加 `Material.GBufferData.ShadingModelID == SHADINGMODELID_TOON` 条件：Toon 材质沿用原有禁用策略，Default Lit 等其他材质保留已经计算的原生间接镜面结果。使用外层有效的 `Material.GBufferData`，避免引用前面局部作用域的 `GBuffer`。

这是原生间接光合成的作用范围修复，不是新增盒体环境反射风格化。直接高光、间接漫反射导出及独立 AO 算法未改；恢复后的环境反射仍按盒体现有的剩余光照合成规则处理。本次只修改 USF，不需要重新编译 C++。正在运行的编辑器可执行 `RecompileShaders Changed` 加载，或重启后加载新 Shader。

验证：在独立 D3D12/SM6 测试进程中关闭盒体、启用 Lumen GI 与反射、关闭 SSR，创建临时低粗糙度金属平面及红/绿自发光物体。分别使用 Default Lit 与自定义 Toon 材质，对比 `r.Lumen.Reflections.Allow` 开关，共四组 960×540 截图。普通材质出现清晰红球/绿方块倒影；选定倒影区域的开关差异为平均 17.514/255、4905 个像素差异超过 10/255。Toon 材质相同区域开关差异为 0，保持禁用策略。结果为 `TOON_REFLECTION_SCOPE_PASS`。Shader 编译与运行完成，未保存场景或材质；仅启动阶段出现前述已有的两条 AutomationTest 条件错误。此验证覆盖普通非 Substrate 的 Lumen 合成路径，未将结果泛化为透明水体或所有反射路径。
