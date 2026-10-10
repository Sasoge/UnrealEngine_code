#include "Engine/DeferredToonVolume.h"
#include "Components/DeferredToonVolumeComponent.h"
#include "Components/BoxComponent.h"

// [DeferredToon] Editor-only bounds visualization is excluded from runtime render passes.
#include UE_INLINE_GENERATED_CPP_BY_NAME(DeferredToonVolume)

ADeferredToonVolume::ADeferredToonVolume(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    DeferredToonVolumeComponent = CreateDefaultSubobject<UDeferredToonVolumeComponent>(TEXT("DeferredToonVolumeComponent"));
    RootComponent = DeferredToonVolumeComponent;

#if WITH_EDITORONLY_DATA
    VolumeBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("VolumeBounds"));
    VolumeBounds->SetupAttachment(DeferredToonVolumeComponent);
    VolumeBounds->SetBoxExtent(DeferredToonVolumeComponent->BoxExtent);
    VolumeBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    VolumeBounds->SetGenerateOverlapEvents(false);
    VolumeBounds->SetCanEverAffectNavigation(false);
    VolumeBounds->SetHiddenInGame(true);
    VolumeBounds->SetVisibility(true);
    // Keep the selected editor visualization out of every runtime render target.
    VolumeBounds->SetRenderInMainPass(false);
    VolumeBounds->SetRenderInDepthPass(false);
    VolumeBounds->SetRenderCustomDepth(false);
    VolumeBounds->bDrawOnlyIfSelected = true;
    // [DeferredToon] This component is a visualization proxy only.  Its extent is
    // driven by UDeferredToonVolumeComponent::BoxExtent, so it must not become a
    // second, independently editable source of the runtime volume bounds.
    VolumeBounds->SetIsVisualizationComponent(true);
    VolumeBounds->CastShadow = false;
    VolumeBounds->bVisibleInReflectionCaptures = false;
    VolumeBounds->bVisibleInRayTracing = false;
#endif

    PrimaryActorTick.bCanEverTick = false;
    SetHidden(false);
}

#if WITH_EDITORONLY_DATA
void ADeferredToonVolume::SyncEditorBounds()
{
    if (VolumeBounds && DeferredToonVolumeComponent)
    {
        // [DeferredToon] The custom component is the single source of truth for
        // both shader influence and the editor wireframe box.
        VolumeBounds->SetBoxExtent(DeferredToonVolumeComponent->BoxExtent, false);
        VolumeBounds->UpdateBounds();
    }
}
#endif

