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
    AmbientOcclusion = 9 UMETA(DisplayName="9 - Ambient Occlusion"),
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Toon Output", meta=(ClampMin="0.0", ClampMax="1.0", EditConditionHides, DisplayName="Keep Other Lighting", DisplayPriority="3", EditCondition="OutputMode == EDeferredToonVolumeOutputMode::DirectLightGrade", ToolTip="0 keeps only processed direct diffuse; 1 also retains the original scene residual (including indirect light and specular). This can change the visible palette colors."))
    float OtherLightingWeight = 1.0f;

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


