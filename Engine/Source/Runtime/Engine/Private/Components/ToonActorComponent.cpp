// ----------------------------------YK Engine Start-----------------------------------------
// ToonActor

#include "Components/ToonActorComponent.h"
#include "Runtime/Engine/Public/ToonActorProxy.h"

// Sets default values for this component's properties
UToonActorComponent::UToonActorComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	ToonActorID = 0;
	PivotOffset = FVector::Zero();
	
	// ...
}

FToonActorProxy* UToonActorComponent::CreateSceneProxy()
{
	return new FToonActorProxy(this);
}


void UToonActorComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);

	bool bHidden = false;
#if WITH_EDITORONLY_DATA
	bHidden = GetOwner() ? GetOwner()->bHiddenEdLevel : false;
#endif // WITH_EDITORONLY_DATA
	if (!ShouldComponentAddToScene())
	{
		bHidden = true;
	}

	if (GetVisibleFlag() && !bHidden && ShouldComponentAddToScene() && ShouldRender() && IsRegistered() && (GetOuter() == NULL || !GetOuter()->HasAnyFlags(RF_ClassDefaultObject)))
	{
		ToonActorProxy = CreateSceneProxy();
		GetWorld()->Scene->AddToonActor(ToonActorProxy);
	}
}

void UToonActorComponent::SendRenderTransformCommand()
{
	if (ToonActorProxy)
	{
		FVector NewActorPivot = GetPivot(true);
		FToonActorProxy* SceneProxy = ToonActorProxy;
		ENQUEUE_RENDER_COMMAND(FUpdateToonActorProxyTransformCommand)(
			[SceneProxy, NewActorPivot](FRHICommandList& RHICmdList)
			{
				// Nothing else is needed so that command could actually go.
				SceneProxy->Pivot = NewActorPivot;
			});
	}
}

void UToonActorComponent::SendRenderTransform_Concurrent()
{
	Super::SendRenderTransform_Concurrent();
	SendRenderTransformCommand();
}

void UToonActorComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();
	
	if (ToonActorProxy)
	{
		GetWorld()->Scene->RemoveToonActor(ToonActorProxy);

		FToonActorProxy* SceneProxy = ToonActorProxy;
		ENQUEUE_RENDER_COMMAND(FDestroyToonCharacterProxyCommand)(
			[SceneProxy](FRHICommandList& RHICmdList)
			{
				delete SceneProxy;
			});

		ToonActorProxy = nullptr;
	}
}

#if WITH_EDITOR

bool UToonActorComponent::CanEditChange(const FProperty* InProperty) const
{
	return Super::CanEditChange(InProperty);
}

void UToonActorComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MarkRenderStateDirty();
	
	// FProperty* Property = PropertyChangedEvent.Property;
	// const FName PropertyName = Property ? Property->GetFName() : NAME_None;
	//
	// if (PropertyName == TEXT("ActorCenter"))
	// {
	// 	SendRenderTransformCommand();
	// }
	// else
	// {
	// 	MarkRenderStateDirty();
	// }
}

#endif

void UToonActorComponent::SetID(int32 NewValue)
{
	if (AreDynamicDataChangesAllowed()
		&& ToonActorID != NewValue)
	{
		ToonActorID = NewValue;
		MarkRenderStateDirty();
	}
}

int32 UToonActorComponent::GetID() const
{
	return ToonActorID;
}

bool UToonActorComponent::SetPivot(const FVector& NewValue, bool bWorldSpace)
{
	const FVector NewPivotOffset = bWorldSpace ? NewValue - GetComponentLocation() : NewValue;
	if (AreDynamicDataChangesAllowed()
		&& NewPivotOffset != PivotOffset)
	{
		PivotOffset = NewPivotOffset;
		MarkRenderStateDirty();
		return true;
	}

	// ActorCenter没有发生改变
	return false;
}

FVector UToonActorComponent::GetPivot(bool bWorldSpace) const
{
	return bWorldSpace ? GetComponentLocation() + PivotOffset : PivotOffset;
}

// ----------------------------------YK Engine End-----------------------------------------