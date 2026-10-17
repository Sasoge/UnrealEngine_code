#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/DeferredToonStyleProfile.h"
#include "DeferredToonVolumeComponent.generated.h"

class UCurveFloat;
class UCurveLinearColor;
class UCurveBase;
class UVolumeTexture;
class UTexture2D; // [DeferredToon] Optional 2D shadow color mapping.
class FDeferredToonVolumeSceneProxy;

// [DeferredToon] A discrete lighting interval, independent of material albedo.
USTRUCT(BlueprintType)
struct FDeferredToonLightBand
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Band", meta=(DisplayName="Starts At", ClampMin="0.0", ToolTip="Inclusive scene-linear reference response lower bound. Bands are sorted at runtime; the first band covers zero."))
    float MinResponse = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Band", meta=(DisplayName="Level", ClampMin="0.0", ClampMax="1.0", ToolTip="Discrete light level: 0 darkest, 1 brightest. Also selects the color-curve coordinate and interpolates the two Replace Weights."))
    float Value = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Band", meta=(DisplayName="Use Custom Color"))
    bool bOverrideColor = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Band", meta=(EditCondition="bOverrideColor", EditConditionHides))
    FLinearColor Color = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Light Band", AdvancedDisplay, meta=(DisplayName="Shadow Texture Influence", ClampMin="0.0", ClampMax="1.0", ToolTip="Influence of the existing 2D/3D shadow textures on this band. Independent of the band's Value."))
    float ShadowMappingWeight = 1.0f;
};

// [DeferredToon] Stable serialized mode IDs. Mode 1 is a migration token only.
UENUM(BlueprintType)
enum class EDeferredToonVolumeOutputMode : uint8
{
    NativeComposite = 0 UMETA(DisplayName="Scene Color Tint (Advanced)"),
    RecomposeDiffuse = 1 UMETA(Hidden),
    Replace = 2 UMETA(DisplayName="Style Layers Preview"),
    // [DeferredToon] Append to preserve serialized values of existing modes.
    ToonRecompose = 3 UMETA(DisplayName="Toon Recompose"),
    // [DeferredToon] Grade/replace only native visible direct diffuse, optionally retain other lighting.
    DirectLightGrade = 4 UMETA(DisplayName="Direct Light Grade"),
};

// [DeferredToon] Preserve native color grading unless hard two-tone output is selected.
UENUM(BlueprintType)
enum class EDeferredToonIndirectDiffuseStyle : uint8
{
    ColorGrade = 0 UMETA(DisplayName="Color Grade"),
    TwoTone = 1 UMETA(DisplayName="Hard Two-Tone"),
};

// [DeferredToon] Explicit compatibility mode avoids merging unequal saved settings.
UENUM(BlueprintType)
enum class EDeferredToonIndirectOcclusionMode : uint8
{
    Native = 0 UMETA(DisplayName="Native (No Adjustment)"),
    Unified = 1 UMETA(DisplayName="Unified (Automatic Source)"),
    Legacy = 2 UMETA(DisplayName="Legacy Separate Settings"),
};

