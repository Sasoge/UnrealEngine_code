# Toon 光照调试面板

更新：2026-10-26。第一版提供独立的编辑器调试入口，沿用已有光照输出与公开索引。

## 打开与使用

- 在编辑器“窗口”菜单打开 **Toon 光照调试 / Lighting Debugger**；也可输入控制台命令 `Toon.Debug.Open`。
- 这是可停靠的编辑器标签页，可以拖到视口旁或作为独立窗口使用。场景图像仍由原场景视口显示。
- 顶部选择目标盒体；从列表手动选择会锁定目标。勾选“跟随选择”后，选中另一个盒体会切换目标；选普通物体时保留上一个有效目标。
- 左侧按功能选择分类，右侧单击视图名称立即预览。搜索支持编号、中英文名称及枚举名，跨分类生效；清空搜索后回到所选分类。
- 点击 ☆ 收藏／取消收藏；收藏和面板语言保存在本机编辑器项目偏好中，不写入关卡。
- **正常画面**：临时选择 0，让当前编辑器场景的盒体按正常输出模式合成。即使组件此前保存了调试图，也可立即看正常结果。
- **上一视图**：在最近两张不同视图间切换，方便对照。
- **退出调试**（或面板内按 Esc）：解除临时覆盖，恢复组件和全局控制台原本的选择。关闭标签页也会解除覆盖。如果原本保存的就是某张调试图，退出后会恢复那张图。

当前受支持的完整索引见[调试索引表](deferred-toon-debug-view-indices.md)。旧版隐藏视图 103/104 不在面板中重复开放。

## AO 子诊断

选择 **51 · 遮蔽二分遮罩** 后，下方出现六个按钮：

| 模式 | 内容 |
|---|---|
| 0 | 最终二分遮罩，白为无遮挡 |
| 1 | 滤波后的连续可见度 |
| 2 | 有效样本支持情况 |
| 3 | 深度导出的几何法线 |
| 4 | 接受的历史 |
| 5 | 历史拒绝原因 |

这些按钮只改变本次预览，不写入 `r.Toon.DeferredVolumes.AOFilterDebug`。53 用于检查来源是否有效，54 用于识别来源；42 是整层间接漫反射二分，不能当作 AO 二分。

## 临时覆盖与作用范围

1. **不写组件属性**：不调用 `SetDebugView`，不修改 `DebugView`、光照参数、全局 DebugView 或 AOFilterDebug，也不因此将关卡标成未保存。
2. **精确目标身份**：目标以 `FObjectKey` 的对象索引与序列号匹配场景代理，不依赖可重复的 Actor 名称或 ObjectID。对象删除重建后不能继承旧目标的覆盖。
3. **仅在包含该目标的编辑器场景中生效**：同一场景的编辑器视口共享预览；不承诺只影响一个活跃视口。场景捕获、反射捕获以及不同世界不接收该临时覆盖。开始 PIE 时退出预览。
4. **重叠盒体**：调试图大多输出整屏缓冲。预览非零视图时只运行目标盒体的诊断合成，防止其他重叠盒体把调试图再次调色或覆盖。正常画面 0 则仍合成全部盒体，临时强制它们使用正常输出。
5. **恢复与刷新**：切关卡、目标失效/停用、进入 PIE、关闭面板及模块卸载会释放预览。只在目标／诊断选项变化及退出预览时刷新相关世界的显示像素，不清除 Hit Proxy 缓存，也不强制打开任何视口的实时渲染。需要观察时域 AO 连续变化时，由用户开启场景视口的“实时”选项。
6. **没有有效目标时不伪造结果**：盒体未启用、未注册或处于 PIE 时不能开始预览；全局关闭 `r.Toon.DeferredVolumes` 会显示提示，不擅自开启渲染。
7. **面板接管优先于组件和全局选择**，解除后原来的选择自然恢复；不需要先将组件改为“跟随控制台”。

第一版只切换诊断视图与 AO 子模式，不编辑阈值、颜色、强度等着色参数。也不提供双视口同时显示或运行时游戏 UI。

## 实现入口

| 文件 | 职责 |
|---|---|
| `Engine/Plugins/Editor/DeferredToonDebugger/DeferredToonDebugger.uplugin` | 默认启用的 Editor 插件，不进入运行时游戏模块 |
| `Source/DeferredToonDebugger/Private/DeferredToonDebuggerModule.cpp`（插件内） | Window 菜单、Nomad 标签页、打开命令及关闭清理 |
| `Source/DeferredToonDebugger/Private/SToonLightingDebugger.cpp`（插件内） | 分类、搜索、收藏、双语、目标选择、按需显示刷新 |
| `Engine/Source/Runtime/Engine/Public/DeferredToonDebugPreview.h` 与对应 Private 实现 | 游戏线程提交、渲染线程读取的临时状态，带面板所有权 |
| `Engine/Source/Runtime/Engine/Public/DeferredToonDebugViews.h` | 复用的公开索引和双语视图名称 |
| `Engine/Source/Runtime/Engine/Public/DeferredToonVolumeSceneProxy.h` | 保存目标身份键；渲染线程只比较，不解析或访问 UObject |
| `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessDeferredToonVolumes.cpp` | 按目标、场景和视图类型解析覆盖，选择 AO Pass 与 Shader 分支 |
| `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp` | 识别本视图的临时诊断，沿用原调试显示路径 |

覆盖状态通过 Render Command 按序提交；渲染线程只比较身份键，不访问组件 UObject。关闭时只清除自己的 OwnerId，避免旧标签页延迟析构清掉新面板的预览。基础 RT、着色计算、AO 降噪算法及已有序列化 ID 均未改变。

