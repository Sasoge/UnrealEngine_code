#include "Components/DeferredToonVolumeComponent.h"
#include "Engine/DeferredToonVolume.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h" // [DeferredToon]
#include "Curves/CurveBase.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "SceneInterface.h"
#include "UObject/UnrealType.h"
#include "RenderCommandFence.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DeferredToonVolumeComponent)

// [DeferredToon] Render-state hooks mirror the component into the render-thread scene.
// Free functions keep the asset-facing component independent of renderer implementation.
ENGINE_API FDeferredToonVolumeSceneProxy* CreateDeferredToonVolumeSceneProxy(const UDeferredToonVolumeComponent* Component);
ENGINE_API void AddDeferredToonVolumeProxy(FSceneInterface* Scene, FDeferredToonVolumeSceneProxy* Proxy);
ENGINE_API void RemoveDeferredToonVolumeProxy(FSceneInterface* Scene, FDeferredToonVolumeSceneProxy* Proxy);
ENGINE_API void UpdateDeferredToonVolumeProxyTransform(FDeferredToonVolumeSceneProxy* Proxy, const FTransform& Transform);
ENGINE_API void DestroyDeferredToonVolumeSceneProxy(FDeferredToonVolumeSceneProxy* Proxy);

UDeferredToonVolumeComponent::UDeferredToonVolumeComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , bEnabled(true)
    , EdgeFalloff(0.25f)
    , BoxExtent(100.0f)
    , FalloffCurve(nullptr)
    , ShadowColorVolume(nullptr)
    , ShadowColorVolumeWeight(0.0f)
    , LightingColorCurve(nullptr)
    , ShadowColorCurve(nullptr)
    // [DeferredToon] Retired serialized fields; not used by the renderer.
    , DirectLightMaskThreshold(0.5f)
    , DirectLightMaskSoftness(0.1f)
    , BlendWeight(1.0f)
    , OutputMode(EDeferredToonVolumeOutputMode::NativeComposite)
    // [DeferredToon] Start with pure two-color output and a midpoint shadow cut.
    , BaseColorWeight(0.0f)
    , ToonShadowThreshold(0.5f)
    , DebugView(EDeferredToonDebugView::ConsoleGlobal)
    , StyleSource(EDeferredToonStyleSource::StyleProfile)
    , StyleProfile(nullptr)
    , Priority(0)
    , ObjectID(0)
    , DeferredToonVolumeSceneProxy(nullptr)
{
    Mobility = EComponentMobility::Movable;
}

// [DeferredToon] Preserve old asset IDs but retire the incorrect diffuse reconstruction.
void UDeferredToonVolumeComponent::PostLoad()
{
    Super::PostLoad();
    if (OutputMode == EDeferredToonVolumeOutputMode::RecomposeDiffuse)
    {
        OutputMode = EDeferredToonVolumeOutputMode::DirectLightGrade;
    }
}

// [DeferredToon] Curve delegates keep LUT data live while an editor asset is being edited.
static int32 GetDeferredToonVolumePathObjectID(const UDeferredToonVolumeComponent* Component)
{
    int32 StableID = static_cast<int32>(GetTypeHash(Component->GetPathName()) & 0x7fffffffU);
    return StableID != 0 ? StableID : 1;
}

void UDeferredToonVolumeComponent::OnRegister()
{
    Super::OnRegister();
#if WITH_EDITORONLY_DATA
    BindCurveDelegates();
#endif
    if (ObjectID == 0 && !HasAnyFlags(RF_ClassDefaultObject))
    {
        // GetPathName is serialized from the actor/component names, so this is stable
        // across runs without relying on a process-local object pointer.
        ObjectID = GetDeferredToonVolumePathObjectID(this);
    }
}

