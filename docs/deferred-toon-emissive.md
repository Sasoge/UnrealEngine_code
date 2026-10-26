# 盒体自发光独立输出

> 调试编号说明（2026-10-26）：本文保留当时的旧编号，运行命令和界面请按[新版索引对照表](deferred-toon-debug-view-indices.md)换算；`AOFilterDebug` 子模式编号不变。

日期：2026-10-26。

## 使用与语义

输出模式选择「直接光调色」，在「风格化 | 自发光」开启「独立调整自发光」。强度 0 去除提取出的可见发光，1 保留原生 HDR 颜色和强度。即使「保留其他光照」为 0，自发光也能独立显示；默认关闭以保留已有场景行为。

调试视图 43 显示原始材质自发光，44 显示可用范围（白=有效，黑=未提供）。有效且没有发光的表面是 RGB=0、A=1，不应把它视为缺失。预览不随作者强度变化；最终效果请返回正常输出。面板支持中英文，提供 Blueprint setter。

这控制的是当前可见表面的发光分量，不改材质表达式、Lumen Surface Cache 或反射场景。将强度设为 0 不表示该物体停止照亮其他物体，也不保证它从镜子里消失。后续 Bloom、曝光与色调映射仍生效；本阶段没有把 Bloom 单独拆成一层。

## 数据来源

在 `BasePassPixelShader.usf` 从 `GetMaterialEmissive(PixelMaterialInputs)` 直接取得材质发光，写入 `FSceneTextures::ToonEmissive`。不能把 Base Pass 的整个 SceneColor 当成自发光，因为其中可能已有 Lightmap、VLM 等光照。

RGB 保留预曝光，与 SceneColor 使用相同空间；A 是可用性。使用 RGBA16F，不重复乘底色、F0 或灯光颜色。材质纹理、UV、顶点工厂和 WPO 等仍由原生 Base Pass 求值。MRT 写入继承原生遮罩与深度测试，不用无序 UAV 写入可见性，因此被遮住的表面不能覆盖前景发光。

通过传统 GBuffer 布局末尾的附加 MRT 导出，不改已有 GBuffer 编码。正常不透明布局最多使用七个原生目标，附加后不超过八个。编译器输出掩码、格式、渲染绑定、PSO 以及 Nanite 输出绑定保持一致；GBuffer DDC 版本递增，避免复用旧材质布局。首次启动需要重新编译受影响的材质 Shader。

## 覆盖与限制

目标路径为桌面延迟、传统 GBuffer 的不透明与 Masked 材质，包含 Lit、Unlit 与 Nanite。透明、SingleLayerWater、天空材质、Hair、需要独立散射的 SSS、Substrate、移动和 Forward 不作为该层的有效输入。水体继续使用自身的额外 MRT，绑定及 PSO 会移除自发光槽；后续水面覆盖旧不透明像素时，消费侧也拒绝旧发光数据。

这是表面 Base Pass 材质发光，不包含后续独立 Decal Pass 添加的自发光，也不从残差提取透明粒子发光；这些内容仍保留在未拆分贡献中。允许负 Emissive 的特殊材质不作为完整的有符号分量拆分：该层只提取非负发光。

Unlit 通常没有有效法线。仅在开启独立自发光且有有效导出时，允许其参与盒体范围计算，并跳过直接光明暗替换，避免给纯发光材质额外叠加暗部色板。

附加纹理采用固定 Base Pass 布局：在受支持的延迟场景中分配与写入，**并非只有开启盒体时才产生 GPU 成本**。1080p RGBA16F 约占 15.8 MiB，另外增加 MRT 写入带宽。暂未测量 GPU 时间；后续可考虑项目级编译开关或专用布局来裁剪。

SM5 的像素 UAV 与 MRT 共用八个输出槽。开启虚拟纹理时预留一个反馈槽；若原生布局已经占七个 MRT，则该配置不启用自发光导出，视图 44 为黑并沿用原合成，避免破坏虚拟纹理。SM6 路径不受此 SM5 槽数限制。

## 合成

