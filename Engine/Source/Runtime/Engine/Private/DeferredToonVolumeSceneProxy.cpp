#include "DeferredToonVolumeSceneProxy.h"
#include "Components/DeferredToonVolumeComponent.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/Texture.h"
#include "Engine/VolumeTexture.h"
#include "TextureResource.h"
#include "SceneInterface.h"
#include "RHICommandList.h"

// [DeferredToon] Copies curves, bounds, transforms, and optional Texture3D resources to the render thread.
FDeferredToonVolumeSceneProxy::FDeferredToonVolumeSceneProxy(const UDeferredToonVolumeComponent* Component)
{
    check(Component);
    bEnabled = Component->bEnabled;
    EdgeFalloff = Component->EdgeFalloff;
    ShadowColorVolumeWeight = Component->ShadowColorVolumeWeight;
    BlendWeight = Component->BlendWeight;
    Priority = Component->Priority;
    ObjectID = Component->ObjectID;
    StableSortPath = Component->GetPathName();
    StableSortKey = GetTypeHash(StableSortPath);
    if (StableSortKey == 0) StableSortKey = 1;
    LocalBoxExtent = Component->BoxExtent.ComponentMax(FVector(1.0f));
    UpdateComponentTransform(Component->GetComponentTransform());

    for (int32 Index = 0; Index < CurveSampleCount; ++Index)
    {
        const float SampleTime = (float(Index) + 0.5f) / float(CurveSampleCount);
        FalloffSamples[Index] = Component->FalloffCurve ? Component->FalloffCurve->GetFloatValue(SampleTime) : 1.0f;
        LightingColorSamples[Index] = Component->LightingColorCurve ? Component->LightingColorCurve->GetLinearColorValue(SampleTime) : FLinearColor::White;
        ShadowColorSamples[Index] = Component->ShadowColorCurve ? Component->ShadowColorCurve->GetLinearColorValue(SampleTime) : FLinearColor::Black;
    }
    if (UVolumeTexture* Volume = Component->ShadowColorVolume)
    {
        if (FTextureResource* Resource = Volume->GetResource())
        {
            ShadowColorVolumeResource = Resource->TextureRHI;
            bShadowColorVolumeValid = ShadowColorVolumeResource.IsValid();
        }
    }
}

FDeferredToonVolumeSceneProxy::~FDeferredToonVolumeSceneProxy() = default;

void FDeferredToonVolumeSceneProxy::UpdateComponentTransform(const FTransform& Transform)
{
    WorldTransform = Transform;
    InverseTransform = Transform.Inverse();
    WorldBounds = FBox(-LocalBoxExtent, LocalBoxExtent).TransformBy(WorldTransform);
}

FDeferredToonVolumeSceneProxy* CreateDeferredToonVolumeSceneProxy(const UDeferredToonVolumeComponent* Component)
{
    return Component ? new FDeferredToonVolumeSceneProxy(Component) : nullptr;
}

void AddDeferredToonVolumeProxy(FSceneInterface* Scene, FDeferredToonVolumeSceneProxy* Proxy)
{
    if (Scene && Proxy) Scene->AddDeferredToonVolume(Proxy);
}

void RemoveDeferredToonVolumeProxy(FSceneInterface* Scene, FDeferredToonVolumeSceneProxy* Proxy)
{
    if (Scene && Proxy) Scene->RemoveDeferredToonVolume(Proxy);
}

void UpdateDeferredToonVolumeProxyTransform(FDeferredToonVolumeSceneProxy* Proxy, const FTransform& Transform)
{
    if (Proxy) Proxy->UpdateComponentTransform(Transform);
}

void DestroyDeferredToonVolumeSceneProxy(FDeferredToonVolumeSceneProxy* Proxy)
{
    delete Proxy;
}
