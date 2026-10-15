// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DeferredShadingRenderer.cpp: Top level rendering loop for deferred shading
=============================================================================*/

#include "DeferredShadingRenderer.h"
#include "BasePassRendering.h"
#include "VelocityRendering.h"
#include "SingleLayerWaterRendering.h"
#include "SkyAtmosphereRendering.h"
#include "VolumetricCloudRendering.h"
#include "SparseVolumeTexture/SparseVolumeTextureViewerRendering.h"
#include "VolumetricRenderTarget.h"
#include "ScenePrivate.h"
#include "SceneProxies/SkyLightSceneProxy.h"
#include "SceneOcclusion.h"
#include "ScreenRendering.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "PostProcess/PostProcessVisualizeCalibrationMaterial.h"
#include "PostProcess/TemporalAA.h"
#include "CompositionLighting/CompositionLighting.h"
#include "FXSystem.h"
#include "OneColorShader.h"
#include "CompositionLighting/PostProcessDeferredDecals.h"
#include "CompositionLighting/PostProcessAmbientOcclusion.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "GlobalDistanceField.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/PostProcessEyeAdaptation.h"
#include "DistanceFieldAtlas.h"
#include "EngineModule.h"
#include "SceneViewExtension.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "RendererModule.h"
#include "VT/VirtualTextureFeedbackResource.h"
#include "VT/VirtualTextureSystem.h"
#include "GPUScene.h"
#include "PathTracing.h"
#include "RayTracing/RayTracing.h"
#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "RayTracing/RayTracingLighting.h"
#include "RayTracing/RayTracingDecals.h"
#include "RayTracing/RayTracingScene.h"
#include "RayTracing/RayTracingInstanceMask.h"
#include "RayTracingDynamicGeometryUpdateManager.h"
#include "RayTracingVisualizationData.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceDenoise.h"
#include "ScreenSpaceRayTracing.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracingDefinitions.h"
#include "RayTracingInstance.h"
#include "ShaderPrint.h"
#include "GPUSortManager.h"
#include "HairStrands/HairStrandsRendering.h"
#include "HairStrands/HairStrandsData.h"
#include "PhysicsField/PhysicsFieldComponent.h"
#include "PhysicsFieldRendering.h"
#include "NaniteVisualizationData.h"
#include "Rendering/NaniteResources.h"
#include "Rendering/NaniteStreamingManager.h"
#include "Rendering/NaniteCoarseMeshStreamingManager.h"
#include "SceneTextureReductions.h"
#include "VirtualShadowMaps/VirtualShadowMapCacheManager.h"
#include "Substrate/Substrate.h"
#include "Lumen/Lumen.h"
#include "Experimental/Containers/SherwoodHashTable.h"
#include "Rendering/RayTracingGeometryManager.h"
#include "InstanceCulling/InstanceCullingManager.h"
#include "InstanceCulling/InstanceCullingOcclusionQuery.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Engine/SubsurfaceProfile.h"
#include "Engine/SpecularProfile.h"
#include "SceneCaptureRendering.h"
#include "NaniteSceneProxy.h"
#include "Nanite/NaniteRayTracing.h"
#include "Nanite/NaniteComposition.h"
#include "Nanite/Voxel.h"
#include "Nanite/NaniteShading.h"
#include "RayTracing/RayTracingInstanceCulling.h"
#include "GPUMessaging.h"
#include "RectLightTextureManager.h"
#include "IESTextureManager.h"
#include "Lumen/LumenFrontLayerTranslucency.h"
#include "Lumen/LumenSceneLighting.h"
#include "Lumen/LumenScreenProbeGather.h"
#include "Containers/ChunkedArray.h"
#include "Async/ParallelFor.h"
#include "Shadows/ShadowSceneRenderer.h"
#include "HeterogeneousVolumes/HeterogeneousVolumes.h"
#include "ComponentRecreateRenderStateContext.h"
#include "RenderCore.h"
#include "VariableRateShadingImageManager.h"
#include "LocalFogVolumeRendering.h"
#include "Shadows/ShadowScene.h"
#include "Lumen/LumenHardwareRayTracingCommon.h"
#include "SparseVolumeTexture/ISparseVolumeTextureStreamingManager.h"
#include "WaterInfoTextureRendering.h"
#include "PostProcess/DebugAlphaChannel.h"
#include "MegaLights/MegaLights.h"
#include "Rendering/CustomRenderPass.h"
#include "CustomRenderPassSceneCapture.h"
#include "EnvironmentComponentsFlags.h"
#include "GenerateMips.h"
#include "Froxel/Froxel.h"
#include "ViewData.h"
#include "MaterialCache/MaterialCache.h"
#include "MaterialCache/MaterialCacheRenderer.h"
#include "SceneCulling/SceneCullingRenderer.h"
#include "Shadows/FirstPersonSelfShadow.h"
#include "GPUSkinCache.h"

#if !UE_BUILD_SHIPPING
#include "RenderCaptureInterface.h"
#endif

extern int32 GNaniteShowStats;
extern int32 GNanitePickingDomain;

extern DynamicRenderScaling::FBudget GDynamicNaniteScalingPrimary;

static TAutoConsoleVariable<int32> CVarClearCoatNormal(
	TEXT("r.ClearCoatNormal"),
	0,
	TEXT("0 to disable clear coat normal.\n")
	TEXT(" 0: off\n")//设为 0 时：渲染管线会跳过清漆法线的采样、计算和应用，仅使用基础法线渲染材质，清漆层无独立的法线细节；
	TEXT(" 1: on"),//设为 1 时：渲染管线会读取材质中设置的清漆法线贴图 / 参数，将清漆法线与基础法线融合，参与光照计算，还原清漆层的微表面光影。
	ECVF_ReadOnly);
//清漆法线开关
//是 PBR 材质系统中的重要属性，用于模拟物体表面的透明涂层效果（如车漆、钢琴漆、高光泽塑料等），
//而「清漆法线」是独立于基础法线的额外法线信息，用于控制清漆层的微表面起伏，让涂层的光影效果更真实。

static TAutoConsoleVariable<int32> CVarIrisNormal(
	TEXT("r.IrisNormal"),
	0,
	TEXT("0 to disable iris normal.\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on"),
	ECVF_ReadOnly);
//虹膜法线开关类似于上者都是独立的法线层

int32 GbEnableAsyncComputeTranslucencyLightingVolumeClear = 0; // @todo: disabled due to GPU crashes
static FAutoConsoleVariableRef CVarEnableAsyncComputeTranslucencyLightingVolumeClear(
	TEXT("r.EnableAsyncComputeTranslucencyLightingVolumeClear"),
	GbEnableAsyncComputeTranslucencyLightingVolumeClear,
	TEXT("Whether to clear the translucency lighting volume using async compute.\n"),//控制是否使用异步计算（Async Compute）清理半透明光照体积
	ECVF_RenderThreadSafe | ECVF_Scalability
);
//半透明光照体积异步清理

#if !UE_BUILD_SHIPPING

static int32 GCaptureNextDeferredShadingRendererFrame = -1;
static FAutoConsoleVariableRef CVarCaptureNextRenderFrame(
	TEXT("r.CaptureNextDeferredShadingRendererFrame"),
	GCaptureNextDeferredShadingRendererFrame,
	TEXT("0 to capture the immideately next frame using e.g. RenderDoc or PIX.\n")//立即捕获下一帧的渲染数据（供 RenderDoc/PIX 等工具抓取）
	TEXT(" > 0: N frames delay\n")//延迟 N 帧后捕获（如设为 5，就是等待 5 帧后，捕获第 6 帧的渲染数据）
	TEXT(" < 0: disabled"),//禁用帧捕获功能，无任何操作
	ECVF_RenderThreadSafe);
//渲染帧捕获控制台变量

#endif

static TAutoConsoleVariable<int32> CVarRayTracing(
	TEXT("r.RayTracing"),
	0,
	TEXT("0 to disable ray tracing.\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);//标记为只读，运行时无法通过控制台直接修改
//光线追踪开关

int32 GRayTracingUseTextureLod = 0;
static TAutoConsoleVariable<int32> CVarRayTracingTextureLod(
	TEXT("r.RayTracing.UseTextureLod"),
	GRayTracingUseTextureLod,
	TEXT("Enable automatic texture mip level selection in ray tracing material shaders.\n")//启用光线追踪材质着色器中纹理MIP层级的自动选择功能。
	TEXT(" 0: highest resolution mip level is used for all texture (default).\n")//所有纹理均使用最高分辨率的MIP层级（默认值）
	TEXT(" 1: texture LOD is approximated based on total ray length, output resolution and texel density at hit point (ray cone method)."),//纹理LOD层级基于光线总长度、输出分辨率以及命中点的纹理像素密度（光线锥方法）进行估算。
	ECVF_RenderThreadSafe | ECVF_ReadOnly);//标记为只读
//把全局变量 GRayTracingUseTextureLod 暴露为 UE 控制台可识别的指令 r.RayTracing.UseTextureLod，让开发者能通过控制台控制光线追踪的纹理 LOD 逻辑。

static int32 GForceAllRayTracingEffects = -1;
static TAutoConsoleVariable<int32> CVarForceAllRayTracingEffects(
	TEXT("r.RayTracing.ForceAllRayTracingEffects"),
	GForceAllRayTracingEffects,
	TEXT("Force all ray tracing effects ON/OFF.\n")
	TEXT(" -1: Do not force (default) \n")//不强制干预（Do not force）：各 RT 子特性（阴影、AO、GI 等）按自身开关独立生效
	TEXT(" 0: All ray tracing effects disabled\n")//强制禁用所有 RT（All disabled）：无论单个 RT 子特性是否开启，全部失效
	TEXT(" 1: All ray tracing effects enabled"),//强制启用所有 RT (All enabled)：无论单个 RT 子特性是否关闭，全部生效
	ECVF_RenderThreadSafe);//标记，可在运行时（甚至游戏 / 编辑器实时运行中）通过控制台修改
//全局强制控制所有光线追踪（RT）特效开关,快速统一控制 RT 特效状态，提升调试 / 测试效率

static int32 GRayTracingAllowInline = 1;//引擎默认启用内联光线追踪
static TAutoConsoleVariable<int32> CVarRayTracingAllowInline(
	TEXT("r.RayTracing.AllowInline"), // 控制台调用名
	GRayTracingAllowInline,// 绑定到上述全局变量
	TEXT("Allow use of Inline Ray Tracing if supported (default=1)."),	// 帮助文本
	ECVF_RenderThreadSafe);
//内联光线追踪（Inline Ray Tracing）：将光线追踪指令直接嵌入到常规的计算 / 像素着色器中，无需独立构建 RT 管线和 SBT，调用更灵活、GPU 调度开销更低。；

static int32 GRayTracingAllowPipeline = 1;//是否启用「管线式光线追踪（Ray Tracing Pipelines）」 
static TAutoConsoleVariable<int32> CVarRayTracingAllowPipeline(
	TEXT("r.RayTracing.AllowPipeline"),
	GRayTracingAllowPipeline,
	TEXT("Allow use of Ray Tracing pipelines if supported (default=1)."),//默认启用管线式光线追踪
	ECVF_RenderThreadSafe);
//传统管线式光线追踪（Pipeline Ray Tracing）：需要先构建独立的光线追踪管线、着色器绑定表（SBT），再调度执行，流程繁琐、开销稍高；

static TAutoConsoleVariable<int32> CVarRayTracingAsyncBuild(
	TEXT("r.RayTracing.AsyncBuild"),
	0,//默认采用「同步方式」构建光线追踪加速结构；
	TEXT("Whether to build ray tracing acceleration structures on async compute queue.\n"),
	ECVF_RenderThreadSafe
);
//0 同步构建：加速结构在 GPU 渲染主线程队列中构建，构建完成前渲染线程会等待,优点：逻辑简单、稳定性高；缺点：场景加载 / 物体移动时，易出现画面卡顿（尤其是复杂场景）
//1 异步构建：加速结构在 GPU「异步计算队列」中构建，不占用渲染主线程资源,优点：渲染线程不阻塞，画面流畅无卡顿；缺点：需 GPU 支持异步计算，且存在极短时间的 “加速结构不一致”（画面无感知）

static int32 GRayTracingMultiGpuTLASMask = 0;//采用传统逻辑
static FAutoConsoleVariableRef CVarRayTracingMultiGpuTLASMask(
	TEXT("r.RayTracing.MultiGpuMaskTLAS"),
	GRayTracingMultiGpuTLASMask,
	TEXT("For Multi-GPU, controls which GPUs TLAS and material pipeline updates run on.  (default = 0)\n")
	TEXT(" 0: Run TLAS and material pipeline updates on all GPUs.  Original behavior -- the optimized version is disabled for now due to a bug.\n")
	//传统逻辑：所有 GPU 都执行 TLAS 和材质管线的更新操作；
    //注：注释明确说明 “优化版本因 BUG 暂禁用，暂时沿用原逻辑”
	TEXT(" 1: Run TLAS and material pipeline updates masked to the active view's GPUs to improve performance.  BLAS updates still run on all GPUs.")
	//优化逻辑：仅让「当前活跃视图（Active View）」对应的 GPU 执行 TLAS 和材质管线更新；
    //BLAS 更新仍在所有 GPU 执行
);

static TAutoConsoleVariable<int32> CVarSceneDepthHZBAsyncCompute(
	TEXT("r.SceneDepthHZBAsyncCompute"), 0,//禁用异步计算
	TEXT("Selects whether HZB for scene depth buffer should be built with async compute.\n")
	TEXT(" 0: Don't use async compute (default)\n")//同步构建：HZB 在渲染主线程中构建，构建完成前渲染线程等待
	TEXT(" 1: Use async compute, start as soon as possible\n")//异步构建（立即启动）：启用异步计算，HZB 构建任务立即提交到异步队列执行
	TEXT(" 2: Use async compute, start after ComputeLightGrid.CompactLinks pass"),
	//异步构建（延迟启动）：启用异步计算，但 HZB 构建需等待「ComputeLightGrid.CompactLinks」阶段完成后再启动
	ECVF_RenderThreadSafe);
//控制场景深度缓冲区的 HZB（Hierarchical Z-Buffer，层级 Z 缓冲）是否 / 如何通过异步计算（Async Compute）构建
//HZB 是对场景深度缓冲区（Depth Buffer）的 “金字塔式分层压缩” 结构：
//将原始深度缓冲按 2×2、4×4、8×8… 的比例逐级下采样，生成不同分辨率的深度层级；
//作用：UE 用 HZB 快速判断 “像素是否被遮挡”，大幅加速阴影、光照、剔除等渲染计算（是延迟渲染管线的核心优化手段）。

static TAutoConsoleVariable<int32> CVarShadowMapsRenderEarly(
	TEXT("r.shadow.ShadowMapsRenderEarly"), 0,//禁用“提前渲染阴影贴图”
	TEXT("If enabled, shadows will render earlier in the frame. This can help async compute scheduling on some platforms\n")//提前渲染阴影贴图：将阴影贴图绘制提前到帧的更早阶段
	TEXT("Note: This is not compatible with VSMs\n"),//完全不兼容 VSMs
	//VSM（Variance Shadow Maps，方差阴影贴图）是 UE 中实现软阴影的一种经典技术，
	//本质是对传统阴影贴图（Shadow Maps）的扩展 —— 通过存储深度值的 “均值 + 方差” 而非单一深度值，实现更柔和、自然的阴影边缘，同时解决传统阴影贴图的 “阴影痤疮（Shadow Acne）”“走样（Aliasing）” 等问题。
	ECVF_RenderThreadSafe);
//控制阴影贴图（Shadow Maps）是否在渲染帧早期阶段绘制 

static TAutoConsoleVariable<int32> CVarTranslucencyVelocity(
	TEXT("r.Translucency.Velocity"), 1,
	TEXT("Whether translucency can draws depth/velocity (enabled by default)"),
	ECVF_RenderThreadSafe);

static FAutoConsoleCommand RecreateRenderStateContextCmd(
	TEXT("r.RecreateRenderStateContext"),
	TEXT("Recreate render state."),
	FConsoleCommandDelegate::CreateStatic([] { FGlobalComponentRecreateRenderStateContext Context; }));

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarForceBlackVelocityBuffer(
	TEXT("r.Test.ForceBlackVelocityBuffer"), 0,
	TEXT("Force the velocity buffer to have no motion vector for debugging purpose."),
	ECVF_RenderThreadSafe);
#endif

static TAutoConsoleVariable<int32> CVarNaniteViewMeshLODBiasEnable(
	TEXT("r.Nanite.ViewMeshLODBias.Enable"), 1,
	TEXT("Whether LOD offset to apply for rasterized Nanite meshes for the main viewport should be based off TSR's ScreenPercentage (Enabled by default)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarNaniteViewMeshLODBiasOffset(
	TEXT("r.Nanite.ViewMeshLODBias.Offset"), 0.0f,
	TEXT("LOD offset to apply for rasterized Nanite meshes for the main viewport when using TSR (Default = 0)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarNaniteViewMeshLODBiasMin(
	TEXT("r.Nanite.ViewMeshLODBias.Min"), -2.0f,
	TEXT("Minimum LOD offset for rasterizing Nanite meshes for the main viewport (Default = -2)."),
	ECVF_RenderThreadSafe);

namespace Lumen
{
	extern bool AnyLumenHardwareRayTracingPassEnabled();
}
namespace Nanite
{
	extern bool IsStatFilterActive(const FString& FilterName);
	extern void ListStatFilters(FSceneRenderer* SceneRenderer);
}
extern bool ShouldVisualizeLightGrid();

DECLARE_CYCLE_STAT(TEXT("InitViews Intentional Stall"), STAT_InitViews_Intentional_Stall, STATGROUP_InitViews);

DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer UpdateDownsampledDepthSurface"), STAT_FDeferredShadingSceneRenderer_UpdateDownsampledDepthSurface, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Render Init"), STAT_FDeferredShadingSceneRenderer_Render_Init, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer FXSystem PreRender"), STAT_FDeferredShadingSceneRenderer_FXSystem_PreRender, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer AllocGBufferTargets"), STAT_FDeferredShadingSceneRenderer_AllocGBufferTargets, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer PrepareForwardLightData"), STAT_FDeferredShadingSceneRenderer_PrepareForwardLightData, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer DBuffer"), STAT_FDeferredShadingSceneRenderer_DBuffer, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer ResolveDepth After Basepass"), STAT_FDeferredShadingSceneRenderer_ResolveDepth_After_Basepass, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Resolve After Basepass"), STAT_FDeferredShadingSceneRenderer_Resolve_After_Basepass, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer FXSystem PostRenderOpaque"), STAT_FDeferredShadingSceneRenderer_FXSystem_PostRenderOpaque, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer AfterBasePass"), STAT_FDeferredShadingSceneRenderer_AfterBasePass, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Lighting"), STAT_FDeferredShadingSceneRenderer_Lighting, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderLightShaftOcclusion"), STAT_FDeferredShadingSceneRenderer_RenderLightShaftOcclusion, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderAtmosphere"), STAT_FDeferredShadingSceneRenderer_RenderAtmosphere, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderSkyAtmosphere"), STAT_FDeferredShadingSceneRenderer_RenderSkyAtmosphere, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderFog"), STAT_FDeferredShadingSceneRenderer_RenderFog, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderLocalFogVolume"), STAT_FDeferredShadingSceneRenderer_RenderLocalFogVolume, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderLightShaftBloom"), STAT_FDeferredShadingSceneRenderer_RenderLightShaftBloom, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderFinish"), STAT_FDeferredShadingSceneRenderer_RenderFinish, STATGROUP_SceneRendering);

DECLARE_GPU_STAT(RayTracingScene);
DECLARE_GPU_STAT(RayTracingGeometry);

DEFINE_GPU_STAT(Postprocessing);
DECLARE_GPU_STAT(VisibilityCommands);
DECLARE_GPU_STAT(RenderDeferredLighting);
DECLARE_GPU_STAT(AllocateRendertargets);
DECLARE_GPU_STAT(FrameRenderFinish);
DECLARE_GPU_STAT(PostRenderOpsFX);
DECLARE_GPU_STAT_NAMED(Unaccounted, TEXT("[unaccounted]"));
DECLARE_GPU_STAT(WaterRendering);
DECLARE_GPU_STAT(HairRendering);
DECLARE_GPU_STAT(UploadDynamicBuffers);
DECLARE_GPU_STAT(PostOpaqueExtensions);
DEFINE_GPU_STAT(CustomRenderPasses);
DECLARE_GPU_STAT(Substrate);

DECLARE_GPU_STAT_NAMED(NaniteVisBuffer, TEXT("Nanite VisBuffer"));

DECLARE_DWORD_COUNTER_STAT(TEXT("BasePass Total Raster Bins"), STAT_NaniteBasePassTotalRasterBins, STATGROUP_Nanite);
DECLARE_DWORD_COUNTER_STAT(TEXT("BasePass Visible Raster Bins"), STAT_NaniteBasePassVisibleRasterBins, STATGROUP_Nanite);

DECLARE_DWORD_COUNTER_STAT(TEXT("BasePass Total Shading Bins"), STAT_NaniteBasePassTotalShadingBins, STATGROUP_Nanite);
DECLARE_DWORD_COUNTER_STAT(TEXT("BasePass Visible Shading Bins"), STAT_NaniteBasePassVisibleShadingBins, STATGROUP_Nanite);

CSV_DEFINE_CATEGORY(LightCount, true);

/*-----------------------------------------------------------------------------
	Global Illumination Plugin Function Delegates
-----------------------------------------------------------------------------*/

static FGlobalIlluminationPluginDelegates::FAnyRayTracingPassEnabled GIPluginAnyRaytracingPassEnabledDelegate;
FGlobalIlluminationPluginDelegates::FAnyRayTracingPassEnabled& FGlobalIlluminationPluginDelegates::AnyRayTracingPassEnabled()
{
	return GIPluginAnyRaytracingPassEnabledDelegate;
}

static FGlobalIlluminationPluginDelegates::FPrepareRayTracing GIPluginPrepareRayTracingDelegate;
FGlobalIlluminationPluginDelegates::FPrepareRayTracing& FGlobalIlluminationPluginDelegates::PrepareRayTracing()
{
	return GIPluginPrepareRayTracingDelegate;
}

static FGlobalIlluminationPluginDelegates::FRenderDiffuseIndirectLight GIPluginRenderDiffuseIndirectLightDelegate;
FGlobalIlluminationPluginDelegates::FRenderDiffuseIndirectLight& FGlobalIlluminationPluginDelegates::RenderDiffuseIndirectLight()
{
	return GIPluginRenderDiffuseIndirectLightDelegate;
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
static FGlobalIlluminationPluginDelegates::FRenderDiffuseIndirectVisualizations GIPluginRenderDiffuseIndirectVisualizationsDelegate;
FGlobalIlluminationPluginDelegates::FRenderDiffuseIndirectVisualizations& FGlobalIlluminationPluginDelegates::RenderDiffuseIndirectVisualizations()
{
	return GIPluginRenderDiffuseIndirectVisualizationsDelegate;
}
#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)

const TCHAR* GetDepthPassReason(bool bDitheredLODTransitionsUseStencil, EShaderPlatform ShaderPlatform)
{
	if (IsForwardShadingEnabled(ShaderPlatform))
	{
		return TEXT("(Forced by ForwardShading)");
	}

	if (UseNanite(ShaderPlatform))
	{
		return TEXT("(Forced by Nanite)");
	}

	if (IsUsingDBuffers(ShaderPlatform))
	{
		return TEXT("(Forced by DBuffer)");
	}

	if (UseVirtualTexturing(ShaderPlatform))
	{
		return TEXT("(Forced by VirtualTexture)");
	}

	if (bDitheredLODTransitionsUseStencil)
	{
		return TEXT("(Forced by StencilLODDither)");
	}

	return TEXT("");
}

/*-----------------------------------------------------------------------------
	FDeferredShadingSceneRenderer
-----------------------------------------------------------------------------*/

FDeferredShadingSceneRenderer::FDeferredShadingSceneRenderer(const FSceneViewFamily* InViewFamily, FHitProxyConsumer* HitProxyConsumer)
	: FSceneRenderer(InViewFamily, HitProxyConsumer)
	, DepthPass(GetDepthPassInfo(Scene))
	, bAreLightsInLightGrid(false)
{
	ViewPipelineStates.SetNum(AllViews.Num());
	// Initialize scene renderer extensions here, after the rest of the renderer has been initialized
	InitSceneExtensionsRenderers(ViewFamily.EngineShowFlags, true);
}

/** 
* Renders the view family. 
*/
DECLARE_CYCLE_STAT(TEXT("Wait RayTracing Dynamic Bindings"), STAT_WaitRayTracingDynamicBindings, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("Wait Ray Tracing Scene Initialization"), STAT_WaitRayTracingSceneInitTask, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("Wait Ray Tracing Visible Shader Bindings Finalize"), STAT_WaitRayTracingVisibleShaderBindingsFinalizeTask, STATGROUP_SceneRendering);

DECLARE_CYCLE_STAT(TEXT("Wait Gather And Sort Lights"), STAT_WaitGatherAndSortLightsTask, STATGROUP_SceneRendering);

/**
 * Returns true if the depth Prepass needs to run
 */
bool FDeferredShadingSceneRenderer::ShouldRenderPrePass() const
{
	return (DepthPass.EarlyZPassMode != DDM_None || DepthPass.bEarlyZPassMovable != 0);
}

/**
 * Returns true if the Nanite rendering needs to run
 */
bool FDeferredShadingSceneRenderer::ShouldRenderNanite() const
{
	return UseNanite(ShaderPlatform) && ViewFamily.EngineShowFlags.NaniteMeshes && Nanite::GStreamingManager.HasResourceEntries();
}

bool FDeferredShadingSceneRenderer::RenderHzb(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture, const FBuildHZBAsyncComputeParams* AsyncComputeParams, Froxel::FRenderer& FroxelRenderer)
{
	RDG_EVENT_SCOPE_STAT(GraphBuilder, HZB, "HZB");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HZB);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

		FSceneViewState* ViewState = View.ViewState;
		const FPerViewPipelineState& ViewPipelineState = GetViewPipelineState(View);


		if (ViewPipelineState.bClosestHZB || ViewPipelineState.bFurthestHZB)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "BuildHZB(ViewId=%d)", ViewIndex);

			FRDGTextureRef ClosestHZBTexture = nullptr;
			FRDGTextureRef FurthestHZBTexture = nullptr;

			BuildHZB(
				GraphBuilder,
				SceneDepthTexture,
				/* VisBufferTexture = */ nullptr,
				View.ViewRect,
				View.GetFeatureLevel(),
				View.GetShaderPlatform(),
				TEXT("HZBClosest"),
				/* OutClosestHZBTexture = */ ViewPipelineState.bClosestHZB ? &ClosestHZBTexture : nullptr,
				TEXT("HZBFurthest"),
				/* OutFurthestHZBTexture = */ &FurthestHZBTexture,
				BuildHZBDefaultPixelFormat,
				AsyncComputeParams,
				FroxelRenderer.GetView(ViewIndex));

			// Update the view.
			{
				View.HZBMipmap0Size = FurthestHZBTexture->Desc.Extent;
				View.HZB = FurthestHZBTexture;

				// Extract furthest HZB texture.
				if (View.ViewState)
				{
					if (ShouldRenderNanite() || FInstanceCullingContext::IsOcclusionCullingEnabled())
					{
						GraphBuilder.QueueTextureExtraction(FurthestHZBTexture, &View.ViewState->PrevFrameViewInfo.HZB);
					}
					else
					{
						View.ViewState->PrevFrameViewInfo.HZB = nullptr;
					}
				}

				// Extract closest HZB texture.
				if (ViewPipelineState.bClosestHZB)
				{
					View.ClosestHZB = ClosestHZBTexture;
				}
			}
		}

		if (FamilyPipelineState->bHZBOcclusion && ViewState && ViewState->HZBOcclusionTests.GetNum() != 0)
		{
			check(ViewState->HZBOcclusionTests.IsValidFrame(ViewState->OcclusionFrameCounter));
			ViewState->HZBOcclusionTests.Submit(GraphBuilder, View);
		}

		if (Scene->InstanceCullingOcclusionQueryRenderer && View.ViewState)
		{
			// Render per-instance occlusion queries and save the mask to interpret results on the next frame
			const uint32 OcclusionQueryMaskForThisView = Scene->InstanceCullingOcclusionQueryRenderer->Render(GraphBuilder, Scene->GPUScene, View);
			View.ViewState->PrevFrameViewInfo.InstanceOcclusionQueryMask = OcclusionQueryMaskForThisView;
		}
	}

	return FamilyPipelineState->bHZBOcclusion;
}

BEGIN_SHADER_PARAMETER_STRUCT(FRenderOpaqueFXPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
END_SHADER_PARAMETER_STRUCT()

static void RenderOpaqueFX(
	FRDGBuilder& GraphBuilder,
	TConstStridedView<FSceneView> Views,
	FSceneUniformBuffer &SceneUniformBuffer,
	FFXSystemInterface* FXSystem,
	ERHIFeatureLevel::Type FeatureLevel,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer)
{
	// Notify the FX system that opaque primitives have been rendered and we now have a valid depth buffer.
	if (FXSystem && Views.Num() > 0)
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, PostRenderOpsFX, "PostRenderOpsFX");
		RDG_GPU_STAT_SCOPE(GraphBuilder, PostRenderOpsFX);
		RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderOpaqueFX);

		const ERDGPassFlags UBPassFlags = ERDGPassFlags::Compute | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::NeverCull;

		if (HasRayTracedOverlay(*Views[0].Family))
		{
			// In the case of Path Tracing/RT Debug -- we have not yet written to the SceneColor buffer, so make a dummy set of textures instead
			SceneTexturesUniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, nullptr, FeatureLevel, ESceneTextureSetupMode::SceneVelocity);
		}

		// Add a pass which extracts the RHI handle from the scene textures UB and sends it to the FX system.
		FRenderOpaqueFXPassParameters* ExtractUBPassParameters = GraphBuilder.AllocParameters<FRenderOpaqueFXPassParameters>();
		ExtractUBPassParameters->SceneTextures = SceneTexturesUniformBuffer;
		GraphBuilder.AddPass(RDG_EVENT_NAME("SetSceneTexturesUniformBuffer"), ExtractUBPassParameters, UBPassFlags, [ExtractUBPassParameters, FXSystem](FRHICommandListImmediate&)
		{
			FXSystem->SetSceneTexturesUniformBuffer(ExtractUBPassParameters->SceneTextures->GetRHIRef());
		});

		FXSystem->PostRenderOpaque(GraphBuilder, Views, SceneUniformBuffer, true /*bAllowGPUParticleUpdate*/);

		// Clear the scene textures UB pointer on the FX system. Use the same pass parameters to extend resource lifetimes.
		GraphBuilder.AddPass(RDG_EVENT_NAME("UnsetSceneTexturesUniformBuffer"), ExtractUBPassParameters, UBPassFlags, [FXSystem](FRHICommandListImmediate&)
		{
			FXSystem->SetSceneTexturesUniformBuffer({});
		});

		if (FGPUSortManager* GPUSortManager = FXSystem->GetGPUSortManager())
		{
			GPUSortManager->OnPostRenderOpaque(GraphBuilder);
		}
	}
}

