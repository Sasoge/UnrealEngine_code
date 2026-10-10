// ----------------------------------YK Engine Start-----------------------------------------
#include "ToonActorTexture.h"
#include "ScenePrivate.h"
#include "ToonActorProxy.h"
#include "DataDrivenShaderPlatformInfo.h"

/*=============================================================================
	FScene functions
=============================================================================*/

void FScene::AddToonActor(FToonActorProxy* ToonActorProxy)
{
	check(ToonActorProxy);
	FScene* Scene = this;

	ENQUEUE_RENDER_COMMAND(FAddToonCharacterCommand)(
		[Scene, ToonActorProxy](FRHICommandListImmediate& RHICmdList)
		{
			check(!Scene->ToonActors.Contains(ToonActorProxy));
			Scene->ToonActors.Push(ToonActorProxy);
		}
	);
}

void FScene::RemoveToonActor(FToonActorProxy* ToonActorProxy)
{
	check(ToonActorProxy);
	FScene* Scene = this;
	
	ENQUEUE_RENDER_COMMAND(FRemoveToonCharacterCommand)(
		[Scene, ToonActorProxy](FRHICommandListImmediate& RHICmdList)
		{
			Scene->ToonActors.RemoveSingle(ToonActorProxy);
		} );
}

bool FScene::HasAnyToonActor() const
{
	return ToonActors.Num() > 0;
}

/*=============================================================================
	ToonActorTexture rendering
=============================================================================*/
// ToonActorTexture
// ToonActorTexture分辨率固定为16x16，即最多保存256 - 1个Actor的信息(uv = (0, 0)的地方默认不填入数据)
FRDGTextureRef CreateToonActorTexture(FRDGBuilder& GraphBuilder)
{
	const FRDGTextureDesc TextureDescGather = FRDGTextureDesc::Create2D(
		FIntPoint(16), PF_A32B32G32R32F, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	
	return GraphBuilder.CreateTexture(TextureDescGather, TEXT("ToonActorTexture"));
}
class FToonActorTextureCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FToonActorTextureCS)
		SHADER_USE_PARAMETER_STRUCT(FToonActorTextureCS, FGlobalShader)

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWTarget)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(uint32, NumToonActor)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FToonActorData>, ToonActorDatas)
		END_SHADER_PARAMETER_STRUCT()

		using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static int32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FToonActorTextureCS, "/Engine/Private/Toon/ToonActorTextureCS.usf", "MainCS", SF_Compute);

FToonActorTextureCS::FParameters* GetToonActorTexturePassParameters(FRDGBuilder& GraphBuilder, FScene* Scene, const FViewInfo& View, const FSceneTextures& SceneTextures)
{
	
	const FRDGTextureRef Target = SceneTextures.ToonActor;

	TArray<FToonActorData> ToonActorDatas;
	for (const auto ToonActor : Scene->ToonActors)
	{
		ToonActorDatas.Add(ToonActor->GetToonActorData(View));
	}

	constexpr uint32 ByteSize = sizeof(int32) + sizeof(FVector3f);
	FRDGBufferRef ToonActorDataBuffer;
	if (ToonActorDatas.Num() == 0)
	{
		static const FToonActorData DummyElement = FToonActorData(FVector3f(0), 0);
		ToonActorDataBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("ToonActorDataBuffer"),
			ByteSize, 1, &DummyElement, ByteSize, ERDGInitialDataFlags::None);
	}
	else
	{
		ToonActorDataBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("ToonActorDataBuffer"),
			ByteSize, ToonActorDatas.Num(), ToonActorDatas.GetData(), ToonActorDatas.Num() * ByteSize, ERDGInitialDataFlags::None);
	}
	
	FToonActorTextureCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FToonActorTextureCS::FParameters>();
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->RWTarget = GraphBuilder.CreateUAV(Target);
	PassParameters->NumToonActor = ToonActorDatas.Num();
	PassParameters->ToonActorDatas = GraphBuilder.CreateSRV(ToonActorDataBuffer);

	return PassParameters;
}

void AddToonActorTextureCSPass(FRDGBuilder& GraphBuilder, FScene* Scene,FViewInfo& View, FSceneTextures& SceneTextures)
{
	FToonActorTextureCS::FPermutationDomain PermutationVector;
	TShaderRef<FToonActorTextureCS> ComputeShader = View.ShaderMap->GetShader<FToonActorTextureCS>(PermutationVector);
	
	if (!HasBeenProduced(SceneTextures.ToonActor))
	{
		SceneTextures.ToonActor = CreateToonActorTexture(GraphBuilder);
	}
	// 在绘制前先清除ToonActorTexture
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SceneTextures.ToonActor), FVector(0));
	
	FToonActorTextureCS::FParameters* PassParameters = GetToonActorTexturePassParameters(GraphBuilder, Scene, View, SceneTextures);
	
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("CreateToonActorTexture"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(FIntVector(16, 16, 1), FToonActorTextureCS::GetGroupSize()));
}
