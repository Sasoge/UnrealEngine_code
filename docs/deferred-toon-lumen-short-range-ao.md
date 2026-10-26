# Lumen 短程 AO 独立控制（第一阶段）

2026-10-17 面板更新：日常控制现已并入 [间接漫反射遮蔽统一入口](deferred-toon-unified-occlusion.md)。本文的数据范围、原始调试视图和限制继续有效；旧分路参数在兼容模式的高级设置中保留。

日期：2026-10-16。承接[间接漫反射](deferred-toon-indirect-diffuse.md)和[天光遮蔽](deferred-toon-sky-occlusion.md)。本次只增加 Lumen 独立短程漫反射 AO 的预览、强度控制。默认不启用，不改变已有资产效果；没有新增颜色、纹理槽或自定义时序历史。

## 诊断依据与边界

在 `2026.10.16-19.18.16_capture.rdc` 的 Level_Day 捕获中，墙后大面积暗部已经存在于 Lumen ScreenProbeGather 积分结果，早于 TemporalReprojection、DiffuseIndirectComposite 和盒体处理。同一区域的独立短程 AO 约为 1。因此，大范围 GI 暗影不能通过关闭短程 AO 消除。

短程 AO 图主要包含墙脚、箱格及物体接触处的细节遮蔽。本功能只控制这项独立因子对间接漫反射的影响，保留 GI 追踪产生的明暗、材质响应、颜色与能量守恒，不改变直接光和高光。

## 输入与导出

在 `DiffuseIndirectComposite.usf` 中，保留原生最终 `ToonIndirectDiffuseOutput`，额外输出 `ToonLumenDiffuseBeforeShortAOOutput`：

| 通道 | 内容 |
|---|---|
| RGB | 短程 AO 乘法前的预曝光间接漫反射，包含相同材质响应和 GI |
| A | 原始短程可见性，1 无遮蔽、0 完全遮蔽；-1 表示不支持 |

Bent Normal 模式取 `saturate(length(UnpackScreenBentNormal(...)))`；scalar 模式直接读取标量。沿用原生已上采样/时序处理后的输入，不自行重建或累积 AO。

**A 不是最终 RGB 遮蔽倍率。** 原生还应用材质相关多次反弹补偿、植被遮蔽强度等，因此不把 RGB 除以 A。直接保存真正乘法前的照明，可以处理 A=0，也保留原生多次反弹颜色。既有 Toon Lumen 的亮度/颜色平坦化继续保留；关闭短程 AO 只去掉其 `ToonOcclusion` 乘法效果。

支持传统 GBuffer 的 Lumen 最终合成路径（`ShortRangeAOMode` 为 1 或 2）。Hair 接收表面、需要后续独立次表面散射的像素、Unlit/背景、Substrate 不提供有效拆分。普通非 Lumen SkyLight 继续走原有天光遮蔽接口。

`r.Lumen.ScreenProbeGather.ShortRangeAO.ApplyDuringIntegration=1` 会把 AO 提前融入积分并将合成阶段模式清零，本阶段对此正确回退，不冒充“AO 前”数据，也不自动修改用户全局配置。关闭短程 AO 同样回退；SSAO fallback 不属于这项控制。

资源 `FSceneTextures::ToonLumenDiffuseBeforeShortAO` 为 RGBA16F，只在场景有盒体、正常 Lumen 渲染且原生合成阶段提供独立短程 AO 时分配。每帧首次分配全尺寸清为 `(0,0,0,-1)`；所有盒体共享。RDG 显式跟踪 UAV 写入、SRV 读取；没有改变 Pass 顺序。1920×1080 约增加 15.8 MiB，加一次清理和导出/采样。尚无专项 GPU 性能测量。

## 操作

1. 输出模式选择“直接光调色 / Direct Light Grade”。
2. 在“间接漫反射”启用“独立调整间接漫反射”。观察 AO 时使用强度 1、白色、颜色替换 0；颜色替换 1 会覆盖间接照明差异。
3. 展开“风格化 | Lumen 短程 AO”，启用“独立调整短程 AO”。
4. “短程 AO 强度”：1 保留原生，0 去掉独立短程 AO，0.5 在两种实际照明结果间插值。保持 0—1 范围，本阶段不提供加重或调色。

界面新增两个字段，默认折叠，提供中英文与工具提示。Blueprint setter 更新场景代理。旧枚举编号不变。