#if RHI_RAYTRACING

static bool ShouldPrepareRayTracingDecals(const FScene& Scene, const FSceneViewFamily& ViewFamily)
{
	if (!IsRayTracingEnabled() || !RHISupportsRayTracingCallableShaders(ViewFamily.GetShaderPlatform()))
	{
		return false;
	}

	if (Scene.Decals.Num() == 0 || RayTracing::ShouldExcludeDecals())
	{
		return false;
	}

	return ViewFamily.EngineShowFlags.PathTracing && PathTracing::UsesDecals(ViewFamily);
}

static void DeduplicateRayGenerationShaders(TArray< FRHIRayTracingShader*>& RayGenShaders)
{
	TSet<FRHIRayTracingShader*> UniqueRayGenShaders;
	for (FRHIRayTracingShader* Shader : RayGenShaders)
	{
		UniqueRayGenShaders.Add(Shader);
	}
	RayGenShaders = UniqueRayGenShaders.Array();
}

BEGIN_SHADER_PARAMETER_STRUCT(FSetRayTracingBindingsPassParams, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FRayTracingLightGrid, LightGridPacked)
	SHADER_PARAMETER_STRUCT_REF(FLumenHardwareRayTracingUniformBufferParameters, LumenHardwareRayTracingUniformBuffer)

	RDG_BUFFER_ACCESS(InlineRayTracingBindingDataBuffer, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()

bool FDeferredShadingSceneRenderer::SetupRayTracingPipelineStatesAndSBT(FRDGBuilder& GraphBuilder, bool bAnyInlineHardwareRayTracingPassEnabled, bool& bOutIsUsingFallbackRTPSO)
{
	if (!IsRayTracingEnabled() || Views.Num() == 0)
	{
		return false;
	}

	if (!FamilyPipelineState[&FFamilyPipelineState::bRayTracing])
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::SetupRayTracingPipelineStatesAndSBT);

	if (!GRHISupportsRayTracingShaders && !GRHISupportsInlineRayTracing)
	{
		return false;
	}

	const bool bIsPathTracing = ViewFamily.EngineShowFlags.PathTracing;

	// Get the max required local binding data size - SBTs are versioned together so using single initializer for now
	uint32 MaxLocalBindingDataSize = 0;
	ERayTracingShaderBindingMode ShaderBindingMode = ERayTracingShaderBindingMode::Disabled;

	if (GRHISupportsRayTracingShaders)
	{
		// #dxr_todo: UE-72565: refactor ray tracing effects to not be member functions of DeferredShadingRenderer. 
		// Should register each effect at startup and just loop over them automatically to gather all required shaders.

		TArray<FRHIRayTracingShader*> RayGenShaders;

		// We typically see ~120 raygen shaders, but allow some headroom to avoid reallocation if our estimate is wrong.
		RayGenShaders.Reserve(256);

		if (bIsPathTracing)
		{
			// This view only needs the path tracing raygen shaders as all other
			// passes should be disabled.
			for (const FViewInfo& View : Views)
			{
				PreparePathTracing(View, *Scene, RayGenShaders);
			}
		}
		else
		{
			// Path tracing is disabled, get all other possible raygen shaders
			PrepareRayTracingDebug(ViewFamily, RayGenShaders);

			// These other cases do potentially depend on the camera position since they are
			// driven by FinalPostProcessSettings, which is why we need to merge them across views
			if (!IsForwardShadingEnabled(ShaderPlatform))
			{
				for (const FViewInfo& View : Views)
				{
					PrepareRayTracingShadows(View, *Scene, RayGenShaders);
					PrepareRayTracingAmbientOcclusion(View, RayGenShaders);
					PrepareRayTracingSkyLight(View, *Scene, RayGenShaders);
					PrepareRayTracingGlobalIlluminationPlugin(View, RayGenShaders);
					PrepareRayTracingTranslucency(View, RayGenShaders);
					PrepareRayTracingVolumetricFogShadows(View, *Scene, RayGenShaders);

					if (DoesPlatformSupportLumenGI(ShaderPlatform) && Lumen::UseHardwareRayTracing(ViewFamily))
					{
						PrepareLumenHardwareRayTracingScreenProbeGather(View, RayGenShaders);
						PrepareLumenHardwareRayTracingShortRangeAO(View, RayGenShaders);
						PrepareLumenHardwareRayTracingRadianceCache(View, RayGenShaders);
						PrepareLumenHardwareRayTracingReflections(View, RayGenShaders);
						PrepareHardwareRayTracingTranslucency(View, RayGenShaders);
						PrepareLumenHardwareRayTracingReSTIR(View, RayGenShaders);
						PrepareLumenHardwareRayTracingVisualize(View, RayGenShaders);
					}

					PrepareMegaLightsHardwareRayTracing(View, *Scene, RayGenShaders);
				}
			}
		}

		if (Views.Num() > 1)
		{
			// If we have more than one View, chances are we got many duplicates, so compact the list here
			DeduplicateRayGenerationShaders(RayGenShaders);
		}

		if (RayGenShaders.Num())
		{
			// Create RTPSO and kick off high-level material parameter binding tasks which will be consumed during RDG execution in BindRayTracingMaterialPipeline()			
			CreateMaterialRayTracingMaterialPipeline(GraphBuilder, RayGenShaders, MaxLocalBindingDataSize, bOutIsUsingFallbackRTPSO);
			
			// Need RTPSO
			EnumAddFlags(ShaderBindingMode, ERayTracingShaderBindingMode::RTPSO);

			// Allocate, or check if the persistent SBT RHI is still valid
			if (Scene->MaterialRayTracingSBTID == INDEX_NONE)
			{
				Scene->MaterialRayTracingSBTID = Scene->RayTracingSBT.AllocatePersistentSBTID(GraphBuilder.RHICmdList, ERayTracingShaderBindingMode::RTPSO);
			}
		}
		else if (Scene->MaterialRayTracingSBTID != INDEX_NONE)
		{
			Scene->RayTracingSBT.ReleasePersistentSBT(Scene->MaterialRayTracingSBTID);
			Scene->MaterialRayTracingSBTID = INDEX_NONE;
		}
	}

	// Create Lumen hardware ray tracing SBT and material pipeline
	{
		ERayTracingShaderBindingMode LumenShaderBindingMode = ERayTracingShaderBindingMode::Disabled;

		if (!bIsPathTracing)
		{
			TArray<FRHIRayTracingShader*> LumenHardwareRayTracingRayGenShaders;

			if (GRHISupportsRayTracingShaders)
			{
				if (DoesPlatformSupportLumenGI(ShaderPlatform))
				{
					for (const FViewInfo& View : Views)
					{
						PrepareLumenHardwareRayTracingVisualizeLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
						PrepareLumenHardwareRayTracingRadianceCacheLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
						PrepareLumenHardwareRayTracingTranslucencyVolumeLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
						PrepareLumenHardwareRayTracingRadiosityLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
						PrepareLumenHardwareRayTracingReflectionsLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
						PrepareLumenHardwareRayTracingReSTIRLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
						PrepareLumenHardwareRayTracingScreenProbeGatherLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
						PrepareLumenHardwareRayTracingDirectLightingLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
					}
				}

				for (const FViewInfo& View : Views)
				{
					PrepareMegaLightsHardwareRayTracingLumenMaterial(View, *Scene, LumenHardwareRayTracingRayGenShaders);
				}

				DeduplicateRayGenerationShaders(LumenHardwareRayTracingRayGenShaders);
			}

			if (LumenHardwareRayTracingRayGenShaders.Num())
			{
				CreateLumenHardwareRayTracingMaterialPipeline(GraphBuilder, LumenHardwareRayTracingRayGenShaders, MaxLocalBindingDataSize);
				EnumAddFlags(LumenShaderBindingMode, ERayTracingShaderBindingMode::RTPSO);
			}
		}

		if (LumenShaderBindingMode != ERayTracingShaderBindingMode::Disabled)
		{
			// Allocate, or check if the persistent SBT RHI is still valid
			if (Scene->LumenRayTracingSBTID == INDEX_NONE)
			{
				Scene->LumenRayTracingSBTID = Scene->RayTracingSBT.AllocatePersistentSBTID(GraphBuilder.RHICmdList, LumenShaderBindingMode);
			}
		}
		else if (Scene->LumenRayTracingSBTID != INDEX_NONE)
		{
			// release Lumen persistent SBT when not needed
			Scene->RayTracingSBT.ReleasePersistentSBT(Scene->LumenRayTracingSBTID);
			Scene->LumenRayTracingSBTID = INDEX_NONE;
		}

		// Merge Lumen shader binding mode with the shared shader binding mode
		EnumAddFlags(ShaderBindingMode, LumenShaderBindingMode);
	}

	// Check if inline SBT is needed or not			
	if (bAnyInlineHardwareRayTracingPassEnabled && GRHIGlobals.RayTracing.RequiresInlineRayTracingSBT)
	{
		if (Scene->InlineRayTracingSBTID == INDEX_NONE)
		{
			Scene->InlineRayTracingSBTID = Scene->RayTracingSBT.AllocatePersistentSBTID(GraphBuilder.RHICmdList, ERayTracingShaderBindingMode::Inline);
		}
		
		EnumAddFlags(ShaderBindingMode, ERayTracingShaderBindingMode::Inline);
	}
	else if (Scene->InlineRayTracingSBTID != INDEX_NONE)
	{
		Scene->RayTracingSBT.ReleasePersistentSBT(Scene->InlineRayTracingSBTID);
		Scene->InlineRayTracingSBTID = INDEX_NONE;
	}

	FRHIShaderBindingTable* MaterialSBT = nullptr;
	FRHIShaderBindingTable* LumenSBT = nullptr;
	FRHIShaderBindingTable* InlineSBT = nullptr;

	FRDGBufferRef InlineBindingDataBuffer = nullptr;

	if (ShaderBindingMode != ERayTracingShaderBindingMode::Disabled)
	{
		Scene->RayTracingSBT.CheckPersistentRHI(GraphBuilder.RHICmdList, MaxLocalBindingDataSize);

		MaterialSBT = Scene->MaterialRayTracingSBTID != INDEX_NONE ? Scene->RayTracingSBT.GetPersistentSBT(Scene->MaterialRayTracingSBTID) : nullptr;
		LumenSBT = Scene->LumenRayTracingSBTID != INDEX_NONE ? Scene->RayTracingSBT.GetPersistentSBT(Scene->LumenRayTracingSBTID) : nullptr;

		if (Scene->InlineRayTracingSBTID != INDEX_NONE)
		{
			InlineSBT = Scene->RayTracingSBT.GetPersistentSBT(Scene->InlineRayTracingSBTID);
			InlineBindingDataBuffer = Scene->RayTracingSBT.GetPersistentInlineBindingDataBuffer(GraphBuilder, Scene->InlineRayTracingSBTID);
		}
	}

	// Send SBTs to all views since they all share the same ones
	EnumerateLinkedViews([MaterialSBT, LumenSBT, InlineSBT, InlineBindingDataBuffer] (FViewInfo& View)
	{
		if (View.bHasAnyRayTracingPass)
		{
			View.MaterialRayTracingData.ShaderBindingTable = MaterialSBT;
			View.LumenRayTracingData.ShaderBindingTable = LumenSBT;
			View.InlineRayTracingData.ShaderBindingTable = InlineSBT;

			View.InlineRayTracingBindingDataBuffer = InlineBindingDataBuffer;
		}
		return true;
	});

	return true;
}

void FDeferredShadingSceneRenderer::SetupRayTracingLightDataForViews(FRDGBuilder& GraphBuilder)
{
	if (!FamilyPipelineState[&FFamilyPipelineState::bRayTracing])
	{
		return;
	}

	const bool bPathTracingEnabled = ViewFamily.EngineShowFlags.PathTracing && FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(Scene->GetShaderPlatform());

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];

		bool bBuildLightGrid = false;

		// Path Tracing currently uses its own code to manage lights, so doesn't need to run this.
		if (!bPathTracingEnabled)
		{
			if (Lumen::IsUsingRayTracingLightingGrid(ViewFamily, View, GetViewPipelineState(View).DiffuseIndirectMethod)
				|| GetRayTracingTranslucencyOptions(View).bEnabled
				|| ViewFamily.EngineShowFlags.RayTracingDebug)
			{
				bBuildLightGrid = true;
			}
		}

		// The light data is built in TranslatedWorld space so must be built per view
		View.RayTracingLightGridUniformBuffer = CreateRayTracingLightData(GraphBuilder, Scene, View, View.ShaderMap, bBuildLightGrid);
	}
}

bool FDeferredShadingSceneRenderer::DispatchRayTracingWorldUpdates(FRDGBuilder& GraphBuilder, FRDGBufferRef& OutDynamicGeometryScratchBuffer, ERHIPipeline ResourceAccessPipelines)
{
	OutDynamicGeometryScratchBuffer = nullptr;

	if (!FamilyPipelineState[&FFamilyPipelineState::bRayTracing])
	{
		// - Nanite ray tracing instances are already pointing at the new BLASes and RayTracingDataOffsets in GPUScene have been updated
		Nanite::GRayTracingManager.ProcessBuildRequests(GraphBuilder);
		return false;
	}

	check(IsRayTracingEnabled() && !Views.IsEmpty());

	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::DispatchRayTracingWorldUpdates);

	const int32 ReferenceViewIndex = 0;
	FViewInfo& ReferenceView = Views[ReferenceViewIndex];

	{
		SCOPE_CYCLE_COUNTER(STAT_WaitRayTracingSceneInitTask);
		ReferenceView.RayTracingSceneInitTask.Wait();
	}

	const bool bRayTracingAsyncBuild = CVarRayTracingAsyncBuild.GetValueOnRenderThread() != 0 && GRHISupportsRayTracingAsyncBuildAccelerationStructure;
	const ERDGPassFlags ComputePassFlags = bRayTracingAsyncBuild ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute;

	FRayTracingScene& RayTracingScene = Scene->RayTracingScene;
	if (RayTracingScene.GeometriesToBuild.Num() > 0)
	{
		// Force update all the collected geometries (use stack allocator?)
		GRayTracingGeometryManager->ForceBuildIfPending(GraphBuilder.RHICmdList, RayTracingScene.GeometriesToBuild);
	}

	{
		Nanite::GRayTracingManager.ProcessUpdateRequests(GraphBuilder, GetSceneUniforms());
		const bool bAnyBlasRebuilt = Nanite::GRayTracingManager.ProcessBuildRequests(GraphBuilder);
		if (bAnyBlasRebuilt)
		{
			for (FViewInfo& View : Views)
			{
				if (View.ViewState != nullptr && !View.bIsOfflineRender)
				{
					// don't invalidate in the offline case because we only get one attempt at rendering each sample
					View.ViewState->PathTracingInvalidate();
				}
			}
		}
	}

	// Keep mask the same as what's already set (which will be the view mask) if TLAS updates should be masked to the view
	RDG_GPU_MASK_SCOPE(GraphBuilder, GRayTracingMultiGpuTLASMask ? GraphBuilder.RHICmdList.GetGPUMask() : FRHIGPUMask::All());

	FRayTracingDynamicGeometryUpdateManager* DynamicGeometryUpdateManager = Scene->GetRayTracingDynamicGeometryUpdateManager();
	DynamicGeometryUpdateManager->AddDynamicGeometryUpdatePass(GraphBuilder, ComputePassFlags, GetSceneUniformBufferRef(GraphBuilder), RayTracingScene.bTracingFeedbackEnabled, ResourceAccessPipelines, OutDynamicGeometryScratchBuffer);

	((FRayTracingGeometryManager*)GRayTracingGeometryManager)->ResetVisibleGeometries();

	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, RayTracingScene, "RayTracingScene");
		RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingScene);
		RayTracingScene.Create(GraphBuilder, ReferenceView, &Scene->GPUScene, ComputePassFlags);
		RayTracingScene.Build(GraphBuilder, ComputePassFlags | ERDGPassFlags::NeverCull, OutDynamicGeometryScratchBuffer);
	}

	GraphBuilder.AddDispatchHint();

	return true;
}

void FDeferredShadingSceneRenderer::SetupRayTracingRenderingData(FRDGBuilder& GraphBuilder)
{
	check(FamilyPipelineState[&FFamilyPipelineState::bRayTracing]);

	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::SetupRayTracingRenderingData);

	// Keep mask the same as what's already set (which will be the view mask) if TLAS updates should be masked to the view
	RDG_GPU_MASK_SCOPE(GraphBuilder, GRayTracingMultiGpuTLASMask ? GraphBuilder.RHICmdList.GetGPUMask() : FRHIGPUMask::All());

	bool bAnyInlineHardwareRayTracingPassEnabled = false;

	for (FViewInfo& View : Views)
	{
		if (View.bHasAnyRayTracingPass)
		{
			SetupLumenHardwareRayTracingUniformBuffer(View);
		}
		
		if (Lumen::AnyLumenHardwareInlineRayTracingPassEnabled(Scene, View)
			|| MegaLights::UseInlineHardwareRayTracing(ViewFamily))
		{
			bAnyInlineHardwareRayTracingPassEnabled = true;
		}
	}

	const bool bShouldRenderNanite = ShouldRenderNanite();

	Nanite::GRayTracingManager.UpdateUniformBuffer(GraphBuilder, bShouldRenderNanite);

	{
		SCOPE_CYCLE_COUNTER(STAT_WaitRayTracingDynamicBindings);

		for (FViewInfo& View : Views)
		{
			if (View.AddDynamicRayTracingMeshBatchTaskList.IsEmpty())
			{
				continue;
			}

			// need to wait for dynamic mesh batches tasks to finish before executing SetupRayTracingPipelineStatesAndSBT(...)
			// since they can request new materials that need to be included in RTPSO 
			UE::Tasks::Wait(View.AddDynamicRayTracingMeshBatchTaskList);
		}
	}

	bool bIsUsingFallbackRTPSO = false;
	SetupRayTracingPipelineStatesAndSBT(GraphBuilder, bAnyInlineHardwareRayTracingPassEnabled, bIsUsingFallbackRTPSO);

	const int32 ReferenceViewIndex = 0;
	FViewInfo& ReferenceView = Views[ReferenceViewIndex];

	{
		SCOPE_CYCLE_COUNTER(STAT_WaitRayTracingVisibleShaderBindingsFinalizeTask);
		ReferenceView.VisibleRayTracingShaderBindingsFinalizeTask.Wait();
	}

	// merge dynamic bindings from all views into ReferenceView visible bindings
	for (FViewInfo& View : Views)
	{
		for (int32 TaskIndex = 0; TaskIndex < View.AddDynamicRayTracingMeshBatchTaskList.Num(); TaskIndex++)
		{
			ReferenceView.VisibleRayTracingShaderBindings.Append(*View.DynamicRayTracingShaderBindingsPerTask[TaskIndex]);
		}

		View.AddDynamicRayTracingMeshBatchTaskList.Empty();
	}
	
	// Build the dirty persistent shader bindings from the visible shader bindings (SBT version is updated after the PSO and SBTs have been created)
	// Inline lumen buffer bindings don't use the dirty bindings yet - so don't need to build it here (uses the visible binding set)
	bool bRequireBindingsUpdate = ReferenceView.MaterialRayTracingData.ShaderBindingTable || ReferenceView.LumenRayTracingData.ShaderBindingTable || ReferenceView.InlineRayTracingData.ShaderBindingTable;
	if (bRequireBindingsUpdate)
	{
		// If fallback RTPSO then mark all bindings as dirty because they need to bound again when final RTPSO is ready (Shader identifier could have changed)
		bool bForceAllDirty = bIsUsingFallbackRTPSO;
		ReferenceView.DirtyPersistentRayTracingShaderBindings = Scene->RayTracingSBT.GetDirtyBindings(ReferenceView.VisibleRayTracingShaderBindings, bForceAllDirty);
	}

	// Prepare the local ray tracing shader binding data to update on RHI timeline for Material, Lumen and InlineLumen
	if (ReferenceView.MaterialRayTracingData.ShaderBindingTable)
	{
		SetupMaterialRayTracingHitGroupBindings(GraphBuilder, ReferenceView);
	}
	if (ReferenceView.LumenRayTracingData.ShaderBindingTable)
	{
		SetupLumenHardwareRayTracingHitGroupBindings(GraphBuilder, ReferenceView);	
	}
	if (ReferenceView.InlineRayTracingData.ShaderBindingTable)
	{		
		TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::SetupInlineHardwareRaytracingHitGroupBindings);

		uint32 ShaderSlotsPerSegment = Scene->RayTracingSBT.GetNumShaderSlotsPerSegment();
		AddRayTracingLocalShaderBindingWriterTasks(GraphBuilder, ReferenceView.DirtyPersistentRayTracingShaderBindings, ReferenceView.InlineRayTracingData.MaterialBindings, 
		                                           [ShaderSlotsPerSegment](const FRayTracingShaderBindingData& RTShaderBindingData, FRayTracingLocalShaderBindingWriter* BindingWriter)
		{
			const FRayTracingMeshCommand& MeshCommand = *RTShaderBindingData.RayTracingMeshCommand;

			for (uint32 SlotIndex = 0; SlotIndex < ShaderSlotsPerSegment; ++SlotIndex)
			{
				FRayTracingLocalShaderBindings& Binding = BindingWriter->AddWithExternalParameters();
				Binding.RecordIndex = RTShaderBindingData.SBTRecordIndex + SlotIndex;
				Binding.Geometry = RTShaderBindingData.RayTracingGeometry;
				Binding.SegmentIndex = MeshCommand.GeometrySegmentIndex;							
				Binding.BindingType = RTShaderBindingData.BindingType;
				Binding.UserData = 0;
			}
		});
	}
	
	if (bAnyInlineHardwareRayTracingPassEnabled)
	{
		SetupLumenHardwareRayTracingHitGroupBuffer(GraphBuilder, ReferenceView);
	}

	const bool bIsPathTracing = ViewFamily.EngineShowFlags.PathTracing;

	FSetRayTracingBindingsPassParams* PassParams = GraphBuilder.AllocParameters<FSetRayTracingBindingsPassParams>();
	PassParams->Scene = GetSceneUniformBufferRef(GraphBuilder);
	PassParams->LightGridPacked = bIsPathTracing ? nullptr : ReferenceView.RayTracingLightGridUniformBuffer; // accessed by FRayTracingLightingMS // Is this needed for anything?
	PassParams->LumenHardwareRayTracingUniformBuffer = ReferenceView.LumenHardwareRayTracingUniformBuffer;
	PassParams->InlineRayTracingBindingDataBuffer = ReferenceView.InlineRayTracingBindingDataBuffer;

	const FRayTracingLightFunctionMap* RayTracingLightFunctionMap = GraphBuilder.Blackboard.Get<FRayTracingLightFunctionMap>();
	GraphBuilder.AddPass(RDG_EVENT_NAME("SetRayTracingBindings"), PassParams, ERDGPassFlags::Copy | ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
		[this, PassParams, bIsPathTracing, &ReferenceView, RayTracingLightFunctionMap](FRDGAsyncTask, FRHICommandList& RHICmdList)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SetRayTracingBindings);

		check(ReferenceView.MaterialRayTracingData.PipelineState || ReferenceView.MaterialRayTracingData.MaterialBindings.Num() == 0);

		Scene->RayTracingSBT.FlushAllocationsToClear(RHICmdList);

		if (ReferenceView.MaterialRayTracingData.PipelineState && (ReferenceView.MaterialRayTracingData.MaterialBindings.Num() || ReferenceView.MaterialRayTracingData.CallableBindings.Num()))
		{
			SetRaytracingShaderBindings(RHICmdList, Allocator, ReferenceView.MaterialRayTracingData);

			if (bIsPathTracing)
			{
				SetupPathTracingDefaultMissShader(RHICmdList, ReferenceView);

				BindLightFunctionShadersPathTracing(RHICmdList, Scene, RayTracingLightFunctionMap, ReferenceView);
			}
			else
			{
				SetupRayTracingDefaultMissShader(RHICmdList, ReferenceView);
				SetupRayTracingLightingMissShader(RHICmdList, ReferenceView);

				BindLightFunctionShaders(RHICmdList, Scene, RayTracingLightFunctionMap, ReferenceView);
			}
			
			RHICmdList.CommitShaderBindingTable(ReferenceView.MaterialRayTracingData.ShaderBindingTable);			
		}

		if (!bIsPathTracing)
		{
			if (GRHISupportsRayTracingShaders || GRHISupportsInlineRayTracing)
			{
				if (ReferenceView.LumenRayTracingData.PipelineState)
				{
					RHICmdList.SetRayTracingMissShader(ReferenceView.LumenRayTracingData.ShaderBindingTable, RAY_TRACING_MISS_SHADER_SLOT_DEFAULT, ReferenceView.LumenRayTracingData.PipelineState, 0 /* MissShaderPipelineIndex */, 0, nullptr, 0);
				}

				if (ReferenceView.LumenRayTracingData.ShaderBindingTable)
				{
					SetRaytracingShaderBindings(RHICmdList, Allocator, ReferenceView.LumenRayTracingData);
					RHICmdList.CommitShaderBindingTable(ReferenceView.LumenRayTracingData.ShaderBindingTable);
				}

				if (ReferenceView.InlineRayTracingData.ShaderBindingTable)
				{
					check(ReferenceView.InlineRayTracingBindingDataBuffer);

					SetRaytracingShaderBindings(RHICmdList, Allocator, ReferenceView.InlineRayTracingData);
					
					RHICmdList.CommitShaderBindingTable(ReferenceView.InlineRayTracingData.ShaderBindingTable, PassParams->InlineRayTracingBindingDataBuffer->GetRHI());
				}
			}
		}
	});
}

#endif // RHI_RAYTRACING

void FDeferredShadingSceneRenderer::BeginInitDynamicShadows(FRDGBuilder& GraphBuilder, FInitViewTaskDatas& TaskDatas, FInstanceCullingManager& InstanceCullingManager)
{
	extern int32 GEarlyInitDynamicShadows;

	// This is called from multiple locations and will succeed if the visibility tasks are ready.
	if (!TaskDatas.DynamicShadows
		&& GEarlyInitDynamicShadows != 0
		&& ViewFamily.EngineShowFlags.DynamicShadows
		&& !ViewFamily.EngineShowFlags.HitProxies
		&& !HasRayTracedOverlay(ViewFamily)
		&& TaskDatas.VisibilityTaskData->IsTaskWaitingAllowed())
	{
		TaskDatas.DynamicShadows = FSceneRenderer::BeginInitDynamicShadows(GraphBuilder, true, TaskDatas.VisibilityTaskData, InstanceCullingManager);
	}
}

void FDeferredShadingSceneRenderer::FinishInitDynamicShadows(FRDGBuilder& GraphBuilder, FDynamicShadowsTaskData*& TaskData, FInstanceCullingManager& InstanceCullingManager)
{
	if (ViewFamily.EngineShowFlags.DynamicShadows && !ViewFamily.EngineShowFlags.HitProxies && !HasRayTracedOverlay(ViewFamily))
	{
		// Setup dynamic shadows.
		if (TaskData)
		{
			FSceneRenderer::FinishInitDynamicShadows(GraphBuilder, TaskData);
		}
		else
		{
			TaskData = InitDynamicShadows(GraphBuilder, InstanceCullingManager);
		}
	}
}

static TAutoConsoleVariable<float> CVarStallInitViews(
	TEXT("CriticalPathStall.AfterInitViews"),
	0.0f,
	TEXT("Sleep for the given time after InitViews. Time is given in ms. This is a debug option used for critical path analysis and forcing a change in the critical path."));

void FDeferredShadingSceneRenderer::CommitFinalPipelineState()
{
	// Family pipeline state
	{
		FamilyPipelineState.Set(&FFamilyPipelineState::bNanite, UseNanite(ShaderPlatform)); // TODO: Should this respect ViewFamily.EngineShowFlags.NaniteMeshes?

		static const auto ICVarHZBOcc = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HZBOcclusion"));
		FamilyPipelineState.Set(&FFamilyPipelineState::bHZBOcclusion, ICVarHZBOcc->GetInt() != 0);	
	}

	CommitIndirectLightingState();

	// Views pipeline states
	for (int32 ViewIndex = 0; ViewIndex < AllViews.Num(); ViewIndex++)
	{
		const FViewInfo& View = *AllViews[ViewIndex];
		TPipelineState<FPerViewPipelineState>& ViewPipelineState = GetViewPipelineStateWritable(View);

		// Commit HZB state
		{
			const bool bHasSSGI = ViewPipelineState[&FPerViewPipelineState::DiffuseIndirectMethod] == EDiffuseIndirectMethod::SSGI;
			const bool bUseLumen = ViewPipelineState[&FPerViewPipelineState::DiffuseIndirectMethod] == EDiffuseIndirectMethod::Lumen 
				|| ViewPipelineState[&FPerViewPipelineState::ReflectionsMethod] == EReflectionsMethod::Lumen;
			const bool bHasFirstPersonSelfShadow = ShouldRenderFirstPersonSelfShadow(ViewFamily);

			// Requires FurthestHZB
			ViewPipelineState.Set(&FPerViewPipelineState::bFurthestHZB,
				FamilyPipelineState[&FFamilyPipelineState::bHZBOcclusion] ||
				FamilyPipelineState[&FFamilyPipelineState::bNanite] ||
				ViewPipelineState[&FPerViewPipelineState::AmbientOcclusionMethod] == EAmbientOcclusionMethod::SSAO ||
				ViewPipelineState[&FPerViewPipelineState::ReflectionsMethod] == EReflectionsMethod::SSR ||
				bHasSSGI || bUseLumen);

			ViewPipelineState.Set(&FPerViewPipelineState::bClosestHZB, 
				bHasSSGI || bUseLumen || bHasFirstPersonSelfShadow || MegaLights::IsUsingClosestHZB(ViewFamily));
		}
	}

	// Commit all the pipeline states.
	{
		for (int32 ViewIndex = 0; ViewIndex < AllViews.Num(); ViewIndex++)
		{
			const FViewInfo& View = *AllViews[ViewIndex];

			GetViewPipelineStateWritable(View).Commit();
		}
		FamilyPipelineState.Commit();
	}
}

