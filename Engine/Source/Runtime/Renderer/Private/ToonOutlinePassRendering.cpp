// ToonOutlinePassRendering.cpp
// 自定义卡通轮廓线渲染通道实现

#include "ToonOutlinePassRendering.h"  

#include "DeferredShadingRenderer.h"       // 延迟渲染器
#include "ScenePrivate.h"                  // 场景私有数据
#include "MeshPassProcessor.inl"           // 网格通道处理器（内联实现）
#include "SimpleMeshDrawCommandPass.h"     // 简化网格绘制命令通道
#include "MeshDrawCommands.h"              // 网格绘制命令
#include "ProfilingDebugging/CsvProfiler.h"// CSV性能分析器

// ============================================================================
// Shader 类型注册
// ============================================================================
// 注册顶点着色器类型：将HLSL中的MainVS函数注册为FToonOutlineVS
IMPLEMENT_MATERIAL_SHADER_TYPE(, FToonOutlineVS, TEXT("/Engine/Private/ToonOutLine.usf"), TEXT("MainVS"), SF_Vertex);
// 注册像素着色器类型：将HLSL中的MainPS函数注册为FToonOutlinePS
IMPLEMENT_MATERIAL_SHADER_TYPE(, FToonOutlinePS, TEXT("/Engine/Private/ToonOutLine.usf"), TEXT("MainPS"), SF_Pixel);

// ============================================================================
// 渲染状态设置
// ============================================================================
// 设置轮廓线通道的渲染状态（深度/模板状态）
void SetupToonOutLinePassState(FMeshPassProcessorRenderState& DrawRenderState)
{
	// 可选：设置混合状态为不写入任何颜色通道（被注释掉）
	//DrawRenderState.SetBlendState(TStaticBlendState<CW_NONE>::GetRHI());  
	
	// 设置深度测试状态：启用深度测试，深度比较函数为小于等于
	// 这意味着只有深度值小于等于当前深度缓冲的片段才会被渲染
	DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_LessEqual>::GetRHI());
}

// ============================================================================
// 通道处理器工厂函数
// ============================================================================
// 创建ToonOutlinePass的网格处理器实例
FMeshPassProcessor* CreateToonOutLinePassProcessor(
	ERHIFeatureLevel::Type FeatureLevel,          // 渲染特性级别（SM5/ES3.1等）
	const FScene* Scene,                           // 场景指针
	const FSceneView* InViewIfDynamicMeshCommand,  // 视图指针（动态网格命令用）
	FMeshPassDrawListContext* InDrawListContext)   // 绘制列表上下文
{
	// 初始化渲染状态
	FMeshPassProcessorRenderState ToonOutLinePassState;
	SetupToonOutLinePassState(ToonOutLinePassState);

	// 创建并返回新的通道处理器实例
	return new FToonOutlineMeshPassProcessor(
		Scene,
		InViewIfDynamicMeshCommand,
		ToonOutLinePassState,
		InDrawListContext);
}

// ============================================================================
// 全局通道注册
// ============================================================================
// 将ToonOutlinePass注册到引擎的全局通道管理器中
FRegisterPassProcessorCreateFunction RegisterToonOutLineMeshPass(
	&CreateToonOutLinePassProcessor,                // 工厂函数指针
	EShadingPath::Deferred,                          // 仅在延迟渲染路径下使用
	EMeshPass::ToonOutlinePass,                      // 通道枚举值
	EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView  // 缓存网格命令 | 主视图
);

// ============================================================================
// 构造函数
// ============================================================================
FToonOutlineMeshPassProcessor::FToonOutlineMeshPassProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InPassDrawRenderState,  // 传入的渲染状态
	FMeshPassDrawListContext* InDrawListContext)
	:FMeshPassProcessor(
		EMeshPass::ToonOutlinePass,           // 通道类型
		Scene,                                 // 场景
		Scene->GetFeatureLevel(),              // 特性级别
		InViewIfDynamicMeshCommand,            // 视图
		InDrawListContext),                    // 绘制列表上下文
	PassDrawRenderState(InPassDrawRenderState) // 初始化渲染状态成员
{
	// 如果没有设置深度模板状态，使用默认状态（禁用深度测试，深度比较为近或相等）
	if (PassDrawRenderState.GetDepthStencilState() == nullptr)
	{
		PassDrawRenderState.SetDepthStencilState(
			TStaticDepthStencilState<false, CF_DepthNearOrEqual>().GetRHI());
	}
	
	// 如果没有设置混合状态，使用默认不透明混合状态
	if (PassDrawRenderState.GetBlendState() == nullptr)
	{
		PassDrawRenderState.SetBlendState(TStaticBlendState<>().GetRHI());
	}
}

