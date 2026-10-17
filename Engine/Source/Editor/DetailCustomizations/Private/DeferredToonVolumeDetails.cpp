// [DeferredToon] Keep rendering/serialization independent from editor presentation.
#include "DeferredToonVolumeDetails.h"
#include "DeferredToonDetailsLanguage.h"
#include "Containers/Ticker.h"
#include "DetailWidgetRow.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "Components/DeferredToonVolumeComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"

namespace
{
    void SetLanguageAndRefresh(bool bChinese)
    {
        DeferredToonDetailsLanguage::SetChinese(bChinese);
        // [DeferredToon] Rebuild all details views after the button event returns.
        // This refreshes native enum options and avoids destroying an active widget.
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
        {
            if (FPropertyEditorModule* Module = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
            {
                Module->NotifyCustomizationModuleChanged();
            }
            return false;
        }));
    }
}

TSharedRef<IDetailCustomization> FDeferredToonVolumeDetails::MakeInstance()
{
    DeferredToonDetailsLanguage::Apply(DeferredToonDetailsLanguage::IsChinese());
    return MakeShared<FDeferredToonVolumeDetails>();
}

void FDeferredToonVolumeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
    const bool bChinese = DeferredToonDetailsLanguage::IsChinese();
    // [DeferredToon] This preference belongs to the editor user/project, not the volume.
    IDetailCategoryBuilder& Language = DetailBuilder.EditCategory(TEXT("Toon Language"), FText::FromString(TEXT("界面语言 / Language")));
    Language.SetSortOrder(-1);
    Language.AddCustomRow(FText::FromString(TEXT("中文 English Language 语言")))
    .WholeRowContent()
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 2, 8, 2)
        [
            SNew(SButton).Text(FText::FromString(TEXT("中文"))).IsEnabled(!bChinese)
            .ToolTipText(FText::FromString(TEXT("只切换风格化盒体面板，不修改场景或编辑器全局语言。")))
            .OnClicked_Lambda([]() { SetLanguageAndRefresh(true); return FReply::Handled(); })
        ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 2, 0, 2)
        [
            SNew(SButton).Text(FText::FromString(TEXT("English"))).IsEnabled(bChinese)
            .ToolTipText(FText::FromString(TEXT("Change only the toon authoring UI; scene assets and global editor language stay unchanged.")))
            .OnClicked_Lambda([]() { SetLanguageAndRefresh(false); return FReply::Handled(); })
        ]
    ];
    // [DeferredToon] Order by authoring task, not by the order fields were added.
    struct FSection { const TCHAR* Name; FText Label; bool bCollapsed; };
    const FSection Sections[] = {
        {TEXT("Toon Output"), FText::FromString(bChinese ? TEXT("风格化 | 输出") : TEXT("Toon | Output")), false},
        {TEXT("Toon Bands"), FText::FromString(bChinese ? TEXT("风格化 | 明暗分档") : TEXT("Toon | Light Bands")), false},
        {TEXT("Toon Palette"), FText::FromString(bChinese ? TEXT("风格化 | 配色") : TEXT("Toon | Colors")), false},
        {TEXT("Toon Indirect"), FText::FromString(bChinese ? TEXT("风格化 | 间接漫反射") : TEXT("Toon | Indirect Diffuse")), true}, // [DeferredToon]
        {TEXT("Toon Occlusion"), FText::FromString(bChinese ? TEXT("风格化 | 间接漫反射遮蔽") : TEXT("Toon | Indirect Diffuse Occlusion")), true}, // [DeferredToon]
        {TEXT("Toon Textures"), FText::FromString(bChinese ? TEXT("风格化 | 纹理") : TEXT("Toon | Textures")), true},
        {TEXT("Toon Bounds"), FText::FromString(bChinese ? TEXT("风格化 | 盒体范围与混合") : TEXT("Toon | Box & Blending")), true},
        {TEXT("Toon Debug"), FText::FromString(bChinese ? TEXT("风格化 | 调试") : TEXT("Toon | Debug")), true},
        {TEXT("Toon Advanced"), FText::FromString(bChinese ? TEXT("风格化 | 高级设置") : TEXT("Toon | Advanced")), true},
        {TEXT("Toon Style Lab"), FText::FromString(bChinese ? TEXT("风格化 | 实验分层") : TEXT("Toon | Experimental Layers")), true},
    };
    // [DeferredToon] Source preview reads real per-pixel provider validity, not project defaults.
    DetailBuilder.EditCategory(TEXT("Toon Occlusion")).AddCustomRow(FText::FromString(TEXT("AO Source 来源")))
    .WholeRowContent()
    [
        SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(bChinese
            ? TEXT("自动选择有效来源：Lumen 短程 AO / 普通天光综合遮蔽。33 查看原始遮蔽，36 查看来源，38 查看遮蔽二分。单独处理 AO 时无需开启整层间接光调色或二分。旧分路参数保留在高级设置中。")
            : TEXT("Auto source: Lumen short-range AO / dynamic sky occlusion. View 33: raw AO; 36: source; 38: AO two-tone mask. AO-only processing needs no whole-layer indirect grading or quantization. Legacy settings remain under Advanced.")))
    ];
    TArray<TPair<FName, FText>> SectionLabels;
    SectionLabels.Emplace(TEXT("Toon Language"), FText::FromString(TEXT("界面语言 / Language")));
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sections); ++Index)
    {
        const FSection& Section = Sections[Index];
        IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(Section.Name, Section.Label);
        Category.SetSortOrder(Index);
        Category.InitiallyCollapsed(Section.bCollapsed);
        SectionLabels.Emplace(Section.Name, Section.Label);
    }

    // [DeferredToon] Apply names last: native CallInEditor buttons may otherwise
    // reset the band category's display label when selecting the component itself.
    DetailBuilder.SortCategories([SectionLabels](const TMap<FName, IDetailCategoryBuilder*>& Categories)
    {
        for (int32 Index = 0; Index < SectionLabels.Num(); ++Index)
        {
            if (IDetailCategoryBuilder* const* Category = Categories.Find(SectionLabels[Index].Key))
            {
                (*Category)->SetDisplayName(SectionLabels[Index].Value);
                (*Category)->SetSortOrder(Index - 1);
            }
        }
    });

    TArray<TWeakObjectPtr<UObject>> Objects;
    DetailBuilder.GetObjectsBeingCustomized(Objects);
    // [DeferredToon] Show controls if ANY selected component needs them. An
    // invalid band array falls back to binary shading, so its endpoints and
    // binary threshold must remain editable even when Use Multiple Bands is on.
    const auto EndpointVisibility = TAttribute<EVisibility>::CreateLambda([Objects]()
    {
        for (const TWeakObjectPtr<UObject>& Object : Objects)
        {
            const UDeferredToonVolumeComponent* Component = Cast<UDeferredToonVolumeComponent>(Object.Get());
            if (!Component) { return EVisibility::Visible; }
            if (Component->OutputMode == EDeferredToonVolumeOutputMode::NativeComposite)
            {
                return EVisibility::Visible;
            }
            if (Component->OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade ||
                Component->OutputMode == EDeferredToonVolumeOutputMode::ToonRecompose)
            {
                if (!Component->bUseLightBands || Component->LightBands.Num() < 2 || !Component->LightBandColorCurve)
                {
                    return EVisibility::Visible;
                }
            }
        }
        return EVisibility::Collapsed;
    });
    for (const FName Name : {GET_MEMBER_NAME_CHECKED(UDeferredToonVolumeComponent, LightingColorCurve),
                            GET_MEMBER_NAME_CHECKED(UDeferredToonVolumeComponent, ShadowColorCurve)})
    {
        if (IDetailPropertyRow* Row = DetailBuilder.EditDefaultProperty(DetailBuilder.GetProperty(Name)))
        {
            Row->Visibility(EndpointVisibility);
        }
    }
    if (IDetailPropertyRow* Row = DetailBuilder.EditDefaultProperty(DetailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UDeferredToonVolumeComponent, EffectiveLightThreshold))))
    {
        Row->Visibility(TAttribute<EVisibility>::CreateLambda([Objects]()
        {
            for (const TWeakObjectPtr<UObject>& Object : Objects)
            {
                const UDeferredToonVolumeComponent* Component = Cast<UDeferredToonVolumeComponent>(Object.Get());
                if (!Component) { return EVisibility::Visible; }
                if (Component->OutputMode == EDeferredToonVolumeOutputMode::NativeComposite ||
                    ((Component->OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade ||
                      Component->OutputMode == EDeferredToonVolumeOutputMode::ToonRecompose) &&
                     (!Component->bUseLightBands || Component->LightBands.Num() < 2)))
                {
                    return EVisibility::Visible;
                }
            }
            return EVisibility::Collapsed;
        }));
    }
}