void UDeferredToonVolumeComponent::BindCurveDelegates()
{
#if WITH_EDITORONLY_DATA
    UnbindCurveDelegates();
    // Curve assets broadcast key edits through OnUpdateCurve, so the render proxy
    // is rebuilt immediately without requiring the asset reference to be reassigned.
    if (FalloffCurve)
    {
        BoundFalloffCurve = FalloffCurve;
        FalloffCurveChangedHandle = FalloffCurve->OnUpdateCurve.AddUObject(this, &UDeferredToonVolumeComponent::OnReferencedCurveChanged);
    }
    if (LightingColorCurve)
    {
        BoundLightingColorCurve = LightingColorCurve;
        LightingColorCurveChangedHandle = LightingColorCurve->OnUpdateCurve.AddUObject(this, &UDeferredToonVolumeComponent::OnReferencedCurveChanged);
    }
    if (ShadowColorCurve)
    {
        BoundShadowColorCurve = ShadowColorCurve;
        ShadowColorCurveChangedHandle = ShadowColorCurve->OnUpdateCurve.AddUObject(this, &UDeferredToonVolumeComponent::OnReferencedCurveChanged);
    }
    // [DeferredToon] Editing the band palette must refresh CPU-sampled band colors.
    if (LightBandColorCurve)
    {
        BoundLightBandColorCurve = LightBandColorCurve;
        LightBandColorCurveChangedHandle = LightBandColorCurve->OnUpdateCurve.AddUObject(this, &UDeferredToonVolumeComponent::OnReferencedCurveChanged);
    }
#endif
}

void UDeferredToonVolumeComponent::UnbindCurveDelegates()
{
#if WITH_EDITORONLY_DATA
    if (BoundFalloffCurve.IsValid() && FalloffCurveChangedHandle.IsValid()) BoundFalloffCurve->OnUpdateCurve.Remove(FalloffCurveChangedHandle);
    if (BoundLightingColorCurve.IsValid() && LightingColorCurveChangedHandle.IsValid()) BoundLightingColorCurve->OnUpdateCurve.Remove(LightingColorCurveChangedHandle);
    if (BoundShadowColorCurve.IsValid() && ShadowColorCurveChangedHandle.IsValid()) BoundShadowColorCurve->OnUpdateCurve.Remove(ShadowColorCurveChangedHandle);
    if (BoundLightBandColorCurve.IsValid() && LightBandColorCurveChangedHandle.IsValid()) BoundLightBandColorCurve->OnUpdateCurve.Remove(LightBandColorCurveChangedHandle); // [DeferredToon]
    FalloffCurveChangedHandle.Reset();
    LightingColorCurveChangedHandle.Reset();
    ShadowColorCurveChangedHandle.Reset();
    BoundFalloffCurve.Reset();
    BoundLightingColorCurve.Reset();
    BoundShadowColorCurve.Reset();
    LightBandColorCurveChangedHandle.Reset(); // [DeferredToon]
    BoundLightBandColorCurve.Reset();
#endif
}

void UDeferredToonVolumeComponent::OnReferencedCurveChanged(UCurveBase* Curve, EPropertyChangeType::Type ChangeType)
{
#if WITH_EDITORONLY_DATA
    MarkRenderStateDirty();
#endif
}

FDeferredToonVolumeSceneProxy* UDeferredToonVolumeComponent::CreateSceneProxy()
{
    return CreateDeferredToonVolumeSceneProxy(this);
}

void UDeferredToonVolumeComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
    Super::CreateRenderState_Concurrent(Context);
    bool bHidden = false;
#if WITH_EDITORONLY_DATA
    bHidden = GetOwner() ? GetOwner()->bHiddenEdLevel : false;