void FDeferredShadingSceneRenderer::RenderNanite(FRDGBuilder& GraphBuilder, const TArray<FViewInfo>& InViews, FSceneTextures& SceneTextures, bool bIsEarlyDepthComplete,
	FNaniteBasePassVisibility& InNaniteBasePassVisibility,
	TArray<Nanite::FRasterResults, TInlineAllocator<2>>& NaniteRasterResults,
	TArray<Nanite::FPackedView, SceneRenderingAllocator>& PrimaryNaniteViews,
	FRDGTextureRef FirstStageDepthBuffer)
{
	LLM_SCOPE_BYTAG(Nanite);
	TRACE_CPUPROFILER_EVENT_SCOPE(InitNaniteRaster);

	NaniteRasterResults.AddDefaulted(InViews.Num());
	if (InNaniteBasePassVisibility.Query != nullptr)
	{
		// For now we'll share the same visibility results across all views
		for (int32 ViewIndex = 0; ViewIndex < NaniteRasterResults.Num(); ++ViewIndex)
		{
			NaniteRasterResults[ViewIndex].VisibilityQuery = InNaniteBasePassVisibility.Query;
		}

#if STATS
		// Launch a setup task that will process stats when the visibility task completes.
		GraphBuilder.AddSetupTask([Query = InNaniteBasePassVisibility.Query]
		{
			const FNaniteVisibilityResults* VisibilityResults = Nanite::GetVisibilityResults(Query);

			uint32 TotalRasterBins = 0;
			uint32 VisibleRasterBins = 0;
			VisibilityResults->GetRasterBinStats(VisibleRasterBins, TotalRasterBins);

			uint32 TotalShadingBins = 0;
			uint32 VisibleShadingBins = 0;
			VisibilityResults->GetShadingBinStats(VisibleShadingBins, TotalShadingBins);

			SET_DWORD_STAT(STAT_NaniteBasePassTotalRasterBins, TotalRasterBins);
			SET_DWORD_STAT(STAT_NaniteBasePassVisibleRasterBins, VisibleRasterBins);

			SET_DWORD_STAT(STAT_NaniteBasePassTotalShadingBins, TotalShadingBins);
			SET_DWORD_STAT(STAT_NaniteBasePassVisibleShadingBins, VisibleShadingBins);

		}, Nanite::GetVisibilityTask(InNaniteBasePassVisibility.Query));
#endif
	}

	const FIntPoint RasterTextureSize = SceneTextures.Depth.Target->Desc.Extent;

	// Primary raster view
	{
		Nanite::FSharedContext SharedContext{};
		SharedContext.FeatureLevel = Scene->GetFeatureLevel();
		SharedContext.ShaderMap = GetGlobalShaderMap(SharedContext.FeatureLevel);
		SharedContext.Pipeline = Nanite::EPipeline::Primary;

		FIntRect RasterTextureRect(0, 0, RasterTextureSize.X, RasterTextureSize.Y);
		if (InViews.Num() == 1)
		{
			const FViewInfo& View = InViews[0];
			if (View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0)
			{
				RasterTextureRect = View.ViewRect;
			}
		}

		Nanite::FRasterContext RasterContext;

		// Nanite::VisBuffer (Visibility Buffer Clear)
		{
			const FNaniteVisualizationData& VisualizationData = GetNaniteVisualizationData();

			bool bVisualizeActive = VisualizationData.IsActive() && ViewFamily.EngineShowFlags.VisualizeNanite;
			bool bVisualizeOverdraw = false;
			if (bVisualizeActive)
			{
				if (VisualizationData.GetActiveModeID() == 0) // Overview
				{
					bVisualizeOverdraw = VisualizationData.GetOverviewModeIDs().Contains(NANITE_VISUALIZE_OVERDRAW);
				}
				else
				{
					bVisualizeOverdraw = (VisualizationData.GetActiveModeID() == NANITE_VISUALIZE_OVERDRAW);
				}
			}

			RDG_EVENT_SCOPE_STAT(GraphBuilder, NaniteVisBuffer, "Nanite::VisBuffer");
			RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteVisBuffer);

			RasterContext = Nanite::InitRasterContext(
				GraphBuilder,
				SharedContext,
				ViewFamily,
				RasterTextureSize,
				RasterTextureRect,
				Nanite::EOutputBufferMode::VisBuffer,
				true, // bClearTarget
				true, // bAsyncCompute
				nullptr, 0, // Rect buffers
				nullptr, // ExternalDepthBuffer
				false, // bCustomPass
				bVisualizeActive,
				bVisualizeOverdraw
			);
		}

		Nanite::FConfiguration CullingConfig = { 0 };
		CullingConfig.bTwoPassOcclusion = true;
		CullingConfig.bUpdateStreaming = true;
		CullingConfig.bPrimaryContext = true;

		static FString EmptyFilterName = TEXT(""); // Empty filter represents primary view.
		CullingConfig.bExtractStats = Nanite::IsStatFilterActive(EmptyFilterName);

		const bool bDrawSceneViewsInOneNanitePass = InViews.Num() > 1 && Nanite::ShouldDrawSceneViewsInOneNanitePass(InViews[0]);

		// creates one or more Nanite views (normally one per view unless drawing multiple views together - e.g. Stereo ISR views)
		auto CreateNaniteViews = [bDrawSceneViewsInOneNanitePass, &InViews, &PrimaryNaniteViews, &GraphBuilder](const FViewInfo& View, int32 ViewIndex, const FIntPoint& RasterTextureSize, float MaxPixelsPerEdgeMultipler, TArray<FConvexVolume> &OutViewsCullingVolumes) -> Nanite::FPackedViewArray*
		{
			Nanite::FPackedViewArray::ArrayType OutViews;

			// always add the primary view. In case of bDrawSceneViewsInOneNanitePass HZB is built from all views so using viewrects
			// to account for a rare case when the primary view doesn't start from 0, 0 (maybe can happen in splitscreen?)
			FIntRect HZBTestRect = bDrawSceneViewsInOneNanitePass ?
				View.PrevViewInfo.ViewRect :
				FIntRect(0, 0, View.PrevViewInfo.ViewRect.Width(), View.PrevViewInfo.ViewRect.Height());

			Nanite::FPackedView PackedView = Nanite::CreatePackedViewFromViewInfo(
				View,
				RasterTextureSize,
				NANITE_VIEW_FLAG_HZBTEST | NANITE_VIEW_FLAG_NEAR_CLIP,
				/* StreamingPriorityCategory = */ 3,
				/* MinBoundsRadius = */ 0.0f,
				MaxPixelsPerEdgeMultipler,
				&HZBTestRect
			);
			OutViewsCullingVolumes.Add(View.ViewFrustum);
			OutViews.Add(PackedView);
			PrimaryNaniteViews.Add(PackedView);

			if (bDrawSceneViewsInOneNanitePass)
			{
				// All other views in the family will need to be rendered in one go, to cover both ISR and (later) split-screen
				for (int32 ViewIdx = 1, NumViews = InViews.Num(); ViewIdx < NumViews; ++ViewIdx)
				{
					const FViewInfo& SecondaryViewInfo = InViews[ViewIdx];

					/* viewport rect in HZB space. For instanced stereo passes HZB is built for all atlased views */
					FIntRect SecondaryHZBTestRect = SecondaryViewInfo.PrevViewInfo.ViewRect;
					Nanite::FPackedView SecondaryPackedView = Nanite::CreatePackedViewFromViewInfo(
						SecondaryViewInfo,
						RasterTextureSize,
						NANITE_VIEW_FLAG_HZBTEST | NANITE_VIEW_FLAG_NEAR_CLIP,
						/* StreamingPriorityCategory = */ 3,
						/* MinBoundsRadius = */ 0.0f,
						MaxPixelsPerEdgeMultipler,
						&SecondaryHZBTestRect
					);
					OutViewsCullingVolumes.Add(SecondaryViewInfo.ViewFrustum);
					OutViews.Add(SecondaryPackedView);
					PrimaryNaniteViews.Add(SecondaryPackedView);
				}
			}

			return Nanite::FPackedViewArray::Create(GraphBuilder, OutViews.Num(), MoveTemp(OutViews));
		};

		// in case of bDrawSceneViewsInOneNanitePass we only need one iteration
		uint32 ViewsToRender = (bDrawSceneViewsInOneNanitePass ? 1u : (uint32)InViews.Num());
		for (uint32 ViewIndex = 0; ViewIndex < ViewsToRender; ++ViewIndex)
		{
			Nanite::FRasterResults& RasterResults = NaniteRasterResults[ViewIndex];
			const FViewInfo& View = InViews[ViewIndex];
			// We don't check View.ShouldRenderView() since this is already taken care of by bDrawSceneViewsInOneNanitePass.
			// If bDrawSceneViewsInOneNanitePass is false, we need to render the secondary view even if ShouldRenderView() is false
			// NOTE: Except when there are no primitives to draw for the view
			if (View.bHasNoVisiblePrimitive)
			{
				continue;
			}
			
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, InViews.Num() > 1 && !bDrawSceneViewsInOneNanitePass, "View%u", ViewIndex);
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, InViews.Num() > 1 && bDrawSceneViewsInOneNanitePass, "View%u (together with %d more)", ViewIndex, InViews.Num() - 1);

			FIntRect ViewRect = bDrawSceneViewsInOneNanitePass ? FIntRect(0, 0, FamilySize.X, FamilySize.Y) : View.ViewRect;
			CullingConfig.SetViewFlags(View);

			float LODScaleFactor = 1.0f;
			if (View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale &&
				CVarNaniteViewMeshLODBiasEnable.GetValueOnRenderThread() != 0)
			{
				float TemporalUpscaleFactor = float(View.GetSecondaryViewRectSize().X) / float(ViewRect.Width());

				LODScaleFactor = TemporalUpscaleFactor * FMath::Exp2(-CVarNaniteViewMeshLODBiasOffset.GetValueOnRenderThread());
				LODScaleFactor = FMath::Min(LODScaleFactor, FMath::Exp2(-CVarNaniteViewMeshLODBiasMin.GetValueOnRenderThread()));
			}

			float MaxPixelsPerEdgeMultipler = 1.0f / LODScaleFactor;

			float QualityScale = Nanite::GStreamingManager.GetQualityScaleFactor();
			if (GDynamicNaniteScalingPrimary.GetSettings().IsEnabled())
			{
				QualityScale = FMath::Min(QualityScale, DynamicResolutionFractions[GDynamicNaniteScalingPrimary]);
			}
			MaxPixelsPerEdgeMultipler /= QualityScale;

			TArray<FConvexVolume> ViewsToRenderCullingVolumes;
			Nanite::FPackedViewArray* NaniteViewsToRender = CreateNaniteViews(View, ViewIndex, RasterTextureSize, MaxPixelsPerEdgeMultipler, ViewsToRenderCullingVolumes);

			TUniquePtr< Nanite::IRenderer > NaniteRenderer;

			// Nanite::VisBuffer (Culling and Rasterization)
			{
				DynamicRenderScaling::FRDGScope DynamicScalingScope(GraphBuilder, GDynamicNaniteScalingPrimary);

				RDG_EVENT_SCOPE_STAT(GraphBuilder, NaniteVisBuffer, "Nanite::VisBuffer");
				RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteVisBuffer);

				NaniteRenderer = Nanite::IRenderer::Create(
					GraphBuilder,
					*Scene,
					View,
					GetSceneUniforms(),
					SharedContext,
					RasterContext,
					CullingConfig,
					ViewRect,
					!bIsEarlyDepthComplete ? View.PrevViewInfo.NaniteHZB : View.PrevViewInfo.HZB
				);

				FSceneInstanceCullingQuery *SceneInstanceCullQuery = GetSceneExtensionsRenderers().GetRenderer<FSceneCullingRenderer>().CullInstances(GraphBuilder, ViewsToRenderCullingVolumes);
				NaniteRenderer->DrawGeometry(
					Scene->NaniteRasterPipelines[ENaniteMeshPass::BasePass],
					RasterResults.VisibilityQuery,
					*NaniteViewsToRender,
					SceneInstanceCullQuery
				);

				NaniteRenderer->ExtractResults( RasterResults );
			}

			// Nanite::BasePass (Depth Pre-Pass and HZB Build)
			{
				RDG_EVENT_SCOPE_STAT(GraphBuilder, NaniteBasePass, "NaniteBasePass");
				RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteBasePass);

				// Emit velocity with depth if not writing it in base pass.
				FRDGTexture* VelocityBuffer = !IsUsingBasePassVelocity(ShaderPlatform) ? SceneTextures.Velocity : nullptr;

				Nanite::EmitDepthTargets(
					GraphBuilder,
					*Scene,
					InViews[ViewIndex],
					bDrawSceneViewsInOneNanitePass,
					RasterResults,
					SceneTextures.Depth.Target,
					VelocityBuffer,
					FirstStageDepthBuffer
				);
				
				// Sanity check (always force Z prepass)
				check(bIsEarlyDepthComplete);
			}
		}
	}
}

#if RHI_RAYTRACING
extern void RenderRayTracingDebug(FRDGBuilder& GraphBuilder, const FScene& Scene, const FViewInfo& View, FSceneTextures& SceneTextures, FRayTracingPickingFeedback& PickingFeedback);
extern void RayTracingDebugDisplayOnScreenMessages(FScreenMessageWriter& Writer, const FViewInfo& View);
#endif // RHI_RAYTRACING
//

void FDeferredShadingSceneRenderer::Render(FRDGBuilder& GraphBuilder, const FSceneRenderUpdateInputs* SceneUpdateInputs)
{
	// 第一步：检查渲染开关——如果引擎显示标志中禁用了“Rendering”，直接返回，不执行任何渲染
	if (!ViewFamily.EngineShowFlags.Rendering)
	{
		return;
	}
	// 第二步：进入核心渲染逻辑块（后续所有渲染步骤都在这个代码块内执行）
	{
		// 获取光线追踪可视化数据（为RT调试/可视化做准备）
		FRayTracingVisualizationData& RayTracingVisualizationData = GetRayTracingVisualizationData();
		
		if (RayTracingVisualizationData.HasOverrides())// 判断RT可视化数据是否存在“自定义覆盖配置”（开发者手动设置的调试参数）
		{
			// When activating the view modes from the command line, automatically enable the RayTracingDebug show flag for convenience.
			// 注释：当通过命令行激活RT视图模式时，自动启用RayTracingDebug显示标志，提升调试便利性
			ViewFamily.EngineShowFlags.SetRayTracingDebug(true);
		}
	}

	// If this is scene capture rendering depth pre-pass, we'll take the shortcut function RenderSceneCaptureDepth if optimization switch is on.
	// 注释：如果当前是“场景捕获”的深度预渲染，且优化开关开启，会调用快捷函数RenderSceneCaptureDepth
	const ERendererOutput RendererOutput = GetRendererOutput();// 获取当前渲染器的输出目标（决定渲染路径分支）

	const bool bNaniteEnabled = ShouldRenderNanite();// 判定当前是否启用Nanite（UE的自适应细分网格技术）
	const bool bHasRayTracedOverlay = HasRayTracedOverlay(ViewFamily);// 判定当前视口家族是否包含光线追踪叠加层（RT Overlay，如RT调试信息、RT选中高亮等）

#if !UE_BUILD_SHIPPING
	// 创建渲染捕获作用域对象：用于捕获“延迟渲染器单帧”的渲染数据（DrawCall、资源、耗时等）
	RenderCaptureInterface::FScopedCapture RenderCapture(GCaptureNextDeferredShadingRendererFrame-- == 0, // 捕获启用条件：全局变量递减后等于0
		GraphBuilder, // 渲染图构建器（关联捕获的渲染上下文）
		TEXT("DeferredShadingSceneRenderer"));// 捕获事件名
	// Prevent overflow every 2B frames.
	//注释：每2B帧防止数值溢出（避免全局变量无限递减）
	GCaptureNextDeferredShadingRendererFrame = FMath::Max(-1, GCaptureNextDeferredShadingRendererFrame);// 将全局捕获计数变量限制为 ≥-1，防止溢出为超大正数
#endif

	GPU_MESSAGE_SCOPE(GraphBuilder);//用于 GPU 调试 / 性能分析的核心宏,为 GPU 渲染阶段添加 “标记 / 作用域”，方便开发者定位 GPU 性能瓶颈、追踪渲染流程
	
	// 编译宏：仅当引擎启用硬件光追（RHI_RAYTRACING）时编译这段代码
#if RHI_RAYTRACING
	// 条件判定：场景更新输入有效 + 当前渲染输出是“最终场景颜色”（常规渲染，非深度/阴影捕获）
	if (SceneUpdateInputs && RendererOutput == FSceneRenderer::ERendererOutput::FinalSceneColor)
	{
		// 第一步：RT几何管理器预渲染准备（初始化几何数据上下文、清理上一帧残留）
		GRayTracingGeometryManager->PreRender();

		// TODO: should only process build requests once per frame
		// 注释：TODO优化点——每帧只处理一次构建请求（当前可能重复处理，待优化）
		// 第二步：添加GPU调试标记（面包屑事件），用于追踪RT几何构建的GPU耗时/流程
		RHI_BREADCRUMB_EVENT_STAT(GraphBuilder.RHICmdList, RayTracingGeometry, "RayTracingGeometry");
		// 第三步：添加GPU性能统计标记，归类RT几何构建的性能数据
		SCOPED_GPU_STAT(GraphBuilder.RHICmdList, RayTracingGeometry);
		
		// 第四步：处理RT几何的构建请求（核心操作：更新TLAS/BLAS、实例数据、材质关联）
		GRayTracingGeometryManager->ProcessBuildRequests(GraphBuilder.RHICmdList);
	}
#endif
	
//“全维度资源初始化” 阶段，位于 RT 几何构建之后、深度预渲染之前，负责视口任务、虚拟纹理、Lumen、GPU 场景等核心模块的初始化，
	// 第一步：初始化视口任务数据——渲染开始前的核心上下文准备
	FInitViewTaskDatas InitViewTaskDatas = OnRenderBegin(GraphBuilder, SceneUpdateInputs);
	// 第二步：声明/初始化渲染核心辅助资源
	// 1. RDG外部访问队列：管理渲染图（RDG）与外部模块的资源访问同步
	FRDGExternalAccessQueue ExternalAccessQueue;
	// 2. 虚拟纹理更新器：负责虚拟纹理（VT）的流式加载、更新、解压
	TUniquePtr<FVirtualTextureUpdater> VirtualTextureUpdater;
	// 3. Lumen帧临时资源：为Lumen全局光照（GI）准备每帧临时数据（如光线追踪缓存、光照探针）
	FLumenSceneFrameTemporaries LumenFrameTemporaries(Views);
	// 第三步：GPU场景作用域初始化——管理GPU场景的生命周期与资源绑定
	FGPUSceneScopeBeginEndHelper GPUSceneScopeBeginEndHelper(GraphBuilder, Scene->GPUScene, GPUSceneDynamicContext);
	// 第四步：判定当前平台是否启用虚拟纹理（VT）
	const bool bUseVirtualTexturing = UseVirtualTexturing(ShaderPlatform);

	// Virtual texturing isn't needed for depth prepass
	// 注释：深度预渲染不需要虚拟纹理（仅渲染深度，不采样纹理颜色/法线）
	//虚拟纹理（VT）的专项初始化逻辑
	if (bUseVirtualTexturing && RendererOutput != ERendererOutput::DepthPrepassOnly)
	{
		FVirtualTextureUpdateSettings Settings;
		Settings.EnableThrottling(!ViewFamily.bOverrideVirtualTextureThrottle);

		VirtualTextureUpdater = FVirtualTextureSystem::Get().BeginUpdate(GraphBuilder, FeatureLevel, this, Settings);
		VirtualTextureFeedbackBegin(GraphBuilder, Views, GetActiveSceneTexturesConfig().Extent);
	}

	if (SceneUpdateInputs)
	{
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CommitFinalPipelineState);
			//在 UE 内置的 CPU Profiler（CPU 性能分析工具）中添加一个名为CommitFinalPipelineState的 “事件作用域”
			//用于：记录该代码块的 CPU 耗时（如每帧耗时多少毫秒）；定位性能瓶颈（如管线状态提交耗时过长，可能是着色器组合过多导致）；生成性能报告，方便开发者优化渲染线程开销。
			for (FSceneRenderer* Renderer : SceneUpdateInputs->Renderers)
			{
				// Compute & commit the final state of the entire dependency topology of the renderer.
				// 注释：计算并提交渲染器整个依赖拓扑的最终状态
				static_cast<FDeferredShadingSceneRenderer*>(Renderer)->CommitFinalPipelineState();
			}
		}

		// Initialize global system textures (pass-through if already initialized).
		// 注释：初始化全局系统纹理（如果已初始化则直接跳过）
		GSystemTextures.InitializeTextures(GraphBuilder.RHICmdList, FeatureLevel);
	}

	UE::Tasks::TTask<void> UpdateLightFunctionAtlasTask;//光照函数图集的异步更新
	if (LightFunctionAtlas.IsLightFunctionAtlasEnabled())
	{
		UpdateLightFunctionAtlasTask = LaunchSceneRenderTask<void>(TEXT("UpdateLightFunctionAtlas"), [this]
			{
				UpdateLightFunctionAtlasTaskFunction();
			}, UE::Tasks::FTask());
	}
	//光照函数图集（Light Function Atlas）：将场景中所有光源的光照函数纹理合并到一张大纹理（图集）中，而非每个光源单独绑定一张纹理 —— 核心价值是减少 GPU 纹理采样次数、降低绘制调用（DrawCall），是 UE 优化多动态光源的关键手段；

	FShadowSceneRenderer& ShadowSceneRenderer = GetSceneExtensionsRenderers().GetRenderer<FShadowSceneRenderer>();//天空大气与太阳光参数预处理
	{
		if (RendererOutput == ERendererOutput::FinalSceneColor)
		{
			// 1. Update sky atmosphere
			// This needs to be done prior to start Lumen scene lighting to ensure directional light color is correct, as the sun color needs atmosphere transmittance
			//必须在 Lumen 场景光照开始前完成，因为太阳光颜色依赖大气传输（Atmosphere Transmittance）计算，只有大气参数更新后，Lumen 才能采样到正确的太阳光颜色。
			{
				//判定当前是否启用路径追踪大气
				const bool bPathTracedAtmosphere = ViewFamily.EngineShowFlags.PathTracing && Views.Num() > 0 && PathTracing::UsesReferenceAtmosphere(Views[0]);
				if (ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags) && !bPathTracedAtmosphere)
				{
					for (int32 LightIndex = 0; LightIndex < NUM_ATMOSPHERE_LIGHTS; ++LightIndex)
					{
						if (Scene->AtmosphereLights[LightIndex])
						{
							PrepareSunLightProxy(*Scene->GetSkyAtmosphereSceneInfo(),LightIndex, *Scene->AtmosphereLights[LightIndex]);
						}
					}
				}
				else
				{
					Scene->ResetAtmosphereLightsProperties();
				}
			}

			// 2. Update lumen scene
			//Lumen 全局光照系统的核心初始化与任务启动阶段
			{
				InitViewTaskDatas.LumenFrameTemporaries = &LumenFrameTemporaries;
				//将前面初始化的 LumenFrameTemporaries（Lumen 每帧临时资源）赋值给视口任务数据 InitViewTaskDatas。
				//让整个 Lumen 渲染流程（包括后续的阴影、光照收集、可见性计算）都能共享同一套临时资源（如临时渲染目标、缓存缓冲区），避免资源重复分配与冲突。
	
				// Important that this uses consistent logic throughout the frame, so evaluate once and pass in the flag from here
				// NOTE: Must be done after  system texture initialization
				// TODO: This doesn't take into account the potential for split screen views with separate shadow caches
				//是否启用 UE 5 的核心阴影优化技术 ——虚拟阴影贴图（VSM）
				const bool bEnableVirtualShadowMaps = UseVirtualShadowMaps(ShaderPlatform, FeatureLevel) //全局开关：必须满足平台支持、项目启用 VSM
				&& ViewFamily.EngineShowFlags.DynamicShadows //避免在禁用阴影的场景中初始化 VSM，节省显存
				&& !bHasRayTracedOverlay;//当渲染 RT 叠加层（如调试视图、选中高亮）时禁用VSM，因为 RT 叠加层需要精确的传统阴影数据，VSM 精度不足易出错
				VirtualShadowMapArray.Initialize(GraphBuilder, Scene->GetVirtualShadowMapCache(), bEnableVirtualShadowMaps, ViewFamily.EngineShowFlags);
	
				if (InitViewTaskDatas.LumenFrameTemporaries)
				{
					BeginUpdateLumenSceneTasks(GraphBuilder, *InitViewTaskDatas.LumenFrameTemporaries);//任务分发函数，负责将 Lumen 的复杂计算（如场景距离场更新、法线烘焙、光照探测等）分发到渲染线程或Render Workers中并行执行。
				}
	
				BeginGatherLumenLights(
					*InitViewTaskDatas.LumenFrameTemporaries, 
					InitViewTaskDatas.LumenDirectLighting, //输出 Lumen 直接光照的缓存数据；
					InitViewTaskDatas.VisibilityTaskData, //依赖前面的可见性任务（如视锥剔除、遮挡查询），确保只处理可见光源
					UpdateLightFunctionAtlasTask//任务依赖（Prerequisite） —— 明确规定：必须等到光照函数图集（Light Function Atlas）更新任务完成后，才能收集光源。
					);
			}
		}

		//Nanite部分
		if (bNaniteEnabled)
		{
			TArray<FConvexVolume, TInlineAllocator<2>> NaniteCullingViews;//存储用于剔除的视锥体（View Frustum）数组

			// For now we'll share the same visibility results across all views
			// 注释：目前我们会在所有视图间共享相同的可见性结果
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				FViewInfo& View = Views[ViewIndex];
				NaniteCullingViews.Add(View.ViewFrustum);
			}

			FNaniteVisibility& NaniteVisibility = Scene->NaniteVisibility[ENaniteMeshPass::BasePass];
			const FNaniteRasterPipelines&  NaniteRasterPipelines  = Scene->NaniteRasterPipelines[ENaniteMeshPass::BasePass];
			const FNaniteShadingPipelines& NaniteShadingPipelines = Scene->NaniteShadingPipelines[ENaniteMeshPass::BasePass];

			NaniteVisibility.BeginVisibilityFrame();

			NaniteBasePassVisibility.Visibility = &NaniteVisibility;
			NaniteBasePassVisibility.Query = NaniteVisibility.BeginVisibilityQuery(
				Allocator,
				*Scene,
				NaniteCullingViews,
				&NaniteRasterPipelines,
				&NaniteShadingPipelines,
				InitViewTaskDatas.VisibilityTaskData->GetComputeRelevanceTask()
			);
		}
	}
	ShaderPrint::BeginViews(GraphBuilder, Views);

	//作用域清理与场景扩展预初始化
	ON_SCOPE_EXIT
	{
		ShaderPrint::EndViews(Views);
	};

	GetSceneExtensionsRenderers().PreInitViews(GraphBuilder);

	//场景距离场准备（仅最终场景颜色渲染）
	if (RendererOutput == ERendererOutput::FinalSceneColor)
	{
		if (SceneUpdateInputs)
		{
			//场景距离场准备函数
			//核心作用：
			//更新场景全局有符号距离场（SDF），为 Lumen 全局光照、动态阴影、体积光等提供高效的遮挡查询数据；
			//处理场景中动态物体的距离场更新，保证 SDF 与当前场景状态一致；
			//将距离场资源上传到 GPU，并通过 ExternalAccessQueue 管理与外部模块的资源同步，避免访问冲突。
			PrepareDistanceFieldScene(GraphBuilder, ExternalAccessQueue, *SceneUpdateInputs);
		}

		//多视口视图级初始化循环
		//遍历所有视口（如分屏、多视角渲染），为每个视口单独初始化视图级资源，保证多视角渲染的正确性
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];
			//GPU 掩码作用域
			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
			
			//着色能量守恒初始化
			ShadingEnergyConservation::Init(GraphBuilder, View);
			//ShadingEnergyConservation：着色能量守恒系统，用于保证材质光照计算符合物理能量守恒定律（避免能量过溢 / 不足导致的画面过亮 / 过暗）
			//Init：为当前视口初始化能量守恒相关的着色器参数、常量缓冲区，确保后续 Base Pass、光照计算时的材质着色符合物理规律。
			
			//闪光着色 LUT 状态初始化
			FGlintShadingLUTsStateData::Init(GraphBuilder, View);
			//FGlintShadingLUTsStateData：UE 的闪光着色 LUT（查找表）状态数据，用于实现材质的微表面闪光 / 各向异性效果（如金属、丝绸、宝石的高光细节）；
			//Init：为当前视口初始化闪光着色所需的 LUT 资源（如 BRDF 积分图、微表面分布函数表），将 LUT 上传到 GPU 并绑定到着色器，保证闪光效果的正确渲染。
		}

//硬件光线追踪（RHI_RAYTRACING）的实例收集核心逻辑，为后续 RT 光照（Lumen RT、路径追踪）、RT 阴影等提供实例化几何数据
#if RHI_RAYTRACING
		if (FamilyPipelineState[&FFamilyPipelineState::bRayTracing])
		{
			const int32 ReferenceViewIndex = 0;
			FViewInfo& ReferenceView = Views[ReferenceViewIndex];

			InitViewTaskDatas.RayTracingGatherInstances = RayTracing::CreateGatherInstancesTaskData(
				Allocator,// 内存分配器：用于分配临时实例数据
				*Scene,// 场景指针：获取场景中所有几何实例
				ReferenceView,// 引用视口：获取视口参数（如视锥、LOD设置）
				ViewFamily,// 视口家族：获取多视口共享配置
				GetViewPipelineState(ReferenceView).DiffuseIndirectMethod,// 漫反射间接光方法（决定RT实例类型）
				GetViewPipelineState(ReferenceView).ReflectionsMethod);// 反射方法（决定RT实例类型）

			RayTracing::BeginGatherInstances(GraphBuilder, *InitViewTaskDatas.RayTracingGatherInstances, InitViewTaskDatas.VisibilityTaskData->GetFrustumCullTask());
		}
#endif
	}

	//虚拟纹理（SVT）异步流送启动
	UE::SVT::GetStreamingManager().BeginAsyncUpdate(GraphBuilder);
	//Nanite 流送与可视化配置（仅 Nanite 启用时）
	bool bUpdateNaniteStreaming = false;
	bool bVisualizeNanite = false;
	if (bNaniteEnabled)
	{
		if (SceneUpdateInputs)
		{
			Nanite::GGlobalResources.Update(GraphBuilder);
			Nanite::GStreamingManager.BeginAsyncUpdate(GraphBuilder);
			bUpdateNaniteStreaming = true;
		}

		FNaniteVisualizationData& NaniteVisualization = GetNaniteVisualizationData();
		if (Views.Num() > 0)
		{
			const FName& NaniteViewMode = Views[0].CurrentNaniteVisualizationMode;
			if (NaniteVisualization.Update(NaniteViewMode))
			{
				// When activating the view modes from the command line, automatically enable the VisualizeNanite show flag for convenience.
				ViewFamily.EngineShowFlags.SetVisualizeNanite(true);
			}

			bVisualizeNanite = NaniteVisualization.IsActive() && ViewFamily.EngineShowFlags.VisualizeNanite;
		}
	}
	
	// CSV 计时统计（CPU 侧）
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderOther);
	
	//命名事件作用域（GPU/CPU 侧）
	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_Render, FColor::Emerald);

//多 GPU（MGPU）初始化
#if WITH_MGPU
	ComputeGPUMasks(&GraphBuilder.RHICmdList);
