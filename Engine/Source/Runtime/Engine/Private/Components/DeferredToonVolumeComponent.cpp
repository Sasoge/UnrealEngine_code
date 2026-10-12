#include "Components/DeferredToonVolumeComponent.h"
#include "Engine/DeferredToonVolume.h"
#include "Engine/World.h"
#include "Curves/CurveBase.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "SceneInterface.h"
#include "UObject/UnrealType.h"
#include "RenderCommandFence.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DeferredToonVolumeComponent)

// [DeferredToon] Render-state hooks mirror the component into the render-thread scene.
// These hooks are intentionally kept as free functions: Task 2 can provide scene/proxy implementations
// without requiring this asset-facing component to depend on the concrete render implementation.
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
    // [DeferredToon] Neutral DirectLight defaults keep NativeComposite unchanged.
    , DirectLightColor(FLinearColor::White)
    , DirectLightIntensity(1.0f)
    , DirectLightThreshold(0.5f)
    , DirectLightSoftness(0.1f)
    , DirectShadowColor(FLinearColor::Black)
    , BlendWeight(1.0f)
    , OutputMode(EDeferredToonVolumeOutputMode::NativeComposite)
    , DebugView(EDeferredToonDebugView::ConsoleGlobal)
    , StyleSource(EDeferredToonStyleSource::StyleProfile)
    , StyleProfile(nullptr)
    , Priority(0)
    , ObjectID(0)
    , DeferredToonVolumeSceneProxy(nullptr)
{
    Mobility = EComponentMobility::Movable;
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
#endif
}

void UDeferredToonVolumeComponent::UnbindCurveDelegates()
{
#if WITH_EDITORONLY_DATA
    if (BoundFalloffCurve.IsValid() && FalloffCurveChangedHandle.IsValid()) BoundFalloffCurve->OnUpdateCurve.Remove(FalloffCurveChangedHandle);
    if (BoundLightingColorCurve.IsValid() && LightingColorCurveChangedHandle.IsValid()) BoundLightingColorCurve->OnUpdateCurve.Remove(LightingColorCurveChangedHandle);
    if (BoundShadowColorCurve.IsValid() && ShadowColorCurveChangedHandle.IsValid()) BoundShadowColorCurve->OnUpdateCurve.Remove(ShadowColorCurveChangedHandle);
    FalloffCurveChangedHandle.Reset();
    LightingColorCurveChangedHandle.Reset();
    ShadowColorCurveChangedHandle.Reset();
    BoundFalloffCurve.Reset();
    BoundLightingColorCurve.Reset();
    BoundShadowColorCurve.Reset();
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
// [DeferredToon] DirectLight authoring setters keep the render proxy in sync.
void UDeferredToonVolumeComponent::SetDirectLightColor(FLinearColor NewValue) { DTOON_SET(DirectLightColor, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetDirectLightIntensity(float NewValue) { DTOON_SET(DirectLightIntensity, FMath::Clamp(NewValue, 0.0f, 16.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetDirectLightThreshold(float NewValue) { DTOON_SET(DirectLightThreshold, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetDirectLightSoftness(float NewValue) { DTOON_SET(DirectLightSoftness, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetDirectShadowColor(FLinearColor NewValue) { DTOON_SET(DirectShadowColor, NewValue, MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetBlendWeight(float NewValue) { DTOON_SET(BlendWeight, FMath::Clamp(NewValue, 0.0f, 1.0f), MarkRenderStateDirty()); }
void UDeferredToonVolumeComponent::SetOutputMode(EDeferredToonVolumeOutputMode NewValue) { DTOON_SET(OutputMode, NewValue, MarkRenderStateDirty()); }
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
            PropertyName == GET_MEMBER_NAME_CHECKED(UDeferredToonVolumeComponent, ShadowColorCurve))
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

