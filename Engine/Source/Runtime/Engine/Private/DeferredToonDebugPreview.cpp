#include "DeferredToonDebugPreview.h"
#include "DeferredToonDebugViews.h"
#include "RenderingThread.h"
#include "RHICommandList.h"

namespace DeferredToonDebugPreview
{
namespace { FState RenderState; uint64 NextOwnerId = 0; }
uint64 AllocateOwnerId()
{
    check(IsInGameThread());
    return ++NextOwnerId;
}
void Set(uint64 OwnerId, const UDeferredToonVolumeComponent* Target, int32 PublicIndex, int32 AOFilterMode)
{
    check(IsInGameThread());
    check(OwnerId != 0);
    if (!Target) { Clear(OwnerId); return; }
    FState State;
    State.OwnerId = OwnerId;
    State.Target = FObjectKey(Target);
    State.ShaderId = DeferredToonDebugViews::ToShaderId(PublicIndex);
    State.AOFilterMode = FMath::Clamp(AOFilterMode, 0, 5);
    State.bActive = true;
    ENQUEUE_RENDER_COMMAND(SetDeferredToonDebugPreview)([State](FRHICommandListImmediate&) { RenderState = State; });
}
void Clear(uint64 OwnerId)
{
    check(IsInGameThread());
    ENQUEUE_RENDER_COMMAND(ClearDeferredToonDebugPreview)([OwnerId](FRHICommandListImmediate&)
    {
        if (RenderState.OwnerId == OwnerId) { RenderState = FState(); }
    });
}
const FState& Get_RenderThread()
{
    check(IsInRenderingThread());
    return RenderState;
}
}
