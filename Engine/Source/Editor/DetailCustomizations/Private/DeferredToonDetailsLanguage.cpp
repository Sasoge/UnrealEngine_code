// [DeferredToon] Scoped editor metadata translations. No assets or enum values are changed.
#include "DeferredToonDetailsLanguage.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace DeferredToonDetailsLanguage
{
namespace
{
    constexpr const TCHAR* ConfigSection = TEXT("DeferredToon.AuthoringUI");
    TMap<FString, TOptional<FString>> OriginalMetadata;

    // [DeferredToon] Cache original text, including missing keys, so English and
    // module shutdown restore exactly the native metadata. Paths avoid stale pointers.
    template <typename T>
    void Translate(T* Owner, const TCHAR* Key, const TCHAR* Chinese, bool bChinese)
    {
        if (!Owner) { return; }
        const FString CacheKey = Owner->GetPathName() + TEXT(":") + Key;
        TOptional<FString>* Original = OriginalMetadata.Find(CacheKey);
        if (!Original)
        {
            Original = &OriginalMetadata.Add(CacheKey, Owner->HasMetaData(Key)
                ? TOptional<FString>(Owner->GetMetaData(Key)) : TOptional<FString>());
        }
        if (bChinese) { Owner->SetMetaData(Key, Chinese); }
        else if (Original->IsSet()) { Owner->SetMetaData(Key, *Original->GetValue()); }
        else { Owner->RemoveMetaData(Key); }
    }
    struct FPropertyText { const TCHAR* Name; const TCHAR* Label; const TCHAR* Tip; };
    void TranslateProperties(UStruct* Owner, TConstArrayView<FPropertyText> Texts, bool bChinese)
    {
        if (!Owner) { return; }
        for (const FPropertyText& Text : Texts)
        {
            FProperty* Property = FindFProperty<FProperty>(Owner, Text.Name);
            Translate(Property, TEXT("DisplayName"), Text.Label, bChinese);
            Translate(Property, TEXT("ToolTip"), Text.Tip, bChinese);
        }
    }
    struct FEnumText { const TCHAR* Name; const TCHAR* Label; };
    void TranslateEnum(const TCHAR* Path, TConstArrayView<FEnumText> Texts, bool bChinese)
    {
        UEnum* Enum = FindObject<UEnum>(nullptr, Path);
        if (!Enum) { return; }
        for (const FEnumText& Text : Texts)
        {
            const int32 Index = Enum->GetIndexByNameString(Text.Name);
            if (Index == INDEX_NONE) { continue; }
            const FString Prefix = Enum->GetNameStringByIndex(Index);
            Translate(Enum, *(Prefix + TEXT(".DisplayName")), Text.Label, bChinese);
            Translate(Enum, *(Prefix + TEXT(".ToolTip")), Text.Label, bChinese);
        }
    }
}

bool IsChinese()
{
    bool bChinese = FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName() == TEXT("zh");
    GConfig->GetBool(ConfigSection, TEXT("Chinese"), bChinese, GEditorPerProjectIni);
    return bChinese;
}

void SetChinese(bool bChinese)
{
    GConfig->SetBool(ConfigSection, TEXT("Chinese"), bChinese, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
    Apply(bChinese);
}

void Apply(bool bChinese)
{
    UClass* Component = FindObject<UClass>(nullptr, TEXT("/Script/Engine.DeferredToonVolumeComponent"));
    const FPropertyText Properties[] = {
        {TEXT("bEnabled"), TEXT("启用风格化盒体"), TEXT("启用或停用此盒体的风格化效果。")},
        {TEXT("OutputMode"), TEXT("输出模式"), TEXT("直接光调色：处理原生有效直接漫反射；独立重组：输出色板与可选材质底色。整体染色与输入层预览用于辅助调试。")},
        {TEXT("BlendWeight"), TEXT("效果强度"), TEXT("0 保留原画面；1 在盒体内部完整应用效果。")},
        // [DeferredToon] Keep new authoring controls bilingual.
        {TEXT("bOverrideIndirectDiffuse"), TEXT("独立调整间接漫反射"), TEXT("从其他光照中分出支持的 Lumen 或动态天光漫反射。不改变直接光档位；不支持的像素沿用原合成。")},
        {TEXT("IndirectDiffuseIntensity"), TEXT("强度"), TEXT("1 保留原始强度；0 去除已分离的间接漫反射。")},
        {TEXT("IndirectDiffuseColor"), TEXT("颜色"), TEXT("线性空间乘色或替换色。颜色替换为 0 时白色为中性；最后仍经过 UE 曝光和色调映射。")},
        {TEXT("IndirectDiffuseColorReplaceWeight"), TEXT("颜色替换"), TEXT("0 对原生间接漫反射乘色；1 替换为指定颜色（包括零间接光表面）。材质底色影响只作用于替换部分。")},
        // [DeferredToon] Sky occlusion is optional and has a compact collapsed section.
        {TEXT("bOverrideSkyOcclusion"), TEXT("独立调整天光遮蔽"), TEXT("先启用独立调整间接漫反射。仅支持普通动态 SkyLight；Lumen 和不支持的像素保持原结果。")},
        {TEXT("SkyOcclusionStrength"), TEXT("遮蔽强度"), TEXT("1 保留原生遮蔽；0 恢复局部 AO、弯曲法线和遮蔽染色应用前的天光漫反射。保留云遮蔽，不改变直接光阴影。")},
        {TEXT("SkyOcclusionColor"), TEXT("遮蔽颜色"), TEXT("遮蔽区域的线性替换色，只改变间接漫反射，不给直接光和反射染色。最终仍经过曝光与色调映射。")},
        {TEXT("SkyOcclusionColorReplaceWeight"), TEXT("颜色替换"), TEXT("0 保留间接光调色结果；1 按原生综合遮蔽程度和遮蔽强度混入遮蔽颜色。无遮挡处不变。")},
        {TEXT("OtherLightingWeight"), TEXT("保留其他光照"), TEXT("0 只输出处理后的直接漫反射；1 保留原画面减去直接漫反射后的其余贡献，包括间接光与高光。启用独立调整间接漫反射后，已分离部分改由间接光参数控制。这会影响最终看到的色板颜色。")},
        {TEXT("bUseLightBands"), TEXT("启用多档明暗"), TEXT("支持 2～8 档。关闭或不足两档时使用二分阈值。")},
        {TEXT("EffectiveLightThreshold"), TEXT("二分受光阈值"), TEXT("二分模式的受光分界。多档模式使用每档受光下限；调试视图 14、19、20 仍使用这个二分参考阈值。")},
        {TEXT("LightBands"), TEXT("明暗档位"), TEXT("前 8 项按受光下限排序；下限相同则后项优先。至少两项。数组标题为“受光下限 / 档位值”。")},
        {TEXT("LightBandColorCurve"), TEXT("多档色板曲线"), TEXT("在各档“档位值”处取色。留空则混合亮暗两条端点曲线；单档自定义颜色优先。")},
        {TEXT("LightingColorCurve"), TEXT("亮部端点曲线"), TEXT("直接光调色和独立重组只取时间 1 的颜色，留空为白色。没有多档色板时用于生成中间档颜色。整体染色模式使用整条曲线。")},
        {TEXT("ShadowColorCurve"), TEXT("暗部端点曲线"), TEXT("只取时间 1 的颜色，留空为黑色。没有多档色板时与亮部端点混合生成中间档颜色。")},
        {TEXT("LitColorReplaceWeight"), TEXT("亮部颜色替换"), TEXT("0：原生直接漫反射乘目标色；1：用目标色替换。中间档按档位值混合亮暗替换权重。亮部曲线留空仍提供白色。")},
        {TEXT("ShadowColorReplaceWeight"), TEXT("暗部颜色替换"), TEXT("0：原生直接漫反射乘目标色；1：用目标色替换，包括完全无光区域。中间档按档位值混合亮暗替换权重。")},
        {TEXT("BaseColorWeight"), TEXT("材质底色影响"), TEXT("0 使用纯目标颜色；1 将替换颜色或纹理乘以材质底色。只影响替换部分。")},
        {TEXT("ShadowTexture"), TEXT("阴影纹理（2D）"), TEXT("在盒体局部空间进行三平面映射。多档模式由各档的阴影纹理影响控制；二分模式影响暗部。")},
        {TEXT("ShadowTextureWeight"), TEXT("纹理混合强度"), TEXT("将纹理 RGB 混入目标颜色，并乘以纹理 Alpha 和每档纹理影响。")},
        {TEXT("ShadowTextureTiling"), TEXT("纹理平铺"), TEXT("盒体投影中的重复次数；三平面投影避免侧面拉伸。")},
        {TEXT("ShadowTextureOffset"), TEXT("纹理偏移"), TEXT("三个投影方向共用的 UV 偏移。")},
        {TEXT("ShadowColorVolume"), TEXT("阴影体积纹理（3D）"), TEXT("可选三维颜色纹理，在盒体局部空间采样。")},
        {TEXT("ShadowColorVolumeWeight"), TEXT("体积纹理混合强度"), TEXT("控制三维纹理对目标颜色的影响，多档模式还会乘以每档阴影纹理影响。")},
        {TEXT("BoxExtent"), TEXT("盒体半尺寸"), TEXT("局部空间的半尺寸，单位为厘米。物体或组件缩放也会影响实际范围。")},
        {TEXT("EdgeFalloff"), TEXT("边缘羽化"), TEXT("控制盒体边缘与原画面的过渡范围。")},
        {TEXT("FalloffCurve"), TEXT("边缘羽化曲线"), TEXT("自定义盒体边缘过渡形状。")},
        {TEXT("Priority"), TEXT("重叠优先级"), TEXT("数值较大的盒体先收集、先处理；后处理的盒体仍可与之前结果混合。")},
        {TEXT("ObjectID"), TEXT("稳定排序编号"), TEXT("自动生成的稳定编号，用于优先级相同时的排序，不是着色参数。")},
        {TEXT("ToonShadowThreshold"), TEXT("艺术阴影覆盖阈值"), TEXT("独立艺术阴影通道达到该阈值时强制选择暗部，多档模式选择第一档。")},
        {TEXT("DebugView"), TEXT("缓冲视图预览"), TEXT("局部调试视图。跟随全局使用遥控器或控制台；当前输出显示输出模式结果。24 显示所选明暗档位。")},
        {TEXT("StyleSource"), TEXT("分层来源"), TEXT("选择风格资产、盒体内的输入层，或停用实验分层。")},
        {TEXT("StyleProfile"), TEXT("分层风格资产"), TEXT("复用数据资产中配置的 RT 输入层，用于实验分层预览。")},
        {TEXT("StyleLayers"), TEXT("输入层列表"), TEXT("直接在盒体中配置 RT 输入层，用于实验分层预览。")},
    };
    TranslateProperties(Component, Properties, bChinese);
    UScriptStruct* Band = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Engine.DeferredToonLightBand"));
    const FPropertyText BandProperties[] = {
        {TEXT("MinResponse"), TEXT("受光下限"), TEXT("进入此档的最小受光量，包含等号。按下限排序，第一档从 0 开始。")},
        {TEXT("Value"), TEXT("档位值"), TEXT("0 代表最暗端，1 代表最亮端。用于色板采样、调试显示和亮暗替换权重插值，不直接乘最终颜色。")},
        {TEXT("bOverrideColor"), TEXT("使用自定义颜色"), TEXT("为此档指定独立颜色，优先于色板曲线和端点混合。")},
        {TEXT("Color"), TEXT("自定义颜色"), TEXT("此档使用的线性颜色，之后仍经过曝光和色调映射。")},
        {TEXT("ShadowMappingWeight"), TEXT("阴影纹理影响"), TEXT("该档接受 2D/3D 阴影纹理的程度，与档位值独立；另乘全局纹理混合强度。")},
    };
    TranslateProperties(Band, BandProperties, bChinese);
    const FPropertyText LayerProperties[] = {
        {TEXT("InputId"), TEXT("输入缓冲"), TEXT("选择此层采样的渲染缓冲。")},
        {TEXT("Channel"), TEXT("采样通道"), TEXT("选择 RGB、单通道或亮度。")},
        {TEXT("Operation"), TEXT("混合方式"), TEXT("将本层与已有层结果相乘、相加或替换。")},
        {TEXT("bInvert"), TEXT("反转"), TEXT("反转采样值。")},
        {TEXT("Weight"), TEXT("层权重"), TEXT("控制此输入层的作用强度。")},
    };
    TranslateProperties(FindObject<UScriptStruct>(nullptr, TEXT("/Script/Engine.DeferredToonStyleInputLayer")), LayerProperties, bChinese);
    if (Component)
    {
        Translate(Component, TEXT("ToolTip"), TEXT("在盒体范围内进行风格化处理：选择输出模式、划分明暗、设置配色，再按需添加纹理。展开盒体范围与混合可调整作用区域。"), bChinese);
        Translate(FindFProperty<FProperty>(Component, TEXT("LightBands")), TEXT("TitleProperty"), TEXT("下限 {MinResponse} / 档位 {Value}"), bChinese);
        UFunction* Four = Component->FindFunctionByName(TEXT("ApplyFourLightBands"));
        Translate(Four, TEXT("DisplayName"), TEXT("重置为四档"), bChinese);
        Translate(Four, TEXT("ToolTip"), TEXT("用四档预设替换当前档位数组并启用多档。保留其他配色及输出设置，支持撤销。"), bChinese);
        UFunction* Two = Component->FindFunctionByName(TEXT("UseTwoBandCompatibility"));
        Translate(Two, TEXT("DisplayName"), TEXT("切换为二分"), bChinese);
        Translate(Two, TEXT("ToolTip"), TEXT("关闭多档但保留已有档位数组，支持撤销。"), bChinese);
    }
    const FEnumText Enum0[] = {
        {TEXT("NativeComposite"), TEXT("整体染色（高级）")},
        {TEXT("Replace"), TEXT("输入层预览")},
        {TEXT("ToonRecompose"), TEXT("独立重组")},
        {TEXT("DirectLightGrade"), TEXT("直接光调色")},
    };
    TranslateEnum(TEXT("/Script/Engine.EDeferredToonVolumeOutputMode"), Enum0, bChinese);
    const FEnumText Enum1[] = {
        {TEXT("NativeComposite"), TEXT("0 - 当前输出")},
        {TEXT("BoxInfluence"), TEXT("1 - 盒体影响范围")},
        {TEXT("ShadowComposite"), TEXT("2 - 合成阴影")},
        {TEXT("Priority"), TEXT("3 - 优先级")},
        {TEXT("DirectLight"), TEXT("4 - 直接光")},
        {TEXT("NativeShadow"), TEXT("5 - 原生阴影")},
        {TEXT("ShadowChannel"), TEXT("6 - 艺术阴影通道")},
        {TEXT("BaseColor"), TEXT("7 - 材质底色")},
        {TEXT("WorldNormal"), TEXT("8 - 世界空间法线")},
        {TEXT("AmbientOcclusion"), TEXT("9 - 屏幕空间 AO")},
        {TEXT("StyleMask"), TEXT("10 - 分层遮罩")},
        {TEXT("StyleColor"), TEXT("11 - 分层颜色")},
        {TEXT("Depth"), TEXT("12 - 线性深度")},
        {TEXT("Velocity"), TEXT("13 - 运动速度")},
        {TEXT("DirectLightMask"), TEXT("14 - 直接光遮罩")},
        {TEXT("CombinedShadowMask"), TEXT("17 - 组合阴影遮罩")},
        {TEXT("SpecularDirectLight"), TEXT("18 - 直接光高光")},
        {TEXT("ToonShadowMask"), TEXT("19 - 卡通阴影遮罩")},
        {TEXT("ToonBinaryShadowMask"), TEXT("20 - 卡通二分阴影遮罩")},
        {TEXT("DirectLightFacing"), TEXT("21 - 朝向受光（N·L）")},
        {TEXT("EffectiveDirectLight"), TEXT("22 - 有效直接漫反射")},
        {TEXT("EffectiveLightResponse"), TEXT("23 - 有效受光响应")},
        {TEXT("LightBandValue"), TEXT("24 - 所选明暗档位（0 暗 / 1 亮）")},
        {TEXT("IndirectDiffuse"), TEXT("25 - 间接漫反射")},
        {TEXT("IndirectDiffuseCoverage"), TEXT("26 - 间接漫反射数据有效性")},
        {TEXT("SkyDiffuseBeforeAO"), TEXT("27 - 天光漫反射（AO 前）")}, // [DeferredToon]
        {TEXT("SkyOcclusion"), TEXT("28 - 天光综合遮蔽（白为无遮挡）")},
        {TEXT("SkyOcclusionCoverage"), TEXT("29 - 天光遮蔽数据有效性")},
        {TEXT("ConsoleGlobal"), TEXT("跟随全局 / 控制台")},
    };
    TranslateEnum(TEXT("/Script/Engine.EDeferredToonDebugView"), Enum1, bChinese);
    const FEnumText Enum2[] = {
        {TEXT("StyleProfile"), TEXT("风格数据资产")},
        {TEXT("InlineLayers"), TEXT("盒体内配置")},
        {TEXT("DefaultStack"), TEXT("不使用分层")},
    };
    TranslateEnum(TEXT("/Script/Engine.EDeferredToonStyleSource"), Enum2, bChinese);
    const FEnumText Enum3[] = {
        {TEXT("Unknown"), TEXT("无")},
        {TEXT("DirectLight"), TEXT("直接光")},
        {TEXT("NativeShadow"), TEXT("原生阴影")},
        {TEXT("ShadowChannel"), TEXT("艺术阴影通道")},
        {TEXT("AmbientOcclusion"), TEXT("屏幕空间 AO")},
        {TEXT("BaseColor"), TEXT("材质底色")},
        {TEXT("WorldNormal"), TEXT("世界空间法线")},
        {TEXT("Roughness"), TEXT("粗糙度")},
        {TEXT("ShadingModel"), TEXT("着色模型")},
        {TEXT("Depth"), TEXT("深度")},
        {TEXT("Velocity"), TEXT("运动速度")},
        {TEXT("SpecularDirectLight"), TEXT("直接光高光")},
        {TEXT("DirectLightFacing"), TEXT("朝向受光（N·L）")},
        {TEXT("EffectiveDirectLight"), TEXT("有效直接漫反射")},
        {TEXT("EffectiveLightResponse"), TEXT("有效受光响应")},
        {TEXT("IndirectDiffuse"), TEXT("间接漫反射")},
        {TEXT("SkyDiffuseBeforeAO"), TEXT("天光漫反射（AO 前）")}, // [DeferredToon]
        {TEXT("SkyOcclusion"), TEXT("天光综合遮蔽")},
        {TEXT("DeprecatedShadowBuffer"), TEXT("旧版阴影缓冲（已弃用）")},
    };
    TranslateEnum(TEXT("/Script/Engine.EDeferredToonStyleInputId"), Enum3, bChinese);
    const FEnumText Enum4[] = {
        {TEXT("RGB"), TEXT("RGB")},
        {TEXT("R"), TEXT("红（R）")},
        {TEXT("G"), TEXT("绿（G）")},
        {TEXT("B"), TEXT("蓝（B）")},
        {TEXT("A"), TEXT("A 通道")},
        {TEXT("Luminance"), TEXT("亮度")},
    };
    TranslateEnum(TEXT("/Script/Engine.EDeferredToonStyleInputChannel"), Enum4, bChinese);
    const FEnumText Enum5[] = {
        {TEXT("Multiply"), TEXT("相乘")},
        {TEXT("Add"), TEXT("相加")},
        {TEXT("Replace"), TEXT("替换")},
    };
    TranslateEnum(TEXT("/Script/Engine.EDeferredToonStyleLayerOperation"), Enum5, bChinese);
}
}