#endif // WITH_MGPU

	// By default, limit our GPU usage to only GPUs specified in the view masks.
	//GPU 掩码与事件 Scope 配置（调度与调试）
	RDG_GPU_MASK_SCOPE(GraphBuilder, ViewFamily.EngineShowFlags.PathTracing ? FRHIGPUMask::All() : AllViewsGPUMask);
	RDG_EVENT_SCOPE(GraphBuilder, "Scene");
	FString FrameNumDescription = FString::Printf(TEXT("%s Frame: %d"), *ViewFamily.ProfileDescription, GFrameCounterRenderThread);
	RDG_GPU_STAT_SCOPE_VERBOSE(GraphBuilder, Unaccounted, *FrameNumDescription);
	
	//渲染目标分配与性能统计（仅最终场景颜色）
	if (RendererOutput == ERendererOutput::FinalSceneColor)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_Render_Init);
		RDG_RHI_GPU_STAT_SCOPE(GraphBuilder, AllocateRendertargets);
		
		//强制更新核心光照 / 材质纹理（最后的资源锁定）
		// Force the subsurface profiles and specular profiles textures to be updated.
		
		SubsurfaceProfile::UpdateSubsurfaceProfileTexture(GraphBuilder, ShaderPlatform);
		//更新次表面散射轮廓纹理。次表面散射（如皮肤、蜡质材质）依赖于一组预计算的 Profile 纹理。此函数强制将最新的 Profile 数据上传到 GPU 纹理。
		SpecularProfile::UpdateSpecularProfileTextureAtlas(GraphBuilder, ShaderPlatform);
		//更新镜面反射轮廓的图集纹理。将所有材质的镜面反射相关数据打包（Atlas），保证后续光照计算能采样到正确的高光分布。
		

		// Force the rect light texture & IES texture to be updated.
		RectLightAtlas::UpdateAtlasTexture(GraphBuilder, FeatureLevel);// 强制更新矩形光图集纹理
		IESAtlas::UpdateAtlasTexture(GraphBuilder, ShaderPlatform);// 强制更新 IES 纹理图集
	}
	
	//场景纹理与系统纹理配置
	FSceneTexturesConfig& SceneTexturesConfig = GetActiveSceneTexturesConfig();
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Create(GraphBuilder);
	
	//静态光照允许判定
	//仅当无 RT 叠加层且项目允许静态光照时，才启用静态光照，保证光照计算的正确性与兼容性
	const bool bAllowStaticLighting = !bHasRayTracedOverlay && IsStaticLightingAllowed();
	
	//早期深度预渲染完成判定
	// if DDM_AllOpaqueNoVelocity was used, then velocity should have already been rendered as well
	// 注释：如果使用了DDM_AllOpaqueNoVelocity，那么运动矢量也应该已经渲染完成
	const bool bIsEarlyDepthComplete = (DepthPass.EarlyZPassMode == DDM_AllOpaque || DepthPass.EarlyZPassMode == DDM_AllOpaqueNoVelocity);

	// Use read-only depth in the base pass if we have a full depth prepass.
	// 注释：如果我们有完整的深度预渲染，在Base Pass中使用只读深度
	const bool bAllowReadOnlyDepthBasePass = bIsEarlyDepthComplete
		&& !ViewFamily.EngineShowFlags.ShaderComplexity//未启用 Shader 复杂度调试视图；
		&& !ViewFamily.UseDebugViewPS()//未启用像素着色器调试视图；
		&& !ViewFamily.EngineShowFlags.Wireframe//未启用线框模式；
		&& !ViewFamily.EngineShowFlags.LightMapDensity;//未启用光照贴图密度视图。
	
	//Base Pass 深度 / 模板访问权限配置
	const FExclusiveDepthStencil::Type BasePassDepthStencilAccess =
		bAllowReadOnlyDepthBasePass
		? FExclusiveDepthStencil::DepthRead_StencilWrite
		: FExclusiveDepthStencil::DepthWrite_StencilWrite;
	
	//管理器对象分配（RDG 生命周期管理）
	FRendererViewDataManager& ViewDataManager = *GraphBuilder.AllocObject<FRendererViewDataManager>(GraphBuilder, *Scene, GetSceneUniforms(), AllViews);
	// 实例剔除管理器
	FInstanceCullingManager& InstanceCullingManager = *GraphBuilder.AllocObject<FInstanceCullingManager>(GraphBuilder, *Scene, GetSceneUniforms(), ViewDataManager);
	
	//Substrate 材质系统预初始化
	::Substrate::PreInitViews(*Scene);
	
	//场景纹理视图家族初始化
	FSceneTextures::InitializeViewFamily(GraphBuilder, ViewFamily, FamilySize);
	FSceneTextures& SceneTextures = GetActiveSceneTextures();
	//可见性命令初始化（核心性能统计与任务启动）
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, VisibilityCommands, "VisibilityCommands");
		RDG_GPU_STAT_SCOPE(GraphBuilder, VisibilityCommands);
		
		//视图初始化入口
		BeginInitViews(
			GraphBuilder, 
			SceneTexturesConfig, //场景纹理配置，传递渲染目标信息；
			InstanceCullingManager, //实例剔除管理器，执行可见性计算；
			ExternalAccessQueue, //外部资源访问队列，处理跨模块资源依赖；
			InitViewTaskDatas);//视图任务数据，传递任务依赖与状态。
		
		//BeginInitViews 函数的主要职责
		//视锥体剔除 (Frustum Culling)
		//遮挡剔除准备 (Occlusion Culling Preparation)
		// 实例剔除 (Instance Culling)
		//渲染状态初始化
		//动态网格元素收集
	}

#if !UE_BUILD_SHIPPING
	if (CVarStallInitViews.GetValueOnRenderThread() > 0.0f)
	{
		SCOPE_CYCLE_COUNTER(STAT_InitViews_Intentional_Stall);
		FPlatformProcess::Sleep(CVarStallInitViews.GetValueOnRenderThread() / 1000.0f);
	}
#endif

	extern TSet<IPersistentViewUniformBufferExtension*> PersistentViewUniformBufferExtensions;

	for (IPersistentViewUniformBufferExtension* Extension : PersistentViewUniformBufferExtensions)
	{
		Extension->BeginFrame();

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			// Must happen before RHI thread flush so any tasks we dispatch here can land in the idle gap during the flush
			Extension->PrepareView(&Views[ViewIndex]);
		}
	}
	
//硬件光线追踪（RHI_RAYTRACING）场景重置与贴花数据准备
#if RHI_RAYTRACING
	const int32 ReferenceViewIndex = 0;
	FViewInfo& ReferenceView = Views[ReferenceViewIndex];
	FRayTracingScene& RayTracingScene = Scene->RayTracingScene;
	FRayTracingShaderBindingTable& RayTracingSBT = Scene->RayTracingSBT;
