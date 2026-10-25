# 间接漫反射与环境镜面反射独立查看

日期：2026-10-26。

## 使用

盒体组件的「调试 → 缓冲视图预览」：

| 视图 | 内容 |
|---|---|
| 25 - 间接漫反射 | 已接入的 Lumen 漫反射或非 Lumen 动态天光漫反射 |
| 26 - 间接漫反射可用范围 | 白色表示该像素有受支持的漫反射输入 |
| 41 - 间接镜面反射（环境反射） | 原生环境反射最终贡献，包括当前路径的倒影与粗糙反射 |
| 42 - 间接镜面反射可用范围 | 白色表示该像素有受支持的镜面输入 |

视图 18 仍是直接光高光，39/40 仍是它的硬边遮罩与风格化结果。41 是另一层，不是直接光高光。

这次只增加独立数据和查看入口，未添加环境镜面反射调色参数。最终画面合成沿用现有规则；「保留其他光照」仍控制残余贡献中的环境反射。可继续使用间接漫反射独立调色，不会将新镜面 RT 当作漫反射输入。

## 提取位置

不是用 SceneColor 减去漫反射来估算反射，而是在原生合成阶段导出对应分量：

* Lumen GI 路径：`DiffuseIndirectComposite.usf` 的 `IndirectLighting.Specular`，位于原生遮蔽、材质响应、粗糙/追踪反射混合、SSS 检查及自定义 Toon 屏蔽之后，加入 SceneColor 之前。
* 非 Lumen / 独立反射路径：`ReflectionEnvironmentPixelShader.usf` 的 `ReflectionEnvironment(...)` 返回值，支持当前实际执行的 SSR、反射捕获、天空镜面及独立 Lumen 反射输入。动态天光漫反射仍在另一处导出。

后者的导出开关独立于动态 SkyLight，因此没有天光时仍能导出 SSR。两处按当前视图的原生合成路径选择，不重复累加。

`FSceneTextures::ToonIndirectSpecular` 与现有 `ToonIndirectDiffuse` 分开存储。RGB 是已带材质/F0/原生反射响应和预曝光的结果，不能再次乘底色或 F0。A=1 为有效，A=0 为未接入；零亮度不等于无数据。调试画面仍可能受到后续显示处理影响。

自定义 `SHADINGMODELID_TOON` 保留现有反射屏蔽策略：支持的像素 RGB 为零、A 为一。因此视图 41 黑、42 白在该材质上是正常的。

## 范围和成本

覆盖传统 GBuffer 的受支持延迟不透明表面。Substrate、Hair、需要独立扩散的 SSS、透明表面与 SingleLayerWater 的专用反射合成不在本次完整拆分范围；这些专用表面不能依赖 41 作为完整反射层。SingleLayerWater 明确标为无效，避免将不完整结果误认为完整水面倒影。

25 的范围没有扩大：独立 SSGI 反弹、Lightmap/VLM、Ambient Cubemap 等未接入来源仍不能宣称已完整拆出，详见 [间接漫反射设计](deferred-toon-indirect-diffuse.md)。

新增一张共享全分辨率 RGBA16F（8 字节/像素，1080p 约 15.8 MiB）RT。在存在盒体消费者、传统 GBuffer 路径下创建；每帧整张清零一次，原生 Pass 通过 UAV 写入，RDG 跟踪依赖。不是每个盒体各建一张。未做 GPU 时间基准。

## 验证

* UnrealEditor Win64 Development 编译通过。
* SM6/D3D12：Lumen 与非 Lumen 共 22 个截图阶段；SM5/D3D11：非 Lumen 9 个阶段。
* 纯金属地面：25 为零，41 能看到红球和绿方块的反射。关闭 Lumen 反射后物体倒影消失，天空回退仍可存在；恢复后及强制 Copy 合成路径均恢复倒影。
* 非 Lumen：移除 SkyLight 后，25/26 无数据，41 仍有 SSR、42 有效。反射输入也关闭后，41/42 为黑。
* 自定义 Toon：41 为零，42 有效。新增导出前后的中性最终合成进行图像差异检查。
* `verify_indirect_split_editor.py` 只修改临时场景状态，不保存资产；`check_indirect_split_images.py` 验证上述图像契约。SSR 使用屏幕颜色与历史，调试视图中的反射颜色不作为真实场景配色断言。
* 现有工作区 99 项测试和 Light Bands 7 项测试通过。运行日志在编辑器初始化时仍有两条无详细定位的 `LogAutomationTest: Condition failed`，均早于本次 Python 测试；截图流程正常完成，没有全局 Shader 编译失败。不能把这两条启动日志宣称已修复。
