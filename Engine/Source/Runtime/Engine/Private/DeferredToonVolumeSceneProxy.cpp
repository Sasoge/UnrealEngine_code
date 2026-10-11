#include "DeferredToonVolumeSceneProxy.h"
#include "Components/DeferredToonVolumeComponent.h"
#include "Engine/DeferredToonInputRegistry.h"
#include "Engine/DeferredToonStyleProfile.h"
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
    OutputMode = static_cast<uint8>(Component->OutputMode);
    bUseStyleProfile = Component->StyleProfile != nullptr && Component->StyleProfile->Layers.Num() > 0;
    if (bUseStyleProfile)
    {
        const int32 LayerCount = FMath::Min(Component->StyleProfile->Layers.Num(), FDeferredToonInputStack::MaxInputLayers);
        InputStack.NumLayers = static_cast<uint32>(LayerCount);
        for (int32 Index = 0; Index < LayerCount; ++Index)
        {
            const FDeferredToonStyleInputLayer& AuthoredLayer = Component->StyleProfile->Layers[Index];
            FDeferredToonInputLayer& RuntimeLayer = InputStack.Layers[Index];
            RuntimeLayer = FDeferredToonInputRegistry::MakeFallbackLayer(
                static_cast<EDeferredToonInputId>(FMath::Clamp(AuthoredLayer.InputId, 0, 65535)));
            RuntimeLayer.Channel = static_cast<EDeferredToonInputChannel>(FMath::Clamp(AuthoredLayer.Channel, 0, 5));
            RuntimeLayer.Operation = static_cast<EDeferredToonLayerOperation>(FMath::Clamp(AuthoredLayer.Operation, 0, 2));
            RuntimeLayer.bInvert = AuthoredLayer.bInvert ? 1u : 0u;
            RuntimeLayer.Weight = FMath::Clamp(AuthoredLayer.Weight, 0.0f, 1.0f);
        }
    }
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