#endif

	if (RendererOutput == ERendererOutput::FinalSceneColor)
	{
#if RHI_RAYTRACING
		// Prepare the scene for rendering this frame.
		RayTracingScene.Reset(IsRayTracingInstanceDebugDataEnabled(ReferenceView)); // Resets the internal arrays, but does not release any resources.
		
		RayTracingSBT.ResetMissAndCallableShaders();

		if (ViewFamily.EngineShowFlags.PathTracing)
		{
			if (ShouldPrepareRayTracingDecals(*Scene, ViewFamily))
			{
				// Calculate decal grid for ray tracing per view since decal fade is view dependent
				// TODO: investigate reusing the same grid for all views (ie: different callable shader SBT entries for each view so fade alpha is still correct for each view)

				for (FViewInfo& View : Views)
				{
					View.RayTracingDecalUniformBuffer = CreateRayTracingDecalData(GraphBuilder, *Scene, View, RayTracingSBT.NumCallableShaderSlots);
					View.bHasRayTracingDecals = true;
					RayTracingSBT.NumCallableShaderSlots += Scene->Decals.Num();
				}
			}
			else
			{
				TRDGUniformBufferRef<FRayTracingDecals> NullRayTracingDecalUniformBuffer = CreateNullRayTracingDecalsUniformBuffer(GraphBuilder);

				for (FViewInfo& View : Views)
				{
					View.RayTracingDecalUniformBuffer = NullRayTracingDecalUniformBuffer;
					View.bHasRayTracingDecals = false;
				}
			}

			// If we might be path tracing the clouds -- call the path tracer's method for cloud callable shader setup
			// this will skip work if cloud rendering is not being used
			PreparePathTracingCloudMaterial(GraphBuilder, Scene, Views);
		}

		if (IsRayTracingEnabled(ViewFamily.GetShaderPlatform()) && GRHISupportsRayTracingShaders)
		{
			if (!ViewFamily.EngineShowFlags.PathTracing)
			{
				// get the default lighting miss shader (to implicitly fill in the MissShader library before the RT pipeline is created)
				GetRayTracingLightingMissShader(ReferenceView.ShaderMap);
				RayTracingSBT.NumMissShaderSlots++;
			}

			if (ViewFamily.EngineShowFlags.LightFunctions)
			{
				// gather all the light functions that may be used (and also count how many miss shaders we will need)
				FRayTracingLightFunctionMap RayTracingLightFunctionMap;
				if (ViewFamily.EngineShowFlags.PathTracing)
				{
					RayTracingLightFunctionMap = GatherLightFunctionLightsPathTracing(Scene, ViewFamily.EngineShowFlags, ReferenceView.GetFeatureLevel());
				}
				else
				{
					RayTracingLightFunctionMap = GatherLightFunctionLights(Scene, ViewFamily.EngineShowFlags, ReferenceView.GetFeatureLevel());
				}
				if (!RayTracingLightFunctionMap.IsEmpty())
				{
					// If we got some light functions in our map, store them in the RDG blackboard so downstream functions can use them.
					// The map itself will be strictly read-only from this point on.
					GraphBuilder.Blackboard.Create<FRayTracingLightFunctionMap>(MoveTemp(RayTracingLightFunctionMap));
				}
			}
		}
#endif // RHI_RAYTRACING
		
//调试渲染、动态网格可见性收尾、FX 系统预渲染与 GPU 场景更新统计
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		Scene->DebugRender(Views);
#endif
	}

	InitViewTaskDatas.VisibilityTaskData->FinishGatherDynamicMeshElements(BasePassDepthStencilAccess, InstanceCullingManager, VirtualTextureUpdater.Get());

	//FX 系统（粒子系统）的预渲染准备阶段
	// Notify the FX system that the scene is about to be rendered.
	// TODO: These should probably be moved to scene extensions
	if (FXSystem && Views.IsValidIndex(0))
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_FXSystem_PreRender);
		const bool bAllowGPUParticleUpdate = IsHeadLink();
		FXSystem->PreRender(GraphBuilder, GetSceneViews(), GetSceneUniforms(), bAllowGPUParticleUpdate);
		if (FGPUSortManager* GPUSortManager = FXSystem->GetGPUSortManager())
		{
			GPUSortManager->OnPreRender(GraphBuilder);
		}
	}

	{
		RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, UpdateGPUScene);
		RDG_EVENT_SCOPE_STAT(GraphBuilder, GPUSceneUpdate, "GPUSceneUpdate");
		RDG_GPU_STAT_SCOPE(GraphBuilder, GPUSceneUpdate);

		for (int32 ViewIndex = 0; ViewIndex < AllViews.Num(); ViewIndex++)
		{
			FViewInfo& View = *AllViews[ViewIndex];
			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

			Scene->GPUScene.UploadDynamicPrimitiveShaderDataForView(GraphBuilder, View);
			Scene->GPUScene.DebugRender(GraphBuilder, GetSceneUniforms(), View);
		}

		// Must be called after all views have flushed the dynamic primitives.
		ViewDataManager.InitInstanceState(GraphBuilder);

		if (Views.Num() > 0)
		{
			FViewInfo& View = Views[0];
			Scene->UpdatePhysicsField(GraphBuilder, View);
		}
	}


	//如果启用了场景剔除渲染器（通常用于调试），则绘制剔除结果的可视化信息（例如用不同颜色显示被剔除的物体）
	if (FSceneCullingRenderer* SceneCullingRenderer = GetSceneExtensionsRenderers().GetRendererPtr<FSceneCullingRenderer>())
	{
		SceneCullingRenderer->DebugRender(GraphBuilder, Views);
	}

	//更新场景扩展数据
	//SceneCullingInfo.SceneUniformBuffer = GetSceneUniforms().GetBuffer(GraphBuilder);
	GetSceneExtensionsRenderers().UpdateViewData(GraphBuilder, ViewDataManager);

	// Allow scene extensions to affect the scene uniform buffer after GPU scene has fully updated
	GetSceneExtensionsRenderers().UpdateSceneUniformBuffer(GraphBuilder, GetSceneUniforms());

	// 执行延迟实例剔除
	// Must happen after visibility state & scene UB has been updated.
	//这里的 BeginDeferredCulling 通常是在 GPU 上进行的实例剔除（Instance Culling）。
	//它利用刚才更新的 Scene Uniform Buffer 和可见性状态，对大量的实例化物体（如草地、树木、人群）进行更精细的剔除。
	InstanceCullingManager.BeginDeferredCulling(GraphBuilder);

	const bool bUseGBuffer = IsUsingGBuffers(ShaderPlatform);
	const bool bShouldRenderVolumetricFog = ShouldRenderVolumetricFog();
	const bool bShouldRenderLocalFogVolume = ShouldRenderLocalFogVolume(Scene, ViewFamily);
	const bool bShouldRenderLocalFogVolumeDuringHeightFogPass = ShouldRenderLocalFogVolumeDuringHeightFogPass(Scene, ViewFamily);
	const bool bShouldRenderLocalFogVolumeInVolumetricFog = ShouldRenderLocalFogVolumeInVolumetricFog(Scene, ViewFamily, bShouldRenderLocalFogVolume);
	const bool bShouldRenderLocalFogVolumeVisualizationPass = ShouldRenderLocalFogVolumeVisualizationPass(Scene, ViewFamily);
	
	//确定光照渲染路径 (延迟 vs 前向)
	const bool bRenderDeferredLighting = ViewFamily.EngineShowFlags.Lighting//用户开启了光照显示 (Lighting)。
		&& FeatureLevel >= ERHIFeatureLevel::SM5//硬件支持 Shader Model 5.0 及以上
		&& ViewFamily.EngineShowFlags.DeferredLighting//用户明确开启了延迟渲染选项
		&& bUseGBuffer//当前平台支持 G-Buffer
		&& !bHasRayTracedOverlay;//没有启用光线追踪覆盖层

	//Lumen 状态初始化
	bool bAnyLumenEnabled = false;

	// Virtual texturing isn't needed for depth prepass
	if (bUseVirtualTexturing && RendererOutput != ERendererOutput::DepthPrepassOnly)
	{
		// Note, should happen after the GPU-Scene update to ensure rendering to runtime virtual textures is using the correctly updated scene
		FVirtualTextureSystem::Get().EndUpdate(GraphBuilder, MoveTemp(VirtualTextureUpdater), FeatureLevel);
	}

	UE::Tasks::TTask<FSortedLightSetSceneInfo*> GatherAndSortLightsTask;

	if (RendererOutput == ERendererOutput::FinalSceneColor)
	{
#if RHI_RAYTRACING
		if (FamilyPipelineState[&FFamilyPipelineState::bRayTracing])
		{
			RayTracing::FinishGatherInstances(
				GraphBuilder,
				*InitViewTaskDatas.RayTracingGatherInstances,
				RayTracingScene,
				RayTracingSBT,
				DynamicReadBufferForRayTracing,
				Allocator);
		}
#endif // RHI_RAYTRACING

		if (!bHasRayTracedOverlay)
		{
			for (const FViewInfo& View : Views)
			{
				bAnyLumenEnabled = bAnyLumenEnabled
					|| GetViewPipelineState(View).DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen
					|| GetViewPipelineState(View).ReflectionsMethod == EReflectionsMethod::Lumen;
			}
		}

		{//异步收集并排序场景中所有可见光源，为后续阴影渲染、光照计算做准备。
			extern bool IsVSMOnePassProjectionEnabled(const FEngineShowFlags& ShowFlags);//判断是否启用单通道 VSM（Virtual Shadow Maps）投影，用于优化虚拟阴影贴图的渲染效率。
			extern UE::Tasks::FTask GetGatherAndSortLightsPrerequisiteTask(const FDynamicShadowsTaskData* TaskData);//获取光照收集任务的前置依赖任务（如动态阴影数据准备），保证执行顺序正确。

			auto* SortedLightSet = GraphBuilder.AllocObject<FSortedLightSetSceneInfo>();
			const bool bShadowedLightsInClustered = ShouldUseClusteredDeferredShading()
				&& IsVSMOnePassProjectionEnabled(ViewFamily.EngineShowFlags)
				&& VirtualShadowMapArray.IsEnabled();

			TArray<UE::Tasks::FTask, TInlineAllocator<2>> IssuedTasksCompletionEvents;
			IssuedTasksCompletionEvents.Add(GetGatherAndSortLightsPrerequisiteTask(InitViewTaskDatas.DynamicShadows));
			IssuedTasksCompletionEvents.Add(UpdateLightFunctionAtlasTask);

			GatherAndSortLightsTask = LaunchSceneRenderTask<FSortedLightSetSceneInfo*>(UE_SOURCE_LOCATION, [this, SortedLightSet, bShadowedLightsInClustered]
			{
				GatherAndSortLights(*SortedLightSet, bShadowedLightsInClustered);
				return SortedLightSet;
			}, IssuedTasksCompletionEvents);
		}
	}

	// force using occ queries for wireframe if rendering is parented or frozen in the first view
	check(Views.Num());
	#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
		const bool bIsViewFrozen = false;
	#else
		const bool bIsViewFrozen = Views[0].State && ((FSceneViewState*)Views[0].State)->bIsFrozen;
	#endif

	
	const bool bIsOcclusionTesting = DoOcclusionQueries()
		&& (!ViewFamily.EngineShowFlags.Wireframe || bIsViewFrozen);
	const bool bNeedsPrePass = ShouldRenderPrePass();

	// Sanity check - Note: Nanite forces a Z prepass in ShouldForceFullDepthPass()
	check(!UseNanite(ShaderPlatform) || bNeedsPrePass);

	GetSceneExtensionsRenderers().PreRender(GraphBuilder);
	GEngine->GetPreRenderDelegateEx().Broadcast(GraphBuilder);

	if (DepthPass.IsComputeStencilDitherEnabled())
	{
		AddDitheredStencilFillPass(GraphBuilder, Views, SceneTextures.Depth.Target, DepthPass);
		//AddDitheredStencilFillPass 会向渲染图中添加一个计算型模板抖动填充 Pass，核心用途包括：
		//对模板缓冲（Stencil Buffer）进行抖动填充，实现软边缘、半透明遮挡效果（如植被、透明物体的轮廓平滑）。
		//基于深度信息生成带噪声的模板值，用于后续的后处理（如 TAA、SSAO 或风格化渲染）。
	}

	if (bNaniteEnabled)
	{
		// Must happen before any Nanite rendering in the frame
		if (bUpdateNaniteStreaming)
		{
			Nanite::GStreamingManager.EndAsyncUpdate(GraphBuilder);

			const TMap<uint32, uint32> ModifiedResources = Nanite::GStreamingManager.GetAndClearModifiedResources();
#if RHI_RAYTRACING
			if (RendererOutput == ERendererOutput::FinalSceneColor)
			{
				Nanite::GRayTracingManager.RequestUpdates(ModifiedResources);
			}
#endif
		}
	}

	// Render all pending material cache pages
	if (IsMaterialCacheEnabled(Scene->GetShaderPlatform()))//材质缓存
	{
		MaterialCacheRenderPages(GraphBuilder, this);//将材质相关的预计算数据（如烘焙的纹理、参数缓存）提前写入 GPU 资源，避免后续渲染时产生卡顿
	}

	// Virtual texturing isn't needed for depth prepass
	if (bUseVirtualTexturing && RendererOutput != ERendererOutput::DepthPrepassOnly)
	{
		FVirtualTextureSystem::Get().FinalizeRequests(GraphBuilder, this);
	}

	{
		RDG_RHI_GPU_STAT_SCOPE(GraphBuilder, VisibilityCommands);
		EndInitViews(GraphBuilder, LumenFrameTemporaries, InstanceCullingManager, InitViewTaskDatas);
	}

	// Substrate initialisation is always run even when not enabled.
	// Need to run after EndInitViews() to ensure ViewRelevance computation are completed
	const bool bSubstrateEnabled = Substrate::IsSubstrateEnabled();
	Substrate::InitialiseSubstrateFrameSceneData(GraphBuilder, *this);

	UE::SVT::GetStreamingManager().EndAsyncUpdate(GraphBuilder);

	FHairStrandsBookmarkParameters& HairStrandsBookmarkParameters = *GraphBuilder.AllocObject<FHairStrandsBookmarkParameters>();
	if (IsHairStrandsEnabled(EHairStrandsShaderType::All, Scene->GetShaderPlatform()) && RendererOutput == ERendererOutput::FinalSceneColor)
	{
		CreateHairStrandsBookmarkParameters(Scene, Views, AllViews, HairStrandsBookmarkParameters);
		check(Scene->HairStrandsSceneData.TransientResources);
		HairStrandsBookmarkParameters.TransientResources = Scene->HairStrandsSceneData.TransientResources;
		RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessTasks, HairStrandsBookmarkParameters);

		// Interpolation needs to happen after the skin cache run as there is a dependency 
		// on the skin cache output.
		const bool bRunHairStrands = HairStrandsBookmarkParameters.HasInstances() && (Views.Num() > 0);
		if (bRunHairStrands)
		{
			RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessCardsAndMeshesInterpolation_PrimaryView, HairStrandsBookmarkParameters);
		}
		else
		{
			for (FViewInfo& View : Views)
			{
				View.HairStrandsViewData.UniformBuffer = HairStrands::CreateDefaultHairStrandsViewUniformBuffer(GraphBuilder, View);
			}
		}
	}

	ExternalAccessQueue.Submit(GraphBuilder);
	
	//天空大气查找表（LUT）与水信息纹理预渲染
	const bool bShouldRenderSkyAtmosphere = ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags);
	const ESkyAtmospherePassLocation SkyAtmospherePassLocation = GetSkyAtmospherePassLocation();
	FSkyAtmospherePendingRDGResources SkyAtmospherePendingRDGResources;
	//PrePass 前的天空大气 LUT 预渲染
	if (SkyAtmospherePassLocation == ESkyAtmospherePassLocation::BeforePrePass && bShouldRenderSkyAtmosphere)
	{
		// Generate the Sky/Atmosphere look up tables overlaping the pre-pass
		RenderSkyAtmosphereLookUpTables(GraphBuilder, /* out */ SkyAtmospherePendingRDGResources);//预计算大气散射、透射率、 Mie/ Rayleigh 散射等物理大气数据；
	}

	RenderWaterInfoTexture(GraphBuilder, *this, Scene);//预渲染水信息纹理

	const bool bShouldRenderVelocities = ShouldRenderVelocities();//运动矢量渲染开关
	const EShaderPlatform Platform = GetViewFamilyInfo(Views).GetShaderPlatform();//着色器平台获取
	const bool bBasePassCanOutputVelocity = FVelocityRendering::BasePassCanOutputVelocity(Platform);//基础通道输出速度判定
	//毛发模拟（Hair Strands）启用判定
	const bool bHairStrandsEnable = HairStrandsBookmarkParameters.HasInstances() && Views.Num() > 0 && IsHairStrandsEnabled(EHairStrandsShaderType::Strands, Platform);
	const bool bForceVelocityOutput = bHairStrandsEnable || ShouldRenderDistortion();//强制速度输出判定（毛发 / 畸变适配）

	auto RenderPrepassAndVelocity = [&](auto& InViews, auto& InNaniteBasePassVisibility, auto& NaniteRasterResults, auto& PrimaryNaniteViews)
	{
		//GPU 蒙皮缓存异步等待
		if (FGPUSkinCache* GPUSkinCache = Scene->GetGPUSkinCache())
		{
			GPUSkinCache->AddAsyncComputeWait(GraphBuilder);
		}

		FRDGTextureRef FirstStageDepthBuffer = nullptr;
		{
			//深度 / 模板缓冲清除配置
			// Both compute approaches run earlier, so skip clearing stencil here, just load existing.
			const ERenderTargetLoadAction StencilLoadAction = DepthPass.IsComputeStencilDitherEnabled()
				? ERenderTargetLoadAction::ELoad
				: ERenderTargetLoadAction::EClear;

			const ERenderTargetLoadAction DepthLoadAction = ERenderTargetLoadAction::EClear;
			AddClearDepthStencilPass(GraphBuilder, SceneTextures.Depth.Target, DepthLoadAction, StencilLoadAction);

			//PrePass（Early Z Pass）渲染
			// Draw the scene pre-pass / early z pass, populating the scene depth buffer and HiZ
			if (bNeedsPrePass)
			{
				RenderPrePass(GraphBuilder, InViews, SceneTextures.Depth.Target, InstanceCullingManager, &FirstStageDepthBuffer);//执行场景深度预渲染
			}
			else
			{
				// We didn't do the prepass, but we still want the HMD mask if there is one
				RenderPrePassHMD(GraphBuilder, InViews, SceneTextures.Depth.Target);//当不需要完整 PrePass 时（如静态场景），仅渲染 HMD 遮罩（VR 头显遮挡区域），保证 VR 场景的深度缓冲正确。
			}
			
			//特殊模式下的运动矢量渲染
			// special pass for DDM_AllOpaqueNoVelocity, which uses the velocity pass to finish the early depth pass write
			if (bShouldRenderVelocities && Scene->EarlyZPassMode == DDM_AllOpaqueNoVelocity && RendererOutput == ERendererOutput::FinalSceneColor)
			{
				// Render the velocities of movable objects.  Don't bind the velocity render target for custom render passes (it's not used downstream), to avoid needing to clear it again.
				RenderVelocities(GraphBuilder, InViews, SceneTextures, EVelocityPass::Opaque, bForceVelocityOutput, /*bBindRenderTarget=*/ InViews[0].CustomRenderPass == nullptr);
			}
		}

		{
			//Nanite 材质缓存等待与渲染
			Scene->WaitForCacheNaniteMaterialBinsTask();

			if (bNaniteEnabled && InViews.Num() > 0)
			{
				RenderNanite(GraphBuilder, InViews, SceneTextures, bIsEarlyDepthComplete, InNaniteBasePassVisibility, NaniteRasterResults, PrimaryNaniteViews, FirstStageDepthBuffer);
			}
		}
		//分阶段深度缓冲
		if (FirstStageDepthBuffer)
		{
			SceneTextures.PartialDepth = FirstStageDepthBuffer;
			AddResolveSceneDepthPass(GraphBuilder, InViews, SceneTextures.PartialDepth);//添加深度解析 Pass，将分阶段深度缓冲解析为完整深度纹理，保证后续渲染能正确采样深度数据。
		}
		else
		{
			// Setup default partial depth to be scene depth so that it also works on transparent emitter when partial depth has not been generated.
			SceneTextures.PartialDepth = SceneTextures.Depth;//若不存在分阶段深度，则将PartialDepth指向完整场景深度，确保透明物体渲染时仍能正确获取深度信息。
		}
		//场景纹理 Uniform Buffer 初始化
		SceneTextures.SetupMode = ESceneTextureSetupMode::SceneDepth;
		SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);

		AddResolveSceneDepthPass(GraphBuilder, InViews, SceneTextures.Depth);
	};
	
	//DBuffer 纹理创建
	//DBuffer（Decal Buffer）：贴花缓冲，用于存储贴花的颜色、法线、粗糙度等数据，在 Base Pass 中与 GBuffer 混合。
	//CreateDBufferTextures：创建 DBuffer 所需的纹理资源（颜色、法线、粗糙度等），并绑定到 RDG，保证贴花渲染时资源就绪。
	FDBufferTextures DBufferTextures = CreateDBufferTextures(GraphBuilder, SceneTextures.Config.Extent, ShaderPlatform);

	//局部雾体积 Dummy 数据初始化
	// Initialise local fog volume with dummy data before volumetric cloud view initialization (further down) which can bind LFV data.
	// Also need to do this before custom render passes (included in AllViews), as base pass rendering may bind LFV data.
	//必须提前初始化，因为体积云与 Base Pass 都可能绑定 LFV 数据，防止资源未就绪导致的渲染错误。
	SetDummyLocalFogVolumeForViews(GraphBuilder, AllViews);

	//自定义渲染 Pass（Custom Render Pass）执行前的准备与状态保护
	if (CustomRenderPassInfos.Num() > 0)
	{
		//自定义渲染 Pass 性能统计
		QUICK_SCOPE_CYCLE_COUNTER(STAT_CustomRenderPasses);//统计 CPU 耗时，用于 Unreal Insights 等 CPU Profiler；
		RDG_EVENT_SCOPE_STAT(GraphBuilder, CustomRenderPasses, "CustomRenderPasses");//在 RDG 事件系统中标记CustomRenderPasses事件，用于可视化调试；
		RDG_GPU_STAT_SCOPE(GraphBuilder, CustomRenderPasses);//统计 GPU 耗时，监控自定义 Pass 的 GPU 负载。

		//场景纹理状态备份
		// We want to reset the scene texture uniform buffer to its original state after custom render passes,
		// so they can't affect downstream rendering.
		//在执行自定义 Pass 前，备份场景纹理的设置模式与Uniform Buffer，以便在自定义 Pass 执行后恢复原始状态，防止自定义逻辑修改核心渲染参数，影响后续 Base Pass / 光照计算。
		ESceneTextureSetupMode OriginalSceneTextureSetupMode = SceneTextures.SetupMode;//备份场景纹理初始化模式（如SceneDepth/BasePass等）；
		TRDGUniformBufferRef<FSceneTextureUniformParameters> OriginalSceneTextureUniformBuffer = SceneTextures.UniformBuffer;//备份场景纹理 Uniform Buffer（包含深度 / GBuffer 尺寸、采样器等核心参数）。
		//遍历并初始化自定义渲染 Pass
		for (int32 i = 0; i < CustomRenderPassInfos.Num(); ++i)
		{
			FCustomRenderPassBase* CustomRenderPass = CustomRenderPassInfos[i].CustomRenderPass;
			TArray<FViewInfo>& CustomRenderPassViews = CustomRenderPassInfos[i].Views;
			FNaniteShadingCommands& NaniteBasePassShadingCommands = CustomRenderPassInfos[i].NaniteBasePassShadingCommands;
			check(CustomRenderPass);//安全断言，确保自定义 Pass 指针有效，避免空指针崩溃。

			CustomRenderPass->BeginPass(GraphBuilder);//调用自定义 Pass 的BeginPass生命周期函数，用于初始化 Pass 资源、绑定 RDG 参数。
			
			//单自定义 Pass 性能统计与 PreRender 执行
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_CustomRenderPass);//统计单个自定义 Pass 的 CPU 耗时；
				RDG_EVENT_SCOPE(GraphBuilder, "CustomRenderPass[%d] %s", i, *CustomRenderPass->GetDebugName());//在 RDG 中标记带索引和调试名的事件，方便在可视化工具中定位具体 Pass。

				CustomRenderPass->PreRender(GraphBuilder);//执行自定义 Pass 的预渲染逻辑，用于资源准备、指令预提交。
				
				//为自定义 Pass 独立运行 PrePass 与速度渲染
				TArray<Nanite::FRasterResults, TInlineAllocator<2>> NaniteRasterResults;
				TArray<Nanite::FPackedView, SceneRenderingAllocator> PrimaryNaniteViews;
				FNaniteBasePassVisibility DummyNaniteBasePassVisibility;
				RenderPrepassAndVelocity(CustomRenderPassViews, DummyNaniteBasePassVisibility, NaniteRasterResults, PrimaryNaniteViews);
				
				//单层水深度预渲染
				//检查自定义 Pass 是否需要渲染单层水深度预渲染。
				const FSingleLayerWaterPrePassResult* SingleLayerWaterPrePassResult = nullptr;
				if (ShouldRenderSingleLayerWaterDepthPrepass(CustomRenderPassViews))
				{
					//执行水深度预渲染，生成水面深度缓冲，为后续水面反射 / 折射做准备。
					SingleLayerWaterPrePassResult = RenderSingleLayerWaterDepthPrepass(GraphBuilder, CustomRenderPassViews, SceneTextures, ESingleLayerWaterPrepassLocation::BeforeBasePass, NaniteRasterResults);
				}
				
				//场景捕获用户数据获取
				//让自定义 Pass 可以访问场景捕获的配置，实现如 “渲染到纹理”“多视角捕获” 等高级功能。
				const FSceneCaptureCustomRenderPassUserData& SceneCaptureUserData = FSceneCaptureCustomRenderPassUserData::Get(CustomRenderPass);
				//渲染模式判定
				if (CustomRenderPass->GetRenderMode() == FCustomRenderPassBase::ERenderMode::DepthAndBasePass)
				{
					//场景纹理配置更新
					SceneTextures.SetupMode |= ESceneTextureSetupMode::SceneColor;
					SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);
					
					//Nanite 着色命令构建（Nanite 启用时）
					if (bNaniteEnabled)
					{
						Nanite::BuildShadingCommands(GraphBuilder, *Scene, ENaniteMeshPass::BasePass, NaniteBasePassShadingCommands, Nanite::EBuildShadingCommandsMode::Custom);
					}
					
					//自定义渲染 Pass 的核心绘制与资源配置
					//Base Pass 绘制
					RenderBasePass(*this, 
						GraphBuilder, 
						CustomRenderPassViews, 
						SceneTextures, 
						DBufferTextures, 
						BasePassDepthStencilAccess, 
						/*ForwardScreenSpaceShadowMaskTexture=*/nullptr, 
						InstanceCullingManager, 
						bNaniteEnabled, 
						NaniteBasePassShadingCommands, 
						NaniteRasterResults);
					
					//单层水渲染
					if (ShouldRenderSingleLayerWater(CustomRenderPassViews))
					{
						// GBuffer code paths in RenderSingleLayerWater don't use the bIsCameraUnderWater flag, so just pass in false.  Normally this is
						// computed by a render extension, but those aren't run for custom render passes.
						FSceneWithoutWaterTextures SceneWithoutWaterTextures;
						RenderSingleLayerWater(GraphBuilder, CustomRenderPassViews, SceneTextures, SingleLayerWaterPrePassResult, /*bShouldRenderVolumetricCloud=*/false, SceneWithoutWaterTextures, LumenFrameTemporaries, /*bIsCameraUnderWater=*/false);
					}
					
					//场景纹理配置更新（针对场景捕获输出）
					FCustomRenderPassBase::ERenderOutput RenderOutput = CustomRenderPass->GetRenderOutput();
					if (RenderOutput == FCustomRenderPassBase::ERenderOutput::BaseColor || RenderOutput == FCustomRenderPassBase::ERenderOutput::Normal ||
						!SceneCaptureUserData.UserSceneTextureBaseColor.IsNone() || !SceneCaptureUserData.UserSceneTextureNormal.IsNone() || !SceneCaptureUserData.UserSceneTextureSceneColor.IsNone())
					{
						// CopySceneCaptureComponentToTarget uses scene texture uniforms
						SceneTextures.SetupMode |= ESceneTextureSetupMode::GBuffers;
						SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);
					}

					//UE 自定义渲染 Pass 半透明渲染与反射捕获配置
					if (CustomRenderPass->IsTranslucentIncluded())//半透明渲染模式判定
					{
						// Empty defaults
						//半透明资源初始化（空默认值）
						FTranslucencyLightingVolumeTextures TranslucencyLightingVolumeTextures;
						FTranslucencyPassResourcesMap TranslucencyResourceMap(CustomRenderPassViews.Num());
						const bool bStandardTranslucentCanRenderSeparate = false;
						FRDGTextureMSAA TranslucencySharedDepthTexture;
						FSeparateTranslucencyDimensions CustomTranslucencyDimensions = { SceneTexturesConfig.Extent };
						
						//反射捕获（Reflection Capture）空数据配置
						FReflectionCaptureShaderData EmptyData;
						TUniformBufferRef<FReflectionCaptureShaderData> EmptyReflectionCaptureUniformBuffer = TUniformBufferRef<FReflectionCaptureShaderData>::CreateUniformBufferImmediate(EmptyData, UniformBuffer_SingleFrame);
						for (FViewInfo& View : CustomRenderPassViews)
						{
							View.ReflectionCaptureUniformBuffer = EmptyReflectionCaptureUniformBuffer;
						}
						
						//核心半透明渲染调用
						RenderTranslucency(*this, 
							GraphBuilder, 
							SceneTextures, 
							TranslucencyLightingVolumeTextures, 
							&TranslucencyResourceMap, 
							CustomRenderPassViews, 
							ETranslucencyView::AboveWater, 
							CustomTranslucencyDimensions, 
							InstanceCullingManager, 
							bStandardTranslucentCanRenderSeparate, 
							TranslucencySharedDepthTexture);
					}
				}
				//将场景捕获组件（Scene Capture Component） 渲染到的场景图像数据（如颜色、深度、法线等），从临时的渲染目标（Render Target）拷贝到自定义 Pass 传入的目标纹理
				CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, CustomRenderPass->GetRenderTargetTexture(), ViewFamily, CustomRenderPassViews);
				
				//UE 自定义渲染 Pass：用户场景纹理（BaseColor）注册与分配
				//核心逻辑：创建 / 获取用户场景纹理
				if (!SceneCaptureUserData.UserSceneTextureBaseColor.IsNone())//将用户自定义的 BaseColor 纹理纳入引擎场景纹理管理体系，为后续渲染输出做准备。
				{
					bool bFirstRender;
					FRDGTextureRef BaseColorSceneTexture = SceneTextures.FindOrAddUserSceneTexture(GraphBuilder, 
						0, 
						SceneCaptureUserData.UserSceneTextureBaseColor, 
						SceneCaptureUserData.SceneTextureDivisor, 
						bFirstRender, 
						nullptr, 
						CustomRenderPassViews[0].ViewRect);
					
					//非 Shipping 版本：注册用户场景纹理事件
#if !(UE_BUILD_SHIPPING)
					SceneTextures.UserSceneTextureEvents.Add({ EUserSceneTextureEvent::CustomRenderPass, 
					NAME_None,
						(uint16)FCustomRenderPassBase::ERenderOutput::BaseColor, 
						0, 
						(const UMaterialInterface*)CustomRenderPass });
#endif

					CustomRenderPass->OverrideRenderOutput(FCustomRenderPassBase::ERenderOutput::BaseColor);
					CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, BaseColorSceneTexture, ViewFamily, CustomRenderPassViews);
				}

				if (!SceneCaptureUserData.UserSceneTextureNormal.IsNone())
				{
					bool bFirstRender;
					FRDGTextureRef NormalSceneTexture = SceneTextures.FindOrAddUserSceneTexture(GraphBuilder, 0, SceneCaptureUserData.UserSceneTextureNormal, SceneCaptureUserData.SceneTextureDivisor, bFirstRender, nullptr, CustomRenderPassViews[0].ViewRect);
#if !(UE_BUILD_SHIPPING)
					SceneTextures.UserSceneTextureEvents.Add({ EUserSceneTextureEvent::CustomRenderPass, NAME_None, (uint16)FCustomRenderPassBase::ERenderOutput::Normal, 0, (const UMaterialInterface*)CustomRenderPass });
#endif

					CustomRenderPass->OverrideRenderOutput(FCustomRenderPassBase::ERenderOutput::Normal);
					CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, NormalSceneTexture, ViewFamily, CustomRenderPassViews);
				}

				if (!SceneCaptureUserData.UserSceneTextureSceneColor.IsNone())
				{
					bool bFirstRender;
					FRDGTextureRef SceneColorSceneTexture = SceneTextures.FindOrAddUserSceneTexture(GraphBuilder, 0, SceneCaptureUserData.UserSceneTextureSceneColor, SceneCaptureUserData.SceneTextureDivisor, bFirstRender, nullptr, CustomRenderPassViews[0].ViewRect);
#if !(UE_BUILD_SHIPPING)
					SceneTextures.UserSceneTextureEvents.Add({ EUserSceneTextureEvent::CustomRenderPass, NAME_None, (uint16)FCustomRenderPassBase::ERenderOutput::SceneColorAndAlpha, 0, (const UMaterialInterface*)CustomRenderPass });
#endif

					CustomRenderPass->OverrideRenderOutput(FCustomRenderPassBase::ERenderOutput::SceneColorAndAlpha);
					CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, SceneColorSceneTexture, ViewFamily, CustomRenderPassViews);
				}

				CustomRenderPass->PostRender(GraphBuilder);

				// Mips are normally generated in UpdateSceneCaptureContentDeferred_RenderThread, but that doesn't run when the
				// scene capture runs as a custom render pass.  The function does nothing if the render target doesn't have mips.
				if (CustomRenderPassViews[0].bIsSceneCapture)
				{
					FGenerateMips::Execute(GraphBuilder, FeatureLevel, CustomRenderPass->GetRenderTargetTexture(), FGenerateMipsParams());
				}

			#if WITH_MGPU
				DoCrossGPUTransfers(GraphBuilder, CustomRenderPass->GetRenderTargetTexture(), CustomRenderPassViews, false, FRHIGPUMask::All(), nullptr);
			#endif
			}

			CustomRenderPass->EndPass(GraphBuilder);

			// Restore original scene texture uniforms
			SceneTextures.SetupMode = OriginalSceneTextureSetupMode;
			SceneTextures.UniformBuffer = OriginalSceneTextureUniformBuffer;
		}
	}

	TArray<Nanite::FRasterResults, TInlineAllocator<2>> NaniteRasterResults;
	TArray<Nanite::FPackedView, SceneRenderingAllocator> PrimaryNaniteViews;
	RenderPrepassAndVelocity(Views, NaniteBasePassVisibility, NaniteRasterResults, PrimaryNaniteViews);

	// Run Nanite compute commands early in the frame to allow some task overlap on the CPU until the base pass runs.
	if (bNaniteEnabled && RendererOutput != ERendererOutput::DepthPrepassOnly && !bHasRayTracedOverlay)
	{
		Nanite::BuildShadingCommands(GraphBuilder, *Scene, ENaniteMeshPass::BasePass, Scene->NaniteShadingCommands[ENaniteMeshPass::BasePass]);
		if (bAnyLumenEnabled && RendererOutput == ERendererOutput::FinalSceneColor)
		{
			Nanite::BuildShadingCommands(GraphBuilder, *Scene, ENaniteMeshPass::LumenCardCapture, Scene->NaniteShadingCommands[ENaniteMeshPass::LumenCardCapture]);
		}
	}

	FComputeLightGridOutput ComputeLightGridOutput = {};

	FCompositionLighting CompositionLighting(InitViewTaskDatas.Decals, Views, SceneTextures, [this](int32 ViewIndex)
	{
		return GetViewPipelineState(Views[ViewIndex]).AmbientOcclusionMethod == EAmbientOcclusionMethod::SSAO;
	});

	const auto RenderOcclusionLambda = [&]() -> Froxel::FRenderer 
	{
		const int32 AsyncComputeMode = CVarSceneDepthHZBAsyncCompute.GetValueOnRenderThread();
		bool bAsyncCompute = AsyncComputeMode != 0;

		FBuildHZBAsyncComputeParams AsyncComputeParams = {};
		if (AsyncComputeMode == 2)
		{
			AsyncComputeParams.Prerequisite = ComputeLightGridOutput.CompactLinksPass;
		}

		bool bShouldGenerateFroxels = DoesVSMWantFroxels(ShaderPlatform);

		Froxel::FRenderer FroxelRenderer(bShouldGenerateFroxels, GraphBuilder, Views);

		//执行遮挡剔除 + HZB 构建
		RenderOcclusion(GraphBuilder, SceneTextures, bIsOcclusionTesting,
			bAsyncCompute ? &AsyncComputeParams : nullptr, FroxelRenderer);
		//通知组合光照模块处理遮挡后逻辑
		CompositionLighting.ProcessAfterOcclusion(GraphBuilder);

		return FroxelRenderer;
	};

	const bool bShouldRenderVolumetricCloudBase = ShouldRenderVolumetricCloud(Scene, ViewFamily.EngineShowFlags);
	const bool bShouldRenderVolumetricCloud = bShouldRenderVolumetricCloudBase && (!ViewFamily.EngineShowFlags.VisualizeVolumetricCloudConservativeDensity && !ViewFamily.EngineShowFlags.VisualizeVolumetricCloudEmptySpaceSkipping);
	const bool bShouldVisualizeVolumetricCloud = bShouldRenderVolumetricCloudBase && (!!ViewFamily.EngineShowFlags.VisualizeVolumetricCloudConservativeDensity || !!ViewFamily.EngineShowFlags.VisualizeVolumetricCloudEmptySpaceSkipping);
	const bool bAsyncComputeVolumetricCloud = IsVolumetricRenderTargetEnabled() && IsVolumetricRenderTargetAsyncCompute();
	const bool bVolumetricRenderTargetRequired = bShouldRenderVolumetricCloud && !bHasRayTracedOverlay;

	Froxel::FRenderer FroxelRenderer;

	FRDGTextureRef ViewFamilyTexture = TryCreateViewFamilyTexture(GraphBuilder, ViewFamily);
	FRDGTextureRef ViewFamilyDepthTexture = TryCreateViewFamilyDepthTexture(GraphBuilder, ViewFamily);
	if (RendererOutput == ERendererOutput::DepthPrepassOnly)
	{
		const FSingleLayerWaterPrePassResult* SingleLayerWaterPrePassResult = nullptr;
		//判断是否需要为 单层水体（Single Layer Water）执行深度预通，如果需要则执行对应的渲染函数，生成水体深度信息。
		if (ShouldRenderSingleLayerWaterDepthPrepass(Views))
		{
			SingleLayerWaterPrePassResult = RenderSingleLayerWaterDepthPrepass(GraphBuilder, Views, SceneTextures, ESingleLayerWaterPrepassLocation::BeforeBasePass, NaniteRasterResults);
		}

		FroxelRenderer = RenderOcclusionLambda();//构建用于遮挡剔除的渲染 Lambda
		
		//如果开启了 Nanite 流送更新，就向流送管理器提交当前帧的资源请求，保证 Nanite 几何体的正确加载和卸载。
		if (bUpdateNaniteStreaming)
		{
			Nanite::GStreamingManager.SubmitFrameStreamingRequests(GraphBuilder);
		}
		//将场景捕获组件（Scene Capture Component）的结果拷贝到目标渲染目标，用于后续的后处理或其他渲染阶段。
		CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, ViewFamilyTexture, ViewFamilyDepthTexture, ViewFamily, Views);
	}
	else
	{
		GVRSImageManager.PrepareImageBasedVRS(GraphBuilder, ViewFamily, SceneTextures);
		//延迟渲染路径下的动态阴影初始化
		if (!IsForwardShadingEnabled(ShaderPlatform))//只有在非前向渲染（即延迟渲染 Deferred Shading）时才执行
		{
			// Dynamic shadows are synced later when using the deferred path to make more headroom for tasks.
			//注释说明：延迟渲染路径下会延后同步动态阴影，给其他任务留出更多执行空间，提升多线程调度效率。
			FinishInitDynamicShadows(GraphBuilder, InitViewTaskDatas.DynamicShadows, InstanceCullingManager);
		}

		// Update groom only visible in shadow
		//毛发 Strands 渲染（仅阴影可见部分）
		if (IsHairStrandsEnabled(EHairStrandsShaderType::All, Scene->GetShaderPlatform()) && RendererOutput == ERendererOutput::FinalSceneColor)
		{
			UpdateHairStrandsBookmarkParameters(Scene, Views, HairStrandsBookmarkParameters);//更新毛发书签参数
			//书签（Bookmark）是 UE 毛发系统的一种优化机制，用来批量管理和调度毛发渲染任务。

			// Interpolation for cards/meshes only visible in shadow needs to happen after the shadow jobs are completed
			//执行毛发插值（仅阴影可见部分）
			const bool bRunHairStrands = HairStrandsBookmarkParameters.HasInstances() && (Views.Num() > 0);
			if (bRunHairStrands)
			{
				RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessCardsAndMeshesInterpolation_ShadowView, HairStrandsBookmarkParameters);
			}
		}

		// Early occlusion queries
		//早期遮挡查询
		const bool bOcclusionBeforeBasePass = ((DepthPass.EarlyZPassMode == EDepthDrawingMode::DDM_AllOccluders) || bIsEarlyDepthComplete);
		//bOcclusionBeforeBasePass：标记是否在 Base Pass 前执行遮挡剔除。
		//深度预通模式为 DDM_AllOccluders：引擎配置为渲染所有遮挡物的早期深度 Pass，生成完整深度缓冲。
		//bIsEarlyDepthComplete 为 true：早期深度数据已准备完成（可能来自前一帧或其他预渲染步骤）。

		if (bOcclusionBeforeBasePass)//执行 Froxel 遮挡剔除
		{
			FroxelRenderer = RenderOcclusionLambda();
		}

		// End early occlusion queries
		// 结束早期遮挡查询
		
		
		//为前向渲染 / 延迟渲染准备光照数据。
		
		//视图扩展前置回调（回顾），遍历所有视图扩展，在 Base Pass 前执行自定义渲染准备，确保插件 / 扩展逻辑能在光照数据准备前完成资源注入。
		for (FSceneViewExtensionRef& ViewExtension : ViewFamily.ViewExtensions)
		{
			ViewExtension->PreRenderBasePass_RenderThread(GraphBuilder, ShouldRenderPrePass() /*bDepthBufferIsPopulated*/);
		}
		
		//等待光照收集与排序任务完成
		{
			SCOPE_CYCLE_COUNTER(STAT_WaitGatherAndSortLightsTask);
			GatherAndSortLightsTask.Wait();
		}

		//开启 RDG 与 CPU 周期统计，标记「准备前向光照数据」阶段。
		{
			RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, PrepareForwardLightData);
			SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_PrepareForwardLightData);

			const FSortedLightSetSceneInfo* SortedLightSet = GatherAndSortLightsTask.GetResult();
			
			//非路径追踪模式：准备前向光照数据
			if (!ViewFamily.EngineShowFlags.PathTracing)
			{
				//PrepareForwardLightData()：核心函数，将已排序的光源数据打包为前向渲染可用的格式。
				//bCullLightsToGrid: true：启用光照网格剔除，将光源映射到 3D 光照网格（Light Grid），只保留对当前区域有影响的光源，大幅降低光照计算开销。
				ComputeLightGridOutput = PrepareForwardLightData(GraphBuilder, true, *SortedLightSet);

				// Store this flag if lights are injected in the grids, check with 'AreLightsInLightGrid()'
				bAreLightsInLightGrid = true;
			}
			//路径追踪模式：设置 dummy 光照缓冲区
			//路径追踪模式下，光照计算由路径追踪管线独立处理，无需前向光照数据。
			else
			{
				SetDummyForwardLightUniformBufferOnViews(GraphBuilder, ShaderPlatform, Views);
			}
			//将三类关键光照数量数据写入 CSV 日志。
			CSV_CUSTOM_STAT(LightCount, All,  float(SortedLightSet->SortedLights.Num()), ECsvCustomStatOp::Set);//All（全部光源）
			CSV_CUSTOM_STAT(LightCount, Batched, float(SortedLightSet->UnbatchedLightStart), ECsvCustomStatOp::Set);//Batched（批处理光源），记录可被批量处理的光源数量，
			CSV_CUSTOM_STAT(LightCount, Unbatched, float(SortedLightSet->SortedLights.Num()) - float(SortedLightSet->UnbatchedLightStart), ECsvCustomStatOp::Set);//Unbatched（非批处理光源），记录需要单独处理的光源数量
		}
		
		//光照函数图集渲染
		LightFunctionAtlas.RenderLightFunctionAtlas(GraphBuilder, Views);

		//体积云初始化
		// Run before RenderSkyAtmosphereLookUpTables for cloud shadows to be valid.
		//必须在 RenderSkyAtmosphereLookUpTables 之前执行，以保证云阴影数据有效。
		InitVolumetricCloudsForViews(GraphBuilder, bShouldRenderVolumetricCloudBase, InstanceCullingManager);
		
		//异步距离场阴影投影启动
		BeginAsyncDistanceFieldShadowProjections(GraphBuilder, SceneTextures, InitViewTaskDatas.DynamicShadows);
		
		//局部雾体积初始化
		// Run local fog volume culling before base pass and after HZB generation to benefit from more culling.
		//在 HZB（Hierarchical Z-Buffer）生成后、Base Pass 前执行，以利用 HZB 做更高效的剔除。
		InitLocalFogVolumesForViews(Scene, Views, ViewFamily, GraphBuilder, bShouldRenderVolumetricFog, false /*bool bUseHalfResLocalFogVolume*/);
		
		//体积渲染目标管理（云 / 雾）
		if (bShouldRenderVolumetricCloudBase)
		{
			InitVolumetricRenderTargetForViews(GraphBuilder, Views, SceneTextures);
		}
		else
		{
			ResetVolumetricRenderTargetForViews(GraphBuilder, Views);
		}

		// Generate sky LUTs
		//生成天空 / 大气的预计算查找表（LUT），避免在运行时重复计算复杂大气光照。
		// TODO: Valid shadow maps (for volumetric light shafts) have not yet been generated at this point in the frame. Need to resolve dependency ordering!
		//TODO 问题：当前阶段还未生成有效的阴影贴图，会影响体积光轴（Volumetric Light Shafts）的正确性，需要后续解决依赖顺序问题。
		// This also must happen before the BasePass for Sky material to be able to sample valid LUTs.
		//执行时机约束：必须在 Base Pass 之前完成，否则天空材质无法采样到有效的 LUT 数据。
		
		//Base Pass 前生成 LUT
		if (SkyAtmospherePassLocation == ESkyAtmospherePassLocation::BeforeBasePass && bShouldRenderSkyAtmosphere)//天空大气 Pass 位置配置为 BeforeBasePass，当前帧需要渲染天空大气（bShouldRenderSkyAtmosphere 为 true）
		{
			// Generate the Sky/Atmosphere look up tables
			RenderSkyAtmosphereLookUpTables(GraphBuilder, /* out */ SkyAtmospherePendingRDGResources);//预计算大气散射、吸收等复杂光照数据，生成 2D/3D 查找表

			SkyAtmospherePendingRDGResources.CommitToSceneAndViewUniformBuffers(GraphBuilder, /* out */ ExternalAccessQueue);//将预计算的 LUT 资源绑定到场景和视图的 Uniform Buffer，让后续所有 Pass（尤其是 Base Pass 的天空材质）可以直接采样。
		}
		else if (SkyAtmospherePassLocation == ESkyAtmospherePassLocation::BeforePrePass && bShouldRenderSkyAtmosphere)//天空大气 Pass 位置配置为 BeforePrePass，当前帧需要渲染天空大气
		{
			SkyAtmospherePendingRDGResources.CommitToSceneAndViewUniformBuffers(GraphBuilder, /* out */ ExternalAccessQueue);//此分支不重新生成 LUT，仅将已有的 SkyAtmospherePendingRDGResources 提交到 Uniform Buffer。
			//说明 LUT 生成已在更早阶段完成，此处仅完成资源绑定，保证 Pre Pass 能访问到大气数据。
		}
		
		//实时天空光照捕获（SkyLight Capture）
		// Capture the SkyLight using the SkyAtmosphere and VolumetricCloud component if available.
		//条件判断：是否启用实时天空捕获
		const bool bRealTimeSkyCaptureEnabled = Scene->SkyLight && Scene->SkyLight->bRealTimeCaptureEnabled && Views.Num() > 0 && ViewFamily.EngineShowFlags.SkyLighting;//判断是否满足实时天空捕获的全部条件
		const bool bPathTracedAtmosphere = ViewFamily.EngineShowFlags.PathTracing && Views.Num() > 0 && PathTracing::UsesReferenceAtmosphere(Views[0]);//判断是否启用了路径追踪大气模式：
		if (bRealTimeSkyCaptureEnabled && !bPathTracedAtmosphere)
		{
			// Sky capture accesses the view uniform buffer which uses LUT's.
			//注释说明：Sky 捕获需要访问使用了天空大气 LUT 的视图 Uniform Buffer，因此必须先提交 ExternalAccessQueue。
			ExternalAccessQueue.Submit(GraphBuilder);//将之前生成的天空大气 LUT 资源提交到 RDG 图中，确保 Sky 捕获阶段能正确采样到 LUT 数据，避免资源依赖错误。
			
			//执行实时天空环境贴图捕获
			FViewInfo& MainView = Views[0];
			Scene->AllocateAndCaptureFrameSkyEnvMap(GraphBuilder, *this, MainView, bShouldRenderSkyAtmosphere, bShouldRenderVolumetricCloud, InstanceCullingManager, ExternalAccessQueue);
		}
		//获取自定义深度 Pass 位置
		const ECustomDepthPassLocation CustomDepthPassLocation = GetCustomDepthPassLocation(ShaderPlatform);
		//条件分支：Base Pass 前执行自定义深度
		if (CustomDepthPassLocation == ECustomDepthPassLocation::BeforeBasePass)
		{	
			//性能统计与执行自定义深度 Pass
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_CustomDepthPass_BeforeBasePass);
			if (RenderCustomDepthPass(GraphBuilder, SceneTextures.CustomDepth, SceneTextures.GetSceneTextureShaderParameters(FeatureLevel), NaniteRasterResults, PrimaryNaniteViews))
			{
				SceneTextures.SetupMode |= ESceneTextureSetupMode::CustomDepth;
				SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);
			}
		}
		
		// 单层水体深度预通（Single Layer Water Depth Prepass） 的执行逻辑，专门处理在 Base Pass 之前渲染水体深度的情况。
		// Single layer water depth prepass. Needs to run before VSM page allocation. If there's a full depth prepass, it can run before the base pass, otherwise after.
		//单层水体深度预通必须在 VSM（Virtual Shadow Maps）页分配 之前执行，否则会破坏虚拟阴影贴图的分配逻辑。
		//如果存在完整深度预通（Full Depth Prepass），可以在 Base Pass 前执行。否则，需要在 Base Pass 后执行。
		// Running before the base pass allows for some optimizations to save work in the base pass and lighting stages.
		//在 Base Pass 前执行可以优化后续 Base Pass 和光照阶段的计算量。
		const FSingleLayerWaterPrePassResult* SingleLayerWaterPrePassResult = nullptr;//存储水体深度预通的结果指针，初始为 nullptr。
		const ESingleLayerWaterPrepassLocation SingleLayerWaterPrepassLocation = GetSingleLayerWaterDepthPrepassLocation(bIsEarlyDepthComplete, CustomDepthPassLocation);
		//根据早期深度是否完成（bIsEarlyDepthComplete）和自定义深度 Pass 位置（CustomDepthPassLocation），返回水体预通的执行位置（BeforeBasePass 或 AfterBasePass）。
		const bool bShouldRenderSingleLayerWaterDepthPrepass = !bHasRayTracedOverlay && ShouldRenderSingleLayerWaterDepthPrepass(Views);//判断是否需要执行水体深度预通
		if (bShouldRenderSingleLayerWaterDepthPrepass && SingleLayerWaterPrepassLocation == ESingleLayerWaterPrepassLocation::BeforeBasePass)//需要渲染水体深度预通，且配置为在 Base Pass 前执行
		{
			//提前生成水体深度数据，为 VSM 页分配、Base Pass 水体着色和光照计算提供数据基础
			SingleLayerWaterPrePassResult = RenderSingleLayerWaterDepthPrepass(GraphBuilder, Views, SceneTextures, SingleLayerWaterPrepassLocation, NaniteRasterResults);
		}

		// Lumen updates need access to sky atmosphere LUT.
		//Lumen 的全局光照更新需要采样天空大气 LUT（之前生成的大气预计算数据）。
		ExternalAccessQueue.Submit(GraphBuilder);//将天空大气 LUT 相关的 RDG 资源提交到渲染依赖图，确保 Lumen 阶段能安全访问这些预计算数据，避免资源依赖冲突。

		UpdateLumenScene(GraphBuilder, LumenFrameTemporaries);//更新 Lumen 场景数据
		
		//深度 MinMax 纹理变量初始化
		FRDGTextureRef HalfResolutionDepthCheckerboardMinMaxTexture = nullptr;//半分辨率棋盘格深度 MinMax
		FRDGTextureRef HalfResolutionDepthMinMaxTexture = nullptr;//半分辨率深度 MinMax
		FRDGTextureRef QuarterResolutionDepthMinMaxTexture = nullptr;//四分之一分辨率深度 MinMax
		bool bQuarterResMinMaxDepthRequired = bShouldRenderVolumetricCloud && ShouldVolumetricCloudTraceWithMinMaxDepth(Views);
		
		//这是一个延迟执行的 Lambda，用于根据需求生成不同分辨率的深度 MinMax 金字塔
		auto GenerateQuarterResDepthMinMaxTexture = [&](auto& GraphBuilder, auto& Views, auto& SceneDepthTexture)
		{
			if (bQuarterResMinMaxDepthRequired)//需要体积云 MinMax
			{
				//先验证输入纹理和输出纹理指针的有效性。
				//从场景深度纹理生成半分辨率和四分之一分辨率的深度 MinMax 金字塔，用于体积云的高效光线追踪。
				check(SceneDepthTexture != nullptr);					// Must receive a valid texture
				check(HalfResolutionDepthMinMaxTexture == nullptr);		// Only generate it once
				check(QuarterResolutionDepthMinMaxTexture == nullptr);	// Only generate it once
				CreateQuarterResolutionDepthMinAndMaxFromDepthTexture(GraphBuilder, Views, SceneDepthTexture, HalfResolutionDepthMinMaxTexture, QuarterResolutionDepthMinMaxTexture);
			}
			else//不需要体积云 MinMax
			{
				//生成半分辨率棋盘格深度 MinMax，用于其他需要深度 MinMax 的效果（如 Temporal AA、后处理）
				HalfResolutionDepthCheckerboardMinMaxTexture = CreateHalfResolutionDepthCheckerboardMinMax(GraphBuilder, Views, SceneDepthTexture);
			}
		};
		
		//变量初始化
		//定义前向渲染所需的屏幕空间阴影遮罩纹理（含毛发子像素版本），并标记阴影是否已提前渲染。
		//为了适配前向渲染「先着色后处理」的特性，必须提前生成阴影、毛发、雾效等数据。
		//通过提前渲染阴影深度图和体积雾，让 Base Pass 能直接拿到所有需要的光照和效果数据。
		//同时兼容延迟渲染的提前阴影选项，平衡不同渲染模式的性能与视觉效果。
		FRDGTextureRef ForwardScreenSpaceShadowMaskTexture = nullptr;
		FRDGTextureRef ForwardScreenSpaceShadowMaskHairTexture = nullptr;
		bool bShadowMapsRenderedEarly = false;
		if (IsForwardShadingEnabled(ShaderPlatform))//只有当前向渲染启用时，才进入此分支（延迟渲染走另一套阴影逻辑）
		{
			//提前渲染阴影深度图
			// With forward shading we need to render shadow maps early
			ensureMsgf(!VirtualShadowMapArray.IsEnabled(), TEXT("Virtual shadow maps are not supported in the forward shading path"));//前向渲染不支持虚拟阴影贴图（VSM）
			RenderShadowDepthMaps(GraphBuilder, InitViewTaskDatas.DynamicShadows, InstanceCullingManager, ExternalAccessQueue);//提前渲染所有动态阴影的深度图，为后续 Base Pass 提供阴影数据。
			bShadowMapsRenderedEarly = true;//标记阴影已提前生成，避免后续重复渲染。

			if (bHairStrandsEnable)//毛发 Strands 渲染（若启用）
			{
				RDG_EVENT_SCOPE(GraphBuilder, "Hair");

				RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessStrandsInterpolation, HairStrandsBookmarkParameters);//执行毛发 strands 的插值计算，保证运动平滑。
				if (!bHasRayTracedOverlay)
				{
					//在光线追踪关闭时，提前渲染毛发的预通和 Base Pass，为后续阴影和光照提供毛发深度数据。
					RenderHairPrePass(GraphBuilder, Scene, SceneTextures, Views, InstanceCullingManager, HairStrandsBookmarkParameters.CullingResults);
					RenderHairBasePass(GraphBuilder, Scene, SceneTextures, Views, InstanceCullingManager);
				}
			}
			
			//将提前生成的阴影深度图投影到屏幕空间，生成前向渲染专用的阴影遮罩纹理（含毛发子像素版本），供 Base Pass 直接采样。
			RenderForwardShadowProjections(GraphBuilder, SceneTextures, ForwardScreenSpaceShadowMaskTexture, ForwardScreenSpaceShadowMaskHairTexture);

			// With forward shading we need to render volumetric fog before the base pass
			//前向渲染要求在 Base Pass 前计算体积雾，将雾效数据写入场景纹理，保证后续着色时能正确混合雾效。
			//延迟渲染下，若开启了 r.shadow.ShadowMapsRenderEarly CVar，也会尝试提前渲染阴影。
			ComputeVolumetricFog(GraphBuilder, SceneTextures);
		}
		else if ( CVarShadowMapsRenderEarly.GetValueOnRenderThread() )
		{
			// Disable early shadows if VSM is enabled, but warn
			//提前阴影渲染 不兼容虚拟阴影贴图（VSM）。
			ensureMsgf(!VirtualShadowMapArray.IsEnabled(), TEXT("Virtual shadow maps are not supported with r.shadow.ShadowMapsRenderEarly. Early shadows will be disabled"));
			if (!VirtualShadowMapArray.IsEnabled())
			{
				RenderShadowDepthMaps(GraphBuilder, InitViewTaskDatas.DynamicShadows, InstanceCullingManager, ExternalAccessQueue);
				bShadowMapsRenderedEarly = true;
			}
		}
		
		//提交外部访问队列 & D-Buffer 预处理
		//将之前累积的所有外部资源（如天空 LUT、VSM 页表、自定义数据）提交到 RDG 渲染依赖图。
		ExternalAccessQueue.Submit(GraphBuilder);

		{
			RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, DeferredShadingSceneRenderer_DBuffer);
			SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_DBuffer);
			//此时会结合实例剔除（InstanceCullingManager）和 Substrate 场景数据（UE5 的现代材质系统数据），完成所有非透明物体的深度和属性写入。
			CompositionLighting.ProcessBeforeBasePass(GraphBuilder, DBufferTextures, InstanceCullingManager, Scene->SubstrateSceneData);
		}
		
		// 前向渲染：渲染间接胶囊阴影
		//渲染间接阴影（Indirect Shadows），即由其他物体投射过来的阴影，而非光源直接照射产生的阴影。
		//特别针对 胶囊体（Capsule）形状的光源或接收器（常用于角色、植被等）。
		//前向渲染路径中，间接阴影通常需要提前渲染，以便在 Base Pass 中进行正确的光照混合。
		if (IsForwardShadingEnabled(ShaderPlatform))
		{
			RenderIndirectCapsuleShadows(GraphBuilder, SceneTextures);
		}
		
		//半透明光照体积初始化（异步计算）
		FTranslucencyLightingVolumeTextures TranslucencyLightingVolumeTextures;//存储半透明光照体积（TLV）的数据结构。TLV 是一种 3D 纹理，用于存储半透明物体（如玻璃、烟雾、粒子）的光照信息。

		if (bRenderDeferredLighting && GbEnableAsyncComputeTranslucencyLightingVolumeClear && GSupportsEfficientAsyncCompute)
		{
			TranslucencyLightingVolumeTextures.Init(GraphBuilder, Views, ERDGPassFlags::AsyncCompute);//初始化半透明光照体积的纹理资源。
		}
		//提前准备好半透明物体的光照数据，确保在后续的半透明渲染 Pass 中能快速采样到光照结果。

		//声明一个动态几何暂存缓冲区指针，用于存储光线追踪所需的临时几何数据（如 BLAS/TLAS 构建中间结果）。
		FRDGBufferRef DynamicGeometryScratchBuffer = nullptr;