## 验证记录

2026-10-26：

- `UnrealEditor Win64 Development` 编译成功，包含 Engine、Renderer 及新编辑器插件。
- `Engine.DeferredToon.DebugViewIndices`、`Engine.DeferredToon.DebugPreviewOwnership` 通过：验证索引转换、目标身份、旧面板释放不干扰新面板，以及组件属性、包脏状态和全局 CVar 不变。
- `/Game/NewMap` 上的 `Editor.DeferredToon.DebuggerPanel` 通过：验证目标绑定、上一视图、跨分类中英文搜索、收藏过滤、双语重建、正常输出，以及 PIE／切关卡／释放预览恢复。
- 实际打开标签页检查入口与目标菜单；以真实 Slate 控件离屏渲染 720×640 的中英布局，检查分类、长说明换行及六个 AO 按钮。布局图写入 `Project/test/Saved/Automation/ToonDebugger/panel-zh.png` 和 `panel-en.png`，不跟踪生成图片。
- 既有工作区 Python 检查 99 项、仓库光照分档检查 7 项通过；`git diff --check` 通过。

集成测试使用 `UnrealEditor-Cmd.exe test.uproject /Game/NewMap -RenderOffscreen -unattended -ExecCmds="Automation RunTests Editor.DeferredToon.DebuggerPanel" -TestExit="Automation Test Queue Empty"`。测试地图须包含启用的盒体；运行前释放交互预览。此测试覆盖控件行为与临时状态，不等于所有光照缓冲的逐像素视觉回归。既有缓冲提取与着色算法未修改。


## 2026-10-26：面板刷新开销修正与常规视口调查

用户反馈更新后界面卡顿和掉帧。检查发现初版 `Tick` 每 0.25 秒调用 `SetRealtime(true)`：遍历所有关卡视口，对同世界视口添加强制实时覆盖，并对所有视口调用无参 `Invalidate()`。UE 的默认第二参数 `bInvalidateHitProxies=true`，使鼠标拾取缓存反复失效；后续鼠标拾取可能重绘和读回 Hit Proxy，复杂场景的开销更明显。这是已确认的多余工作；尚未通过用户场景帧时间对照证明它是全部掉帧的唯一原因。

修正：

- 删除强制实时覆盖，完全尊重编辑器原本的实时开关。
- 空闲 Tick 只检查目标有效性与跟随选择，不触发重绘。
- 仅在预览选项变化或释放时，对目标世界的视口调用 `Invalidate(false, false)`，只更新显示，不清除拾取缓存。
- 未接管预览时反复关闭／设置目标不再提交无效释放命令或重绘。
- 增加自动化回归：临时将目标世界一个视口设为非实时，验证预览、空闲 Tick、退出后仍保留非实时状态；测试结束移除测试自身覆盖。

验证：`UnrealEditor Win64 Development` 编译成功；更新后的 `Editor.DeferredToon.DebuggerPanel` 集成测试通过，包括非实时视口的状态保护；`git diff --check` 通过。本轮不改变 AO 或光照计算质量。

用户随后确认“不打开调试面板也卡顿”。此反馈不能用面板的 Tick 单独解释。代码检查显示未创建面板时没有面板 Tick，且预览状态默认 inactive；渲染分支回落原组件／CVar 选择，未增加纹理或光照 Pass。插件禁用对照只能隔离编辑器插件，不能当作切回旧 Renderer 二进制的整版本 A/B。

后台性能对照使用 `/Game/CartoonStreet/Levels/Level_Day` 保存的同一相机位置与旋转、相同编辑器布局、`-RenderOffscreen -csvGpuStats`，启动完成后再预热 20 秒，每组采样 360 帧。用 Slate 回调请求视口绘制，使原本非实时视口也产生场景渲染；三组采用相同策略。未移动鼠标或交互拾取，不能代表用户前台操作或实际显示分辨率下的绝对 FPS。最初未连续绘制场景的试跑数据已排除。

| 条件 | 平均帧时间 ms | 中位数 ms | P95 ms | 平均 GPU ms |
|---|---:|---:|---:|---:|
| 插件加载、面板未打开（刷新修复后） | 21.810 | 17.472 | 30.223 | 7.049 |
| `-DisablePlugins=DeferredToonDebugger` | 21.999 | 17.372 | 30.235 | 7.494 |
| 插件加载、面板未打开，`r.Toon.DeferredVolumes=0` | 21.976 | 17.865 | 30.095 | 7.314 |

该采样未发现插件加载与禁用之间的明显回归，也未通过关闭盒体合成消除帧时间波动。`r.Toon.DeferredVolumes=0` 只停盒体后处理，不停上游光照导出，不能据此排除整个 Toon 渲染管线的成本。当前不将“面板未打开时仍卡顿”标为已修复；前台鼠标交互、视口分辨率与旧版本对照仍未覆盖。

日志还显示 `r.Shaders.Optimize=0`、`r.Shaders.Symbols=1`，启动时导出约 4.8 GB 调试符号；这些来自基线配置，更新前的日志同样存在，不能作为这次回归的证明。未改动这些配置或降低渲染质量。测试结束关闭所有测试进程，并恢复采样前的编辑器项目偏好。

证据：工作区 `outputs/debug-panel-perf-build.log`、`debug-panel-perf-test2.log`、`toon-perf-loaded-render.log`、`toon-perf-disabled-render.log`、`toon-perf-comparison.json`；原始 CSV 位于 `Project/test/Saved/Profiling/CSV/toon_perf_{loaded_render,disabled_render,volumes_off}.csv`。生成数据不纳入 Git。
