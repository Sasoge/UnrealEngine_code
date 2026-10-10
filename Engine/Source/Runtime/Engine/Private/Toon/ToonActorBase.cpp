// ----------------------------------YK Engine Start-----------------------------------------
// ToonActor

#include "Toon/ToonActorBase.h"

#include "Components/ToonActorComponent.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ToonActorBase)

// Sets default values
AToonActorBase::AToonActorBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 设置ToonActor的默认组件为ToonActorComponent
	ToonActorComponent = CreateDefaultSubobject<UToonActorComponent>(TEXT("ToonActorComponent0"));
	SetRootComponent(ToonActorComponent);

	bShowWorldSpacePivot = false;
	ActorPivot = FVector::Zero();
}

void AToonActorBase::SetUseWorldSpaceCenter(bool bNewValue)
{
	if (bShowWorldSpacePivot == bNewValue)
	{
		return;
	}

	bShowWorldSpacePivot = bNewValue;
	// 当bUseWorldSpaceCenter发生了修改，更新ActorPivot
	if (bShowWorldSpacePivot)
	{
		ActorPivot = ToonActorComponent->PivotOffset + ToonActorComponent->GetComponentLocation();
	}
	else
	{
		ActorPivot = ToonActorComponent->PivotOffset;
	}
	
}

void AToonActorBase::SetActorCenter(FVector NewValue, bool bWorldSpace)
{
	if (ToonActorComponent)
	{
		ToonActorComponent->SetPivot(NewValue, bWorldSpace);
		ActorPivot = ToonActorComponent->GetPivot(bShowWorldSpacePivot);
	}
}

FVector AToonActorBase::GetActorCenter(bool bWorldSpace) const
{
	return ToonActorComponent->GetPivot(bWorldSpace);
}

#if WITH_EDITOR

void AToonActorBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FProperty* Property = PropertyChangedEvent.Property;
	const FName PropertyName = Property ? Property->GetFName() : NAME_None;
	if (PropertyName == TEXT("bShowWorldSpaceCenter"))
	{
		ActorPivot = GetActorCenter(this->bShowWorldSpacePivot);
		// UE_LOG(LogTemp, Log, TEXT("SetEditorActorCenter:%1s"), *ActorPivot.ToString())
	}
	else if(PropertyName == TEXT("ActorPivot"))
	{
		ToonActorComponent->SetPivot(ActorPivot, bShowWorldSpacePivot);
	}
}

#endif

// ----------------------------------YK Engine End-----------------------------------------