#if RHI_RAYTRACING
		//根据 Lumen 是否启用异步计算（Lumen::UseAsyncCompute），选择光线追踪资源的访问管线
		ERHIPipeline DynamicRTResourceAccessPipelines = Lumen::UseAsyncCompute(ViewFamily) ? ERHIPipeline::All : ERHIPipeline::Graphics;
		
		//分发光线追踪世界更新
		// Async AS builds can potentially overlap with BasePass.
		//注释说明：异步 AS（Acceleration Structure，加速结构）构建可以与 Base Pass 重叠执行，充分利用多线程资源。
		bool bNeedToSetupRayTracingRenderingData = DispatchRayTracingWorldUpdates(GraphBuilder, DynamicGeometryScratchBuffer, DynamicRTResourceAccessPipelines);

		//为视图设置光线追踪光照数据
		/** Should be called somewhere before "SetupRayTracingRenderingData" */
		//注释强调：此函数必须在 SetupRayTracingRenderingData 之前调用，保证光照数据在 RT 渲染前就绪。
		SetupRayTracingLightDataForViews(GraphBuilder);
#endif
		
		if (!bHasRayTracedOverlay)//无光线追踪覆盖层
		{
#if RHI_RAYTRACING
			// Lumen scene lighting requires ray tracing scene to be ready if HWRT shadows are desired
			//核心约束：如果要使用 Lumen 硬件光线追踪阴影（HWRT Shadows），必须保证光线追踪场景（Acceleration Structure）已经准备就绪。
			if (bNeedToSetupRayTracingRenderingData && Lumen::UseHardwareRayTracedSceneLighting(ViewFamily))
			{
				//执行光线追踪渲染数据设置
				SetupRayTracingRenderingData(GraphBuilder);
				bNeedToSetupRayTracingRenderingData = false;
			}
#endif

			LLM_SCOPE_BYTAG(Lumen);
			//开始收集 Lumen 表面缓存反馈，用于下一帧的表面缓存更新，优化间接光照的精度和性能。
			BeginGatheringLumenSurfaceCacheFeedback(GraphBuilder, Views[0], LumenFrameTemporaries);
			//基于之前构建的加速结构和光照数据，计算场景的间接光照、反射和环境光遮蔽。
			//结合直接光照数据（LumenDirectLighting），生成完整的光照结果，供 Base Pass 采样。
			RenderLumenSceneLighting(GraphBuilder, LumenFrameTemporaries, InitViewTaskDatas.LumenDirectLighting);
		}

		{
			if (!bHasRayTracedOverlay)//无光线追踪覆盖层时执行，保证正常场景着色。
			{
				//遍历所有可见物体，执行顶点 / 像素着色器，写入颜色、深度到场景纹理。
				//采样 D-Buffer（延迟渲染）或直接着色（前向渲染），结合 Lumen 光照、阴影遮罩等数据，计算最终像素颜色。
				//支持 Nanite 几何体：传入 Nanite 着色命令和栅格化结果，高效渲染海量多边形。
				//传入实例剔除管理器，跳过不可见物体，提升性能。
				RenderBasePass(
					*this,													//FDeferredShadingSceneRenderer& Renderer,          // 当前渲染器实例
					GraphBuilder,											//FRDGBuilder& GraphBuilder,                       // RDG 渲染依赖图构建器
					Views,													//TArrayView<FViewInfo> InViews,                   // 输入视图列表
					SceneTextures,											//FSceneTextures& SceneTextures,                   // 场景纹理集合（颜色/深度等）
					DBufferTextures,											//const FDBufferTextures& DBufferTextures,         // D-Buffer 数据（延迟渲染用）
					BasePassDepthStencilAccess,									//FExclusiveDepthStencil::Type BasePassDepthStencilAccess, // 深度/模板访问权限
					ForwardScreenSpaceShadowMaskTexture,						//FRDGTextureRef ForwardShadowMaskTexture,         // 前向渲染阴影遮罩纹理
					InstanceCullingManager,									// FInstanceCullingManager& InstanceCullingManager,  // 实例剔除管理器
					bNaniteEnabled,												//bool bNaniteEnabled,                             // 是否启用 Nanite
					Scene->NaniteShadingCommands[ENaniteMeshPass::BasePass], //FNaniteShadingCommands& NaniteBasePassShadingCommands, // Nanite 着色命令
					NaniteRasterResults);									//const TArrayView<Nanite::FRasterResults>& NaniteRasterResults // Nanite 栅格化结果
			}

			if (!bAllowReadOnlyDepthBasePass)//当 Base Pass 不允许只读深度访问时执行。
			{
				//添加深度解析 Pass，将 MSAA 深度纹理解析为单采样深度纹理，或同步深度数据到后续阶段可用的格式，保证后处理、体积效果等能正确采样场景深度。
				AddResolveSceneDepthPass(GraphBuilder, Views, SceneTextures.Depth);
			}
			
			//Nanite 可视化调试
			if (bNaniteEnabled)
			{
				if (bVisualizeNanite)
				{
					FNanitePickingFeedback PickingFeedback = { 0 };

					Nanite::AddVisualizationPasses(//向 RDG 渲染依赖图中添加 Nanite 可视化相关的渲染 Pass
						GraphBuilder,
						Scene,
						SceneTextures,
						ViewFamily.EngineShowFlags,
						Views,
						NaniteRasterResults,
						PickingFeedback,
						VirtualShadowMapArray
					);

					OnGetOnScreenMessages.AddLambda([this, PickingFeedback, RenderFlags = NaniteRasterResults[0].RenderFlags, ScenePtr = Scene](FScreenMessageWriter& ScreenMessageWriter)->void
					{
						Nanite::DisplayPicking(ScenePtr, PickingFeedback, RenderFlags, ScreenMessageWriter);
					});
				}
			}

			// VisualizeVirtualShadowMap TODO
			//未来将添加虚拟阴影贴图（VSM）的可视化调试
		}
		/////////////////////////////custom////////////////////////////////////
		{
			RenderToonBasePass(GraphBuilder,SceneTextures);
		}
		
		
		
		//自动曝光照度准备
		FRDGTextureRef ExposureIlluminanceSetup = nullptr;
		if (!bHasRayTracedOverlay)//无光线追踪覆盖层时执行（避免干扰自动曝光计算）。
		{
			//AddSetupExposureIlluminancePass：从场景颜色纹理中提取 ** 自发光（Emissive）** 信息，计算当前帧的平均照度
			//为后续的 ** 自动曝光（Auto Exposure）** 后处理 Pass 提供数据，让相机能根据场景亮度动态调整曝光值，保证画面亮度舒适。
			// Extract emissive from SceneColor (before lighting is applied)
			//注释强调：这一步必须在光照应用完成后、最终后处理前执行，以获取准确的场景亮度信息。
			ExposureIlluminanceSetup = AddSetupExposureIlluminancePass(GraphBuilder, Views, SceneTextures);
		}
		
		//光源剔除可视化
		//创建一张与场景颜色纹理格式一致的新纹理 VisualizeLightCullingTexture，专门用于可视化调试。
		//清空这张纹理为透明色，准备绘制调试信息。
		//将场景颜色目标临时切换为这张调试纹理，让后续的光源剔除可视化 Pass 能直接绘制到上面。
		if (ViewFamily.EngineShowFlags.VisualizeLightCulling)
		{
			FRDGTextureRef VisualizeLightCullingTexture = GraphBuilder.CreateTexture(SceneTextures.Color.Target->Desc, TEXT("SceneColorVisualizeLightCulling"));
			AddClearRenderTargetPass(GraphBuilder, VisualizeLightCullingTexture, FLinearColor::Transparent);
			SceneTextures.Color.Target = VisualizeLightCullingTexture;

			// When not in MSAA, assign to both targets.
			//非 MSAA 模式下，同时将 Resolve 目标也指向它，保证单采样渲染时输出正确。
			if (SceneTexturesConfig.NumSamples == 1)
			{
				SceneTextures.Color.Resolve = SceneTextures.Color.Target;
			}
		}

		if (bUseGBuffer)//启用 G-Buffer
		{
			// mark GBufferA for saving for next frame if it's needed
			//标记 GBufferA 用于下一帧保存
			//GBufferA 是 G-Buffer 中的一张纹理，通常存储世界空间法线（World-Space Normals）（或压缩后的法线数据）。
			//如果下一帧需要用到当前帧的法线数据（比如 TAA 重投影、运动矢量计算），就标记这张纹理需要被保存到历史缓冲中，避免被下一帧的渲染覆盖。
			ExtractNormalsForNextFrameReprojection(GraphBuilder, SceneTextures, Views);//提取法线用于下一帧重投影
			//重投影（Reprojection）：是 TAA（时间抗锯齿）、Lumen 等技术的核心步骤，通过将上一帧的像素数据「重投影」到当前帧，实现多帧采样混合，提升画面质量和性能。
		}

		// Rebuild scene textures to include GBuffers.
		//重建场景纹理以包含 G-Buffers
		SceneTextures.SetupMode |= ESceneTextureSetupMode::GBuffers;//在场景纹理的 SetupMode 中添加 GBuffers 标记，告知渲染系统需要创建并管理 G-Buffer 相关纹理（法线、BaseColor、Roughness 等材质属性）。
		
		//场景速度纹理配置
		//添加 SceneVelocity 标记，创建场景速度纹理，存储每个像素的运动矢量，用于 TAA 重投影、运动模糊等时间性效果。
		if (bShouldRenderVelocities && (bBasePassCanOutputVelocity || Scene->EarlyZPassMode == DDM_AllOpaqueNoVelocity))
		{
			SceneTextures.SetupMode |= ESceneTextureSetupMode::SceneVelocity;
		}
		//创建场景纹理 Uniform Buffer
		//根据当前 SetupMode（包含 G-Buffers/SceneVelocity 等标记），创建场景纹理的 Uniform Buffer，将所有场景纹理（颜色、深度、G-Buffer、速度等）绑定到着色器可访问的资源视图。
		//让后续所有着色器 Pass（Base Pass、光照 Pass、后处理）都能统一采样场景纹理数据。
		SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);

		if (bRealTimeSkyCaptureEnabled)//启用了实时天空捕获
		{
			Scene->ValidateSkyLightRealTimeCapture(GraphBuilder, Views[0], SceneTextures.Color.Target);//执行天空捕获的渲染 Pass，将当前天空环境捕获到立方体贴图，供场景光照采样。
			//保证动态天空（如时间流逝、云运动）能正确反映到场景的间接光照和反射中。
		}

		VisualizeVolumetricLightmap(GraphBuilder, SceneTextures);//添加体积光照贴图的可视化 Pass，在场景上叠加调试信息（如光照探针密度、间接光照强度、体积纹理分辨率等）。

		// Occlusion after base pass
		if (!bOcclusionBeforeBasePass)
		{
			FroxelRenderer = RenderOcclusionLambda();
		}

		// End occlusion after base
		//这是「Base Pass 之后执行遮挡」的分支。

		if (!bUseGBuffer)//遮挡渲染放在 Base Pass 之后 时执行
		{
			AddResolveSceneColorPass(GraphBuilder, Views, SceneTextures.Color);
		}

		// Render hair
		if (bHairStrandsEnable && !IsForwardShadingEnabled(ShaderPlatform))//毛发 Strands 渲染（延迟渲染专属）
		{
			RDG_EVENT_SCOPE(GraphBuilder, "Hair");

			RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessStrandsInterpolation, HairStrandsBookmarkParameters);
			if (!bHasRayTracedOverlay)
			{
				RenderHairPrePass(GraphBuilder, Scene, SceneTextures, Views, InstanceCullingManager, HairStrandsBookmarkParameters.CullingResults);//毛发预通，写入深度和模板数据，为后续 Base Pass 做遮挡和排序准备。
				RenderHairBasePass(
					GraphBuilder,						//FRDGBuilder& GraphBuilder,                // RDG 渲染依赖图构建器，用于管理渲染 Pass 与资源
					Scene,									// FScene* Scene,                            // 场景指针，包含所有场景对象与光照数据
					SceneTextures,							// const FSceneTextures& SceneTextures,      // 场景纹理集合（颜色、深度、G-Buffer 等）
					Views,								//TArray<FViewInfo>& Views,                // 视图列表，包含相机位置、投影矩阵等信息
					InstanceCullingManager);				//FInstanceCullingManager& InstanceCullingManager // 实例剔除管理器，用于跳过不可见毛发实例
				//毛发基础着色 Pass，写入颜色、光照和半透明混合数据，实现真实的毛发光照与半透明效果。
			}
		}
		
		//异质体积阴影（Heterogeneous Volume Shadows）
		if (ShouldRenderHeterogeneousVolumes(Scene) && !bHasRayTracedOverlay)
			//检查当前场景是否包含异质体积（如体积云、体积雾、粒子体积等），这类介质的密度 / 属性在空间中不均匀分布。&& 避免在光线追踪调试覆盖层（如 RT 视图、调试 UI）时执行，保证调试视图干净，不被体积阴影干扰。
		{
			RenderHeterogeneousVolumeShadows(GraphBuilder, SceneTextures);//向 RDG 提交体积阴影渲染 Pass
		}

		// Post base pass for material classification
		//Base Pass 后的材质分类处理
		// This needs to run before virtual shadow map, in order to have ready&cleared classified SSS data
		//必须在 VSM 之前执行，以确保 ** 次表面散射（SSS）** 的分类数据已准备就绪并清空，避免阴影计算时使用脏数据。
		if (Substrate::IsSubstrateEnabled() && !bHasRayTracedOverlay)//启用Substrate 时执行
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, Substrate, "Substrate");
			RDG_GPU_STAT_SCOPE(GraphBuilder, Substrate);

			Substrate::AddSubstrateMaterialClassificationPass(GraphBuilder, SceneTextures, DBufferTextures, Views);
			Substrate::AddSubstrateDBufferPass(GraphBuilder, SceneTextures, DBufferTextures, Views);
			Substrate::AddSubstrateSampleMaterialPass(GraphBuilder, Scene, SceneTextures, Views);
		}

		// Copy lighting channels out of stencil before deferred decals which overwrite those values
		//在 ** 延迟贴花（Deferred Decals）覆盖模板缓冲区（Stencil Buffer）之前，将其中存储的光照通道（Lighting Channels）** 信息复制出来，避免贴花渲染后丢失光照通道数据。
		TArray<FRDGTextureRef, TInlineAllocator<2>> NaniteShadingMask;//Nanite 着色遮罩收集
		if (bNaniteEnabled && Views.Num() > 0)
		{
			check(Views.Num() == NaniteRasterResults.Num());
			for (const Nanite::FRasterResults& Results : NaniteRasterResults)
			{
				NaniteShadingMask.Add(Results.ShadingMask);
			}
		}
		FRDGTextureRef LightingChannelsTexture = CopyStencilToLightingChannelTexture(GraphBuilder, SceneTextures.Stencil, NaniteShadingMask);//复制模板缓冲区到光照通道纹理

		// Single layer water depth prepass. Needs to run before VSM page allocation.
		//执行 ** 单层水（Single Layer Water）** 的深度预通 Pass。
		//必须在 VSM（虚拟阴影贴图）页分配 之前执行，确保 VSM 能正确采样到水的深度数据，避免水面对阴影的遮挡计算出错。
		if (bShouldRenderSingleLayerWaterDepthPrepass && SingleLayerWaterPrepassLocation == ESingleLayerWaterPrepassLocation::AfterBasePass)
		{
			SingleLayerWaterPrePassResult = RenderSingleLayerWaterDepthPrepass(GraphBuilder, Views, SceneTextures, SingleLayerWaterPrepassLocation, NaniteRasterResults);
		}

		FAsyncLumenIndirectLightingOutputs AsyncLumenIndirectLightingOutputs;

		GraphBuilder.FlushSetupQueue();

		// Shadows, lumen and fog after base pass
		//此阶段负责 Base Pass 之后的阴影、Lumen 全局光照和雾效计算。
		if (!bHasRayTracedOverlay)
		{
#if RHI_RAYTRACING
			// When Lumen HWRT is running async we need to wait for ray tracing scene before dispatching the work
			//当 Lumen 启用硬件光线追踪异步计算模式时，必须先等待光线追踪场景（Acceleration Structure）准备就绪，才能分发后续的光照 / 阴影工作。
			if (bNeedToSetupRayTracingRenderingData && Lumen::UseAsyncCompute(ViewFamily))
			{
				SetupRayTracingRenderingData(GraphBuilder);
				bNeedToSetupRayTracingRenderingData = false;
			}
#endif // RHI_RAYTRACING
			//将 Lumen 间接光照计算分发到 异步 Compute 队列，与图形队列（Graph Queue）的不透明渲染并行执行。
			DispatchAsyncLumenIndirectLightingWork(
				GraphBuilder,
				CompositionLighting,
				SceneTextures,
				InstanceCullingManager,
				LumenFrameTemporaries,
				InitViewTaskDatas.DynamicShadows,
				LightingChannelsTexture,
				AsyncLumenIndirectLightingOutputs);

			// Kick off volumetric clouds async dispatch after Lumen
			// Lumen has a dependency on the opaque so should run first
			//Lumen 依赖不透明几何体数据，必须先分发异步任务。
			// Volumetric Clouds have a depedency on translucent, so should run second and overlap opaque work after Lumen async is done
			//体积云依赖半透明几何体数据，在 Lumen 异步任务启动后分发，可与后续不透明工作重叠执行，最大化 GPU 利用率。
			if (bShouldRenderVolumetricCloud && bAsyncComputeVolumetricCloud)
			{
				GenerateQuarterResDepthMinMaxTexture(GraphBuilder, Views, SceneTextures.Depth.Resolve);//体积云深度预处理
				
				//体积云异步渲染
				bool bSkipVolumetricRenderTarget = false;
				bool bSkipPerPixelTracing = true;
				RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing,
					HalfResolutionDepthCheckerboardMinMaxTexture, QuarterResolutionDepthMinMaxTexture, true, InstanceCullingManager);
			}

			// If we haven't already rendered shadow maps, render them now (due to forward shading or r.shadow.ShadowMapsRenderEarly)
			//如果阴影贴图尚未提前渲染（前向渲染路径或关闭了 r.shadow.ShadowMapsRenderEarly），则在此阶段执行阴影渲染。
			if (!bShadowMapsRenderedEarly)
			{	//VSM 渲染回调封装
				// We use this somewhat awkward callback to get this into the right scope & place in the timeline (relying on some syncs which we can probably clean up in the future).
				auto RenderVirtualShadowMapsFunc = [&](bool bNaniteEnabled)
				{
					FFrontLayerTranslucencyData FrontLayerTranslucencyData;
					if (ShadowSceneRenderer.GetVirtualShadowMapArray().IsEnabled())
					{
						FrontLayerTranslucencyData = RenderFrontLayerTranslucency(GraphBuilder, Views, SceneTextures, true /*VSM page marking*/);
					}
					ShadowSceneRenderer.RenderVirtualShadowMaps(GraphBuilder, bNaniteEnabled, SingleLayerWaterPrePassResult, FrontLayerTranslucencyData, FroxelRenderer);
				};
				//阴影深度贴图渲染
				RenderShadowDepthMaps(GraphBuilder, InitViewTaskDatas.DynamicShadows, InstanceCullingManager, ExternalAccessQueue, RenderVirtualShadowMapsFunc);
			}
			CheckShadowDepthRenderCompleted();

#if RHI_RAYTRACING
			// Lumen scene lighting requires ray tracing scene to be ready if HWRT shadows are desired
			if (bNeedToSetupRayTracingRenderingData && Lumen::UseHardwareRayTracedSceneLighting(ViewFamily))
			{
				SetupRayTracingRenderingData(GraphBuilder);
				bNeedToSetupRayTracingRenderingData = false;
			}
#endif // RHI_RAYTRACING
		}

		ExternalAccessQueue.Submit(GraphBuilder);

		// End shadow and fog after base pass
		//基础 Pass 之后阴影与雾效渲染流程的结束点

		if (bNaniteEnabled)
		{
			// Needs doing after shadows such that the checks for shadow atlases etc work.
			Nanite::ListStatFilters(this);

			if (GNaniteShowStats != 0)
			{
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					const FViewInfo& View = Views[ViewIndex];
					if (IStereoRendering::IsAPrimaryView(View))
					{
						Nanite::PrintStats(GraphBuilder, View);
					}
				}
			}
		}

		if (bUpdateNaniteStreaming)
		{
			Nanite::GStreamingManager.SubmitFrameStreamingRequests(GraphBuilder);
		}

		{
			if (FVirtualShadowMapArrayCacheManager* CacheManager = VirtualShadowMapArray.CacheManager)
			{
				// Do this even if VSMs are disabled this frame to clean up any previously extracted data
				CacheManager->ExtractFrameData(
					GraphBuilder,
					VirtualShadowMapArray,
					*this,
					ViewFamily.EngineShowFlags.VirtualShadowMapPersistentData);
			}
		}

		if (CustomDepthPassLocation == ECustomDepthPassLocation::AfterBasePass)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_CustomDepthPass_AfterBasePass);
			if (RenderCustomDepthPass(GraphBuilder, SceneTextures.CustomDepth, SceneTextures.GetSceneTextureShaderParameters(FeatureLevel), NaniteRasterResults, PrimaryNaniteViews))
			{
				SceneTextures.SetupMode |= ESceneTextureSetupMode::CustomDepth;
				SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);
			}
		}

		// If we are not rendering velocities in depth or base pass then do that here.
		//注释说明：如果深度 Pass 或基础 Pass 没有负责渲染运动矢量，就由这段代码完成 Velocity 渲染。
		if (bShouldRenderVelocities && !bBasePassCanOutputVelocity && (Scene->EarlyZPassMode != DDM_AllOpaqueNoVelocity))
		{
			RenderVelocities(GraphBuilder, Views, SceneTextures, EVelocityPass::Opaque, bHairStrandsEnable);
		}

		// Pre-lighting composition lighting stage
		//光照前合成光照阶段
		// e.g. deferred decals, SSAO
		{
			RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, AfterBasePass);
			SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_AfterBasePass);

			if (!IsForwardShadingEnabled(ShaderPlatform))
			{
				AddResolveSceneDepthPass(GraphBuilder, Views, SceneTextures.Depth);//添加一个解析（Resolve）场景深度的 Pass，将 MSAA 深度缓冲区解析为单采样深度纹理，供后续贴花、SSAO 等效果使用。
			}

			const FCompositionLighting::EProcessAfterBasePassMode Mode = AsyncLumenIndirectLightingOutputs.bHasDrawnBeforeLightingDecals ?
				FCompositionLighting::EProcessAfterBasePassMode::SkipBeforeLightingDecals : FCompositionLighting::EProcessAfterBasePassMode::All;
			// 执行合成光照后处理
			CompositionLighting.ProcessAfterBasePass(GraphBuilder, InstanceCullingManager, Mode, Scene->SubstrateSceneData);
		}

		// Rebuild scene textures to include velocity, custom depth, and SSAO.
		//将 SetupMode 设为 All，重建场景纹理集合，包含运动矢量（Velocity）、自定义深度（Custom Depth）、SSAO 等后续光照 / 后处理所需的纹理。
		SceneTextures.SetupMode |= ESceneTextureSetupMode::All;
		SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);

		if (!IsForwardShadingEnabled(ShaderPlatform))
		{
			// Clear stencil to 0 now that deferred decals are done using what was setup in the base pass.
			//注释说明：延迟贴花（Deferred Decals）已经使用完 Base Pass 中设置的模板缓冲数据，现在需要将模板缓冲清零。
			AddClearStencilPass(GraphBuilder, SceneTextures.Depth.Target);
		}

#if RHI_RAYTRACING
		// If Lumen did not force an earlier ray tracing scene sync, we must wait for it here.
		//注释说明：如果 Lumen 全局光照没有提前触发光线追踪场景同步，就必须在当前位置完成同步，保证后续光追操作（如 Lumen HWRT、光线追踪阴影）能访问到最新的场景加速结构。
		if (bNeedToSetupRayTracingRenderingData)
		{
			SetupRayTracingRenderingData(GraphBuilder);
			bNeedToSetupRayTracingRenderingData = false;
		}
