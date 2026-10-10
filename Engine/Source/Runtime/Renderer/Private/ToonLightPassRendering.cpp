// ----------------------------------YK Engine Start-----------------------------------------
// Toon Light Pass Step 4
#include "ToonLightPassRendering.h"

#include "ScenePrivate.h"
#include "MeshPassProcessor.inl"
#include "SimpleMeshDrawCommandPass.h"
#include "StaticMeshBatch.h"
#include "DeferredShadingRenderer.h"
#include "LightSceneProxy.h"
#include "MeshDrawCommands.h"
#include "SkyAtmosphereRendering.h"

IMPLEMENT_MATERIAL_SHADER_TYPE(, FToonLightPassVS, TEXT("/Engine/Private/Toon/ToonMeshPassVS.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FToonLightPassPS, TEXT("/Engine/Private/Toon/ToonMainLightPS.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_SHADERPIPELINE_TYPE_VSPS(ToonLightPipeline, FToonLightPassVS, FToonLightPassPS, true);

FToonLightPassPS::FToonLightPassPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FMeshMaterialShader(Initializer)
{
	ToonDataUniformBuffer.Bind(Initializer.ParameterMap, TEXT("ToonData"));
}

FToonLightPassPS::FToonLightPassPS() {}

void FToonLightPassPS::GetShaderBindings(
	const FScene* Scene,
	ERHIFeatureLevel::Type FeatureLevel,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material,
	const FMeshMaterialShaderElementData& ShaderElementData,
	FMeshDrawSingleShaderBindings& ShaderBindings) const
{
	FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, ShaderElementData, ShaderBindings);
	if (ToonDataUniformBuffer.IsBound() && Scene && Scene->UniformBuffers.ToonDataUniformBuffer.IsValid())
	{
		ShaderBindings.Add(ToonDataUniformBuffer, Scene->UniformBuffers.ToonDataUniformBuffer);
	}
}

//--------------------------------------------Toon Shadow Texture---------------------------------------------
// Toon Shadow
FRDGTextureRef CreateToonShadowTexture(FRDGBuilder& GraphBuilder, FIntPoint Extent, ETextureCreateFlags CreateFlags)
{
	const FRDGTextureDesc ToonShadowTextureDesc = FRDGTextureDesc::Create2D(Extent, PF_R8G8B8A8, FClearValueBinding::Black, TexCreate_UAV | TexCreate_RenderTargetable | TexCreate_ShaderResource | CreateFlags);
	return GraphBuilder.CreateTexture(ToonShadowTextureDesc, TEXT("ToonShadow"));
}

//------------------------------------------- Mesh Pass Processor-------------------------------------------------
bool GetToonLightShader(
	const FMaterial& Material,
	const FVertexFactoryType* VertexFactoryType,
	ERHIFeatureLevel::Type FeatureLevel,
	TShaderRef<FToonLightPassVS>& VertexShader,
	TShaderRef<FToonLightPassPS>& PixelShader)
{
	FMaterialShaderTypes ShaderTypes;
	ShaderTypes.PipelineType = &ToonLightPipeline;
	ShaderTypes.AddShaderType<FToonLightPassVS>();
	ShaderTypes.AddShaderType<FToonLightPassPS>();

	FMaterialShaders Shaders;
	if(!Material.TryGetShaders(ShaderTypes , VertexFactoryType , Shaders))
	{
		return false;
	}

	Shaders.TryGetVertexShader(VertexShader);
	Shaders.TryGetPixelShader(PixelShader);
	
	check(VertexShader.IsValid() && PixelShader.IsValid());

	return true;
}

bool ShouldDrawToonLightPass(const FMaterial* Material)
{
	const FMaterialShadingModelField ShadingModels = Material->GetShadingModels();
	const EBlendMode BlendMode = Material->GetBlendMode();
	//const bool bIsNotTranslucent = BlendMode == BLEND_Opaque || BlendMode == BLEND_Masked;
	return ShadingModels.HasShadingModel(MSM_Toon);
}

FToonMainLightMeshProcessor::FToonMainLightMeshProcessor(
    const FScene* Scene,
    ERHIFeatureLevel::Type InFeatureLevel,
    const FSceneView* InViewIfDynamicMeshCommand,
    const FMeshPassProcessorRenderState& InPassDrawRenderState,
    FMeshPassDrawListContext* InDrawListContext)
:FMeshPassProcessor(EMeshPass::ToonLightPass, Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext),
PassDrawRenderState(InPassDrawRenderState)
{
}

void FToonMainLightMeshProcessor::CollectPSOInitializers(const FSceneTexturesConfig& SceneTexturesConfig,
	const FMaterial& Material, const FPSOPrecacheVertexFactoryData& VertexFactoryData,
	const FPSOPrecacheParams& PreCacheParams, TArray<FPSOPrecacheData>& PSOInitializers)
{
	// Early out If Not Unlit
	if(!Material.GetShadingModels().IsUnlit())
	{
		return;
	}

	//Only Do deferred Path for now
	if(FScene::GetShadingPath(FeatureLevel) != EShadingPath::Deferred)
	{
		return;
	}

	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(PreCacheParams);
	const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(Material, OverrideSettings);
	const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(Material, OverrideSettings);
	
	TMeshProcessorShaders<FToonLightPassVS, FToonLightPassPS> PassShaders;

	if(!GetToonLightShader(
		Material ,
		VertexFactoryData.VertexFactoryType ,
		FeatureLevel ,
		PassShaders.VertexShader ,
		PassShaders.PixelShader))
	{
		return;
	}

	FGraphicsPipelineRenderTargetsInfo RenderTargetsInfo;
	AddGraphicsPipelineStateInitializer(
		VertexFactoryData,
		Material,
		PassDrawRenderState,
		RenderTargetsInfo,
		PassShaders,
		MeshFillMode,
		MeshCullMode,
		(EPrimitiveType)PreCacheParams.PrimitiveType,
		EMeshPassFeatures::Default,
		true,
		PSOInitializers);
}

void FToonMainLightMeshProcessor::AddMeshBatch(
    const FMeshBatch& MeshBatch,
    uint64 BatchElementMask,
    const FPrimitiveSceneProxy* PrimitiveSceneProxy,
    int32 StaticMeshId)
{
    const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;

	while (MaterialRenderProxy)
	{
		const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
		if(Material && ShouldDrawToonLightPass(Material))
		{
			if(TryAddMeshBatch(MeshBatch , BatchElementMask , PrimitiveSceneProxy , StaticMeshId ,*MaterialRenderProxy ,*Material))
			{
				break;
			}
		}

		MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
	}
}

bool FToonMainLightMeshProcessor::TryAddMeshBatch(const FMeshBatch& MeshBatch, uint64 BatchElementMask,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy, int32 StaticMeshId,
	const FMaterialRenderProxy& MaterialRenderProxy, const FMaterial& Material)
{
	//mesh Material blend mode
	const EBlendMode BlendMode = Material.GetBlendMode();
	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	const ERasterizerFillMode MeshFullMode = ComputeMeshFillMode(Material , OverrideSettings);
	const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(Material , OverrideSettings);
	
	return Process(MeshBatch, BatchElementMask ,StaticMeshId ,PrimitiveSceneProxy,MaterialRenderProxy , Material , MeshFullMode , MeshCullMode);
}

bool FToonMainLightMeshProcessor::Process(
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

    TMeshProcessorShaders<FToonLightPassVS, FToonLightPassPS> ToonPassShader;
	
	if (!GetToonLightShader(
		MaterialResource,
		VertexFactory->GetType(),
		FeatureLevel,
		ToonPassShader.VertexShader,
		ToonPassShader.PixelShader))
	{
		return false;
	}
	
    FMeshMaterialShaderElementData ShaderElementData;
    ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, true);
    //ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

    const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(ToonPassShader.VertexShader, ToonPassShader.PixelShader);

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);
	
    BuildMeshDrawCommands(
        MeshBatch,
        BatchElementMask,
        PrimitiveSceneProxy,
        MaterialRenderProxy,
        MaterialResource,
        DrawRenderState,
        ToonPassShader,
        MeshFillMode,
        MeshCullMode,
        SortKey,
        EMeshPassFeatures::Default,
        ShaderElementData
    );

    return true;
}

