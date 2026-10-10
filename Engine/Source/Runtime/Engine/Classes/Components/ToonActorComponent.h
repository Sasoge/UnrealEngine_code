// ----------------------------------YK Engine Start-----------------------------------------
// ToonActorTexture

#pragma once

#include "CoreMinimal.h"
#include "SceneComponent.h"
#include "Runtime/Engine/Public/ToonActorProxy.h"
#include "ToonActorComponent.generated.h"

UCLASS(ClassGroup=(Toon), MinimalAPI)
class UToonActorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UToonActorComponent();

	// 用于区分不同的ToonActor，需要确保ToonBuffer中写入的ObjectID和这里的ToonActorID保持一致
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Toon, meta=(DisplayName = "ID", ShouldShowInViewport = true, ClampMin = 0, ClampMax = 256, UIMin = 0, UIMax = 256))
	int32 ToonActorID;

	// Actor中心点相对于其轴心点的Offset值
	FVector PivotOffset;
	
protected:
	//~ Begin UActorComponent Interface.
	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	virtual void SendRenderTransform_Concurrent() override;
	virtual void DestroyRenderState_Concurrent() override;
	//~ End UActorComponent Interface.

	void SendRenderTransformCommand();

public:
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Toon")
	ENGINE_API void SetID(int32 NewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|Components|Toon")
	ENGINE_API int32 GetID() const;
	
	bool SetPivot(const FVector& NewValue, bool bWorldSpace = false);

	FVector GetPivot(bool bWorldSpace = false) const;

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface
	
	/** Callback to create the rendering thread mirror. */
	FToonActorProxy* CreateSceneProxy();
private:

	FToonActorProxy* ToonActorProxy;
};

// ----------------------------------YK Engine End-----------------------------------------