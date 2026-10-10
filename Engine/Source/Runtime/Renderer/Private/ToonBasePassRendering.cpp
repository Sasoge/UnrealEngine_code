#include "ToonBasePassRendering.h"

#include "ScenePrivate.h"
#include "MeshPassProcessor.inl"
#include "SimpleMeshDrawCommandPass.h"
#include "StaticMeshBatch.h"
#include "DeferredShadingRenderer.h"
#include "MeshDrawCommands.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "PixelFormat.h"
#include "ToonActorTexture.h"
#include "ClearQuad.h"

// IMPLEMENT_MATERIAL_SHADER_TYPE接受的参数：
// FToonPassPS:我们在ToonBasePassRendering.h中定义的shader类
// TEXT("/Engine/Private/Toon/ToonPassShader.usf"):我们使用的shader路径
// TEXT("MainPS"):shader的入口函数名
// SF_Pixel:shader的类型，Vertex shader、Pixel shader或者compute shader
IMPLEMENT_MATERIAL_SHADER_TYPE(, FToonBasePassVS, TEXT("/Engine/Private/Toon/ToonMeshPassVS.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FToonBasePassPS, TEXT("/Engine/Private/Toon/ToonBasePassPS.usf"), TEXT("MainPS"), SF_Pixel);
FToonBasePassMeshProcessor::FToonBasePassMeshProcessor(
	const FScene* Scene,
	ERHIFeatureLevel::Type InFeatureLevel,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InPassDrawRenderState,
	FMeshPassDrawListContext* InDrawListContext)
:FMeshPassProcessor(EMeshPass::ToonBasePass, Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext),
PassDrawRenderState(InPassDrawRenderState)
{
	// 设置默认的BlendState和DepthStencilState
	// BlendState控制颜色混合方式
	// DepthStencilState控制深度写入，深度测试等行为
	if (PassDrawRenderState.GetDepthStencilState() == nullptr)
	{
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>().GetRHI());
	}
	if (PassDrawRenderState.GetBlendState() == nullptr)
	{
		PassDrawRenderState.SetBlendState(TStaticBlendState<>().GetRHI());
	}
}
void FToonBasePassMeshProcessor::AddMeshBatch(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	int32 StaticMeshId)
{
	const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;

	const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);

	if (Material != nullptr && Material->GetRenderingThreadShaderMap())
	{
		const FMaterialShadingModelField ShadingModels = Material->GetShadingModels();
		// Only Toon shading model should enter this pass.
		if (ShadingModels.HasShadingModel(MSM_Toon))
		{
			const EBlendMode BlendMode = Material->GetBlendMode();

			bool bResult = true;
			if (BlendMode == BLEND_Opaque || BlendMode == BLEND_Masked)
			{
				Process(
					MeshBatch,
					BatchElementMask,
					StaticMeshId,
					PrimitiveSceneProxy,
					*MaterialRenderProxy,
					*Material,
					FM_Solid,
					CM_CW); //背面剔除
			}
		}
	}
}
bool FToonBasePassMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<FToonBasePassVS, FToonBasePassPS> ToonBasePassShader;
	{
		FMaterialShaderTypes ShaderTypes;
		// 指定使用的shader
		ShaderTypes.AddShaderType<FToonBasePassVS>();
		ShaderTypes.AddShaderType<FToonBasePassPS>();

		const FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();

		FMaterialShaders Shaders;
		if (!MaterialResource.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
		{
			//UE_LOG(LogShaders, Warning, TEXT("Shader Not Found!"));
			return false;
		}

		Shaders.TryGetVertexShader(ToonBasePassShader.VertexShader);
		Shaders.TryGetPixelShader(ToonBasePassShader.PixelShader);
	}


	FMeshMaterialShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(ToonBasePassShader.VertexShader, ToonBasePassShader.PixelShader);
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>().GetRHI());

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);
	
	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		DrawRenderState,
		ToonBasePassShader,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData
	);

	return true;
}
void SetupToonBasePassState(FMeshPassProcessorRenderState& DrawRenderState)
{
	DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
}

