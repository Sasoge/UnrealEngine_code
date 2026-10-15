# 盒体操作面板整理

日期：2026-10-16。基于 `06ffc12` 的多档直接漫反射映射。

## 日常操作顺序

1. **Toon | Output**：启用盒体，选择输出模式，调整 Effect Strength。Direct Light Grade 下的 Keep Other Lighting 控制是否保留原画面的其余光照。
2. **Toon | Light Bands**：选择二分或多档。多档编辑 Starts At（受光下限）和 Level（色板坐标）。数组标题同时显示两者，不再只显示容易误解的单个数值。
3. **Toon | Colors**：选择配色，调整亮暗替换程度以及材质底色影响。
4. **Toon | Textures**：需要阴影贴图时再展开；指定 2D 纹理后才出现混合、平铺和偏移。3D 体积纹理放在本组高级属性中。
5. **Toon | Box & Blending**：调整盒体半尺寸和边缘羽化。羽化曲线和重叠优先级放在本组高级属性中。

前三组默认展开，其余组默认收起。编辑器可能记住用户之后的展开状态。

## 配色入口

- **Light Endpoint Curve / Shadow Endpoint Curve**：原来的 Lighting Color Curve / Shadow Color Curve。生产模式下各取时间 1 的颜色；空值分别为白、黑。
- **Band Palette Curve**：原来的 Light Band Color Curve。在每档 Level 处采样。启用有效多档并指定色板时，两个端点曲线自动隐藏；清空色板后恢复。无效数组（少于两档）仍显示二分阈值和端点，便于修正。
- **Use Custom Color**：单档覆盖色，优先于色板和端点。各档的 Shadow Texture Influence 是高级属性，仍然可以独立调整。
- **Light / Shadow Color Replacement**：0 为原生直接漫反射乘目标色，1 为目标色替换，中间档按 Level 插值。
- **Material Color Influence**：控制替换色乘材质底色的程度。纯乘色模式下不显示，因为该参数不参与运算。
- **Keep Other Lighting**：保留原生场景减去有效直接漫反射之后的残差；它不是已经独立提取出来的 GI。可能改变看到的色板颜色。

本次没有增加另一个配色开关；曲线存在性和单档覆盖已经决定颜色来源，避免同一件事需要两处配置。

## 辅助功能

- **Toon | Debug**：Buffer Preview 对应原来的 Debug View，保留全部既有编号以及 Console / Global 遥控器入口。
- **Toon | Advanced**：艺术阴影覆盖阈值及只读 Stable Sort ID。ID 仍可通过既有 API 设置，普通调色不需要修改。
- **Toon | Experimental Layers**：保留未来需要的 RT 分层、Style Profile 和 Inline Layers。只在层预览、对应调试视图或跟随全局调试时显示相关属性，默认收起。
- 内部 VolumeBounds 辅助组件不再作为可展开属性暴露其物理、碰撞和渲染设置。实际盒体范围仍由 Box Half Size 和 Transform 控制。

## 预设与兼容

预设按钮在选中 **DeferredToonVolumeComponent** 本身时显示；仅选中 Actor 时，可以直接调整属性，但 UE 不显示组件的 CallInEditor 按钮。

- **Reset to Four Bands**：原 Apply Four Light Bands，明确表示它会替换当前档位数组；保留其他配色/输出设置，支持撤销。
- **Use Two Bands**：原 Use Two Band Compatibility，关闭多档但不删除数组，支持撤销。
- 所有序列化属性名、枚举数值、默认值和 Blueprint 函数名称保持原样。更改的是详情面板显示名、分类、条件与编辑器定制。
- 不修改 USF、RT、渲染参数、光照公式、Pass 顺序或任何工程资产。旧场景无需迁移。

## 维护入口

- 属性标签、提示和简单显示条件：`DeferredToonVolumeComponent.h`。
- 分类顺序、默认折叠、端点及二分回退显示：`DeferredToonVolumeDetails.cpp`。
- 多选时只要任意选中组件需要端点或二分阈值，就保留对应控件。
- 编辑器定制通过 DetailCustomizations 模块标准注册/注销流程接入；运行时模块不依赖 Slate。

## 验证记录

- UnrealEditor Win64 Development 与 UHT 编译通过。
- 对照上次提交，组件和 Actor 的全部 UPROPERTY 声明名称、类型和初始化值相同；没有修改渲染源码。
- 在 test 工程实际详情面板检查 Actor 和组件两种选择方式，确认分类顺序、预设按钮、档位摘要与辅助分组。
- 临时关闭多档：数组及多档色板隐藏，二分阈值出现；撤销后恢复四档，地图回到已保存状态。
- 未指定纹理时只显示 2D 纹理入口；3D 入口在高级属性中，纹理混合/平铺/偏移条件隐藏。
- 检查结束正常关闭编辑器，没有保存工程资产。未覆盖所有多选/资产切换组合，复杂使用情况仍需实际项目反馈。