#endif // RHI_RAYTRACING
		//刷新渲染图（RDG）的设置队列，将之前所有 pending 的 Pass 与资源创建操作提交，确保后续光照计算依赖的资源已就绪。
		GraphBuilder.FlushSetupQueue();

		if (bRenderDeferredLighting)
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, RenderDeferredLighting, "RenderDeferredLighting");
			RDG_GPU_STAT_SCOPE(GraphBuilder, RenderDeferredLighting);
			RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderLighting);

			SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_Lighting);
			SCOPED_NAMED_EVENT(RenderLighting, FColor::Emerald);
			
			//声明一个数组，用于存储 ** 动态弯曲法线 AO（Bent Normal AO）** 纹理引用，后续会用于存储环境光遮蔽计算结果，供光照计算使用。
			TArray<FRDGTextureRef> DynamicBentNormalAOTextures;
			
			//
			//计算漫反射间接光照（含 Lumen 全局光照）与环境光遮蔽（AO）
			//
			RenderDiffuseIndirectAndAmbientOcclusion(
				GraphBuilder,							//渲染图构建器，用于编排光照 Pass 依赖。
				SceneTextures,							//场景纹理集合（GBuffer、深度、Velocity 等）。
				LumenFrameTemporaries,					//Lumen 每帧临时资源（如光照探针、历史缓冲）。
				LightingChannelsTexture,					//光照通道纹理，用于控制对象受光照影响的范围。
				/* bCompositeRegularLumenOnly = */ false,	//是否仅合成常规 Lumen 光照（false 表示完整合成）。
				/* bIsVisualizePass = */ false,				//是否为可视化调试 Pass（false 表示正常渲染）。
				AsyncLumenIndirectLightingOutputs);		//Lumen 异步间接光照输出结果，用于合并到最终光照中。

			if (IsTranslucencyLightingVolumeUsingVoxelMarking())//半透明反射的体素标记
			{
				for (FViewInfo& View : Views)
				{
					if (View.TranslucencyVolumeMarkData[0].MarkTexture == nullptr || View.TranslucencyVolumeMarkData[1].MarkTexture == nullptr)
					{
						LumenTranslucencyReflectionsMarkUsedProbes(//执行 Lumen 半透明反射的 “探针使用标记” Pass，通过体素化半透明物体，标记出反射计算实际需要访问的光照探针。
							GraphBuilder,
							*this,
							View,
							SceneTextures,
							nullptr);
					}
				}
			}
			
			//间接胶囊阴影渲染
			// These modulate the scenecolor output from the basepass, which is assumed to be indirect lighting
			//注释说明：这段代码会调制 Base Pass 输出的场景颜色（此时场景颜色已包含间接光照），为间接光照叠加胶囊体阴影效果。
			RenderIndirectCapsuleShadows(GraphBuilder, SceneTextures);
			
			//DFAO 作为间接阴影渲染
			// These modulate the scene color output from the base pass, which is assumed to be indirect lighting
			//注释说明：同样是调制 Base Pass 输出的间接光照场景颜色，将 DFAO（Directional Field Ambient Occlusion，方向场环境光遮蔽）作为间接阴影使用。
			RenderDFAOAsIndirectShadowing(GraphBuilder, SceneTextures, DynamicBentNormalAOTextures);
			
			//半透明光照体积初始化（清理）
			// Clear the translucent lighting volumes before we accumulate
			//注释说明：在累积半透明光照之前，先清理半透明光照体积。
			if ((GbEnableAsyncComputeTranslucencyLightingVolumeClear && GSupportsEfficientAsyncCompute) == false)
			{
				TranslucencyLightingVolumeTextures.Init(GraphBuilder, Views, ERDGPassFlags::Compute);
			}

#if RHI_RAYTRACING
			// Only used by ray traced shadows
			//仅用于光线追踪阴影
			if (IsRayTracingEnabled() && Views[0].bHasRayTracingShadows && Views[0].IsRayTracingAllowedForView())
			{
				RenderDitheredLODFadingOutMask(GraphBuilder, Views[0], SceneTextures.Depth.Target);
			}
#endif
			
			//半透明光照体素标记收集
			GatherTranslucencyVolumeMarkedVoxels(GraphBuilder);

			const FSortedLightSetSceneInfo& SortedLightSet = *GatherAndSortLightsTask.GetResult();//获取异步排序后的光照数据

			// [LightingShadowBuffer] Clear the complete accumulation textures before any
			// deferred light writes. The targets are pooled at Config.Extent, while a
			// view can occupy only a sub-rect after screen percentage or editor resize.
			// Clearing only View.ViewRect leaves pooled pixels outside that rect alive;
			// the later debug/presentation pass then exposes those pixels as moving
			// rectangles when the viewport changes.
			// [DeferredToon] SpecularBuffer is an ELoad MRT just like LightingBuffer
			// and ShadowBuffer; initialize it every view or DebugView 18 can read
			// pooled/stale rows as horizontal corruption.
			if (IsLightingShadowBuffersEnabled() && SceneTextures.LightingBuffer && SceneTextures.ShadowBuffer && SceneTextures.SpecularBuffer && SceneTextures.EffectiveLightBuffer)
			{
				bool bClearedWholeBuffer = false;
				for (const FViewInfo& View : Views)
				{
					if (!bClearedWholeBuffer)
					{
						AddClearRenderTargetPass(GraphBuilder, SceneTextures.LightingBuffer, FLinearColor::Transparent);
						AddClearRenderTargetPass(GraphBuilder, SceneTextures.ShadowBuffer, FLinearColor::Transparent);
						AddClearRenderTargetPass(GraphBuilder, SceneTextures.SpecularBuffer, FLinearColor::Transparent);
						// [DeferredToon] Initialize the effective input for full and partial views.
						AddClearRenderTargetPass(GraphBuilder, SceneTextures.EffectiveLightBuffer, FLinearColor::Transparent);
						bClearedWholeBuffer = true;
					}
					else
					{
						// Additional views own separate rectangles in the same target.
						// Preserve the first view while initializing only this view's area.
						AddClearRenderTargetPass(GraphBuilder, SceneTextures.LightingBuffer, FLinearColor::Transparent, View.ViewRect);
						AddClearRenderTargetPass(GraphBuilder, SceneTextures.ShadowBuffer, FLinearColor::Transparent, View.ViewRect);
						AddClearRenderTargetPass(GraphBuilder, SceneTextures.SpecularBuffer, FLinearColor::Transparent, View.ViewRect);
						// [DeferredToon] Initialize the effective input for full and partial views.
						AddClearRenderTargetPass(GraphBuilder, SceneTextures.EffectiveLightBuffer, FLinearColor::Transparent, View.ViewRect);
					}
				}
			}

			RenderLights(// 常规直接光照渲染
				GraphBuilder,				//渲染图构建器，用于向 RDG 管线中添加光照 Pass、编排资源依赖关系
				SceneTextures,				//最小化场景纹理集合，包含 GBuffer（BaseColor/Normal/Depth/Roughness 等），是光照计算的核心输入
				LightingChannelsTexture,		//光照通道纹理，用于控制对象受哪些光源影响（实现 “光照层” 功能，比如只让特定对象被某类光源照亮）
				SortedLightSet);				//排序后的光照集合，包含按优先级 / 影响范围排序的光源列表，是异步收集与排序的结果
			//遍历排序后的光源集合，为场景中不透明物体渲染直接光照（包括方向光、点光、聚光灯等）。
			
			///////////////////////////////////////////custom/////////////////
			RenderToonOutlinePass(GraphBuilder, SceneTextures);
			/////////////////////////////////////////////////////////////////////////

			if (SortedLightSet.MegaLightsLightStart < SortedLightSet.SortedLights.Num())//megalight渲染
			{
				RenderMegaLights(
					GraphBuilder,
					SceneTextures,
					LightingChannelsTexture,
					SortedLightSet);
			}

			// Copy depth history without water and translucency for ray traced lighting denoising
			//注释说明：为光线追踪光照降噪，拷贝不含水和半透明物体的深度历史数据。
			StoreStochasticLightingSceneHistory(GraphBuilder, LumenFrameTemporaries, SceneTextures);
//////////////////////custom////////////////////////
			// Toon Light Pass Step 6-2
			{
				RenderToonLightPass(GraphBuilder, SceneTextures);
			}
///////////////////////end////////////////////////			
			RenderTranslucencyLightingVolume(GraphBuilder, TranslucencyLightingVolumeTextures, SortedLightSet);//半透明光照体积渲染
			//遍历 SortedLightSet 中的光源，计算每个光源对半透明物体的光照贡献。
			//将光照结果写入 TranslucencyLightingVolumeTextures（半透明光照体积纹理）。
			//后续半透明物体渲染时，会采样该体积纹理，实现半透明物体的全局光照效果。

			// Do DiffuseIndirectComposite after Lights so that async Lumen work can overlap
			//注释说明：在直接光照之后执行间接光照合成，使 Lumen 异步计算能与主线程工作重叠，提升并行效率。
			//漫反射间接光照与 AO 合成（异步 Lumen 优化）
			RenderDiffuseIndirectAndAmbientOcclusion(
				GraphBuilder,
				SceneTextures,
				LumenFrameTemporaries,
				LightingChannelsTexture,
				/* bCompositeRegularLumenOnly = */ true,
				/* bIsVisualizePass = */ false,
				AsyncLumenIndirectLightingOutputs);
			//
			//延迟反射与天空光照渲染
			//
			// Render diffuse sky lighting and reflections that only operate on opaque pixels
			//注释说明：仅对不透明像素渲染漫反射天空光照与反射效果。
			//基于 GBuffer 与深度纹理，计算不透明物体的漫反射天空光照（Sky Lighting）与延迟反射（如 SSR、Lumen 反射）。
			//利用 LumenFrameTemporaries 中的历史数据做时间累积，提升反射稳定性。
			//结合 DynamicBentNormalAOTextures 中的 AO 信息，对光照与反射做遮蔽，增强真实感。
			RenderDeferredReflectionsAndSkyLighting(GraphBuilder, SceneTextures, LumenFrameTemporaries, DynamicBentNormalAOTextures);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			// Renders debug visualizations for global illumination plugins
			RenderGlobalIlluminationPluginVisualizations(GraphBuilder, LightingChannelsTexture);
#endif
			//
			//次表面散射 Pass
			//
			AddSubsurfacePass(
				GraphBuilder,
				SceneTextures, 
				Views);//基于 GBuffer 中的次表面散射参数（散射半径、颜色等），计算光线在材质内部的散射分布。
			
			//
			// Substrate 不透明粗糙折射 Pass
			//
			Substrate::AddSubstrateOpaqueRoughRefractionPasses(GraphBuilder, SceneTextures, Views);

			{
				//渲染毛发 strands 的场景颜色散射效果，处理毛发的自阴影、次表面散射与光线透射。
				RenderHairStrandsSceneColorScattering(GraphBuilder, SceneTextures.Color.Target, Scene, Views);//毛发 strands 颜色散射
			}

		#if RHI_RAYTRACING//光追计算天空光照，结果合成到场景
			if (ShouldRenderRayTracingSkyLight(Scene->SkyLight, Scene->GetShaderPlatform()) 
				//@todo - integrate RenderRayTracingSkyLight into RenderDiffuseIndirectAndAmbientOcclusion
				&& GetViewPipelineState(Views[0]).DiffuseIndirectMethod != EDiffuseIndirectMethod::Lumen
				&& ViewFamily.EngineShowFlags.GlobalIllumination)
			{
				FRDGTextureRef SkyLightTexture = nullptr;
				FRDGTextureRef SkyLightHitDistanceTexture = nullptr;
				RenderRayTracingSkyLight(GraphBuilder, SceneTextures.Color.Target, SkyLightTexture, SkyLightHitDistanceTexture);
				CompositeRayTracingSkyLight(GraphBuilder, SceneTextures, SkyLightTexture, SkyLightHitDistanceTexture);
			}
		#endif

			if (Substrate::IsSubstrateEnabled())//Substrate 材质启用时的模板缓冲清理
			{
				// Now remove all the Substrate tile stencil tags used by deferred tiled light passes. Make later marks such as responssive AA works.
				AddClearStencilPass(GraphBuilder, SceneTextures.Depth.Target);
			}
		}
		else if (HairStrands::HasViewHairStrandsData(Views) && ViewFamily.EngineShowFlags.Lighting)//毛发渲染时的专属光照 / 反射处理
		{
			const FSortedLightSetSceneInfo& SortedLightSet = *GatherAndSortLightsTask.GetResult();
			RenderLightsForHair(GraphBuilder, SceneTextures, SortedLightSet, ForwardScreenSpaceShadowMaskHairTexture, LightingChannelsTexture);
			RenderDeferredReflectionsAndSkyLightingHair(GraphBuilder);
		}

		// Volumetric fog after Lumen GI and shadow depths
		//体积雾在 Lumen 全局光照（GI）和阴影深度计算完成后执行，保证体积效果能正确采样光照 / 阴影数据；
		if (!IsForwardShadingEnabled(ShaderPlatform) && !bHasRayTracedOverlay)
		{
			ComputeVolumetricFog(GraphBuilder, SceneTextures);//体积雾计算（Volumetric Fog）
		}

		if (ShouldRenderHeterogeneousVolumes(Scene) && !bHasRayTracedOverlay)
		{
			RenderHeterogeneousVolumes(GraphBuilder, SceneTextures);//异质体积渲染（Heterogeneous Volumes）
		}

		GraphBuilder.FlushSetupQueue();//刷新渲染图的设置队列，将体积雾 / 异质体积相关的 Pass 与资源创建操作提交，确保后续渲染步骤能依赖到最新的体积效果数据。

		//体积云（Volumetric Cloud）
		if (bShouldRenderVolumetricCloud && !bHasRayTracedOverlay)
		{
			if (!bAsyncComputeVolumetricCloud)
			{
				if(IsVolumetricRenderTargetEnabled())
				{
					GenerateQuarterResDepthMinMaxTexture(GraphBuilder, Views, SceneTextures.Depth.Resolve);
				}

				// Generate the volumetric cloud render target
				bool bSkipVolumetricRenderTarget = false;//不跳过体积云渲染目标的生成；
				bool bSkipPerPixelTracing = true;//跳过逐像素光线追踪
				//体积云渲染
				RenderVolumetricCloud(
					GraphBuilder, 
					SceneTextures, bSkipVolumetricRenderTarget, 
					bSkipPerPixelTracing,
					HalfResolutionDepthCheckerboardMinMaxTexture, 
					QuarterResolutionDepthMinMaxTexture, 
					//关闭异步计算
					false, 
					InstanceCullingManager);
			}
			// Reconstruct the volumetric cloud render target to be ready to compose it over the scene
			//重建体积云渲染目标，使其能够被合成到场景画面之上
			ReconstructVolumetricRenderTarget(GraphBuilder, Views, SceneTextures.Depth.Resolve, HalfResolutionDepthCheckerboardMinMaxTexture, bAsyncComputeVolumetricCloud);
		}
		
		//TSR部分
		//针对性解决 TSR 超分过程中可能出现的摩尔纹、闪烁问题，通过提前提取亮度特征做预处理
		//按视图维度独立处理，适配多视图 / 多玩家场景；
		//仅在必要场景（无光追叠加、需抗闪烁）执行，避免冗余计算，平衡性能与效果。
		TArray<FScreenPassTexture, TInlineAllocator<4>> TSRFlickeringInputTextures;
		if (!bHasRayTracedOverlay)
		{
			// Extract TSR's moire heuristic luminance before rendering translucency into the scene color.
			//在将半透明物体渲染到场景颜色纹理之前，提取TSR（时间超分）的摩尔纹启发式亮度数据。
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				FViewInfo& View = Views[ViewIndex];
				if (NeedTSRAntiFlickeringPass(View))
				{
					if (TSRFlickeringInputTextures.Num() == 0)
					{
						TSRFlickeringInputTextures.SetNum(Views.Num());
					}

					TSRFlickeringInputTextures[ViewIndex] = AddTSRMeasureFlickeringLuma(GraphBuilder, View.ShaderMap, FScreenPassTexture(SceneTextures.Color.Target, View.ViewRect));
				}
			}
		}
		
		
		
		
		//
		//半透明//
		//
		const bool bShouldRenderTranslucency = !bHasRayTracedOverlay && ShouldRenderTranslucency();//当 “无光追叠加层干扰” 且 “场景确实需要半透明渲染” 时，才开启后续半透明渲染流程

		// Union of all translucency view render flags.
		//所有半透明视图渲染标志的合集；
		ETranslucencyView TranslucencyViewsToRender = bShouldRenderTranslucency ? GetTranslucencyViews(Views) : ETranslucencyView::None;

		FTranslucencyPassResourcesMap TranslucencyResourceMap(Views.Num());

		const bool bIsCameraUnderWater = EnumHasAnyFlags(TranslucencyViewsToRender, ETranslucencyView::UnderWater);//水下渲染
		FRDGTextureRef LightShaftOcclusionTexture = nullptr;
		const bool bShouldRenderSingleLayerWater = !bHasRayTracedOverlay && ShouldRenderSingleLayerWater(Views);
		FSceneWithoutWaterTextures SceneWithoutWaterTextures;
		auto RenderLightShaftSkyFogAndCloud = [&]()
		{
			// Draw Lightshafts
			//光轴（LightShaft）
			if (!bHasRayTracedOverlay && ViewFamily.EngineShowFlags.LightShafts)
			{
				SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderLightShaftOcclusion);
				LightShaftOcclusionTexture = RenderLightShaftOcclusion(GraphBuilder, SceneTextures);
			}

			// Draw the sky atmosphere
			//天空大气（Sky Atmosphere）
			if (!bHasRayTracedOverlay && bShouldRenderSkyAtmosphere && !IsForwardShadingEnabled(ShaderPlatform))
			{
				SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderSkyAtmosphere);
				RenderSkyAtmosphere(GraphBuilder, SceneTextures);
			}

			// Draw fog.
			//雾效
			bool bHeightFogHasComposedLocalFogVolume = false;
			if (!bHasRayTracedOverlay && ShouldRenderFog(ViewFamily))
			{
				RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderFog);
				SCOPED_NAMED_EVENT(RenderFog, FColor::Emerald);
				SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderFog);
				const bool bFogComposeLocalFogVolumes = (bShouldRenderLocalFogVolumeInVolumetricFog && bShouldRenderVolumetricFog) || bShouldRenderLocalFogVolumeDuringHeightFogPass;
				RenderFog(GraphBuilder, SceneTextures, LightShaftOcclusionTexture, bFogComposeLocalFogVolumes);//雾效渲染执行与状态更新
				bHeightFogHasComposedLocalFogVolume = bFogComposeLocalFogVolumes;
			}

			if (!bHasRayTracedOverlay) 
			{
				// Local Fog Volumes (LFV) rendering order is first HeightFog, then LFV, then volumetric fog on top.
				//「局部雾体积的渲染顺序：先渲染高度雾，再渲染局部雾体积，最后渲染体积雾（叠在最上层）」；
				// LFVs are rendered as part of the regular height fog + volumetric fog pass when volumetric fog is enabled and it is requested to voxelise LFVs into volumetric fog.
				//当体积雾启用 且 要求将局部雾体积体素化到体积雾中时，局部雾体积会作为常规高度雾 + 体积雾 Pass 的一部分渲染
				// Otherwise, they are rendered in an independent pass (this for instance make it independent of the near clip plane optimization).
				//否则（体积雾未启用 / 不要求体素化），局部雾体积会在独立 Pass 中渲染（此举可使其不受近裁剪平面优化的影响
				if (!bHeightFogHasComposedLocalFogVolume)
				{
					RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderLocalFogVolume);
					SCOPED_NAMED_EVENT(RenderLocalFogVolume, FColor::Emerald);
					SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderLocalFogVolume);
					RenderLocalFogVolume(Scene, Views, ViewFamily, GraphBuilder, SceneTextures, LightShaftOcclusionTexture);
				}
				// Also compose on top the visualisation pass if enabled.
				//若启用了可视化 Pass，也将其合成到顶层；
				if (bShouldRenderLocalFogVolumeVisualizationPass)
				{
					RenderLocalFogVolumeVisualization(Scene, Views, ViewFamily, GraphBuilder, SceneTextures);
				}
			}

			// After the height fog, Draw volumetric clouds (having fog applied on them already) when using per pixel tracing,
			//「在高度雾渲染完成后，当使用逐像素追踪时，绘制体积云（体积云已叠加雾效）」；
			if (!bHasRayTracedOverlay && bShouldRenderVolumetricCloud)
			{
				bool bSkipVolumetricRenderTarget = true;
				bool bSkipPerPixelTracing = false;
				RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing,
					HalfResolutionDepthCheckerboardMinMaxTexture, QuarterResolutionDepthMinMaxTexture, false, InstanceCullingManager);
			}

			// Or composite the off screen buffer over the scene.
			//或者将离屏缓冲区（体积云渲染结果）合成到场景之上
			if (bVolumetricRenderTargetRequired)
			{
				const bool bComposeWithWater = bIsCameraUnderWater ? false : bShouldRenderSingleLayerWater;
				ComposeVolumetricRenderTargetOverScene(
					GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures.Depth.Target,
					bComposeWithWater,
					SceneWithoutWaterTextures, SceneTextures);
			}
		};

		if (bShouldRenderSingleLayerWater)//UE 单层水体 + 水下半透明渲染逻辑
		{
			if (bIsCameraUnderWater)
			{
				RenderLightShaftSkyFogAndCloud();//水下场景的光轴 / 雾效会有特殊处理（如光线散射、雾效密度提升），此调用保证水下环境特效的完整性。

				RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderTranslucency);
				SCOPED_NAMED_EVENT(RenderTranslucency, FColor::Emerald);
				SCOPE_CYCLE_COUNTER(STAT_TranslucencyDrawTime);
				const bool bStandardTranslucentCanRenderSeparate = false;//禁用 “标准半透明物体的分离渲染”，强制水下半透明物体与场景合批渲染；
				FRDGTextureMSAA SharedDepthTexture;//初始化空的共享深度纹理，水下场景无需独立共享深度，仅作为参数占位符
				RenderTranslucency(
					*this, 
					GraphBuilder, 
					SceneTextures, 
					TranslucencyLightingVolumeTextures, 
					&TranslucencyResourceMap, 
					Views, 
					ETranslucencyView::UnderWater, 
					SeparateTranslucencyDimensions, 
					InstanceCullingManager, 
					bStandardTranslucentCanRenderSeparate, 
					SharedDepthTexture);
				EnumRemoveFlags(TranslucencyViewsToRender, ETranslucencyView::UnderWater);
			}

			RenderSingleLayerWater(//单层水体（Single Layer Water）渲染
				GraphBuilder, 
				Views, 
				SceneTextures, 
				SingleLayerWaterPrePassResult, 
				bShouldRenderVolumetricCloud, 
				SceneWithoutWaterTextures, 
				LumenFrameTemporaries, 
				bIsCameraUnderWater);

			// Replace main depth texture with the output of the SLW depth prepass which contains the scene + water.
			//「将主深度纹理替换为 SLW 深度预渲染通道的输出 —— 该输出包含了 “场景 + 水体” 的深度信息」；
			// Note: Stencil now has all water bits marked with 1. As long as no other passes after this point want to read the depth buffer,
			//注意：模板缓冲区（Stencil）中所有水体区域的位都被标记为 1。只要后续没有其他渲染通道需要读取深度缓冲区，此处无需清理模板缓冲区
			// a stencil clear should not be necessary here.
			if (SingleLayerWaterPrePassResult)
			{
				SceneTextures.Depth = SingleLayerWaterPrePassResult->DepthPrepassTexture;//将「场景默认深度纹理」替换为「水体预渲染通道输出的深度纹理」
				//原SceneTextures.Depth：仅包含 “剔除水体后的场景深度”（如地面、建筑的深度）；
				//替换后：包含 “场景深度 + 水体表面深度”（引擎能正确判断 “相机→水体→场景” 的深度层级）。
			}
		}

		// Rebuild scene textures to include scene color.
		//场景纹理统一缓冲区（Uniform Buffer）的重建
		SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);

		if (!bHasRayTracedOverlay)
		{
			// Extract TSR's thin geometry coverage after SLW but before rendering translucency into the scene color.
			//// 注释：在SLW（单层水体）渲染后、半透明物体渲染到场景颜色前，提取TSR的薄层几何覆盖率
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)// 遍历所有渲染视角（如主视角、阴影视角）
			{
				FViewInfo& View = Views[ViewIndex];
				if (NeedTSRAntiFlickeringPass(View))// 判断当前视角是否需要TSR抗闪烁通道
				{
					if (TSRFlickeringInputTextures.Num() == 0)// 初始化TSR抗闪烁输入纹理数组
					{
						TSRFlickeringInputTextures.SetNum(Views.Num());
					}
					// 核心操作：添加TSR薄层几何覆盖率测量通道
					AddTSRMeasureThinGeometryCoverage(GraphBuilder, View.ShaderMap, SceneTextures, TSRFlickeringInputTextures[ViewIndex]);
				}
			}
		}

		if (!bIsCameraUnderWater)// 前提：相机不在水下（水上视角）
		{
			RenderLightShaftSkyFogAndCloud();// 渲染光束、天空雾、云
		}

		FRDGTextureRef ExposureIlluminance = nullptr;// 初始化曝光照度纹理引用为空
		if (!bHasRayTracedOverlay)
		{   
			// 核心操作：添加“曝光照度计算通道”，并将结果赋值给ExposureIlluminance
			ExposureIlluminance = AddCalculateExposureIlluminancePass(GraphBuilder, Views, SceneTextures, TranslucencyLightingVolumeTextures, ExposureIlluminanceSetup);
		}
		
		
		//
		//不透明物体（Opaque）特效渲染
		//
		RenderOpaqueFX(GraphBuilder, GetSceneViews(), GetSceneUniforms(), FXSystem, FeatureLevel, SceneTextures.UniformBuffer);

		
		//获取 UE 渲染器模块的实例，并执行 “不透明物体渲染后” 的扩展逻辑
		FRendererModule& RendererModule = static_cast<FRendererModule&>(GetRendererModule());
		RendererModule.RenderPostOpaqueExtensions(GraphBuilder, Views, SceneTextures);
		
		//延迟 GPU 写入与实例剔除
		if (Scene->GPUScene.ExecuteDeferredGPUWritePass(GraphBuilder, Views, EGPUSceneGPUWritePass::PostOpaqueRendering))
		{
			InstanceCullingManager.BeginDeferredCulling(GraphBuilder);
		}
		
		//毛发渲染（半透明前阶段）
		if (GetHairStrandsComposition() == EHairStrandsCompositionType::BeforeTranslucent)
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, HairRendering, "HairRendering");
			RDG_GPU_STAT_SCOPE(GraphBuilder, HairRendering);
			RenderHairComposition(
				GraphBuilder,
				Views, 
				SceneTextures.Color.Target, //场景颜色纹理（毛发渲染会叠加到该纹理）
				SceneTextures.Depth.Target, //场景深度纹理（用于毛发的深度测试、遮挡计算）；
				SceneTextures.Velocity, //场景速度纹理（用于毛发的运动模糊计算）；
				TranslucencyResourceMap//半透明资源映射表（关联半透明渲染所需的资源，保证毛发与半透明物体的渲染顺序 / 资源兼容）
				);
		}

#if DEBUG_ALPHA_CHANNEL
		if (ShouldMakeDistantGeometryTranslucent())//调试 Alpha 通道（透明通道）
		{
			//将远距离几何体的渲染结果处理为半透明，并更新场景颜色纹理
			SceneTextures.Color = MakeDistanceGeometryTranslucent(GraphBuilder, Views, SceneTextures);
			//重新创建并更新场景纹理的统一缓冲区（Uniform Buffer），保证 GPU 能正确读取最新的场景纹理数据
			SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);
		}
#endif

		// Experimental voxel test code
		//实验性体素测试代码
		//Nanite 系统的实验性体素渲染逻辑
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
		
			Nanite::DrawVisibleBricks( GraphBuilder, *Scene, View, SceneTextures );
		}

		// Composite Heterogeneous Volumes
		//合成异质体积（Heterogeneous Volumes）
		//「异质体积」：区别于均匀体积（如纯色雾），指密度、颜色、透光性等属性随空间位置变化的体积效果（比如不均匀的烟雾、云层、火焰体积、复杂大气雾）；
		if (!bHasRayTracedOverlay && ShouldRenderHeterogeneousVolumes(Scene) &&
			(GetHeterogeneousVolumesComposition() == EHeterogeneousVolumesCompositionType::BeforeTranslucent))
		{
			CompositeHeterogeneousVolumes(GraphBuilder, SceneTextures);//异质体积雾合成
		}

		// Draw translucency.
		//绘制半透明物体
		FRDGTextureMSAA TranslucencySharedDepthTexture;
		if (!bHasRayTracedOverlay && TranslucencyViewsToRender != ETranslucencyView::None)
		{
			RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderTranslucency);
			SCOPED_NAMED_EVENT(RenderTranslucency, FColor::Emerald);
			SCOPE_CYCLE_COUNTER(STAT_TranslucencyDrawTime);

			RDG_EVENT_SCOPE(GraphBuilder, "Translucency");

			// Raytracing doesn't need the distortion effect.
			//注释说明：光线追踪模式不需要畸变效果（因为光追本身会处理折射 / 扭曲）
			const bool bShouldRenderDistortion = TranslucencyViewsToRender != ETranslucencyView::RayTracing && ShouldRenderDistortion();

			// Lumen/VSM translucent front layer
			//注释说明：这是Lumen/VSM 半透明前层的渲染逻辑（VSM = Virtual Shadow Maps，虚拟阴影贴图；Lumen 是 UE5 全局光照系统）
			FFrontLayerTranslucencyData FrontLayerTranslucencyData = RenderFrontLayerTranslucency(GraphBuilder, Views, SceneTextures, false /*VSM page marking*/);

#if RHI_RAYTRACING// ... 光线追踪半透明渲染逻辑
			if (EnumHasAnyFlags(TranslucencyViewsToRender, ETranslucencyView::RayTracing))
			{
				if (!RenderRayTracedTranslucency(GraphBuilder, SceneTextures, LumenFrameTemporaries, FrontLayerTranslucencyData))
				{
					RenderRayTracingTranslucency(GraphBuilder, SceneTextures.Color);
				}

				EnumRemoveFlags(TranslucencyViewsToRender, ETranslucencyView::RayTracing);
			}