// ============================================================================
// 添加网格批次
// ============================================================================
// 此函数在渲染时被调用，用于判断哪些网格体需要渲染到此通道
void FToonOutlineMeshPassProcessor::AddMeshBatch(
	const FMeshBatch& MeshBatch,                    // 网格批次数据
	uint64 BatchElementMask,                         // 批次元素掩码
	const FPrimitiveSceneProxy* PrimitiveSceneProxy, // 图元场景代理
	int32 StaticMeshId)                              // 静态网格ID
{
	// 获取材质渲染代理和材质资源
	const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
	const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);

	// 确保材质和着色器映射有效
	if (Material != nullptr && Material->GetRenderingThreadShaderMap())
	{
		const FMaterialShadingModelField ShadingModels = Material->GetShadingModels();
		
		// ★ 关键过滤条件：
		// 1. 材质必须使用Toon着色模型
		// 2. 材质必须启用渲染轮廓线功能
		if (ShadingModels.HasShadingModel(MSM_Toon) && Material->ShouldRenderToonOutline())
		{
			const EBlendMode BlendMode = Material->GetBlendMode();
			bool bResult = true;
			
			// 仅处理不透明混合模式的材质
			if (BlendMode == BLEND_Opaque)
			{
				// 调用Process处理此网格批次
				Process(
					MeshBatch,
					BatchElementMask,
					StaticMeshId,
					PrimitiveSceneProxy,
					*MaterialRenderProxy,
					*Material,
					FM_Solid,      // 实心填充模式
					CM_CCW);       // 逆时针剔除（剔除背面）
			}
		}
	}
}

// ============================================================================
// 处理网格批次（核心处理逻辑）
// ============================================================================
bool FToonOutlineMeshPassProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& MaterialResource,
	ERasterizerFillMode MeshFillMode,    // 光栅化填充模式（实心/线框）
	ERasterizerCullMode MeshCullMode)    // 光栅化剔除模式（顺时针/逆时针/不剔除）
{
	// 获取顶点工厂（定义了顶点数据的布局和处理方式）
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	// 创建着色器容器（模板化，包含顶点和像素着色器）
	TMeshProcessorShaders<FToonOutlineVS, FToonOutlinePS> ToonOutlinePassShader;
	{
		FMaterialShaderTypes ShaderTypes;
		// 指定此通道需要的着色器类型
		ShaderTypes.AddShaderType<FToonOutlineVS>();  // 添加轮廓线顶点着色器
		ShaderTypes.AddShaderType<FToonOutlinePS>();  // 添加轮廓线像素着色器

		const FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();

		// 尝试从材质资源获取对应的着色器
		FMaterialShaders Shaders;
		if (!MaterialResource.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
		{
			// 获取失败，输出警告并返回false
			UE_LOG(LogShaders, Warning, TEXT("Shader Not Found!"));
			return false;
		}
		
		// 提取顶点和像素着色器
		Shaders.TryGetVertexShader(ToonOutlinePassShader.VertexShader);
		Shaders.TryGetPixelShader(ToonOutlinePassShader.PixelShader);
	}

	// 初始化着色器元素数据（包含材质参数等）
	FMeshMaterialShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(
		ViewIfDynamicMeshCommand, 
		PrimitiveSceneProxy, 
		MeshBatch, 
		StaticMeshId, 
		false);

	// 计算排序键（用于优化绘制顺序，减少状态切换）
	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(
		ToonOutlinePassShader.VertexShader, 
		ToonOutlinePassShader.PixelShader);

	// ★ 设置最终的深度模板状态
	// true: 启用深度测试
	// CF_GreaterEqual: 深度比较函数为大于等于（注意：UE使用反向深度缓冲，与OpenGL的小于比较相反）
	// 模板状态全部禁用（读/写都为0x00）
	PassDrawRenderState.SetDepthStencilState(
		TStaticDepthStencilState<
		true, CF_GreaterEqual,                    // 启用深度测试，大于等于通过
		false, CF_Never, SO_Keep, SO_Keep, SO_Keep,  // 前面模板：禁用，永远失败，保持所有值
		false, CF_Never, SO_Keep, SO_Keep, SO_Keep,  // 后面模板：禁用，永远失败，保持所有值
		0x00,                                      // 禁用模板读取
		0x00>                                      // 禁用模板写入
		::GetRHI());
	PassDrawRenderState.SetStencilRef(0);  // 模板参考值设为0

	// 构建最终的网格绘制命令（将网格批次转换为GPU可执行的命令）
	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		ToonOutlinePassShader,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);
	
	return true;
}