#endif
    const bool bShouldAddToScene = ShouldComponentAddToScene();
    if (!bShouldAddToScene) bHidden = true;
    if (GetVisibleFlag() && !bHidden && bShouldAddToScene && ShouldRender() && IsRegistered() &&
        (GetOuter() == nullptr || !GetOuter()->HasAnyFlags(RF_ClassDefaultObject)))
    {
        DeferredToonVolumeSceneProxy = CreateSceneProxy();
        if (DeferredToonVolumeSceneProxy && GetWorld() && GetWorld()->Scene)
        {
            AddDeferredToonVolumeProxy(GetWorld()->Scene, DeferredToonVolumeSceneProxy);
        }
    }
}

void UDeferredToonVolumeComponent::SendRenderTransformCommand()
{
    if (DeferredToonVolumeSceneProxy)
    {
        FDeferredToonVolumeSceneProxy* Proxy = DeferredToonVolumeSceneProxy;
        const FTransform Transform = GetComponentTransform();
        ENQUEUE_RENDER_COMMAND(FUpdateDeferredToonVolumeProxyTransform)([Proxy, Transform](FRHICommandList& RHICmdList)
        {
            UpdateDeferredToonVolumeProxyTransform(Proxy, Transform);
        });
    }
}

void UDeferredToonVolumeComponent::SendRenderTransform_Concurrent()
{
    Super::SendRenderTransform_Concurrent();
    SendRenderTransformCommand();
}

void UDeferredToonVolumeComponent::DestroyRenderState_Concurrent()
{
    Super::DestroyRenderState_Concurrent();
    if (DeferredToonVolumeSceneProxy)
    {
        FDeferredToonVolumeSceneProxy* Proxy = DeferredToonVolumeSceneProxy;
        if (GetWorld() && GetWorld()->Scene) RemoveDeferredToonVolumeProxy(GetWorld()->Scene, Proxy);
        ENQUEUE_RENDER_COMMAND(FDestroyDeferredToonVolumeProxy)([Proxy](FRHICommandList& RHICmdList)
        {
            DestroyDeferredToonVolumeSceneProxy(Proxy);
        });
        DeferredToonVolumeSceneProxy = nullptr;
    }
}

void UDeferredToonVolumeComponent::OnUnregister()
{
#if WITH_EDITORONLY_DATA
    UnbindCurveDelegates();
#endif
    Super::OnUnregister();
}