// [DeferredToon] Details-panel selector for the same semantic views exposed by
// r.Toon.DeferredVolumes.DebugView.  Console / Global keeps the legacy CVar
// behavior when no per-volume override is desired.
UENUM(BlueprintType)
enum class EDeferredToonDebugView : uint8
{
    NativeComposite = 0 UMETA(DisplayName="0 - Selected Output"),
    BoxInfluence = 1 UMETA(DisplayName="1 - Box Influence"),
    ShadowComposite = 2 UMETA(DisplayName="2 - Shadow Composite"),
    Priority = 3 UMETA(DisplayName="3 - Priority"),
    DirectLight = 4 UMETA(DisplayName="4 - Direct Light"),
    NativeShadow = 5 UMETA(DisplayName="5 - Native Shadow"),
    ShadowChannel = 6 UMETA(DisplayName="6 - Shadow Channel"),
    BaseColor = 7 UMETA(DisplayName="7 - Base Color"),
    WorldNormal = 8 UMETA(DisplayName="8 - World Normal"),
    AmbientOcclusion = 9 UMETA(DisplayName="9 - Screen Space AO"),
    StyleMask = 10 UMETA(DisplayName="10 - Style Mask"),
    StyleColor = 11 UMETA(DisplayName="11 - Style Color"),
    Depth = 12 UMETA(DisplayName="12 - Linear Depth"),
    Velocity = 13 UMETA(DisplayName="13 - Velocity"),
    DirectLightMask = 14 UMETA(DisplayName="14 - Direct Light Mask"),
    NativeShadowMask = 15 UMETA(Hidden),
    ShadowChannelMask = 16 UMETA(Hidden),
    CombinedShadowMask = 17 UMETA(DisplayName="17 - Combined Shadow Mask"),
    SpecularDirectLight = 18 UMETA(DisplayName="18 - Specular Direct Light"),
    ToonShadowMask = 19 UMETA(DisplayName="19 - Toon Shadow Mask"),
    // [DeferredToon] Strict effective-light selection plus artistic shadow override.
    ToonBinaryShadowMask = 20 UMETA(DisplayName="20 - Toon Binary Shadow Mask"),
    DirectLightFacing = 21 UMETA(DisplayName="21 - Direct Light Facing (N dot L)"),
    // [DeferredToon] Independent scene-linear color and selection inputs.
    EffectiveDirectLight = 22 UMETA(DisplayName="22 - Effective Direct Diffuse"),
    EffectiveLightResponse = 23 UMETA(DisplayName="23 - Effective Light Response"),
    // [DeferredToon] Display discrete light level; unlike shadow masks, 1 means brightest.
    LightBandValue = 24 UMETA(DisplayName="24 - Selected Light Band (0 Dark / 1 Light)"),
    // [DeferredToon] Append only: preserve saved debug IDs.
    IndirectDiffuse = 25 UMETA(DisplayName="25 - Indirect Diffuse"),
    IndirectDiffuseCoverage = 26 UMETA(DisplayName="26 - Indirect Diffuse Availability"),
    // [DeferredToon] Sky-only split diagnostics; existing debug IDs remain stable.
    SkyDiffuseBeforeAO = 27 UMETA(DisplayName="27 - Sky Diffuse Before AO"),
    SkyOcclusion = 28 UMETA(DisplayName="28 - Sky Combined AO (White = Unoccluded)"),
    SkyOcclusionCoverage = 29 UMETA(DisplayName="29 - Sky AO Split Availability"),
    // [DeferredToon] Append IDs to preserve serialized debug selections.
    LumenShortRangeAO = 30 UMETA(DisplayName="30 - Lumen Short Range AO (White = Unoccluded)"),
    LumenDiffuseBeforeShortAO = 31 UMETA(DisplayName="31 - Lumen Diffuse Before Short Range AO"),
    LumenShortRangeAOCoverage = 32 UMETA(DisplayName="32 - Lumen Short Range AO Availability"),
    // [DeferredToon] Aggregated diagnostics; raw source views 27-32 stay stable.
    IndirectOcclusion = 33 UMETA(DisplayName="33 - Indirect Diffuse Occlusion (Automatic)"),
    IndirectDiffuseBeforeOcclusion = 34 UMETA(DisplayName="34 - Indirect Diffuse Before Occlusion (Automatic)"),
    IndirectOcclusionCoverage = 35 UMETA(DisplayName="35 - Indirect Occlusion Availability"),
    IndirectOcclusionSource = 36 UMETA(DisplayName="36 - AO Source (Cyan=Lumen, Orange=Sky, Black=None)"),
    IndirectDiffuseBand = 37 UMETA(DisplayName="37 - Indirect Diffuse Two-Tone Mask"),
    ConsoleGlobal = 255 UMETA(DisplayName="Console / Global"),
};