| 调试 ID | 名称与含义 |
|---|---|
| 30 | Lumen 短程 AO：白无遮挡、黑遮蔽；无数据处使用白色 |
| 31 | Lumen 漫反射（短程 AO 前）：仍保留 GI 追踪阴影；无数据黑色 |
| 32 | Lumen 短程 AO 数据有效性：白有效、黑无效 |

预览原始输入不随盒体强度变化；要观察强度效果，回到输出预览 0。用 32 区分“AO 为白”和“没有接入”。遮蔽/有效性预览匹配 PreExposure，但仍经过 UE 后续色调映射。

## 合成与兼容

沿用原有间接光残差预算。N 为原生最终间接光，U 为短程 AO 前间接光，R 为 SceneColor 扣除直接漫反射后的非负残差；以下 RGB 均为预曝光颜色：

```text
E = min(max(N, 0), R)
K = N > epsilon ? E / N : 1       // 分通道残差预算，不是除以 AO
AdjustedIndirect = max(E + (max(U, 0) - max(N, 0)) * K * (1-strength), 0)
```

只在独立间接光和短程 AO 开关启用、数据有效时执行。之后走原有间接光调色，剩余残差仍只扣除 E 一次。原有天光与 Lumen provider 在同一像素互斥；天光参数不会作用于 Lumen。强度 1 严格回到原生 E，不向已遮蔽结果再乘一次 AO。完全遮蔽处 N=0 时仍能恢复 U。

此处保留既有对雾、透明等后续合成的残差预算限制，并非对这些复杂合成精确逆运算。

## 验证

`Tests/DeferredToon/verify_indirect_diffuse_editor.py -ToonLumenShortAO` 在临时、不保存的场景中覆盖：中性兼容、0/0.5/1 强度、真实 AO 空间变化、曝光、强制 copy 合成、直接分档隔离、TSR 降分辨率、Bent Normal/scalar 两种编码、关闭 AO、提前积分模式及非 Lumen 回退、模式恢复。图像断言见 `check_lumen_short_ao_images.py`。

### 已完成验证

- UnrealEditor Win64 Development 编译成功：67 项，约 218 秒；恢复两份中文源文件原有 UTF-8 BOM 后，增量编译 6 项再次通过。
- 106 项既有回归检查通过。更新了工作区 3 个旧调试上限契约，保留原有断言。
- D3D12/SM6 离屏运行完成 27 阶段，`INDIRECT_DIFFUSE_GPU_PASS`，正常退出；图像断言输出 `LUMEN_SHORT_AO_IMAGE_PASS`。
- 中性开关前后 8 位 RGB MAE 约 0.261；直接分档预览在 AO 调节前后约 0.027。
- 关闭短程 AO、非 Lumen、提前积分路径的开关回退 MAE 分别约 0.203、0.140、0.225，没有残留有效数据。
- AO 前照明与原生间接光 MAE 约 0.960；强度 0 与 1 的最终输出 MAE 约 0.377。在存在明确变化的像素中，0.5 的平均亮度位于两端之间。
- AO 前照明预曝光 1→4 / 恢复的 MAE 约 0.864 / 0.992；强制 copy 合成约 1.165；短程 AO 曝光对照约 1.500。均在既定容差内，未因失败调宽容差。
- 已查看实际预览图：30 是接触处短程遮蔽；31 保留原生 GI 明暗；没有新增条纹或矩形。验证了 scalar/Bent Normal 编码、模式恢复以及 TSR 降分辨率输出。

首次测试脚本使用全局调试命令，但组件仍选择本地输出预览，因此诊断截图无效，图像断言拒绝通过。已修正脚本显式切换 ConsoleGlobal 后重跑完整流程；最终结果以上述第二次运行和实际预览图为准。

测试只改变独立进程中的临时场景，没有保存资产。日志仍有两条既有启动 `LogAutomationTest: Error: Condition failed`，没有新增 Shader/PSO 致命错误；不宣称日志完全无错误。多视图、复杂材质及 GPU 性能尚未专项实测；SM5 未专项运行，Lumen 本身使用本次 SM6 路径。

日志位于工作区 `outputs/lumen-short-ao-build.log`、`lumen-short-ao-build-final.log`、`lumen-short-ao-regression.log`、`lumen-short-ao-d3d12-final.log`、`lumen-short-ao-image-check.log`；首次脚本诊断日志保留为 `lumen-short-ao-d3d12.log`。截图位于测试工程 `Saved/Automation/DeferredToonLumenShortAO/`，不提交这些运行产物。
