// ----------------------------------YK Engine Start-----------------------------------------
// ToonActor
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ToonActorComponent.h"
#include "ToonActorBase.generated.h"

UCLASS(MinimalAPI, Abstract, NotBlueprintable)
class AToonActorBase : public AActor
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category= Toon, meta=(DisplayName = "Show World Space Center"))
	uint32 bShowWorldSpacePivot : 1; 
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Toon, meta = (DisplayName = "Actor Center"))
	FVector ActorPivot;

protected:
	UPROPERTY(VisibleAnywhere, Category = Toon, BlueprintReadOnly, meta = (ExposeFunctionCategories = "Toon,Rendering|Components|Toon", AllowPrivateAccess = "true"))
	TObjectPtr<class UToonActorComponent> ToonActorComponent;

public:
	UFUNCTION(BlueprintCallable, Category="Rendering|Toon")
	ENGINE_API class UToonActorComponent* GetToonCharacterComponent() const { return ToonActorComponent; }
	
	UFUNCTION(BlueprintCallable, Category = "Rendering|Toon")
	ENGINE_API void SetUseWorldSpaceCenter(bool bNewValue);
	
	UFUNCTION(BlueprintCallable, Category="Rendering|Toon")
	ENGINE_API void SetActorCenter(FVector NewValue, bool bWorldSpace = true);

	UFUNCTION(BlueprintCallable, Category="Rendering|Toon")
	ENGINE_API FVector GetActorCenter(bool bWorldSpace = true) const;

	ENGINE_API virtual void SetToonID(int NewValue) { if (ToonActorComponent) ToonActorComponent->SetID(NewValue); };
#if WITH_EDITOR
	// 当编辑器调整了蓝图属性时调用
	ENGINE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

// ----------------------------------YK Engine End-----------------------------------------