//------------------FRegisterPassProcessorCreateFunction---------------

void SetupToonLightPassState(FMeshPassProcessorRenderState& DrawRenderState)
{
	DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());
	DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
}

FMeshPassProcessor* CreateToonLightPassProcessor(ERHIFeatureLevel::Type FeatureLevel, const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState ToonPassState;
	SetupToonLightPassState(ToonPassState);
	return new FToonMainLightMeshProcessor(Scene, FeatureLevel, InViewIfDynamicMeshCommand, ToonPassState, InDrawListContext);
}

// RegisterToonPass会将CreateToonPassProcessor函数的地址写入FPassProcessorManager的一个Table里，Table的下标是EShadingPath和EMeshPass
// 这个Table包括了所以Pass的CreatePassProcessor函数，之后引擎就可以根据EShadingPath和EMeshPass找到对应pass的CreatePassProcessor函数
FRegisterPassProcessorCreateFunction RegisterToonLightPass(&CreateToonLightPassProcessor, EShadingPath::Deferred, EMeshPass::ToonLightPass, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);

//------------------FRegisterPassProcessorCreateFunction---------------

DECLARE_CYCLE_STAT(TEXT("ToonLightPass"), STAT_CLP_ToonLightPass, STATGROUP_SceneRendering);

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FToonLightUniformParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ToonShadowTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, ToonShadowTextureSampler)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLightShaderParameters, LightParameters)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(ToonMainLight);
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FToonLightUniformParameters, "ToonMainLight", ToonMainLight);

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FToonDataParameters, )
	SHADER_PARAMETER(FVector3f, MainLightDirectioin)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(ToonData);
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FToonDataParameters, "ToonData", ToonData);