以原始 SceneColor 扣除直接漫反射后的剩余量为预算 R。独立开关开启且输入有效时，E = min(max(EmissiveRT.rgb, 0), R)。先扣除 E，再进行直接高光、环境反射和间接漫反射分离，最后加回 E × 自发光强度。其他光照权重、漫反射调色和 AO 不会再次处理 E。

保持其他光照=1、强度=1 时，扣除与加回相互抵消。无输入时沿用原合成，不能从残差猜测发光。雾等后续处理会改变原生贡献，所以有界提取保证不扣出负数，但不是雾透射后的严格可逆分解。

## 验证入口

`Tests/DeferredToon/verify_emissive_editor.py` 使用临时 Lit/Unlit/Masked 材质检查导出颜色、光源开关独立性、强度 0/0.5/1、曝光以及遮罩裁切。SM6 另建专用测试网格副本启用 Nanite，结束时删除该副本；不保存测试关卡或材质。


## 2026-10-26 验证记录

- UnrealEditor Win64 Development 构建通过；本轮新增公开 GBuffer 绑定，首次构建为 522 个动作。修正编译环境后增量构建通过。
- 已修复一个导出链路问题：材质共享编译环境会在单个 Shader 环境之后合并，不能在共享环境写入禁用的 `TOON_EMISSIVE_MRT_INDEX` 默认值，否则它会覆盖 Base Pass 的有效槽号。DDC 版本同步更新。
- D3D12 / SM6：Lit、Unlit、Masked、Nanite 导出、光源关闭独立性、HDR、强度 0/0.5/1、曝光恢复和水面无效性通过图像检查。中性合成 MAE 为 0.1236/255，半强度误差为 0.3952/255。
- D3D11 / SM5：同类普通网格检查通过；中性合成 MAE 为 0.0015/255，半强度误差为 0.3808/255。SM5 不使用 Nanite 测试。
- HDR 测试使用材质颜色 `(2, 0.6, 0.2)`、预曝光 0.25，自发光强度 0.25 时输出 `(0.5, 0.15, 0.05)`。PNG 在 Gamma-only 显示路径仍会去掉预曝光，因此不能把 PNG 亮度直接当作原始 RT 数值。原始 HDR 的红通道大于 1，缩放后验证不会被过早截成 1。
- 常规工作区 99 项及仓库 Light Bands 7 项回归通过。
- 编辑器启动仍出现此前已有的两条 `LogAutomationTest: Condition failed`；本任务未修复这些启动日志。水材质测试最初缺少必需输出节点，曾回退默认材质；补齐节点输入后重新验证通过，不能把此前回退画面算作验收。

自动化入口：从引擎仓库运行 `UnrealEditor.exe Project/test/test.uproject /Game/NewMap -d3d12 -RenderOffscreen -unattended -nop4 -nosplash -NoSound -ExecutePythonScript=<绝对路径>/Tests/DeferredToon/verify_emissive_editor.py`。SM5 改为 `-d3d11 -sm5 -EmissiveSM5`。测试需要 NewMap 中的 Sphere、Cube 和盒体，修改不保存。

使用带 NumPy、Pillow 的 Python 执行 `Tests/DeferredToon/check_emissive_images.py Project/test/Saved/Automation/Emissive`；SM5 使用 `EmissiveSM5 --sm5`。水面路径可用 `-EmissiveWaterOnly` 单独复测，它只覆盖两张水面截图，其余截图仍来自完整运行。


补充兼容性边界：另启动了 SM5 的 `r.AllowStaticLighting=True`、`r.VelocityOutputPass=1` 配置，验证上限八个 MRT 的布局。只通过命令行 `-ini:Engine:[/Script/Engine.RendererSettings]:r.AllowStaticLighting=True,[/Script/Engine.RendererSettings]:r.VelocityOutputPass=1` 覆盖，没有改项目配置。该配置首次全局 Shader 编译超过十分钟仍未进入场景，因此停止额外测试，**未完成该组合的运行验收**，不能将它列为已通过。SM5 虚拟纹理槽不足的回退也只完成代码审查，未在开启 VT 的项目实测。
