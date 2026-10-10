// Toon Material Output step 1

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "UObject/ObjectMacros.h"
#include "MaterialExpressionToonBufferOutput.generated.h"

/** Toon材质的自定义属性输出. */
UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionToonBufferOutput : public UMaterialExpressionCustomOutput
{
	GENERATED_UCLASS_BODY()
	
	// uint 8bit [0 - 255]
	UPROPERTY()
	FExpressionInput SelfID;
	//// uint 8bit [0 - 255]
	UPROPERTY()
	FExpressionInput ObjectID;
	// uint 3bit [0 - 7]
	UPROPERTY()
	FExpressionInput ToonModel;
	// uint 5bit Mask
	UPROPERTY()
	FExpressionInput ShadowCastFlag;
	UPROPERTY()
	// float 8bit [0 - 1]
	FExpressionInput HairShadowOffset;
	UPROPERTY()
	// float 8bit [0 - 1]
	FExpressionInput SpecularSmoothness;
	UPROPERTY()
	FExpressionInput SpecularOffset;
	// float 8bit [-1 - 1]
	UPROPERTY()
	FExpressionInput ToonDataC;

public:
#if WITH_EDITOR
	//~ Begin UMaterialExpression Interface
	// 主要的功能实现在Compile()函数种
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetInputType(int32 InputIndex) override;
	//~ End UMaterialExpression Interface
#endif

	//~ Begin UMaterialExpressionCustomOutput Interface
	// 针脚的数量
	virtual int32 GetNumOutputs() const override;
	// 获取针脚属性的函数名
	virtual FString GetFunctionName() const override;
	// 节点的名称
	virtual FString GetDisplayName() const override;
	//~ End UMaterialExpressionCustomOutput Interface
};
