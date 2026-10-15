#include "DeferredToonVolumeSceneProxy.h"
#include "Components/DeferredToonVolumeComponent.h"
#include "Engine/DeferredToonInputRegistry.h"
#include "Engine/DeferredToonStyleProfile.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h" // [DeferredToon]
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
    // [DeferredToon] Also normalize programmatic assignments that bypass the setter/PostLoad.
    OutputMode = static_cast<uint8>(Component->OutputMode == EDeferredToonVolumeOutputMode::RecomposeDiffuse
        ? EDeferredToonVolumeOutputMode::DirectLightGrade : Component->OutputMode);
    // [DeferredToon] Capture actual endpoints, not the legacy half-texel LUT
    // samples. Changing this mode must not change the legacy curve mapping.
    BaseColorWeight = FMath::Clamp(Component->BaseColorWeight, 0.0f, 1.0f);
    ToonShadowThreshold = FMath::Clamp(Component->ToonShadowThreshold, 0.001f, 1.0f);
    ToonLitColor = Component->LightingColorCurve ? Component->LightingColorCurve->GetLinearColorValue(1.0f) : FLinearColor::White;
    ToonDarkColor = Component->ShadowColorCurve ? Component->ShadowColorCurve->GetLinearColorValue(1.0f) : FLinearColor::Black;
    // [DeferredToon] Snapshot selection and color policy, never retain a UObject on the render thread.
    EffectiveLightThreshold = Component->EffectiveLightThreshold;
    LitColorReplaceWeight = Component->LitColorReplaceWeight;
    ShadowColorReplaceWeight = Component->ShadowColorReplaceWeight;
    OtherLightingWeight = Component->OtherLightingWeight;
    ShadowTextureWeight = Component->ShadowTextureWeight;
    ShadowTextureTiling = Component->ShadowTextureTiling;
    ShadowTextureOffset = Component->ShadowTextureOffset;
    if (UTexture2D* Texture = Component->ShadowTexture)
    {
        if (FTextureResource* Resource = Texture->GetResource())
        {
            ShadowTextureResource = Resource->TextureRHI;
            bShadowTextureValid = ShadowTextureResource.IsValid();
        }
    }
    DebugView = static_cast<uint8>(Component->DebugView);
    bUseStyleProfile = Component->StyleSource == EDeferredToonStyleSource::StyleProfile &&
        Component->StyleProfile != nullptr && Component->StyleProfile->Layers.Num() > 0;
    bUseInlineStyleLayers = Component->StyleSource == EDeferredToonStyleSource::InlineLayers &&
        Component->StyleLayers.Num() > 0;
    const TArray<FDeferredToonStyleInputLayer>* AuthoredLayers = nullptr;
    if (bUseStyleProfile)
    {
        AuthoredLayers = &Component->StyleProfile->Layers;
    }
    else if (bUseInlineStyleLayers)
    {
        AuthoredLayers = &Component->StyleLayers;
    }
    if (AuthoredLayers)
    {
        const int32 LayerCount = FMath::Min(AuthoredLayers->Num(), FDeferredToonInputStack::MaxInputLayers);
        InputStack.NumLayers = static_cast<uint32>(LayerCount);
        for (int32 Index = 0; Index < LayerCount; ++Index)
        {
            const FDeferredToonStyleInputLayer& AuthoredLayer = (*AuthoredLayers)[Index];
            FDeferredToonInputLayer& RuntimeLayer = InputStack.Layers[Index];
            RuntimeLayer = FDeferredToonInputRegistry::MakeFallbackLayer(
                static_cast<EDeferredToonInputId>(static_cast<uint16>(AuthoredLayer.InputId)));
            RuntimeLayer.Channel = static_cast<EDeferredToonInputChannel>(static_cast<uint8>(AuthoredLayer.Channel));
            RuntimeLayer.Operation = static_cast<EDeferredToonLayerOperation>(static_cast<uint8>(AuthoredLayer.Operation));
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
