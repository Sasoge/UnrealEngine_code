// [DeferredToon] Public debug indices shared by console help and bilingual editor labels.
#pragma once

#include "Components/DeferredToonVolumeComponent.h"

namespace DeferredToonDebugViews
{
struct FEntry
{
    int32 PublicIndex;
    EDeferredToonDebugView View;
    const TCHAR* Name;
    const TCHAR* English;
    const TCHAR* Chinese;
    bool bHidden;
};

// Serialized enum values remain stable. New views get an unused public slot in their group.
inline constexpr FEntry Entries[] =
{
    // Output / 盒体与输出
    {0, EDeferredToonDebugView::NativeComposite, TEXT("NativeComposite"), TEXT("Selected Output"), TEXT("当前输出"), false},
    {1, EDeferredToonDebugView::BoxInfluence, TEXT("BoxInfluence"), TEXT("Box Influence"), TEXT("盒体影响范围"), false},
    {2, EDeferredToonDebugView::Priority, TEXT("Priority"), TEXT("Priority"), TEXT("优先级"), false},
    {3, EDeferredToonDebugView::ShadowComposite, TEXT("ShadowComposite"), TEXT("Shadow Composite"), TEXT("合成阴影"), false},
    // Surface / 表面数据
    {10, EDeferredToonDebugView::BaseColor, TEXT("BaseColor"), TEXT("Base Color"), TEXT("材质底色"), false},
    {11, EDeferredToonDebugView::WorldNormal, TEXT("WorldNormal"), TEXT("World Normal"), TEXT("世界空间法线"), false},
    {12, EDeferredToonDebugView::Depth, TEXT("Depth"), TEXT("Linear Depth"), TEXT("线性深度"), false},
    {13, EDeferredToonDebugView::Velocity, TEXT("Velocity"), TEXT("Velocity"), TEXT("运动速度"), false},
    // Direct diffuse / 直接漫反射
    {20, EDeferredToonDebugView::EffectiveDirectLight, TEXT("EffectiveDirectLight"), TEXT("Effective Direct Diffuse"), TEXT("有效直接漫反射"), false},
    {21, EDeferredToonDebugView::EffectiveLightResponse, TEXT("EffectiveLightResponse"), TEXT("Effective Light Response"), TEXT("有效受光响应"), false},
    {22, EDeferredToonDebugView::LightBandValue, TEXT("LightBandValue"), TEXT("Selected Light Band (0 Dark / 1 Light)"), TEXT("所选明暗档位（0 暗 / 1 亮）"), false},
    {23, EDeferredToonDebugView::DirectLightMask, TEXT("DirectLightMask"), TEXT("Direct Light Mask"), TEXT("直接光遮罩"), false},
    {24, EDeferredToonDebugView::ToonShadowMask, TEXT("ToonShadowMask"), TEXT("Toon Shadow Mask"), TEXT("卡通阴影遮罩"), false},
    {25, EDeferredToonDebugView::ToonBinaryShadowMask, TEXT("ToonBinaryShadowMask"), TEXT("Toon Binary Shadow Mask"), TEXT("卡通二分阴影遮罩"), false},
    {26, EDeferredToonDebugView::DirectLight, TEXT("DirectLight"), TEXT("Direct Light"), TEXT("直接光"), false},
    {27, EDeferredToonDebugView::DirectLightFacing, TEXT("DirectLightFacing"), TEXT("Direct Light Facing (N dot L)"), TEXT("朝向受光（N·L）"), false},
    // Direct specular / 直接高光
    {30, EDeferredToonDebugView::SpecularDirectLight, TEXT("SpecularDirectLight"), TEXT("Effective Direct Specular"), TEXT("有效直接光高光"), false},
    {31, EDeferredToonDebugView::DirectSpecularMask, TEXT("DirectSpecularMask"), TEXT("Direct Specular Hard Mask"), TEXT("直接高光硬边遮罩"), false},
    {32, EDeferredToonDebugView::StyledDirectSpecular, TEXT("StyledDirectSpecular"), TEXT("Styled Direct Specular"), TEXT("风格化直接光高光"), false},
    // Indirect diffuse / 间接漫反射
    {40, EDeferredToonDebugView::IndirectDiffuse, TEXT("IndirectDiffuse"), TEXT("Indirect Diffuse"), TEXT("间接漫反射"), false},
    {41, EDeferredToonDebugView::IndirectDiffuseCoverage, TEXT("IndirectDiffuseCoverage"), TEXT("Indirect Diffuse Availability"), TEXT("间接漫反射数据有效性"), false},
    {42, EDeferredToonDebugView::IndirectDiffuseBand, TEXT("IndirectDiffuseBand"), TEXT("Indirect Diffuse Two-Tone Mask"), TEXT("间接漫反射二分遮罩"), false},
    // Occlusion / 综合遮蔽
    {50, EDeferredToonDebugView::IndirectOcclusion, TEXT("IndirectOcclusion"), TEXT("Indirect Diffuse Occlusion (Automatic)"), TEXT("间接漫反射综合遮蔽（自动）"), false},
    {51, EDeferredToonDebugView::IndirectOcclusionBand, TEXT("IndirectOcclusionBand"), TEXT("AO Two-Tone Mask (White = Unoccluded)"), TEXT("遮蔽二分遮罩（白为无遮挡）"), false},
    {52, EDeferredToonDebugView::IndirectDiffuseBeforeOcclusion, TEXT("IndirectDiffuseBeforeOcclusion"), TEXT("Indirect Diffuse Before Occlusion (Automatic)"), TEXT("遮蔽前间接漫反射（自动）"), false},
    {53, EDeferredToonDebugView::IndirectOcclusionCoverage, TEXT("IndirectOcclusionCoverage"), TEXT("Indirect Occlusion Availability"), TEXT("综合遮蔽数据有效性"), false},
    {54, EDeferredToonDebugView::IndirectOcclusionSource, TEXT("IndirectOcclusionSource"), TEXT("AO Source (Cyan=Lumen, Orange=Sky, Black=None)"), TEXT("遮蔽来源（青=Lumen，橙=天光，黑=无）"), false},
    {55, EDeferredToonDebugView::AmbientOcclusion, TEXT("AmbientOcclusion"), TEXT("Screen Space AO"), TEXT("屏幕空间 AO"), false},
    // Sky / 非 Lumen 天光
    {60, EDeferredToonDebugView::SkyDiffuseBeforeAO, TEXT("SkyDiffuseBeforeAO"), TEXT("Sky Diffuse Before AO"), TEXT("天光漫反射（AO 前）"), false},
    {61, EDeferredToonDebugView::SkyOcclusion, TEXT("SkyOcclusion"), TEXT("Sky Combined AO (White = Unoccluded)"), TEXT("天光综合遮蔽（白为无遮挡）"), false},
    {62, EDeferredToonDebugView::SkyOcclusionCoverage, TEXT("SkyOcclusionCoverage"), TEXT("Sky AO Split Availability"), TEXT("天光遮蔽数据有效性"), false},
    // Lumen short AO / Lumen 短程 AO
    {70, EDeferredToonDebugView::LumenDiffuseBeforeShortAO, TEXT("LumenDiffuseBeforeShortAO"), TEXT("Lumen Diffuse Before Short Range AO"), TEXT("Lumen 漫反射（短程 AO 前）"), false},
    {71, EDeferredToonDebugView::LumenShortRangeAO, TEXT("LumenShortRangeAO"), TEXT("Lumen Short Range AO (White = Unoccluded)"), TEXT("Lumen 短程 AO（白为无遮挡）"), false},
    {72, EDeferredToonDebugView::LumenShortRangeAOCoverage, TEXT("LumenShortRangeAOCoverage"), TEXT("Lumen Short Range AO Availability"), TEXT("Lumen 短程 AO 数据有效性"), false},
    // Reflections / 环境镜面反射
    {80, EDeferredToonDebugView::IndirectSpecular, TEXT("IndirectSpecular"), TEXT("Indirect Specular (Environment Reflections)"), TEXT("间接镜面反射（环境反射）"), false},
    {81, EDeferredToonDebugView::IndirectSpecularCoverage, TEXT("IndirectSpecularCoverage"), TEXT("Indirect Specular Availability"), TEXT("间接镜面反射可用范围"), false},
    // Emission / 自发光
    {90, EDeferredToonDebugView::Emissive, TEXT("Emissive"), TEXT("Material Emissive"), TEXT("材质自发光"), false},
    {91, EDeferredToonDebugView::EmissiveCoverage, TEXT("EmissiveCoverage"), TEXT("Material Emissive Availability"), TEXT("材质自发光可用范围"), false},
    // Shadow inputs / 阴影输入与旧版诊断
    {100, EDeferredToonDebugView::NativeShadow, TEXT("NativeShadow"), TEXT("Native Shadow"), TEXT("原生阴影"), false},
    {101, EDeferredToonDebugView::ShadowChannel, TEXT("ShadowChannel"), TEXT("Shadow Channel"), TEXT("艺术阴影通道"), false},
    {102, EDeferredToonDebugView::CombinedShadowMask, TEXT("CombinedShadowMask"), TEXT("Combined Shadow Mask"), TEXT("组合阴影遮罩"), false},
    {103, EDeferredToonDebugView::NativeShadowMask, TEXT("NativeShadowMask"), TEXT("Native Shadow Mask (Legacy)"), TEXT("原生阴影遮罩（旧版）"), true},
    {104, EDeferredToonDebugView::ShadowChannelMask, TEXT("ShadowChannelMask"), TEXT("Shadow Channel Mask (Legacy)"), TEXT("艺术阴影通道遮罩（旧版）"), true},
    // Style layers / 实验分层
    {110, EDeferredToonDebugView::StyleMask, TEXT("StyleMask"), TEXT("Style Mask"), TEXT("分层遮罩"), false},
    {111, EDeferredToonDebugView::StyleColor, TEXT("StyleColor"), TEXT("Style Color"), TEXT("分层颜色"), false},
};

// Gaps, negative values and ConsoleGlobal (255) are not render views; show selected output.
constexpr int32 ToShaderId(int32 PublicIndex)
{
    for (const FEntry& Entry : Entries)
    {
        if (Entry.PublicIndex == PublicIndex) { return static_cast<int32>(Entry.View); }
    }
    return static_cast<int32>(EDeferredToonDebugView::NativeComposite);
}

inline const TCHAR* GetConsoleHelp()
{
    static const FString Help = []()
    {
        FString Result(TEXT("Deferred toon grouped debug indices (not serialized enum IDs).\n"
            "0-3 output; 10s surface; 20s direct diffuse; 30s direct specular; 40s indirect diffuse;\n"
            "50s occlusion; 60s sky; 70s Lumen short AO; 80s reflections; 90s emission; 100s shadow inputs; 110s style layers.\n"
            "Unused or invalid indices select output 0. Per-volume selections override this CVar.\n"));
        for (const FEntry& Entry : Entries)
        {
            Result += FString::Printf(TEXT("%d: %s%s\n"), Entry.PublicIndex, Entry.English,
                Entry.bHidden ? TEXT(" [console only]") : TEXT(""));
        }
        return Result;
    }();
    return *Help;
}
} // namespace DeferredToonDebugViews
