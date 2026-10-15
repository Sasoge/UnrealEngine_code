// [DeferredToon] Keep rendering/serialization independent from editor presentation.
#include "DeferredToonVolumeDetails.h"

#include "Components/DeferredToonVolumeComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "DeferredToonVolumeDetails"

TSharedRef<IDetailCustomization> FDeferredToonVolumeDetails::MakeInstance()
{
    return MakeShared<FDeferredToonVolumeDetails>();
}

void FDeferredToonVolumeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
    // [DeferredToon] Order by authoring task, not by the order fields were added.
    struct FSection { const TCHAR* Name; FText Label; bool bCollapsed; };
    const FSection Sections[] = {
        {TEXT("Toon Output"), LOCTEXT("Output", "Toon | Output"), false},
        {TEXT("Toon Bands"), LOCTEXT("Bands", "Toon | Light Bands"), false},
        {TEXT("Toon Palette"), LOCTEXT("Palette", "Toon | Colors"), false},
        {TEXT("Toon Textures"), LOCTEXT("Textures", "Toon | Textures"), true},
        {TEXT("Toon Bounds"), LOCTEXT("Bounds", "Toon | Box & Blending"), true},
        {TEXT("Toon Debug"), LOCTEXT("Debug", "Toon | Debug"), true},
        {TEXT("Toon Advanced"), LOCTEXT("Advanced", "Toon | Advanced"), true},
        {TEXT("Toon Style Lab"), LOCTEXT("Lab", "Toon | Experimental Layers"), true},
    };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sections); ++Index)
    {
        const FSection& Section = Sections[Index];
        IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(Section.Name, Section.Label);
        Category.SetSortOrder(Index);
        Category.InitiallyCollapsed(Section.bCollapsed);
    }

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

#undef LOCTEXT_NAMESPACE
