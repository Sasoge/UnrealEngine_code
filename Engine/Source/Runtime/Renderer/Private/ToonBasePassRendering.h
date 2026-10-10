#pragma once

#include "DataDrivenShaderPlatformInfo.h"
#include "MeshPassProcessor.h"
#include "MeshMaterialShader.h"
#include "RenderGraphBuilder.h"
#include "Engine/EngineTypes.h"

// // 用于获取ToonBufferTexture的FRDGTextureDesc，FRDGTextureDesc存储了texture的一些描述信息，如大小，格式，MipMap层数等
// FRDGTextureDesc GetToonBufferTextureDesc(FIntPoint Extent, ETextureCreateFlags CreateFlags);
// // 用于创建一张ToonBufferTexture
// FRDGTextureRef CreateToonBufferTexture(FRDGBuilder& GraphBuilder, FIntPoint Extent, ETextureCreateFlags CreateFlags, const TCHAR* Name);

struct FFastVramConfig;
void CreateToonBuffers(FRDGBuilder& GraphBuilder, FSceneTextures& SceneTexture, FIntPoint Extent, const FFastVramConfig& FastVRamConfig);
//struct FFastVramConfig;
//void CreateToonBuffers(FRDGBuilder& GraphBuilder, FSceneTextures& SceneTexture, FIntPoint Extent, const FFastVramConfig& FastVRamConfig);
/////////////
class FToonBasePassMeshProcessor : public FMeshPassProcessor
{
public:
	FToonBasePassMeshProcessor(
		const FScene* Scene,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		FMeshPassDrawListContext* InDrawListContext
	);

	// 函数将会从引擎底层拿到MeshBatch，Material等资源
	// MeshBatch简单理解就是同一批次的网格
	// 我们通过这个函数筛选哪些Mesh需要绘制并调用Process()
	virtual void AddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId = -1
	) override final;

private:
	// 准备好数据(MeshBatch，要用什么shader绘制，shader参数，剔除方式，深度测试等)
	// 将数据传递给BuildMeshDrawCommands生成MeshDrawCommand
	// MeshDrawCommand是完整描述了一个Pass Draw Call的所有状态和数据，如shader绑定、顶点数据、索引数据、PSO缓存等
	// 之后引擎会把MeshDrawCommand转化为RHI命令进行渲染
	bool Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode
	);

	FMeshPassProcessorRenderState PassDrawRenderState;
};
class FToonBasePassVS : public FMeshMaterialShader
{
    DECLARE_SHADER_TYPE(FToonBasePassVS, MeshMaterial);

public:
    FToonBasePassVS() = default;
    FToonBasePassVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FMeshMaterialShader(Initializer)
    {

    }

    static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {}

    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
            Parameters.MaterialParameters.ShadingModels.HasShadingModel(MSM_Toon) &&
            (Parameters.VertexFactoryType->GetFName() == FName(TEXT("FLocalVertexFactory")) ||
                Parameters.VertexFactoryType->GetFName() == FName(TEXT("TGPUSkinVertexFactoryDefault")));
    }

    void GetShaderBindings(
        const FScene* Scene,
        ERHIFeatureLevel::Type FeatureLevel,
        const FPrimitiveSceneProxy* PrimitiveSceneProxy,
        const FMaterialRenderProxy& MaterialRenderProxy,
        const FMaterial& Material,
        const FMeshMaterialShaderElementData& ShaderElementData,
        FMeshDrawSingleShaderBindings& ShaderBindings) const
    {
        FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, ShaderElementData, ShaderBindings);
    }

};


class FToonBasePassPS : public FMeshMaterialShader
{
    DECLARE_SHADER_TYPE(FToonBasePassPS, MeshMaterial);

public:

    FToonBasePassPS() = default;
    FToonBasePassPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FMeshMaterialShader(Initializer)
    {
        // 这个用于绑定shader的参数InputColor，虽然shader中没有使用
        InputColor.Bind(Initializer.ParameterMap, TEXT("InputColor"));
    }

    static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {}

    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
            Parameters.MaterialParameters.ShadingModels.HasShadingModel(MSM_Toon) &&
            (Parameters.VertexFactoryType->GetFName() == FName(TEXT("FLocalVertexFactory")) ||
                Parameters.VertexFactoryType->GetFName() == FName(TEXT("TGPUSkinVertexFactoryDefault")));
    }

    void GetShaderBindings(
        const FScene* Scene,
        ERHIFeatureLevel::Type FeatureLevel,
        const FPrimitiveSceneProxy* PrimitiveSceneProxy,
        const FMaterialRenderProxy& MaterialRenderProxy,
        const FMaterial& Material,
        const FMeshMaterialShaderElementData& ShaderElementData,
        FMeshDrawSingleShaderBindings& ShaderBindings) const
    {
        FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, ShaderElementData, ShaderBindings);

        FVector3f Color(1.0, 0.0, 0.0);

        ShaderBindings.Add(InputColor, Color);
    }

    LAYOUT_FIELD(FShaderParameter, InputColor);
};