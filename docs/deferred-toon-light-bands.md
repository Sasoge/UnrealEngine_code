# 多档明暗：操作、边界与验证

日期：2026-10-15。基于有效直接漫反射的既有输入，不改变逐灯积分、阴影通道或 Pass 顺序。

2026-10-16 界面名称整理：Light Bands 分类现显示为 **Toon | Light Bands**，Apply Four Light Bands 按钮改为 **Reset to Four Bands**，Use Two Band Compatibility 改为 **Use Two Bands**；Min Response / Value 显示为 **Starts At / Level**。下文中的程序字段名及计算不变，详见[操作面板说明](deferred-toon-authoring-ui.md)。

## 开始使用

1. 选中 `DeferredToonVolumeComponent`，Output Mode 选择 **Direct Light Grade** 或 **Toon Recompose**。
2. 在 **Light Bands** 分类点击 **Apply Four Light Bands**。它填入四条配置并开启 Use Light Bands，不修改已有色曲线、输出模式或其他光照权重。
3. 为观察纯色档位，Direct Light Grade 的两个 Color Replace Weight 都设为 1，Other Lighting Weight 和 Base Color Weight 设为 0；暂时关闭阴影纹理混合。Toon Recompose 本身就是替换输出。
4. 使用 **Debug 24 · Selected Light Band** 查看选区，再调每档 Min Response 和颜色。
5. **Use Two Band Compatibility** 恢复旧二档，保留已编辑的多档数组以便重新开启。已有场景默认不启用多档。

## 四档预设

| 参考受光量 E | Min Response | Value | 默认阴影纹理影响 |
|---|---:|---:|---:|
| 0 ≤ E < 0.1 | 0 | 0 | 1 |
| 0.1 ≤ E < 0.4 | 0.1 | 0.3 | 0.7 |
| 0.4 ≤ E < 1 | 0.4 | 0.6 | 0.4 |
| E ≥ 1 | 1 | 1 | 0 |

E 是场景线性的白色参考漫反射亮度，含逐灯遮挡、衰减、颜色和强度，可以大于 1。这些分界只是起点，不保证每个场景都会出现四档。若全亮或全暗，应先看 Debug 23 并调整分界或真实灯光；不要把 Value 当成 Min Response。

## 每档参数

- **Min Response**：进入该档的受光下限，含等号。
- **Value**：档位输出值，0 最暗、1 最亮；用于 Debug 24、颜色曲线坐标以及两个 Replace Weight 之间的插值，不直接乘到最终颜色上。
- **Override Color / Color**：该档使用独立线性颜色，优先于色板曲线和旧端点。
- **Shadow Mapping Weight**：该档对已有 2D / 3D 阴影纹理的接受权重，另乘全局纹理权重。设 0 则该档不接受阴影纹理；与 Value 独立。

颜色来源优先级：Override Color → **Light Band Color Curve** 在 Value 处采样 → 旧 Shadow/Lighting Color Curve 的时间 1 端点按 Value 插值。旧端点之间的插值只产生有限个档位目标色，不会在每个像素上产生连续光照渐变。

两个 Replace Weight 都设 1，所有档位都替换为目标色；都为 0，所有档位都对原生有效漫反射乘色；不同时按该档 Value 插值。例如暗端为 1、亮端为 0，Value=0.3 的档位替换权重为 0.7。

曝光和色调映射仍在后续运行，因此屏幕色块不保证等于曲线编辑器色块。若要求固定区内颜色，还需避免材质底色、纹理、残差与盒体边缘混合引入变化。

## 明确的异常／兼容规则

- 支持 2～8 档。少于两条配置时回退二档；超过八条只使用数组前八条。
- 运行时对这八条按非负下限稳定排序，不改写用户数组。第一个区间从 0 开始。
- 重复下限以后出现的条目为准；想得到清晰的 N 档应使用严格递增下限。
- E=0 强制第一档，即使多个下限都是 0；高于最后下限的受光留在最后一档，不截断到 1 再比较。
- 艺术 Shadow Channel 达到 Artistic Shadow Threshold 时选择第一档。普通物理遮挡已经计入 E，不再二次叠加。
- Value 与纹理权重夹到 0～1，非有限阈值／权重安全回退。作者仍可指定非单调的 Value 或色板；“第一档”不自动等于视觉上最暗的颜色。
- Effective Light Threshold 是二档兼容阈值，多档时使用数组分界。Debug 14/19/20 仍是二档参考，**Debug 24 才是当前多档输出**。关闭多档时，24 显示旧二档的亮值（与阴影遮罩极性相反）。
- Scene Color Tint / Style Layers Preview 不套用多档着色；Style Profile 仍为独立输入层预览，不隐式修改色板。

## 扩展和成本

不新增 RT，不新增时序历史，也不按档数创建 shader permutation。CPU 在组件／曲线更新时完成排序与颜色采样，代理上传最多八个参数 float4 和八个颜色 float4。GPU 进行最多七次阈值比较后选择一档，复用已有三平面纹理结果。

档位数据由 `FDeferredToonLightBand` 描述，Blueprint 有 SetLightBands、SetUseLightBands、SetLightBandColorCurve，以及两个预设函数。后续统一风格资产可复用此结构，不必再次改 RT 生产流程。

## 验证入口

- `python -m unittest discover -s Tests/DeferredToon`：直接从 HLSL 提取区间选择函数，验证边界、重复阈值、无光、艺术覆盖、正能量单调性与八档上限。
- `Tests/DeferredToon/verify_light_bands_editor.py`：在独立 UnrealEditor-Cmd 离屏进程运行。测试工程需要 Sphere、方向光、点光源和盒体；所有场景调整均不保存。截图写到该工程 `Saved/Automation/DeferredToonBands`。
- 建议命令：使用引擎 `Engine/Binaries/Win64/UnrealEditor-Cmd.exe`，传入工程的**绝对路径**、`-d3d12 -RenderOffscreen -unattended -nop4 -nosplash -NoSound` 和测试脚本绝对路径的 `-ExecutePythonScript=` 参数。

验证结果（2026-10-15）：

- UnrealEditor Win64 Development / UHT 编译通过，工作区日志 `outputs/light-bands-build-final.log`。
- 原有 99 项检查通过；新增 7 项 HLSL 标量区间检查通过。
- D3D12/SM6 离屏渲染 14 阶段完成并正常退出，日志 `outputs/light-bands-d3d12-verified.log` 包含 `LIGHT_BANDS_GPU_PASS`。没有保存工程资产。
- `check_light_band_images.py` 检查通过：四档在参考区域中分别覆盖 13,187 / 1,185 / 594 / 150,034 个像素；使用单值参考截图校准显示变换，确认中间两档存在离散色阶。
- 黑白材质、预曝光、数组重排、二档恢复的平均图像差异均小于 0.14 / 255；纯替换 Direct Light Grade 与 ToonRecompose 差异约 0.174 / 255，处于时序显示容差内。完全无光时 Debug 24 测试区域全黑。
- 已观察独立四色与灰度档位截图。UI 编辑、复杂材质／透明／雾、SM5 与性能基准仍需对应场景专项验证。

本次在 `feature/deferred-toon-light-bands` 分支开发，基线为 `2e281a0`（有效漫反射映射与旧路径清理）。功能与测试单独提交；不改变该基线的 RT 生产与清理流程。