// ============================================================================
// 性能统计和参数结构定义
// ============================================================================
// 声明周期统计计数器（用于性能分析）
DECLARE_CYCLE_STAT(TEXT("ToonOutlinePass"), STAT_CLP_ToonOutlinePass, STATGROUP_SceneRendering);

// 定义轮廓线通道的Shader参数结构体
BEGIN_SHADER_PARAMETER_STRUCT(FToonOutlineMeshPassParameters, )
	// 视图统一参数（投影矩阵、视图矩阵等）
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	// 实例剔除绘制参数
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	// 渲染目标绑定槽位
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

// ============================================================================
// 获取轮廓线通道参数
// ============================================================================
// 为RDG（渲染依赖图）构建通道参数
FToonOutlineMeshPassParameters* GetOutlinePassParameters(
	FRDGBuilder& GraphBuilder,          // RDG构建器
	const FViewInfo& View,              // 视图信息
	FSceneTextures& SceneTextures)      // 场景纹理（颜色、深度等）
{
	// 在RDG中分配参数结构体内存
	FToonOutlineMeshPassParameters* PassParameters = 
		GraphBuilder.AllocParameters<FToonOutlineMeshPassParameters>();
	
	// 设置视图统一缓冲区
	PassParameters->View = View.ViewUniformBuffer;

	// ★ 绑定渲染目标：
	// 颜色目标：加载现有的场景颜色（在已有颜色上绘制轮廓）
	PassParameters->RenderTargets[0] = FRenderTargetBinding(
		SceneTextures.Color.Target, 
		ERenderTargetLoadAction::ELoad);  // 加载模式：保留之前的内容
	
	// 深度模板目标：加载现有深度，允许读写深度和模板
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		SceneTextures.Depth.Target, 
		ERenderTargetLoadAction::ELoad,                                      // 加载深度
		ERenderTargetLoadAction::ELoad,                                      // 加载模板
		FExclusiveDepthStencil::DepthWrite_StencilWrite);                    // 允许深度和模板写入

	return PassParameters;
}

// ============================================================================
// 主渲染函数（延迟渲染器的成员函数）
// ============================================================================
void FDeferredShadingSceneRenderer::RenderToonOutlinePass(
	FRDGBuilder& GraphBuilder,          // RDG构建器
	FSceneTextures& SceneTextures)      // 场景纹理
{
	// 添加RDG事件作用域（用于GPU性能分析）
	RDG_EVENT_SCOPE(GraphBuilder, "ToonOutlinePass");
	
	// 添加CSV性能统计作用域
	RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderToonOutlinePass);

	// 添加命名事件（CPU端性能分析，颜色为翡翠绿）
	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderToonOutlinePass, FColor::Emerald);

	// 遍历所有视图（支持分屏等多人视图场景）
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];
		
		// GPU掩码作用域（多GPU支持）
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		
		// 条件事件作用域（仅在多视图时显示视图索引）
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		// 检查视图是否需要渲染
		const bool bShouldRenderView = View.ShouldRenderView();

		// 获取此视图的轮廓线通道并行绘制命令
		if (auto* Pass = View.ParallelMeshDrawCommandPasses[EMeshPass::ToonOutlinePass]; 
			Pass && bShouldRenderView && HasAnyDraw(Pass))  // 通道存在、需要渲染且有绘制命令
		{
			// 获取通道参数（绑定渲染目标）
			FToonOutlineMeshPassParameters* PassParameters = 
				GetOutlinePassParameters(GraphBuilder, View, SceneTextures);

			// 构建渲染命令（准备实例剔除参数）
			Pass->BuildRenderingCommands(
				GraphBuilder, 
				Scene->GPUScene, 
				PassParameters->InstanceCullingDrawParams);

			// 向RDG添加分发通道（将命令提交到GPU执行）
			GraphBuilder.AddDispatchPass(
				RDG_EVENT_NAME("ToonOutlinePass"),           // 通道名称
				PassParameters,                               // 通道参数
				ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,  // 光栅化 | 跳过渲染通道
				[Pass, PassParameters](FRDGDispatchPassBuilder& DispatchPassBuilder)
				{
					// Lambda：实际分发绘制命令
					Pass->Dispatch(DispatchPassBuilder, &PassParameters->InstanceCullingDrawParams);
				});
		}
	}
}