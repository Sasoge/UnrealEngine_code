// [DeferredToon] Transient editor preview; no component properties or CVars are written.
#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"
class UDeferredToonVolumeComponent;

namespace DeferredToonDebugPreview
{
struct FState
{
    uint64 OwnerId = 0;
    FObjectKey Target;
    int32 ShaderId = 0;
    int32 AOFilterMode = 0;
    bool bActive = false;
    bool Matches(const FObjectKey& Key) const { return bActive && Target == Key; }
};
// Game-thread APIs. Ownership prevents a late tab destructor clearing a newer session.
ENGINE_API uint64 AllocateOwnerId();
ENGINE_API void Set(uint64 OwnerId, const UDeferredToonVolumeComponent* Target, int32 PublicIndex, int32 AOFilterMode);
ENGINE_API void Clear(uint64 OwnerId);
// The render thread only compares immutable keys; it must never resolve them to UObjects.
ENGINE_API const FState& Get_RenderThread();
}
