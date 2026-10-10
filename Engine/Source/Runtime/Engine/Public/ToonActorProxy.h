// ----------------------------------YK Engine Start-----------------------------------------
// ToonActor
#pragma once

#include "GameFramework/Actor.h"

class UToonActorComponent;

struct FToonActorData
{
	FVector3f Pivot;
	int32 ID;
};

class FToonActorProxy
{
protected:
	friend class FScene;
	friend class UToonActorComponent;

	const UToonActorComponent* ToonActorComponent;

	FSceneInterface* SceneInterface;

public:
	/** Initialization constructor. */
	ENGINE_API FToonActorProxy(const UToonActorComponent* InToonActorComponent);
	ENGINE_API virtual ~FToonActorProxy();
	ENGINE_API FToonActorData GetToonActorData(const FViewInfo& View) const;

	// 默认值为0
	int32 ID;
	// Actor的世界空间中心点
	FVector Pivot;
};

// ----------------------------------YK Engine End-----------------------------------------