// [DeferredToon] Authoring UI groups follow output -> bands -> palette -> textures -> bounds.
// Display names, categories and conditions are presentation only; serialized names stay stable.
// Advanced controls remain available without crowding the primary workflow.
// [DeferredToon] Asset-facing component for pre-tonemap multi-box color mapping.
UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent, ToolTip="Styles lighting inside a box. Choose an output, define light bands, then set colors and optional textures. Expand Box & Blending to adjust the affected area."), hidecategories=(Object, Mobility, Activation, Cooking, AssetUserData, ComponentReplication, Tags), MinimalAPI)
class UDeferredToonVolumeComponent : public USceneComponent
{
    GENERATED_UCLASS_BODY()

public:
    // [DeferredToon] Fixed GPU capacity; authored intervals do not create shader permutations.
    static constexpr int32 MaxLightBands = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Output", meta=(DisplayName="Enable Toon Volume", DisplayPriority="0"))
    bool bEnabled;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Bounds", meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0", DisplayName="Edge Feather", DisplayPriority="1"))
    float EdgeFalloff;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Bounds", meta=(ClampMin="1.0", UIMin="1.0", UIMax="100000.0", DisplayName="Box Half Size", DisplayPriority="0", ToolTip="Local half extents in centimeters. Actor/component scale also affects the final world-space bounds."))
    FVector BoxExtent;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Bounds", AdvancedDisplay, meta=(DisplayName="Edge Feather Curve", DisplayPriority="2", EditCondition="EdgeFalloff > 0.0", EditConditionHides))
    TObjectPtr<UCurveFloat> FalloffCurve;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Textures", AdvancedDisplay, meta=(DisplayName="Shadow Volume Texture (3D)", DisplayPriority="4", EditCondition="(OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade || OutputMode == EDeferredToonVolumeOutputMode::ToonRecompose) || OutputMode == EDeferredToonVolumeOutputMode::NativeComposite", EditConditionHides))
    TObjectPtr<UVolumeTexture> ShadowColorVolume;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Textures", AdvancedDisplay, meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0", DisplayName="Volume Texture Blend", DisplayPriority="5", EditCondition="ShadowColorVolume != nullptr && ((OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade || OutputMode == EDeferredToonVolumeOutputMode::ToonRecompose) || OutputMode == EDeferredToonVolumeOutputMode::NativeComposite)", EditConditionHides))
    float ShadowColorVolumeWeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Palette", meta=(DisplayName="Light Endpoint Curve", DisplayPriority="1", ToolTip="Direct Light Grade / Toon Recompose: only the color at time 1 is used; empty means white. Without a band palette, intermediate colors blend between the two endpoints. Scene Color Tint samples the full curve."))
    TObjectPtr<UCurveLinearColor> LightingColorCurve;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Palette", meta=(DisplayName="Shadow Endpoint Curve", DisplayPriority="2", ToolTip="Only the color at time 1 is used; empty means black. Without a band palette, intermediate colors blend from this shadow endpoint to the light endpoint."))
    TObjectPtr<UCurveLinearColor> ShadowColorCurve;

    // [DeferredToon] Serialized compatibility fields only. No render-thread binding.
    UPROPERTY(BlueprintReadWrite, Category="Deferred Toon Volume|Deprecated", meta=(DeprecatedProperty, DeprecationMessage="Angular selection retired; use EffectiveLightThreshold."))
    float DirectLightMaskThreshold;
    UPROPERTY(BlueprintReadWrite, Category="Deferred Toon Volume|Deprecated", meta=(DeprecatedProperty, DeprecationMessage="Strict binary selection has no softness."))
    float DirectLightMaskSoftness;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Output", meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0", DisplayName="Effect Strength", DisplayPriority="2", ToolTip="Overall blend with the scene. 0 bypasses this volume; 1 applies its full effect inside the box."))
    float BlendWeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Output", meta=(DisplayName="Output Mode", DisplayPriority="1", ToolTip="Direct Light Grade processes native direct diffuse. Toon Recompose outputs the palette with optional material color. Scene Color Tint and Style Layers Preview are auxiliary modes."))
    EDeferredToonVolumeOutputMode OutputMode;

    // [DeferredToon] Independent two-band output controls; colors come from
    // LightingColorCurve/ShadowColorCurve at time 1.0 in linear space.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Palette", meta=(ClampMin="0.0", ClampMax="1.0", ToolTip="0 uses pure authored replacement colors; 1 multiplies replacement colors/textures by material Base Color.", DisplayName="Material Color Influence", DisplayPriority="5", EditCondition="(OutputMode == EDeferredToonVolumeOutputMode::ToonRecompose || (OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade && (LitColorReplaceWeight > 0.0 || ShadowColorReplaceWeight > 0.0)))", EditConditionHides))
    float BaseColorWeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Advanced", meta=(ClampMin="0.001", ClampMax="1.0", ToolTip="Cutoff for the separate artistic Shadow Channel override. At or above this value, select the dark band (first band when using multiple levels).", DisplayName="Artistic Shadow Override Threshold", DisplayPriority="0", EditCondition="(OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade || OutputMode == EDeferredToonVolumeOutputMode::ToonRecompose) || OutputMode == EDeferredToonVolumeOutputMode::NativeComposite", EditConditionHides))
    float ToonShadowThreshold;

    // [DeferredToon] Selection is independent of material-colored RGB; color mapping follows selection.
    UPROPERTY(BlueprintReadWrite, Category="Deferred Toon Volume|Deprecated", meta=(DeprecatedProperty, DeprecationMessage="Effective lighting is now the only production selector."))
    bool bUseEffectiveLighting = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Bands", meta=(ClampMin="0.0", UIMax="10.0", ToolTip="Two-band compatibility threshold. With Light Bands enabled, use their Min Response values instead. Debug 14/19/20 retain this binary reference.", DisplayName="Two-Band Threshold", DisplayPriority="1"))
    float EffectiveLightThreshold = 0.1f;

    // [DeferredToon] Opt in so existing serialized volumes retain their two-band behavior.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Bands", meta=(ToolTip="Enable 2-8 discrete bands for Direct Light Grade and Toon Recompose. Disabled/invalid arrays retain the two-band selector.", DisplayName="Use Multiple Bands", DisplayPriority="0", EditCondition="(OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade || OutputMode == EDeferredToonVolumeOutputMode::ToonRecompose)", EditConditionHides))
    bool bUseLightBands = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Bands", meta=(EditConditionHides, TitleProperty="Starts at {MinResponse} / Level {Value}", ToolTip="First 8 entries are used, sorted by Min Response. Equal thresholds: later entry wins. At least 2 entries required.", DisplayName="Bands", DisplayPriority="2", EditCondition="bUseLightBands && (OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade || OutputMode == EDeferredToonVolumeOutputMode::ToonRecompose)"))
    TArray<FDeferredToonLightBand> LightBands;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Palette", meta=(EditConditionHides, DisplayName="Band Palette Curve", DisplayPriority="0", EditCondition="bUseLightBands && (OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade || OutputMode == EDeferredToonVolumeOutputMode::ToonRecompose)", ToolTip="Optional palette sampled at each band Level. Empty: blend Shadow Endpoint and Light Endpoint. An individual band color overrides both."))
    TObjectPtr<UCurveLinearColor> LightBandColorCurve = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Palette", meta=(ClampMin="0.0", ClampMax="1.0", EditConditionHides, DisplayName="Light Color Replacement", DisplayPriority="3", EditCondition="OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", ToolTip="0 tints original direct diffuse; 1 replaces it with the target color. Intermediate bands interpolate the shadow/light replacement amounts by their Level. Empty light curve still supplies white."))
    float LitColorReplaceWeight = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Palette", meta=(ClampMin="0.0", ClampMax="1.0", EditConditionHides, DisplayName="Shadow Color Replacement", DisplayPriority="4", EditCondition="OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", ToolTip="0 tints original direct diffuse; 1 replaces it with the target color, even in fully dark regions. Intermediate bands interpolate the shadow/light replacement amounts by their Level."))
    float ShadowColorReplaceWeight = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Output", meta=(ClampMin="0.0", ClampMax="1.0", EditConditionHides, DisplayName="Keep Other Lighting", DisplayPriority="3", EditCondition="OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", ToolTip="0 keeps only processed direct diffuse; 1 also retains the original scene residual (including indirect light and specular). With Adjust Indirect Diffuse enabled, extracted indirect has separate controls. This can change the visible palette colors."))
    float OtherLightingWeight = 1.0f;

    // [DeferredToon] Opt-in split preserves legacy residual weights in existing assets.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Indirect", meta=(DisplayName="Adjust Indirect Diffuse", EditCondition="OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="Extract supported Lumen or dynamic sky diffuse from Other Lighting and control it independently. Does not change direct-light bands. Unsupported pixels retain the old composition."))
    bool bOverrideIndirectDiffuse = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Indirect", meta=(DisplayName="Intensity", ClampMin="0.0", UIMax="4.0", EditCondition="bOverrideIndirectDiffuse && OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="1 preserves native indirect intensity; 0 removes the extracted indirect layer."))
    float IndirectDiffuseIntensity = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Indirect", meta=(DisplayName="Color", HideAlphaChannel, EditCondition="bOverrideIndirectDiffuse && OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="In Hard Two-Tone this is the fixed lit-band color. Otherwise scene-linear tint or replacement color. White is neutral when Color Replacement is zero. Final UE exposure and tonemapping still apply."))
    FLinearColor IndirectDiffuseColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Indirect", meta=(DisplayName="Color Replacement", ClampMin="0.0", ClampMax="1.0", EditCondition="bOverrideIndirectDiffuse && IndirectDiffuseStyle == EDeferredToonIndirectDiffuseStyle::ColorGrade && OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="0 multiplies native indirect by Color; 1 replaces it with Color, including zero-GI surfaces. Material Base Color Influence affects replacement only."))
    float IndirectDiffuseColorReplaceWeight = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Indirect", meta=(DisplayName="Shading Style", DisplayPriority="1", EditCondition="bOverrideIndirectDiffuse && OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="Color Grade preserves the existing tint/replacement. Hard Two-Tone outputs only the lit or dark color, without native intensity gradients inside each band."))
    EDeferredToonIndirectDiffuseStyle IndirectDiffuseStyle = EDeferredToonIndirectDiffuseStyle::ColorGrade;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Indirect", meta=(DisplayName="Two-Tone Threshold", ClampMin="0.0", UIMax="1.0", EditCondition="bOverrideIndirectDiffuse && IndirectDiffuseStyle == EDeferredToonIndirectDiffuseStyle::TwoTone && OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="Hard threshold of scene-linear indirect diffuse luminance after box AO adjustments, before authored intensity. This input includes material color: dark materials may select the dark band. Zero light always selects dark. Not normalized; HDR values may exceed 1."))
    float IndirectDiffuseThreshold = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Indirect", meta=(DisplayName="Dark Color", HideAlphaChannel, EditCondition="bOverrideIndirectDiffuse && IndirectDiffuseStyle == EDeferredToonIndirectDiffuseStyle::TwoTone && OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="Fixed scene-linear dark-band color. Existing Color supplies the lit band. Two-Tone ignores material color influence and color replacement; UE exposure and temporal antialiasing still apply."))
    FLinearColor IndirectDiffuseDarkColor = FLinearColor::Black;


    // [DeferredToon] One authoring interface, selected by actual per-pixel provider validity.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Occlusion", meta=(DisplayName="Occlusion Control", DisplayPriority="0", EditCondition="OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="Unified automatically uses Lumen short-range AO or dynamic SkyLight occlusion. Native disables all box AO adjustments. Legacy preserves older separate settings."))
    EDeferredToonIndirectOcclusionMode IndirectOcclusionMode = EDeferredToonIndirectOcclusionMode::Native;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Occlusion", meta=(DisplayName="Occlusion Strength", DisplayPriority="1", ClampMin="0.0", ClampMax="1.0", EditCondition="IndirectOcclusionMode == EDeferredToonIndirectOcclusionMode::Unified && OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="1 preserves native diffuse occlusion; 0 restores diffuse before the selected independent occlusion. Traced GI shadows remain. Works without enabling indirect color grading; full color replacement can hide the difference."))
    float IndirectOcclusionStrength = 1.0f;

    // [DeferredToon] Optional sky-only occlusion grading, separate from diffuse color.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Occlusion", AdvancedDisplay, meta=(DisplayName="Adjust Sky Occlusion Separately", EditCondition="IndirectOcclusionMode == EDeferredToonIndirectOcclusionMode::Legacy && bOverrideIndirectDiffuse && OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="SkyLight only. Lumen and unsupported pixels keep their original diffuse result. Enable independent indirect diffuse first."))
    bool bOverrideSkyOcclusion = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Occlusion", AdvancedDisplay, meta=(DisplayName="Occlusion Strength", ClampMin="0.0", ClampMax="1.0", EditCondition="IndirectOcclusionMode == EDeferredToonIndirectOcclusionMode::Legacy && bOverrideSkyOcclusion && bOverrideIndirectDiffuse && OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="1 preserves native sky occlusion; 0 restores sky diffuse before local AO, bent-normal redirection and occlusion tint. Cloud shadowing remains."))
    float SkyOcclusionStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Occlusion", AdvancedDisplay, meta=(DisplayName="Occlusion Color", HideAlphaChannel, EditCondition="IndirectOcclusionMode == EDeferredToonIndirectOcclusionMode::Legacy && bOverrideSkyOcclusion && bOverrideIndirectDiffuse && OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="Scene-linear replacement color for the occluded portion of indirect diffuse. Does not tint direct light or reflections."))
    FLinearColor SkyOcclusionColor = FLinearColor::Black;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Occlusion", AdvancedDisplay, meta=(DisplayName="Color Replacement", ClampMin="0.0", ClampMax="1.0", EditCondition="IndirectOcclusionMode == EDeferredToonIndirectOcclusionMode::Legacy && bOverrideSkyOcclusion && bOverrideIndirectDiffuse && OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="0 preserves graded indirect color. 1 applies Occlusion Color weighted by native combined sky occlusion and Occlusion Strength. White AO pixels remain unchanged."))
    float SkyOcclusionColorReplaceWeight = 0.0f;

    // [DeferredToon] Stage one: native visibility preview and strength only.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Occlusion", AdvancedDisplay, meta=(DisplayName="Adjust Lumen Short Range AO", EditCondition="IndirectOcclusionMode == EDeferredToonIndirectOcclusionMode::Legacy && bOverrideIndirectDiffuse && OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="Enable independent indirect diffuse first. Controls only Lumen's separate short-range diffuse AO; traced GI shadows, direct light and specular remain unchanged."))
    bool bOverrideLumenShortRangeAO = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Occlusion", AdvancedDisplay, meta=(DisplayName="Short Range AO Strength", ClampMin="0.0", ClampMax="1.0", EditCondition="IndirectOcclusionMode == EDeferredToonIndirectOcclusionMode::Legacy && bOverrideLumenShortRangeAO && bOverrideIndirectDiffuse && OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", EditConditionHides, ToolTip="1 preserves the native result; 0 removes only the separate short-range diffuse AO. Missing data preserves the original lighting. Indirect color replacement at 1 hides the lighting difference."))
    float LumenShortRangeAOStrength = 1.0f;



    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Textures", meta=(DisplayName="Shadow Texture (2D)", DisplayPriority="0", EditCondition="(OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade || OutputMode == EDeferredToonVolumeOutputMode::ToonRecompose) || OutputMode == EDeferredToonVolumeOutputMode::NativeComposite", EditConditionHides, ToolTip="Optional box-local triplanar texture. Each band controls its texture influence; in two-band mode it affects the shadow region."))
    TObjectPtr<UTexture2D> ShadowTexture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Textures", meta=(ClampMin="0.0", ClampMax="1.0", ToolTip="Blend texture RGB into the shadow target color, also weighted by texture alpha.", DisplayName="Texture Blend", DisplayPriority="1", EditCondition="ShadowTexture != nullptr && ((OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade || OutputMode == EDeferredToonVolumeOutputMode::ToonRecompose) || OutputMode == EDeferredToonVolumeOutputMode::NativeComposite)", EditConditionHides))
    float ShadowTextureWeight = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Textures", meta=(ToolTip="Repeats per box projection. Triplanar projections avoid stretched side walls.", DisplayName="Texture Tiling", DisplayPriority="2", EditCondition="ShadowTexture != nullptr && ((OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade || OutputMode == EDeferredToonVolumeOutputMode::ToonRecompose) || OutputMode == EDeferredToonVolumeOutputMode::NativeComposite)", EditConditionHides))
    FVector2D ShadowTextureTiling = FVector2D(1.0, 1.0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Textures", meta=(ToolTip="UV offset shared by box-local triplanar projections.", DisplayName="Texture Offset", DisplayPriority="3", EditCondition="ShadowTexture != nullptr && ((OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade || OutputMode == EDeferredToonVolumeOutputMode::ToonRecompose) || OutputMode == EDeferredToonVolumeOutputMode::NativeComposite)", EditConditionHides))
    FVector2D ShadowTextureOffset = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Debug", meta=(DisplayName="Buffer Preview", DisplayPriority="0", ToolTip="Local buffer preview. Console / Global follows the debug remote or CVar; Selected Output shows the current output mode. 24 displays the selected band."))
    EDeferredToonDebugView DebugView;

    // [DeferredToon] This is the editor-facing switch for the authoring UI.
    // StyleProfile remains the reusable asset path; InlineLayers is intended
    // for fast iteration directly on the placed volume.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Style Lab", meta=(DisplayName="Layer Source", DisplayPriority="0", EditCondition="(OutputMode == EDeferredToonVolumeOutputMode::Replace || DebugView == EDeferredToonDebugView::StyleMask || DebugView == EDeferredToonDebugView::StyleColor || DebugView == EDeferredToonDebugView::ConsoleGlobal)", EditConditionHides))
    EDeferredToonStyleSource StyleSource;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Style Lab", meta=(EditConditionHides, DisplayName="Layer Profile", DisplayPriority="1", EditCondition="(OutputMode == EDeferredToonVolumeOutputMode::Replace || DebugView == EDeferredToonDebugView::StyleMask || DebugView == EDeferredToonDebugView::StyleColor || DebugView == EDeferredToonDebugView::ConsoleGlobal) && StyleSource == EDeferredToonStyleSource::StyleProfile"))
    TObjectPtr<UDeferredToonStyleProfile> StyleProfile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Style Lab", meta=(EditConditionHides, TitleProperty="InputId", DisplayName="Input Layers", DisplayPriority="2", EditCondition="(OutputMode == EDeferredToonVolumeOutputMode::Replace || DebugView == EDeferredToonDebugView::StyleMask || DebugView == EDeferredToonDebugView::StyleColor || DebugView == EDeferredToonDebugView::ConsoleGlobal) && StyleSource == EDeferredToonStyleSource::InlineLayers"))
    TArray<FDeferredToonStyleInputLayer> StyleLayers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Bounds", AdvancedDisplay, meta=(ClampMin="-2147483648", ClampMax="2147483647", DisplayName="Overlap Priority", DisplayPriority="3", ToolTip="Controls processing order when volumes overlap. Higher values are collected and processed first; subsequent volumes can blend over them."))
    int32 Priority;

    /** Stable authoring ID used as the final deterministic sort key. */
    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category="Toon Advanced", meta=(ClampMin="0", DisplayName="Stable Sort ID", DisplayPriority="1", ToolTip="Automatic stable identifier used to break equal-priority ties. Not a shading control."))
    int32 ObjectID;

    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetEnabled(bool bNewEnabled);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetEdgeFalloff(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetBoxExtent(FVector NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetFalloffCurve(UCurveFloat* NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetShadowColorVolume(UVolumeTexture* NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetShadowColorVolumeWeight(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetLightingColorCurve(UCurveLinearColor* NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetShadowColorCurve(UCurveLinearColor* NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume", meta=(DeprecatedFunction, DeprecationMessage="Retired compatibility entry; use Effective Light Threshold. This call has no rendering effect.")) ENGINE_API void SetDirectLightMaskThreshold(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume", meta=(DeprecatedFunction, DeprecationMessage="Retired compatibility entry; use Effective Light Threshold. This call has no rendering effect.")) ENGINE_API void SetDirectLightMaskSoftness(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetBlendWeight(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetOutputMode(EDeferredToonVolumeOutputMode NewValue);
    // [DeferredToon] Runtime controls invalidate the render-thread snapshot.
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetBaseColorWeight(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetToonShadowThreshold(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetDebugView(EDeferredToonDebugView NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetStyleSource(EDeferredToonStyleSource NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetStyleProfile(UDeferredToonStyleProfile* NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetPriority(int32 NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetObjectID(int32 NewValue);

    // [DeferredToon] Runtime setters refresh the immutable render proxy.
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume", meta=(DeprecatedFunction, DeprecationMessage="Retired compatibility entry; use Effective Light Threshold. This call has no rendering effect.")) ENGINE_API void SetUseEffectiveLighting(bool NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetEffectiveLightThreshold(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetLitColorReplaceWeight(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetShadowColorReplaceWeight(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetOtherLightingWeight(float NewValue);
    // [DeferredToon] Runtime setters invalidate the render-thread snapshot.
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetOverrideIndirectDiffuse(bool NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetIndirectDiffuseIntensity(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetIndirectDiffuseStyle(EDeferredToonIndirectDiffuseStyle NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetIndirectDiffuseThreshold(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetIndirectDiffuseDarkColor(FLinearColor NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetIndirectDiffuseColor(FLinearColor NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetIndirectDiffuseColorReplaceWeight(float NewValue);
    // [DeferredToon] Runtime authoring invalidates the scene proxy.
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetIndirectOcclusionMode(EDeferredToonIndirectOcclusionMode NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetIndirectOcclusionStrength(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetOverrideSkyOcclusion(bool NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetSkyOcclusionStrength(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetSkyOcclusionColor(FLinearColor NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetSkyOcclusionColorReplaceWeight(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetOverrideLumenShortRangeAO(bool NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetLumenShortRangeAOStrength(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetShadowTexture(UTexture2D* NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetShadowTextureWeight(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetShadowTextureTiling(FVector2D NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetShadowTextureOffset(FVector2D NewValue);

    // [DeferredToon] Editor presets and runtime APIs share the same proxy invalidation.
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Toon Bands", meta=(DisplayName="Reset to Four Bands", ToolTip="Replaces the current band array with the four-band preset and enables it. Existing colors and other output controls are retained. Undo is supported."))
    ENGINE_API void ApplyFourLightBands();
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Toon Bands", meta=(DisplayName="Use Two Bands", ToolTip="Disables multiple bands without deleting your saved band array. Undo is supported."))
    ENGINE_API void UseTwoBandCompatibility();
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume|Light Bands") ENGINE_API void SetUseLightBands(bool NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume|Light Bands") ENGINE_API void SetLightBands(const TArray<FDeferredToonLightBand>& NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume|Light Bands") ENGINE_API void SetLightBandColorCurve(UCurveLinearColor* NewValue);

    UFUNCTION(BlueprintPure, Category="Deferred Toon Volume") int32 GetObjectID() const { return ObjectID; }
    ENGINE_API FDeferredToonVolumeSceneProxy* CreateSceneProxy();

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

protected:
    // [DeferredToon] Migrate retired serialized output without reusing its enum value.
    // [DeferredToon] Version old assets without changing their saved appearance.
    virtual void Serialize(FArchive& Ar) override;
    virtual void PostLoad() override;
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
    virtual void SendRenderTransform_Concurrent() override;
    virtual void DestroyRenderState_Concurrent() override;

    void BindCurveDelegates();
    void UnbindCurveDelegates();
    void OnReferencedCurveChanged(UCurveBase* Curve, EPropertyChangeType::Type ChangeType);
#if WITH_EDITORONLY_DATA
    FDelegateHandle FalloffCurveChangedHandle;
    FDelegateHandle LightingColorCurveChangedHandle;
    FDelegateHandle ShadowColorCurveChangedHandle;
    FDelegateHandle LightBandColorCurveChangedHandle; // [DeferredToon]
    TWeakObjectPtr<UCurveBase> BoundFalloffCurve;
    TWeakObjectPtr<UCurveBase> BoundLightingColorCurve;
    TWeakObjectPtr<UCurveBase> BoundShadowColorCurve;
    TWeakObjectPtr<UCurveBase> BoundLightBandColorCurve; // [DeferredToon]
#endif

private:
#if WITH_EDITOR
    void HandlePostEditChange(FName PropertyName);
#endif
    void SendRenderTransformCommand();
    FDeferredToonVolumeSceneProxy* DeferredToonVolumeSceneProxy;
};