#endif

			for (FViewInfo& View : Views)//Lumen 全局光照与半透明前层反射的渲染逻辑
			{
				if (GetViewPipelineState(View).ReflectionsMethod == EReflectionsMethod::Lumen)
				{
					RenderLumenFrontLayerTranslucencyReflections(GraphBuilder, View, SceneTextures, LumenFrameTemporaries, FrontLayerTranslucencyData);
				}
			}

			// Sort objects' triangles
			//对物体的三角形进行排序，为 OIT 渲染做准备
			//OIT 无序透明（Order Independent Transparency）
			//为什么需要 OIT?
			//传统光栅化半透明渲染（Order Dependent Transparency，ODT）的核心缺陷是依赖绘制顺序
			//半透明物体需要按「从后到前（Back to Front）」的顺序绘制，才能保证透明度叠加正确
			//但复杂场景中（如交叉的半透明物体、动态半透明模型），无法精准排序，会出现半透明物体 “穿透” 彼此的视觉错误等问题
			//OIT 的核心思路是 “先收集、后排序、再合成”
			
			for (FViewInfo& View : Views)//遍历场景中所有半透明物体，提取其三角形数据（顶点、深度、材质透明度等），存储到 OITSceneData
			{
				if (OIT::IsSortedTrianglesEnabled(View.GetShaderPlatform()))
				{	
					//添加三角形排序 Pass
					OIT::AddSortTrianglesPass(GraphBuilder, View, Scene->OITSceneData, FTriangleSortingOrder::BackToFront);
					//对收集到的所有半透明三角形，基于当前视口的相机位置，按「深度从后到前」排序
					//向 RDG 渲染依赖图中添加一个三角形排序 Pass，按 “从后到前” 的顺序对当前视口的半透明三角形进行排序。
				}
			}

			{
				// Render all remaining translucency views.
				//渲染所有剩余的半透明视口
				const bool bStandardTranslucentCanRenderSeparate = bShouldRenderDistortion; // It is only needed to render standard translucent as separate when there is distortion (non self distortion of transmittance/specular/etc.)
				//半透明渲染
				RenderTranslucency(
					*this, 
					GraphBuilder, 
					SceneTextures, 
					TranslucencyLightingVolumeTextures, 
					&TranslucencyResourceMap, 
					Views, 
					TranslucencyViewsToRender, 
					SeparateTranslucencyDimensions, 
					InstanceCullingManager, 
					bStandardTranslucentCanRenderSeparate, 
					TranslucencySharedDepthTexture);
			}
			//不透明后扩展 → GPU 写入/剔除 → 毛发/体积雾 → 半透明前层 → 光追半透明 → Lumen 反射 → OIT 排序 → 传统半透明最终渲染

			
			// Compose hair before velocity/distortion pass since these pass write depth value, 
			// and this would make the hair composition fails in this cases.
			//必须在「速度 / 畸变 Pass」之前合成毛发 —— 因为速度 / 畸变 Pass 会写入深度值，若后执行毛发合成，深度冲突会导致毛发渲染失效
			if (GetHairStrandsComposition() == EHairStrandsCompositionType::AfterTranslucent)
			{
				RDG_EVENT_SCOPE_STAT(GraphBuilder, HairRendering, "HairRendering");
				RDG_GPU_STAT_SCOPE(GraphBuilder, HairRendering);
				//畸变效果渲染
				RenderHairComposition(GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures.Depth.Target, SceneTextures.Velocity, TranslucencyResourceMap);
			}
			
			//半透明速度纹理渲染
			if (bShouldRenderDistortion)
			{
				RenderDistortion(GraphBuilder, SceneTextures.Color.Target, SceneTextures.Depth.Target, SceneTextures.Velocity, TranslucencyResourceMap);
			}

			if (bShouldRenderVelocities && CVarTranslucencyVelocity.GetValueOnRenderThread() != 0)
			{
				const bool bRecreateSceneTextures = !HasBeenProduced(SceneTextures.Velocity);
				
				//为半透明物体渲染速度纹理（记录每个像素的运动方向 / 速度），用于后续运动模糊、TAA 抗锯齿
				RenderVelocities(GraphBuilder, Views, SceneTextures, EVelocityPass::Translucent, false);

				if (bRecreateSceneTextures)
				{
					// Rebuild scene textures to include newly allocated velocity.
					//场景纹理 UBO 重建
					SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);
				}
			}
		}
		else if (GetHairStrandsComposition() == EHairStrandsCompositionType::AfterTranslucent)//半透明后（AfterTranslucent）分支
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, HairRendering, "HairRendering");
			RDG_GPU_STAT_SCOPE(GraphBuilder, HairRendering);
			//将毛发的渲染结果合成到当前场景纹理中，完成毛发与半透明、不透明物体的最终融合。
			RenderHairComposition(GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures.Depth.Target, SceneTextures.Velocity, TranslucencyResourceMap);
		}
		
//调试（debug）
#if !UE_BUILD_SHIPPING
		if (CVarForceBlackVelocityBuffer.GetValueOnRenderThread())
		{
			SceneTextures.Velocity = SystemTextures.Black;

			// Rebuild the scene texture uniform buffer to include black.
			SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);
		}
#endif

		{
			if (HairStrandsBookmarkParameters.HasInstances())
			{
				HairStrandsBookmarkParameters.SceneColorTexture = SceneTextures.Color.Target;
				HairStrandsBookmarkParameters.SceneDepthTexture = SceneTextures.Depth.Target;
				//渲染毛发调试信息
				RenderHairStrandsDebugInfo(GraphBuilder, Scene, Views, HairStrandsBookmarkParameters);
			}
		}
		//调试信息渲染汇总逻辑，集中处理三类核心调试功能：虚拟阴影贴图（VSM）调试、着色能量守恒调试、实例剔除遮挡查询调试。
		if (VirtualShadowMapArray.IsEnabled())
		{
			VirtualShadowMapArray.RenderDebugInfo(GraphBuilder, Views);
		}

		for (FViewInfo& View : Views)
		{
			ShadingEnergyConservation::Debug(GraphBuilder, View, SceneTextures);
		}

		if (ViewFamily.EngineShowFlags.VisualizeInstanceOcclusionQueries
			&& Scene->InstanceCullingOcclusionQueryRenderer)
		{
			for (FViewInfo& View : Views)
			{
				Scene->InstanceCullingOcclusionQueryRenderer->RenderDebug(GraphBuilder, Scene->GPUScene, View, SceneTextures);
			}
		}
		//调试信息渲染汇总逻辑，集中处理三类核心调试功能：虚拟阴影贴图（VSM）调试、着色能量守恒调试、实例剔除遮挡查询调试。

		
		if (!bHasRayTracedOverlay && ViewFamily.EngineShowFlags.LightShafts)
		{
			SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderLightShaftBloom);
			RenderLightShaftBloom(GraphBuilder, SceneTextures, /* inout */ TranslucencyResourceMap);//光束光晕（Light Shaft Bloom）渲染
		}

		{
			//半透明纹理按需上采样
			// Light shaft (rendered just above) can render in separate transluceny at low resolution according to r.SeparateTranslucencyScreenPercentage. 
			//光束可能按 r.SeparateTranslucencyScreenPercentage 控制台变量的设置，以低分辨率渲染在独立半透明缓冲区中；
			// So we can only upsample that buffer if required after the light shaft bloom pass.
			//因此必须在光束光晕 Pass 完成后，才按需对该低分辨率缓冲区进行上采样（避免上采样后再渲染光束导致效果错误
			UpscaleTranslucencyIfNeeded(GraphBuilder, SceneTextures, TranslucencyViewsToRender, /* inout */ &TranslucencyResourceMap, TranslucencySharedDepthTexture);
			TranslucencyViewsToRender = ETranslucencyView::None;//清空半透明视口标记
		}

		FPathTracingResources PathTracingResources;//路径追踪资源声明

#if RHI_RAYTRACING
		if (IsRayTracingEnabled())
		{
			// Path tracer requires the full ray tracing pipeline support, as well as specialized extra shaders.
			// Most of the ray tracing debug visualizations also require the full pipeline, but some support inline mode.
			
			if (ViewFamily.EngineShowFlags.PathTracing 
				&& FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(Scene->GetShaderPlatform()))
			{
				for (const FViewInfo& View : Views)
				{
					RenderPathTracing(GraphBuilder, View, SceneTextures.UniformBuffer, SceneTextures.Color.Target, SceneTextures.Depth.Target,PathTracingResources);
				}
			}
			else if (ViewFamily.EngineShowFlags.RayTracingDebug)
			{
				for (const FViewInfo& View : Views)
				{
					FRayTracingPickingFeedback PickingFeedback = {};
					RenderRayTracingDebug(GraphBuilder, *Scene, View, SceneTextures, PickingFeedback);

					OnGetOnScreenMessages.AddLambda([this, &View, PickingFeedback](FScreenMessageWriter& ScreenMessageWriter)->void
						{
							RayTracingDebugDisplayOnScreenMessages(ScreenMessageWriter, View);
							RayTracingDisplayPicking(PickingFeedback, ScreenMessageWriter);
						});
				}
			}
		}
#endif
		RendererModule.RenderOverlayExtensions(GraphBuilder, Views, SceneTextures);//全局渲染叠加层扩展执行
		
		//物理场（PhysicsField）：UE 物理系统的核心功能，用于模拟大规模动态力场（如风力推动草叶、爆炸波推开物体、水流力场），本身是不可见的，需通过该函数可视化
		if (ViewFamily.EngineShowFlags.PhysicsField && Scene->PhysicsField)
		{
			RenderPhysicsField(GraphBuilder, Views, Scene->PhysicsField, SceneTextures.Color.Target);//物理场可视化渲染
		}
		
		//距离场环境光遮蔽（Distance Field AO，DFAO）的可视化渲染
		if (ViewFamily.EngineShowFlags.VisualizeDistanceFieldAO && ShouldRenderDistanceFieldLighting(Scene->DistanceFieldSceneData, Views))
		{
			// Use the skylight's max distance if there is one, to be consistent with DFAO shadowing on the skylight
			const float OcclusionMaxDistance = Scene->SkyLight && !Scene->SkyLight->bWantsStaticShadowing ? Scene->SkyLight->OcclusionMaxDistance : Scene->DefaultMaxDistanceFieldOcclusionDistance;
			TArray<FRDGTextureRef> DummyOutput;
			RenderDistanceFieldLighting(
				GraphBuilder, 
				SceneTextures, 
				FDistanceFieldAOParameters(OcclusionMaxDistance), 
				DummyOutput, 
				false, 
				ViewFamily.EngineShowFlags.VisualizeDistanceFieldAO);
		}
		
		//距离场 + Lumen 全局光照的可视化调试
		// Draw visualizations just before use to avoid target contamination
		//在距离场使用前绘制可视化，避免渲染目标污染
		if (ViewFamily.EngineShowFlags.VisualizeMeshDistanceFields || ViewFamily.EngineShowFlags.VisualizeGlobalDistanceField)
		{
			RenderMeshDistanceFieldVisualization(GraphBuilder, SceneTextures);
		}
		//Lumen 光照可视化渲染
		if (bRenderDeferredLighting)
		{
			RenderLumenMiscVisualizations(GraphBuilder, SceneTextures, LumenFrameTemporaries);
			RenderDiffuseIndirectAndAmbientOcclusion(
				GraphBuilder,
				SceneTextures,
				LumenFrameTemporaries,
				LightingChannelsTexture,
				/* bCompositeRegularLumenOnly = */ false,
				/* bIsVisualizePass = */ true,
				AsyncLumenIndirectLightingOutputs);
		}
		
		//静态光（Stationary Light）重叠可视化调试
		if (ViewFamily.EngineShowFlags.StationaryLightOverlap)
		{
			RenderStationaryLightOverlap(GraphBuilder, SceneTextures, LightingChannelsTexture);
		}
		
		//异质体积雾（Heterogeneous Volumes）合成
		// Composite Heterogeneous Volumes
		//合成异质体积雾   异质体积雾是指密度非均匀的体积雾（如烟雾、云、火焰周边的不均匀雾效），区别于传统均匀密度的体积雾。
		if (!bHasRayTracedOverlay && ShouldRenderHeterogeneousVolumes(Scene) &&
			(GetHeterogeneousVolumesComposition() == EHeterogeneousVolumesCompositionType::AfterTranslucent))
		{
			CompositeHeterogeneousVolumes(GraphBuilder, SceneTextures);
		}
		
		
		//体积云（Volumetric Cloud）的可视化渲染
		if (bShouldVisualizeVolumetricCloud && !bHasRayTracedOverlay)
		{
			RenderVolumetricCloud( //快速渲染体积云的 “基础轮廓”（跳过逐像素光线追踪，降低调试渲染开销），先输出体积云的大致形状 / 范围，便于快速定位整体问题
				GraphBuilder, 
				SceneTextures, 
				false, 
				true, 
				HalfResolutionDepthCheckerboardMinMaxTexture,
				QuarterResolutionDepthMinMaxTexture, 
				false, 
				InstanceCullingManager);
			//体积云渲染目标（RT）重建
			ReconstructVolumetricRenderTarget(GraphBuilder, Views, SceneTextures.Depth.Resolve, HalfResolutionDepthCheckerboardMinMaxTexture, false);
			//体积云 RT 与场景合成（可视化专用）
			ComposeVolumetricRenderTargetOverSceneForVisualization(GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures);
			RenderVolumetricCloud( //体积云 “开启逐像素追踪” 的精细渲染
				GraphBuilder, 
				SceneTextures, 
				true, 
				false, 
				HalfResolutionDepthCheckerboardMinMaxTexture, 
				QuarterResolutionDepthMinMaxTexture, 
				false, 
				InstanceCullingManager);
		}
		
		//在无光追叠加层时，添加专门的渲染通道来可视化稀疏体纹理的底层数据
		if (!bHasRayTracedOverlay)
		{
			AddSparseVolumeTextureViewerRenderPass(GraphBuilder, *this, SceneTextures);
		}

		// Resolve the scene color for post processing.
		//为后处理解析场景颜色
		AddResolveSceneColorPass(GraphBuilder, Views, SceneTextures.Color);

		RendererModule.RenderPostResolvedSceneColorExtension(GraphBuilder, SceneTextures);//解析后场景颜色扩展执行
		
		//场景捕获组件数据拷贝
		CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, ViewFamilyTexture, ViewFamilyDepthTexture, ViewFamily, Views);
		
		//帧间数据缓存
		//遍历所有视口（Views），为每个视口独立处理帧间数据缓存（支持多视口 / 分屏场景）
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			const FViewInfo& View = Views[ViewIndex];

			if (((View.FinalPostProcessSettings.DynamicGlobalIlluminationMethod == EDynamicGlobalIlluminationMethod::ScreenSpace && ScreenSpaceRayTracing::ShouldKeepBleedFreeSceneColor(View))
				|| GetViewPipelineState(View).DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen
				|| GetViewPipelineState(View).ReflectionsMethod == EReflectionsMethod::Lumen)
				&& !View.bStatePrevViewInfoIsReadOnly)
			{
				// Keep scene color and depth for next frame screen space ray tracing.
				//保留场景颜色和深度用于下一帧的屏幕空间光线追踪
				FSceneViewState* ViewState = View.ViewState;
				GraphBuilder.QueueTextureExtraction(SceneTextures.Depth.Resolve, &ViewState->PrevFrameViewInfo.DepthBuffer);
				GraphBuilder.QueueTextureExtraction(SceneTextures.Color.Resolve, &ViewState->PrevFrameViewInfo.ScreenSpaceRayTracingInput);
			}
		}

		// Finish rendering for each view.
		//完成每个视口的渲染
		if (ViewFamily.bResolveScene && ViewFamilyTexture)
		{	
			RDG_EVENT_SCOPE_STAT(GraphBuilder, Postprocessing, "PostProcessing");
			RDG_GPU_STAT_SCOPE(GraphBuilder, Postprocessing);
			SCOPED_NAMED_EVENT(PostProcessing, FColor::Emerald);
			
			//后处理输入参数初始化
			FPostProcessingInputs PostProcessingInputs;
			PostProcessingInputs.ViewFamilyTexture = ViewFamilyTexture;												//视口家族的颜色纹理		后处理的 “源颜色数据”（核心输入），所有后处理效果均基于该纹理修改；
			PostProcessingInputs.ViewFamilyDepthTexture = ViewFamilyDepthTexture;									//视口家族的深度纹理		用于深度相关后处理（如景深、运动模糊、屏幕空间反射）；
			PostProcessingInputs.CustomDepthTexture = SceneTextures.CustomDepth.Depth;								//自定义深度纹理			用于基于自定义深度的后处理（如选中物体高亮、分层渲染效果）；
			PostProcessingInputs.ExposureIlluminance = ExposureIlluminance;											//曝光照度值				用于自动曝光（Auto Exposure）计算，保证画面亮度自适应场景光照；
			PostProcessingInputs.SceneTextures = SceneTextures.UniformBuffer;										//场景纹理统一缓冲区		将所有场景纹理（颜色、深度、法线等）打包为 GPU 可快速访问的缓冲区，提升后处理采样效率；
			PostProcessingInputs.bSeparateCustomStencil = SceneTextures.CustomDepth.bSeparateStencilBuffer;			//是否分离自定义模板缓冲区	控制后处理是否使用独立的自定义模板缓冲区（如 UI 分层、特效遮罩）；
			PostProcessingInputs.PathTracingResources = PathTracingResources;										//路径追踪资源			为光追路径的后处理提供资源（如光追噪点降噪、路径追踪结果融合）；

			//编辑器立体深度纹理初始化（编辑器专用）
			FRDGTextureRef InstancedEditorDepthTexture = nullptr; // Used to pass instanced stereo depth data from primary to secondary views（用于将实例化立体深度数据从主视口传递到副视口）

			GraphBuilder.FlushSetupQueue();//RDG 准备队列刷新

			if (ViewFamily.UseDebugViewPS())// 调试视图模式：执行调试后处理通道
			{
				//遍历所有视口（Views），为每个视口独立执行调试后处理（支持多视口 / 分屏调试）
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					const FViewInfo& View = Views[ViewIndex];
					const Nanite::FRasterResults* NaniteResults = bNaniteEnabled ? &NaniteRasterResults[ViewIndex] : nullptr;
					RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
					RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);
					PostProcessingInputs.TranslucencyViewResourcesMap = FTranslucencyViewResourcesMap(TranslucencyResourceMap, ViewIndex);
					AddDebugViewPostProcessingPasses(GraphBuilder, View, GetSceneUniforms(), PostProcessingInputs, NaniteResults);
				}
			}
			else// 正常视图模式：执行视口扩展+常规后处理准备
			{
				//双层循环：外层遍历视口扩展（ViewExtensions），内层遍历视口 —— 为每个扩展、每个视口执行后处理前的自定义逻辑。
				//视口扩展预处理
				for (int32 ViewExt = 0; ViewExt < ViewFamily.ViewExtensions.Num(); ++ViewExt)
				{
					for (int32 ViewIndex = 0; ViewIndex < ViewFamily.Views.Num(); ++ViewIndex)
					{
						FViewInfo& View = Views[ViewIndex];
						RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
						PostProcessingInputs.TranslucencyViewResourcesMap = FTranslucencyViewResourcesMap(TranslucencyResourceMap, ViewIndex);
						ViewFamily.ViewExtensions[ViewExt]->PrePostProcessPass_RenderThread(GraphBuilder, View, PostProcessingInputs);
					}
				}
				//常规后处理准备（含 Nanite / 校准材质适配）
				//为每个视口绑定半透明资源映射、GPU 掩码，标记性能事件，保证后续完整后处理（TAA、色调映射、锐化等）的参数和资源就绪。
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					const FViewInfo& View = Views[ViewIndex];
					const int32 NaniteResultsIndex = View.bIsInstancedStereoEnabled ? View.PrimaryViewIndex : ViewIndex;
					const Nanite::FRasterResults* NaniteResults = bNaniteEnabled ? &NaniteRasterResults[NaniteResultsIndex] : nullptr;
					RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
					RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

					PostProcessingInputs.TranslucencyViewResourcesMap = FTranslucencyViewResourcesMap(TranslucencyResourceMap, ViewIndex);

					if (IsPostProcessVisualizeCalibrationMaterialEnabled(View))
					{
						//// 获取当前视口绑定的校准材质（如HDR校准材质、色彩校准材质）
						const UMaterialInterface* DebugMaterialInterface = GetPostProcessVisualizeCalibrationMaterialInterface(View);
						// UE断言：研发阶段强制校验材质非空，空则触发编辑器断点（快速定位材质配置错误）
						check(DebugMaterialInterface);
						//// 添加校准材质专属后处理通道，输出校准调试效果（如亮度直方图、色域热力图）
						AddVisualizeCalibrationMaterialPostProcessingPasses(GraphBuilder, View, PostProcessingInputs, DebugMaterialInterface);
					}
					else
					{
						// 获取当前视口的渲染管线状态（存储间接光/反射等核心配置）
						const FPerViewPipelineState& ViewPipelineState = GetViewPipelineState(View);
						// 判断是否启用Lumen（漫反射间接光/反射任一使用Lumen即标记为激活）
						const bool bAnyLumenActive = ViewPipelineState.DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen || ViewPipelineState.ReflectionsMethod == EReflectionsMethod::Lumen;
						
						// 定义TSR闪烁输入纹理（用于抑制超分后的画面闪烁）
						FScreenPassTexture TSRFlickeringInput;
						// 越界防护：仅当视口索引在纹理列表范围内时赋值（避免数组越界崩溃）
						if (ViewIndex < TSRFlickeringInputTextures.Num())
						{
							TSRFlickeringInput = TSRFlickeringInputTextures[ViewIndex];
						}
						//后处理Pass
						AddPostProcessingPasses(
							GraphBuilder,							// RDG构建器：管理后处理渲染指令
							View, ViewIndex,							// 当前视口+索引：支持多视口/分屏场景
							GetSceneUniforms(),						// 场景统一缓冲区：快速访问场景参数（分辨率/相机位置等）
							bAnyLumenActive,							// Lumen激活状态：适配Lumen专属降噪
							ViewPipelineState.DiffuseIndirectMethod,	// 漫反射间接光方法	
							ViewPipelineState.ReflectionsMethod,		// 反射方法
							PostProcessingInputs,						// 后处理输入参数：聚合颜色/深度/曝光等核心数据
							NaniteResults,								// Nanite光栅化结果：适配Nanite几何抗锯齿
							InstanceCullingManager,					// 实例剔除管理器：优化批量物体后处理	
							&VirtualShadowMapArray,						// 虚拟阴影贴图数组：适配虚拟阴影后处理
							LumenFrameTemporaries,					// Lumen帧临时资源：传入间接光/反射计算数据
							SceneWithoutWaterTextures,					// 无水面场景纹理：适配水面后处理隔离
							TSRFlickeringInput,						// TSR闪烁纹理：优化超分闪烁
							InstancedEditorDepthTexture);			// 编辑器立体深度纹理：适配VR/分屏渲染
					}
				}
			}
		}
		
		//虚拟纹理反馈收尾
		if (bUseVirtualTexturing)
		{
			VirtualTexture::EndFeedback(GraphBuilder);
		}
		
		//Lumen 表面缓存反馈收集
		// After AddPostProcessingPasses in case of Lumen Visualizations writing to feedback
		//在 AddPostProcessingPasses（后处理核心执行）之后，避免 Lumen 可视化数据未写入反馈缓冲区就被收集
		FinishGatheringLumenSurfaceCacheFeedback(GraphBuilder, Views[0], LumenFrameTemporaries);

		//光追后处理
#if RHI_RAYTRACING
		RayTracingScene.PostRender(GraphBuilder);
#endif
		
		//GVRS 调试预览绘制（条件执行）
		//通过 GVRSImageManager（GVRS 是 UE 虚拟纹理 / 图像管理相关模块）绘制渲染调试预览内容 —— 如虚拟纹理加载状态、渲染目标数据预览、性能热力图等研发调试可视化内容
		if (ViewFamily.bResolveScene && ViewFamilyTexture)
		{
			GVRSImageManager.DrawDebugPreview(GraphBuilder, ViewFamily, ViewFamilyTexture);
		}
		
		//全局渲染后回调广播
		//触发引擎全局的 “渲染后委托”——UE 允许项目 / 插件通过绑定该委托，在渲染全流程完成后插入自定义逻辑（如截图保存、性能数据采集、第三方插件的画面处理等）；
		GEngine->GetPostRenderDelegateEx().Broadcast(GraphBuilder);
	}
	//场景扩展渲染器收尾
	GetSceneExtensionsRenderers().PostRender(GraphBuilder);
	
	
#if WITH_MGPU
	//跨 GPU 数据传输
	//将不同 GPU 绘制的渲染目标（如 ViewFamilyTexture）数据传输到主 GPU / 目标 GPU，保证最终画面的合并输出
	if (ViewFamily.bMultiGPUForkAndJoin)
	{
		DoCrossGPUTransfers(GraphBuilder, ViewFamilyTexture, Views, CrossGPUTransferFencesDefer.Num() > 0, RenderTargetGPUMask, CrossGPUTransferDeferred.GetReference());
	}
	FlushCrossGPUTransfers(GraphBuilder);
#endif

	{
		//性能统计与 RDG 事件标记（调试 / 性能分析专用）
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderFinish);

		RDG_EVENT_SCOPE_STAT(GraphBuilder, FrameRenderFinish, "FrameRenderFinish");
		RDG_GPU_STAT_SCOPE(GraphBuilder, FrameRenderFinish);
		
		// 渲染收尾核心操作（RDG 指令生效）	
		OnRenderFinish(GraphBuilder, ViewFamilyTexture);
		GraphBuilder.AddDispatchHint();
		GraphBuilder.FlushSetupQueue();
	}
	// 场景纹理提取排队
	QueueSceneTextureExtractions(GraphBuilder, SceneTextures);

	::Substrate::PostRender(*Scene);						 // Substrate（底层渲染框架）收尾
	HairStrands::PostRender(*Scene);						 // 毛发渲染模块收尾
	HeterogeneousVolumes::PostRender(*Scene, Views);	 // 异构体积云/雾模块收尾

	//视口历史帧数据释放（遍历执行）
	// Release the view's previous frame histories so that their memory can be reused at the graph's execution.
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		Views[ViewIndex].PrevViewInfo = FPreviousViewInfo();
	}
	
	//Nanite 可见性数据收尾（条件执行）
	if (NaniteBasePassVisibility.Visibility)
	{
		NaniteBasePassVisibility.Visibility->FinishVisibilityFrame();
		NaniteBasePassVisibility.Visibility = nullptr;
	}
	
	//实例剔除查询渲染器收尾（条件执行）
	if (Scene->InstanceCullingOcclusionQueryRenderer)
	{
		Scene->InstanceCullingOcclusionQueryRenderer->EndFrame(GraphBuilder);
	}
}

#if RHI_RAYTRACING

static bool AnyRayTracingPassEnabled(const FScene* Scene, const FViewInfo& View, bool bSceneHasRayTracedShadows)
{
	if (!IsRayTracingEnabled(View.GetShaderPlatform()) || Scene == nullptr)
	{
		return false;
	}

	// Path tracer, ray tracing visualization debug modes, and sky light ray tracing force ray tracing on, regardless of what the view says
	if (View.Family->EngineShowFlags.PathTracing
		|| View.Family->EngineShowFlags.RayTracingDebug
		|| ShouldRenderRayTracingSkyLight(Scene->SkyLight, View.GetShaderPlatform()))
	{
		return true;
	}

	if (!View.IsRayTracingAllowedForView())
	{
		return false;
	}

	return bSceneHasRayTracedShadows
		|| ShouldRenderRayTracingAmbientOcclusion(View)
		|| ShouldRenderRayTracingTranslucency(View)
		|| ShouldRenderRayTracingShadows(*View.Family)
		|| ShouldRenderPluginRayTracingGlobalIllumination(View)
        || Lumen::AnyLumenHardwareRayTracingPassEnabled(Scene, View)
		|| MegaLights::UseHardwareRayTracing(*View.Family);
}

static bool ShouldRenderRayTracingEffectInternal(bool bEffectEnabled, ERayTracingPipelineCompatibilityFlags CompatibilityFlags)
{
	const bool bAllowPipeline = GRHISupportsRayTracingShaders && 
								CVarRayTracingAllowPipeline.GetValueOnRenderThread() &&
								EnumHasAnyFlags(CompatibilityFlags, ERayTracingPipelineCompatibilityFlags::FullPipeline);

	const bool bAllowInline = GRHISupportsInlineRayTracing && 
							  CVarRayTracingAllowInline.GetValueOnRenderThread() &&
							  EnumHasAnyFlags(CompatibilityFlags, ERayTracingPipelineCompatibilityFlags::Inline);

	// Disable the effect if current machine does not support the full ray tracing pipeline and the effect can't fall back to inline mode or vice versa.
	if (!bAllowPipeline && !bAllowInline)
	{
		return false;
	}

	const int32 OverrideMode = CVarForceAllRayTracingEffects.GetValueOnRenderThread();

	if (OverrideMode >= 0)
	{
		return OverrideMode > 0;
	}
	else
	{
		return bEffectEnabled;
	}
}

bool ShouldRenderRayTracingEffect(bool bEffectEnabled, ERayTracingPipelineCompatibilityFlags CompatibilityFlags, const FSceneView& View)
{
	if (!IsRayTracingEnabled(View.GetShaderPlatform()) || !View.IsRayTracingAllowedForView())
	{
		return false;
	}

	return ShouldRenderRayTracingEffectInternal(bEffectEnabled, CompatibilityFlags);
}

bool ShouldRenderRayTracingEffect(bool bEffectEnabled, ERayTracingPipelineCompatibilityFlags CompatibilityFlags, const FSceneViewFamily& ViewFamily)
{
	// TODO:  Should this check if ALL views have ray tracing?  ANY views have ray tracing?  Assert that all are the same?  All or any depending
	// on the specific feature or use case?  In practice, current examples (split screen or scene captures) will have ray tracing set the same
	// for all views, so we'll just check the first view of given a family, but having it be a separate function lets us reconsider that approach
	// in the future.
	return ShouldRenderRayTracingEffect(bEffectEnabled, CompatibilityFlags, *ViewFamily.Views[0]);
}

// Most ray tracing effects can be enabled or disabled per view, but the ray tracing sky light effect specifically requires base pass shaders
// in the FScene to be configured differently, and thus can't work if ray tracing is disabled.  There is logic in FScene::Update where
// bCachedShouldRenderSkylightInBasePass is updated based on the result of ShouldRenderSkylightInBasePass(), which is affected by whether sky light
// ray tracing is enabled.  When this value changes, bScenesPrimitivesNeedStaticMeshElementUpdate is set to true, forcing a rebuild of all static mesh
// elements in the scene.  This can't be done per frame (never mind per view), which would be required to allow this setting to vary, at least with
// the current implementation.  Sky light ray tracing is often used for cinematic capture, and not in games, so hopefully this isn't a big limitation.
// 
// This forces ray tracing on, but other ray tracing features are still disabled.  This is its own function to allow ShouldRenderRayTracingEffectInternal
// to be kept private, as all other effects should provide a view or view family, to allow IsRayTracingAllowedForView to be tested.
bool ShouldRenderRayTracingSkyLightEffect()
{
	return ShouldRenderRayTracingEffectInternal(true, ERayTracingPipelineCompatibilityFlags::FullPipeline);
}

bool HasRaytracingDebugViewModeRaytracedOverlay(const FSceneViewFamily& ViewFamily);
bool HasRayTracedOverlay(const FSceneViewFamily& ViewFamily)
{
	// Return true if a full screen ray tracing pass will be displayed on top of the raster pass
	// This can be used to skip certain calculations
	return
		ViewFamily.EngineShowFlags.PathTracing ||
		(ViewFamily.EngineShowFlags.RayTracingDebug && HasRaytracingDebugViewModeRaytracedOverlay(ViewFamily));
}

void FDeferredShadingSceneRenderer::InitializeRayTracingFlags_RenderThread()
{
	bool bRayTracingShadows = false;
	bool bRayTracing = false;

	// We currently don't need a full list of RT lights, only whether there are any RT lights at all.
	for (const FLightSceneInfoCompact& LightSceneInfoCompact : Scene->Lights)
	{
		if (GetLightOcclusionType(LightSceneInfoCompact, ViewFamily) == FLightOcclusionType::Raytraced)
		{
			bRayTracingShadows = true;
			break;
		}
	}

	for (FViewInfo& View : Views)
	{
		const bool bViewHasRayTracing = AnyRayTracingPassEnabled(Scene, View, bRayTracingShadows);

		View.bHasAnyRayTracingPass = bViewHasRayTracing;
		View.bHasRayTracingShadows = bRayTracingShadows;

		bRayTracing |= bViewHasRayTracing;
	}

	FamilyPipelineState.Set(&FFamilyPipelineState::bRayTracingShadows, bRayTracingShadows);
	FamilyPipelineState.Set(&FFamilyPipelineState::bRayTracing, bRayTracing);
}
#endif // RHI_RAYTRACING
