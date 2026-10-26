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
5. **恢复与刷新**：切关卡、目标失效/停用、进入 PIE、关闭面板及模块卸载会释放预览。预览期间添加本工具专属的视口实时刷新覆盖，退出时仅移除自己的覆盖，不保存实时开关。
6. **没有有效目标时不伪造结果**：盒体未启用、未注册或处于 PIE 时不能开始预览；全局关闭 `r.Toon.DeferredVolumes` 会显示提示，不擅自开启渲染。
7. **面板接管优先于组件和全局选择**，解除后原来的选择自然恢复；不需要先将组件改为“跟随控制台”。

第一版只切换诊断视图与 AO 子模式，不编辑阈值、颜色、强度等着色参数。也不提供双视口同时显示或运行时游戏 UI。

## 实现入口

| 文件 | 职责 |
|---|---|
| `Engine/Plugins/Editor/DeferredToonDebugger/DeferredToonDebugger.uplugin` | 默认启用的 Editor 插件，不进入运行时游戏模块 |
| `Source/DeferredToonDebugger/Private/DeferredToonDebuggerModule.cpp`（插件内） | Window 菜单、Nomad 标签页、打开命令及关闭清理 |
| `Source/DeferredToonDebugger/Private/SToonLightingDebugger.cpp`（插件内） | 分类、搜索、收藏、双语、目标选择、实时刷新覆盖 |
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
