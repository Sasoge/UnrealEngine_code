// ----------------------------------YK Engine Start-----------------------------------------
// ToonActor
#include "Runtime/Engine/Public/ToonActorProxy.h"

#include "Components/ToonActorComponent.h"
#include "Runtime/Renderer/Private/SceneRendering.h"

FToonActorProxy::FToonActorProxy(const UToonActorComponent* InToonActorComponent)
	: ToonActorComponent(InToonActorComponent)
	, SceneInterface(InToonActorComponent->GetScene())
	, ID(InToonActorComponent->GetID())
	, Pivot(InToonActorComponent->GetPivot(true))
{
	check(SceneInterface);
}
FToonActorProxy::~FToonActorProxy() = default;
FToonActorData FToonActorProxy::GetToonActorData(const FViewInfo& View) const
{
	FToonActorData Out;
	Out.Pivot = static_cast<FVector3f>(Pivot + View.ViewMatrices.GetPreViewTranslation());
	Out.ID = ID;
	return Out;
}

// ----------------------------------YK Engine End-----------------------------------------