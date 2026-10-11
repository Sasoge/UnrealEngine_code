#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/DeferredToonStyleProfile.h"
#include "DeferredToonVolumeComponent.generated.h"

class UCurveFloat;
class UCurveLinearColor;
class UCurveBase;
class UVolumeTexture;
class FDeferredToonVolumeSceneProxy;

// [DeferredToon] Explicit output policy keeps the legacy composite as the safe
// default while allowing a volume to opt into RT-driven color reconstruction.
UENUM(BlueprintType)
enum class EDeferredToonVolumeOutputMode : uint8
{
    NativeComposite UMETA(DisplayName="Native Composite"),
    RecomposeDiffuse UMETA(DisplayName="Recompose Diffuse"),
    Replace UMETA(DisplayName="Replace"),
};

// [DeferredToon] Details-panel selector for the same semantic views exposed by
// r.Toon.DeferredVolumes.DebugView.  Console / Global keeps the legacy CVar
// behavior when no per-volume override is desired.
UENUM(BlueprintType)
enum class EDeferredToonDebugView : uint8
{
    Invalid = 0 UMETA(Hidden),
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
    ConsoleGlobal = 255 UMETA(DisplayName="Console / Global"),
};

// [DeferredToon] Asset-facing component for pre-tonemap multi-box color mapping.
UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent), hidecategories=(Object, Mobility, Activation), MinimalAPI)
class UDeferredToonVolumeComponent : public USceneComponent
{
    GENERATED_UCLASS_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume", meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
    bool bEnabled;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume", meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
    float EdgeFalloff;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume", meta=(ClampMin="1.0", UIMin="1.0", UIMax="100000.0"))
    FVector BoxExtent;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume")
    TObjectPtr<UCurveFloat> FalloffCurve;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume")
    TObjectPtr<UVolumeTexture> ShadowColorVolume;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume", meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
    float ShadowColorVolumeWeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume")
    TObjectPtr<UCurveLinearColor> LightingColorCurve;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume")
    TObjectPtr<UCurveLinearColor> ShadowColorCurve;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume", meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
    float BlendWeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume")
    EDeferredToonVolumeOutputMode OutputMode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume|Debug")
    EDeferredToonDebugView DebugView;

    // [DeferredToon] This is the editor-facing switch for the authoring UI.
    // StyleProfile remains the reusable asset path; InlineLayers is intended
    // for fast iteration directly on the placed volume.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume|Style")
    EDeferredToonStyleSource StyleSource;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume|Style", meta=(EditCondition="StyleSource == EDeferredToonStyleSource::StyleProfile", EditConditionHides))
    TObjectPtr<UDeferredToonStyleProfile> StyleProfile;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume|Style", meta=(EditCondition="StyleSource == EDeferredToonStyleSource::InlineLayers", EditConditionHides, TitleProperty="InputId"))
    TArray<FDeferredToonStyleInputLayer> StyleLayers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume", meta=(ClampMin="-2147483648", ClampMax="2147483647"))
    int32 Priority;

    /** Stable authoring ID used as the final deterministic sort key. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Volume", meta=(ClampMin="0"))
    int32 ObjectID;

    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetEnabled(bool bNewEnabled);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetEdgeFalloff(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetBoxExtent(FVector NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetFalloffCurve(UCurveFloat* NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetShadowColorVolume(UVolumeTexture* NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetShadowColorVolumeWeight(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetLightingColorCurve(UCurveLinearColor* NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetShadowColorCurve(UCurveLinearColor* NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetBlendWeight(float NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetOutputMode(EDeferredToonVolumeOutputMode NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetDebugView(EDeferredToonDebugView NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetStyleSource(EDeferredToonStyleSource NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetStyleProfile(UDeferredToonStyleProfile* NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetPriority(int32 NewValue);
    UFUNCTION(BlueprintCallable, Category="Deferred Toon Volume") ENGINE_API void SetObjectID(int32 NewValue);

    UFUNCTION(BlueprintPure, Category="Deferred Toon Volume") int32 GetObjectID() const { return ObjectID; }
    ENGINE_API FDeferredToonVolumeSceneProxy* CreateSceneProxy();

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

protected:
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
    TWeakObjectPtr<UCurveBase> BoundFalloffCurve;
    TWeakObjectPtr<UCurveBase> BoundLightingColorCurve;
    TWeakObjectPtr<UCurveBase> BoundShadowColorCurve;
#endif

private:
#if WITH_EDITOR
    void HandlePostEditChange(FName PropertyName);
#endif
    void SendRenderTransformCommand();
    FDeferredToonVolumeSceneProxy* DeferredToonVolumeSceneProxy;
};