#define DTOON_SET(Member, Value, Changed) do { if (AreDynamicDataChangesAllowed() && Member != (Value)) { Member = (Value); Changed; } } while (false)
void UDeferredToonVolumeComponent::SetEnabled(bool bNewEnabled) { DTOON_SET(bEnabled, bNewEnabled, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetEdgeFalloff(float NewValue) { DTOON_SET(EdgeFalloff, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetBoxExtent(FVector NewValue) { DTOON_SET(BoxExtent, NewValue.ComponentMax(FVector(1.0f)), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetFalloffCurve(UCurveFloat* NewValue) { DTOON_SET(FalloffCurve, NewValue, BindCurveDelegates(); MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetShadowColorVolume(UVolumeTexture* NewValue) { DTOON_SET(ShadowColorVolume, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetShadowColorVolumeWeight(float NewValue) { DTOON_SET(ShadowColorVolumeWeight, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetLightingColorCurve(UCurveLinearColor* NewValue) { DTOON_SET(LightingColorCurve, NewValue, BindCurveDelegates(); MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetShadowColorCurve(UCurveLinearColor* NewValue) { DTOON_SET(ShadowColorCurve, NewValue, BindCurveDelegates(); MarkRenderStateDirty()); }
// [DeferredToon] DirectLight mask setters keep the render proxy in sync.
void UDeferredToonVolumeComponent::SetDirectLightMaskThreshold(float NewValue) { DirectLightMaskThreshold = NewValue; } // [DeferredToon] Compatibility only.
void UDeferredToonVolumeComponent::SetDirectLightMaskSoftness(float NewValue) { DirectLightMaskSoftness = NewValue; } // [DeferredToon] Compatibility only.
void UDeferredToonVolumeComponent::SetBlendWeight(float NewValue) { DTOON_SET(BlendWeight, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetOutputMode(EDeferredToonVolumeOutputMode NewValue) { DTOON_SET(OutputMode, NewValue == EDeferredToonVolumeOutputMode::RecomposeDiffuse ? EDeferredToonVolumeOutputMode::DirectLightGrade : NewValue, MarkRenderStateDirty()); }
// [DeferredToon] Keep independent reconstruction controls synchronized.
void UDeferredToonVolumeComponent::SetBaseColorWeight(float NewValue) { DTOON_SET(BaseColorWeight, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetToonShadowThreshold(float NewValue) { DTOON_SET(ToonShadowThreshold, FMath::Clamp(NewValue, 0.001f, 1.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetDebugView(EDeferredToonDebugView NewValue) { DTOON_SET(DebugView, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetStyleSource(EDeferredToonStyleSource NewValue) { DTOON_SET(StyleSource, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetStyleProfile(UDeferredToonStyleProfile* NewValue) { DTOON_SET(StyleProfile, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetPriority(int32 NewValue) { DTOON_SET(Priority, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetObjectID(int32 NewValue)
{
    // Task 2 uses the object-path hash as a secondary key for duplicate explicit IDs;
    // never use a runtime pointer because pointer values are not deterministic.
    const int32 RequestedID = FMath::Max(0, NewValue);
    const int32 EffectiveID = (RequestedID == 0 && IsRegistered()) ? GetDeferredToonVolumePathObjectID(this) : RequestedID;
    DTOON_SET(ObjectID, EffectiveID, MarkRenderStateDirty());
}
// [DeferredToon] All mapping edits rebuild the render-thread snapshot.
void UDeferredToonVolumeComponent::SetUseEffectiveLighting(bool NewValue) { bUseEffectiveLighting = NewValue; } // [DeferredToon] Compatibility only.
void UDeferredToonVolumeComponent::SetEffectiveLightThreshold(float NewValue) { DTOON_SET(EffectiveLightThreshold, FMath::Max(0.0f, NewValue), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetLitColorReplaceWeight(float NewValue) { DTOON_SET(LitColorReplaceWeight, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetShadowColorReplaceWeight(float NewValue) { DTOON_SET(ShadowColorReplaceWeight, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetOtherLightingWeight(float NewValue) { DTOON_SET(OtherLightingWeight, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
// [DeferredToon] Indirect authoring is independent from direct band selection.
void UDeferredToonVolumeComponent::SetOverrideIndirectDiffuse(bool NewValue) { DTOON_SET(bOverrideIndirectDiffuse, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetIndirectDiffuseIntensity(float NewValue) { DTOON_SET(IndirectDiffuseIntensity, FMath::Max(NewValue, 0.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetIndirectDiffuseColor(FLinearColor NewValue) { DTOON_SET(IndirectDiffuseColor, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetIndirectDiffuseColorReplaceWeight(float NewValue) { DTOON_SET(IndirectDiffuseColorReplaceWeight, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
// [DeferredToon] Sky occlusion controls.
void UDeferredToonVolumeComponent::SetOverrideSkyOcclusion(bool NewValue) { DTOON_SET(bOverrideSkyOcclusion, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetSkyOcclusionStrength(float NewValue) { DTOON_SET(SkyOcclusionStrength, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetSkyOcclusionColor(FLinearColor NewValue) { DTOON_SET(SkyOcclusionColor, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetSkyOcclusionColorReplaceWeight(float NewValue) { DTOON_SET(SkyOcclusionColorReplaceWeight, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetShadowTexture(UTexture2D* NewValue) { DTOON_SET(ShadowTexture, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetShadowTextureWeight(float NewValue) { DTOON_SET(ShadowTextureWeight, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetShadowTextureTiling(FVector2D NewValue) { DTOON_SET(ShadowTextureTiling, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetShadowTextureOffset(FVector2D NewValue) { DTOON_SET(ShadowTextureOffset, NewValue, MarkRenderStateDirty()); }
// [DeferredToon] Threshold edits never rebuild the underlying lighting RTs.
void UDeferredToonVolumeComponent::SetUseLightBands(bool NewValue) { DTOON_SET(bUseLightBands, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetLightBandColorCurve(UCurveLinearColor* NewValue) { DTOON_SET(LightBandColorCurve, NewValue, BindCurveDelegates(); MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetLightBands(const TArray<FDeferredToonLightBand>& NewValue)
{
    if (AreDynamicDataChangesAllowed())
    {
        LightBands = NewValue;
        if (LightBands.Num() > MaxLightBands) LightBands.SetNum(MaxLightBands);
        MarkRenderStateDirty();
    }
}

void UDeferredToonVolumeComponent::ApplyFourLightBands()
{
    if (!AreDynamicDataChangesAllowed()) return;
    Modify(); // [DeferredToon] Participate in editor undo/dirty tracking, never auto-save.
    const float Minima[] = { 0.0f, 0.1f, 0.4f, 1.0f };
    const float Values[] = { 0.0f, 0.3f, 0.6f, 1.0f };
    LightBands.SetNum(4);
    for (int32 Index = 0; Index < 4; ++Index)
    {
        LightBands[Index] = FDeferredToonLightBand();
        LightBands[Index].MinResponse = Minima[Index];
        LightBands[Index].Value = Values[Index];
        LightBands[Index].ShadowMappingWeight = 1.0f - Values[Index];
    }
    bUseLightBands = true;
    MarkRenderStateDirty();
}

void UDeferredToonVolumeComponent::UseTwoBandCompatibility()
{
    if (!AreDynamicDataChangesAllowed()) return;
    Modify();
    bUseLightBands = false; // [DeferredToon] Keep the authored palette for later reuse.
    MarkRenderStateDirty();
}
#undef DTOON_SET

#if WITH_EDITOR
void UDeferredToonVolumeComponent::HandlePostEditChange(FName PropertyName)
{
    if (PropertyName == TEXT("RelativeLocation") ||
        PropertyName == TEXT("RelativeRotation") ||
        PropertyName == TEXT("RelativeScale3D"))
    {
        SendRenderTransformCommand();
    }
    else
    {
#if WITH_EDITORONLY_DATA
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UDeferredToonVolumeComponent, FalloffCurve) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(UDeferredToonVolumeComponent, LightingColorCurve) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(UDeferredToonVolumeComponent, ShadowColorCurve) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(UDeferredToonVolumeComponent, LightBandColorCurve)) // [DeferredToon]
        {
            BindCurveDelegates();
        }
#endif
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UDeferredToonVolumeComponent, BoxExtent))
        {
            // [DeferredToon] Do not search for an arbitrary UBoxComponent here.
            // The actor owns a dedicated visualization proxy; keeping it synced
            // explicitly prevents the editor frame and render proxy from drifting.
            if (ADeferredToonVolume* Owner = Cast<ADeferredToonVolume>(GetOwner()))
            {
                Owner->SyncEditorBounds();
            }
        }
        MarkRenderStateDirty();
    }
}

void UDeferredToonVolumeComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName().IsNone()
        ? PropertyChangedEvent.GetPropertyName()
        : PropertyChangedEvent.GetMemberPropertyName();
    HandlePostEditChange(PropertyName);
}

void UDeferredToonVolumeComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
    Super::PostEditChangeChainProperty(PropertyChangedEvent);
    // [DeferredToon] Editing BoxExtent.X/Y/Z reaches this callback.  Use the
    // owning vector (MemberProperty) so the visualization box follows each
    // interactive component edit immediately.
    const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName().IsNone()
        ? PropertyChangedEvent.GetPropertyName()
        : PropertyChangedEvent.GetMemberPropertyName();
    HandlePostEditChange(PropertyName);
}
#endif

