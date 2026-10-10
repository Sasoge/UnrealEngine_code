#include "ScenePrivate.h"
#include "DeferredToonVolumeSceneProxy.h"
#include "HAL/IConsoleManager.h"
#include "RHICommandList.h"

// [DeferredToon] Collection, deterministic ordering, and per-view volume limits.
static TAutoConsoleVariable<int32> CVarDeferredToonVolumeMaxVisible(
    TEXT("r.Toon.DeferredVolumes.MaxVisible"),
    8,
    TEXT("Maximum deferred toon volumes collected for a view."),
    ECVF_RenderThreadSafe);

static int32 GetDeferredToonVolumeMaxVisible()
{
    return FMath::Clamp(CVarDeferredToonVolumeMaxVisible.GetValueOnRenderThread(), 1, 1024);
}

void FScene::AddDeferredToonVolume(FDeferredToonVolumeSceneProxy* Proxy)
{
    check(Proxy);
    FScene* Scene = this;
    ENQUEUE_RENDER_COMMAND(FAddDeferredToonVolumeCommand)([Scene, Proxy](FRHICommandListImmediate&)
    {
        check(!Scene->DeferredToonVolumes.Contains(Proxy));
        Scene->DeferredToonVolumes.Add(Proxy);
    });
}

void FScene::RemoveDeferredToonVolume(FDeferredToonVolumeSceneProxy* Proxy)
{
    check(Proxy);
    FScene* Scene = this;
    ENQUEUE_RENDER_COMMAND(FRemoveDeferredToonVolumeCommand)([Scene, Proxy](FRHICommandListImmediate&)
    {
        Scene->DeferredToonVolumes.RemoveSingle(Proxy);
    });
}

bool FScene::HasAnyDeferredToonVolume() const
{
    return DeferredToonVolumes.Num() > 0;
}

void FScene::CollectDeferredToonVolumes(const FBox& WorldBounds, TArray<FDeferredToonVolumeSceneProxy*>& OutProxies) const
{
    OutProxies.Reset();
    for (FDeferredToonVolumeSceneProxy* Proxy : DeferredToonVolumes)
    {
        if (Proxy && Proxy->IsEnabled() && (!WorldBounds.IsValid || Proxy->GetWorldBounds().Intersect(WorldBounds)))
        {
            OutProxies.Add(Proxy);
        }
    }

    // TArray::Sort dereferences pointer elements before invoking the predicate.
    OutProxies.Sort([](const FDeferredToonVolumeSceneProxy& A, const FDeferredToonVolumeSceneProxy& B)
    {
        if (A.GetPriority() != B.GetPriority())
        {
            return A.GetPriority() > B.GetPriority();
        }
        if (A.GetObjectID() != B.GetObjectID())
        {
            return A.GetObjectID() < B.GetObjectID();
        }
        if (A.GetStableSortKey() != B.GetStableSortKey())
        {
            return A.GetStableSortKey() < B.GetStableSortKey();
        }
        if (A.GetStableSortPath() != B.GetStableSortPath())
        {
            return A.GetStableSortPath() < B.GetStableSortPath();
        }
        const FVector AOrigin = A.GetWorldTransform().GetTranslation();
        const FVector BOrigin = B.GetWorldTransform().GetTranslation();
        if (AOrigin.X != BOrigin.X) return AOrigin.X < BOrigin.X;
        if (AOrigin.Y != BOrigin.Y) return AOrigin.Y < BOrigin.Y;
        return AOrigin.Z < BOrigin.Z;
    });

    const int32 MaxVisible = GetDeferredToonVolumeMaxVisible();
    if (OutProxies.Num() > MaxVisible)
    {
        OutProxies.SetNum(MaxVisible, EAllowShrinking::No);
    }
}