BEGIN_SHADER_PARAMETER_STRUCT(FToonLightPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FToonLightUniformParameters , ToonMainLight)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FToonDataParameters, ToonData)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

FToonLightPassParameters* GetToonLightPassParameters(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScene* Scene, FSceneTextures& SceneTextures)
{
	FToonLightPassParameters* PassParameters = GraphBuilder.AllocParameters<FToonLightPassParameters>();
	FToonLightUniformParameters& ToonLightUniformParameters = *GraphBuilder.AllocParameters<FToonLightUniformParameters>();
	{
		const FRDGTextureRef WhiteDummy = GSystemTextures.GetWhiteDummy(GraphBuilder);
		ToonLightUniformParameters.ToonShadowTexture = WhiteDummy;
		ToonLightUniformParameters.ToonShadowTextureSampler = TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		ToonLightUniformParameters.LightParameters.Color = FVector3f::Zero();
		ToonLightUniformParameters.LightParameters.Direction = FVector3f::Zero();
		ToonLightUniformParameters.LightParameters.SourceRadius = 0;
		ToonLightUniformParameters.LightParameters.SoftSourceRadius = 0;
		
		
		// ToonMainLight
		if (Scene->ToonMainLightId != -1)
		{
			const FLightSceneInfo* MainLight = Scene->Lights[Scene->ToonMainLightId].LightSceneInfo;
			if (MainLight && MainLight->bVisible)
			{
				ToonLightUniformParameters.ToonShadowTexture = SceneTextures.ToonShadow;
				FLightRenderParameters LightParameters;
				MainLight->Proxy->GetLightShaderParameters(LightParameters, 0);
				LightParameters.MakeShaderParameters(View.ViewMatrices, View.GetLastEyeAdaptationExposure(), ToonLightUniformParameters.LightParameters);
			}
		}
	}
	FToonDataParameters& ToonDataParameters = *GraphBuilder.AllocParameters<FToonDataParameters>();
	ToonDataParameters.MainLightDirectioin = ToonLightUniformParameters.LightParameters.Direction;

	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->ToonMainLight = GraphBuilder.CreateUniformBuffer(&ToonLightUniformParameters);
	PassParameters->ToonData = GraphBuilder.CreateUniformBuffer(&ToonDataParameters);
	if (Scene)
	{
		Scene->UniformBuffers.ToonDataUniformBuffer = GraphBuilder.ConvertToExternalUniformBuffer(PassParameters->ToonData.GetUniformBuffer());
	}
	PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneTextures.Color.Target, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets.DepthStencil =
		FDepthStencilBinding(SceneTextures.Depth.Target, ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);
	
	return PassParameters;
}

// 在DeferredShadingSceneRenderer调用这个函数来渲染ToonLightPass
void FDeferredShadingSceneRenderer::RenderToonLightPass(FRDGBuilder& GraphBuilder, FSceneTextures& SceneTextures)
{
    RDG_EVENT_SCOPE(GraphBuilder, "ToonLightPass");
    RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderToonLightPass);
	
    SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderToonLightPass, FColor::Emerald);
	
    for(int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& View = Views[ViewIndex];
        RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
        RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

        const bool bShouldRenderView = View.ShouldRenderView();
        if (auto* Pass = View.ParallelMeshDrawCommandPasses[EMeshPass::ToonLightPass]; Pass && bShouldRenderView && HasAnyDraw(Pass))
        {
        	View.BeginRenderView();

            FToonLightPassParameters* PassParameters = GetToonLightPassParameters(GraphBuilder, View, Scene,SceneTextures);
            Pass->BuildRenderingCommands(GraphBuilder, Scene->GPUScene, PassParameters->InstanceCullingDrawParams);

            GraphBuilder.AddDispatchPass(
                RDG_EVENT_NAME("ToonMainLight"),
                PassParameters,
                ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
                [Pass, PassParameters](FRDGDispatchPassBuilder& DispatchPassBuilder)
            {
                Pass->Dispatch(DispatchPassBuilder, &PassParameters->InstanceCullingDrawParams);
            });
        }
    }
}

REGISTER_MESHPASSPROCESSOR_AND_PSOCOLLECTOR(StylizedData, CreateToonLightPassProcessor , EShadingPath::Deferred, EMeshPass::ToonLightPass, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);

//-------------------------------------YK Engine End------------------------------------------