FMeshPassProcessor* CreateToonBasePassProcessor(ERHIFeatureLevel::Type FeatureLevel, const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState ToonPassState;
	SetupToonBasePassState(ToonPassState);
	return new FToonBasePassMeshProcessor(Scene, FeatureLevel, InViewIfDynamicMeshCommand, ToonPassState, InDrawListContext);
}
// RegisterToonBasePass 将 CreateToonBasePassProcessor 注册到 FPassProcessorManager（EShadingPath + EMeshPass）
FRegisterPassProcessorCreateFunction RegisterToonBasePass(&CreateToonBasePassProcessor, EShadingPath::Deferred, EMeshPass::ToonBasePass, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);
DECLARE_CYCLE_STAT(TEXT("ToonBasePass"), STAT_CLP_ToonBasePass, STATGROUP_SceneRendering);

BEGIN_SHADER_PARAMETER_STRUCT(FToonBasePassParameters, )
    SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
    SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
    RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

// Toon Buffer：与 SceneTextures::InitializeViewFamily 使用同一套 Desc/创建逻辑；Name 用于 RDG 调试与纹理池区分
// FRDGTextureDesc GetToonBufferTextureDesc(FIntPoint Extent, ETextureCreateFlags CreateFlags)
// {
// 	//输入的参数：
// 	//Extent：贴图尺寸；PF_R8G8B8A8_UINT：贴图格式，表示RGBA各个通道均为8bit uint
// 	//FClearValueBinding::Black:清除值，表示清除贴图时将其清除为黑色
// 	//TexCreate_UAV：Unordered Access View，允许在着色器中进行随机读写操作
// 	//TexCreate_RenderTargetable：表示纹理可作为渲染目标使用
// 	//TexCreate_ShaderResource：表示纹理可作为着色器资源，可以在着色器中进行采样等操作
// 	return FRDGTextureDesc(FRDGTextureDesc::Create2D(Extent, PF_R8G8B8A8_UINT, FClearValueBinding::Black, TexCreate_UAV | TexCreate_RenderTargetable | TexCreate_ShaderResource | CreateFlags));
// }
// // Toon Buffer step 5-3
// FRDGTextureRef CreateToonBufferTexture(FRDGBuilder& GraphBuilder, FIntPoint Extent, ETextureCreateFlags CreateFlags, const TCHAR* Name)
// {	
// 	return GraphBuilder.CreateTexture(GetToonBufferTextureDesc(Extent, CreateFlags), Name);
// }

void CreateToonBuffers(FRDGBuilder& GraphBuilder, FSceneTextures& SceneTexture, FIntPoint Extent, const FFastVramConfig& FastVRamConfig)
{
	const FRDGTextureDesc TBufferADesc = FRDGTextureDesc(FRDGTextureDesc::Create2D(Extent, PF_R8G8B8A8_UINT,
		FClearValueBinding::Black, TexCreate_UAV | TexCreate_RenderTargetable | TexCreate_ShaderResource | FastVRamConfig.TBufferA));
	SceneTexture.TBufferA = GraphBuilder.CreateTexture(TBufferADesc, TEXT("TBufferA"));

	const FRDGTextureDesc TBufferBDesc = FRDGTextureDesc(FRDGTextureDesc::Create2D(Extent, PF_R8G8B8A8,
		FClearValueBinding::Black, TexCreate_UAV | TexCreate_RenderTargetable | TexCreate_ShaderResource | FastVRamConfig.TBufferB));
	SceneTexture.TBufferB = GraphBuilder.CreateTexture(TBufferBDesc, TEXT("TBufferB"));

	const FRDGTextureDesc TBufferCDesc = FRDGTextureDesc(FRDGTextureDesc::Create2D(Extent, PF_R8G8B8A8,
		FClearValueBinding::Black, TexCreate_UAV | TexCreate_RenderTargetable | TexCreate_ShaderResource | FastVRamConfig.TBufferC));
	SceneTexture.TBufferC = GraphBuilder.CreateTexture(TBufferCDesc, TEXT("TBufferC"));
}

FRDGTextureRef CreateToonBaseBufferTexture(FRDGBuilder& GraphBuilder, FIntPoint Extent, ETextureCreateFlags CreateFlags, const TCHAR* Name)
{
	const FRDGTextureDesc TBaseBufferDesc = FRDGTextureDesc(FRDGTextureDesc::Create2D(
		Extent, PF_R8G8B8A8_UINT, FClearValueBinding::Black,
		TexCreate_UAV | TexCreate_RenderTargetable | TexCreate_ShaderResource | CreateFlags));
	return GraphBuilder.CreateTexture(TBaseBufferDesc, Name);
}

FToonBasePassParameters* GetToonBasePassParameters(FRDGBuilder& GraphBuilder, const FViewInfo& View, FSceneTextures& SceneTextures)
{
	FToonBasePassParameters* PassParameters = GraphBuilder.AllocParameters<FToonBasePassParameters>();
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneTextures.TBufferA, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets[1] = FRenderTargetBinding(SceneTextures.TBufferB, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets[2] = FRenderTargetBinding(SceneTextures.TBufferC, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneTextures.Depth.Target, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

	return PassParameters;
}

// 用于清空ToonBuffer
void ClearToonBuffer(FRDGBuilder& GraphBuilder, const FViewInfo& View, FSceneTextures& SceneTextures)
{
	// Toon Buffer step 8
	// 将 RenderTarget 设为 ToonBuffer（若已在 InitializeViewFamily 中创建则复用）
	if (!HasBeenProduced(SceneTextures.TBufferA) || !HasBeenProduced(SceneTextures.TBufferB) || !HasBeenProduced(SceneTextures.TBufferC))
	{
		const FSceneTexturesConfig& Config = View.GetSceneTexturesConfig();
		CreateToonBuffers(GraphBuilder, SceneTextures, Config.Extent, GFastVRamConfig);
	}
	//////////////////////////////end/////////////////////
	FToonBasePassParameters* PassParameters = GraphBuilder.AllocParameters<FToonBasePassParameters>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneTextures.TBufferA, ERenderTargetLoadAction::ENoAction);
	PassParameters->RenderTargets[1] = FRenderTargetBinding(SceneTextures.TBufferB, ERenderTargetLoadAction::ENoAction);
	PassParameters->RenderTargets[2] = FRenderTargetBinding(SceneTextures.TBufferC, ERenderTargetLoadAction::ENoAction);
	
	GraphBuilder.AddPass(RDG_EVENT_NAME("TBufferClear"), PassParameters, ERDGPassFlags::Raster,
			[PassParameters](FRHICommandList& RHICmdList)
		{
			// If no fast-clear action was used, we need to do an MRT shader clear.
			const FRenderTargetBindingSlots& RenderTargets = PassParameters->RenderTargets;
			FLinearColor ClearColors[MaxSimultaneousRenderTargets];
			FRHITexture* Textures[MaxSimultaneousRenderTargets];
			int32 TextureIndex = 0;

			RenderTargets.Enumerate([&](const FRenderTargetBinding& RenderTarget)
			{
				FRHITexture* TextureRHI = RenderTarget.GetTexture()->GetRHI();
				ClearColors[TextureIndex] = TextureRHI->GetClearColor();
				Textures[TextureIndex] = TextureRHI;
				++TextureIndex;
			});

			DrawClearQuadMRT(RHICmdList, true, TextureIndex, ClearColors, false, 0, false, 0);
			
		});
}

// 在DeferredShadingSceneRenderer调用这个函数来渲染ToonPass
void FDeferredShadingSceneRenderer::RenderToonBasePass(FRDGBuilder& GraphBuilder, FSceneTextures& SceneTextures)
{
    RDG_EVENT_SCOPE(GraphBuilder, "ToonBasePass");
    RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderToonBasePass);

    SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderToonBasePass, FColor::Emerald);

    for(int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& View = Views[ViewIndex];
        RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
        RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

        const bool bShouldRenderView = View.ShouldRenderView();
        if (!bShouldRenderView)
        {
            continue;
        }

        // Raster ToonBasePass only when there is work; ToonActorTexture must still be produced every
        // frame before deferred lighting reads it (see FDeferredLightPS ToonActorTexture).
        if (auto* Pass = View.ParallelMeshDrawCommandPasses[EMeshPass::ToonBasePass]; Pass && HasAnyDraw(Pass))
        {
        	ClearToonBuffer(GraphBuilder, View, SceneTextures);
            FToonBasePassParameters* PassParameters = GetToonBasePassParameters(GraphBuilder, View, SceneTextures);

            Pass->BuildRenderingCommands(GraphBuilder, Scene->GPUScene, PassParameters->InstanceCullingDrawParams);

            GraphBuilder.AddDispatchPass(
				 RDG_EVENT_NAME("ToonBasePassParallel"),
                PassParameters,
                ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
                [Pass, PassParameters](FRDGDispatchPassBuilder& DispatchPassBuilder)
                {
                    Pass->Dispatch(DispatchPassBuilder, &PassParameters->InstanceCullingDrawParams);
                });
        }

        AddToonActorTextureCSPass(GraphBuilder, Scene, View, SceneTextures);
    }
}