// ToonOutlinePassRendering.h
#pragma once  

#include "DataDrivenShaderPlatformInfo.h"  
#include "MeshPassProcessor.h"  
#include "CoreMinimal.h"  
#include "MeshMaterialShader.h"  
// ============================================================================
// ToonOutlinePassRendering.h - 卡通轮廓线渲染通道（头文件）
// ============================================================================

// ============================================================================
// FToonOutlineMeshPassProcessor - 轮廓线网格通道处理器
// ============================================================================
class FToonOutlineMeshPassProcessor : public FMeshPassProcessor
{
public:
	// 构造函数
	// Scene: 场景指针
	// InViewIfDynamicMeshCommand: 视图指针（用于动态网格命令）
	// InPassDrawRenderState: 传入的渲染状态（深度/模板/混合等）
	// InDrawListContext: 绘制列表上下文
	FToonOutlineMeshPassProcessor(
		const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		FMeshPassDrawListContext* InDrawListContext
	);

	// ★ 重写AddMeshBatch - 引擎每帧为每个可见网格调用此函数
	// MeshBatch: 网格批次数据（包含顶点数据、材质等）
	// BatchElementMask: 批次元素掩码（指定哪些元素需要渲染）
	// PrimitiveSceneProxy: 图元场景代理（提供图元级别的数据）
	// StaticMeshId: 静态网格ID（-1表示非静态网格）
	virtual void AddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId = -1
	) override final;  // 标记为final，禁止子类继续重写

private:
	// 核心处理函数 - 负责着色器匹配、状态设置和绘制命令构建
	// MeshBatch: 网格批次
	// BatchElementMask: 批次元素掩码
	// StaticMeshId: 静态网格ID
	// PrimitiveSceneProxy: 图元代理
	// MaterialRenderProxy: 材质渲染代理
	// MaterialResource: 材质资源
	// MeshFillMode: 填充模式（实心FM_Solid/线框FM_Wireframe）
	// MeshCullMode: 剔除模式（CM_CCW逆时针/CM_CW顺时针/CM_None不剔除）
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

private:
	// 此通道的渲染状态（深度/模板/混合等），在Process中使用
	FMeshPassProcessorRenderState PassDrawRenderState;
};

// ============================================================================
// FToonOutlineVS - 轮廓线顶点着色器（C++端Shader类）
// ============================================================================
class FToonOutlineVS : public FMeshMaterialShader
{
	// ★ 声明着色器类型宏 - 将此C++类与HLSL着色器关联
	DECLARE_SHADER_TYPE(FToonOutlineVS, MeshMaterial);

public:
	/** 默认构造函数 */
	FToonOutlineVS() = default;
	
	// 编译初始化构造函数
	// Initializer: 编译器提供的初始化器，包含参数绑定信息
	FToonOutlineVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		// ★ 绑定Shader参数：从编译后的参数映射中查找"OutlineWidth"参数
		// 将C++端的OutlineWidth变量与HLSL中的OutlineWidth变量关联
		OutlineWidth.Bind(Initializer.ParameterMap, TEXT("OutlineWidth"));
		
		// 可选：绑定场景纹理UniformBuffer（根据渲染路径自动选择）
		//BindSceneTextureUniformBufferDependentOnShadingPath(Initializer, PassUniformBuffer, PassUniformBuffer);  
	}

	// ★ 修改着色器编译环境（设置宏定义等）
	// Parameters: 着色器排列参数（顶点工厂类型、材质等）
	// OutEnvironment: 输出的编译环境
	static void ModifyCompilationEnvironment(
		const FMaterialShaderPermutationParameters& Parameters, 
		FShaderCompilerEnvironment& OutEnvironment)
	{       
		// 示例：在Shader编译时设置宏定义
		//OutEnvironment.SetDefine(TEXT("Define"), Value);  
	}

	// ★ 判断此着色器排列是否需要编译
	// 返回true才会编译此排列，用于减少不必要的着色器变体
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{       
		// 编译条件：
		// 1. 平台必须支持SM5特性级别
		// 2. 顶点工厂必须为本地顶点工厂或GPU蒙皮顶点工厂
		//    （即只支持静态网格和骨骼网格，不支持粒子等特殊顶点工厂）
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			(Parameters.VertexFactoryType->GetFName() == FName(TEXT("FLocalVertexFactory")) ||
			 Parameters.VertexFactoryType->GetFName() == FName(TEXT("TGPUSkinVertexFactoryDefault")));
	}
	
	// ★ 获取着色器绑定 - 每帧/每材质调用，用于设置Shader常量
	// Scene: 场景指针
	// FeatureLevel: 特性级别
	// PrimitiveSceneProxy: 图元代理
	// MaterialRenderProxy: 材质渲染代理
	// Material: 材质资源
	// ShaderElementData: 着色器元素数据
	// ShaderBindings: 输出，着色器参数绑定容器
	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		// 首先调用父类绑定（绑定引擎内置参数）
		FMeshMaterialShader::GetShaderBindings(
			Scene, FeatureLevel, PrimitiveSceneProxy, 
			MaterialRenderProxy, Material, ShaderElementData, ShaderBindings);

		// ★ 从材质获取轮廓线宽度，并绑定到Shader
		const float OutlineWidthFromMat = Material.GetOutlineWidth();
		ShaderBindings.Add(OutlineWidth, OutlineWidthFromMat);
	}
	
	// ★ LAYOUT_FIELD - 标记此字段需要序列化/与Shader参数关联
	// OutlineWidth: 对应HLSL中的float OutlineWidth变量
	LAYOUT_FIELD(FShaderParameter, OutlineWidth);
};

// ============================================================================
// FToonOutlinePS - 轮廓线像素着色器（C++端Shader类）
// ============================================================================
class FToonOutlinePS : public FMeshMaterialShader
{
	// 声明着色器类型宏
	DECLARE_SHADER_TYPE(FToonOutlinePS, MeshMaterial);
	
public:
	/** 默认构造函数 */
	FToonOutlinePS() = default;
	
	// 编译初始化构造函数
	FToonOutlinePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		// ★ 绑定Shader参数：将C++端的OutlineColor与HLSL中的OutlineColor关联
		OutlineColor.Bind(Initializer.ParameterMap, TEXT("OutlineColor"));
	}
	
	// 修改着色器编译环境
	static void ModifyCompilationEnvironment(
		const FMaterialShaderPermutationParameters& Parameters, 
		FShaderCompilerEnvironment& OutEnvironment)
	{       
		// 示例：设置编译宏
		//OutEnvironment.SetDefine(TEXT("Define"), Value);  
	}

	// 判断是否需要编译此排列
	// 与顶点着色器相同的条件：仅支持SM5 + 特定顶点工厂
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{       
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			(Parameters.VertexFactoryType->GetFName() == FName(TEXT("FLocalVertexFactory")) ||
			 Parameters.VertexFactoryType->GetFName() == FName(TEXT("TGPUSkinVertexFactoryDefault")));
	}
	
	// ★ 获取着色器绑定 - 设置像素着色器常量
	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		// 调用父类绑定
		FMeshMaterialShader::GetShaderBindings(
			Scene, FeatureLevel, PrimitiveSceneProxy, 
			MaterialRenderProxy, Material, ShaderElementData, ShaderBindings);

		// ★ 从材质获取轮廓线颜色，并绑定到Shader
		const FLinearColor OutlineColorFromMat = Material.GetOutlineColor();
		
		// 转换为FVector3f（HLSL中的float3）
		FVector3f Color(OutlineColorFromMat.R, OutlineColorFromMat.G, OutlineColorFromMat.B);
		
		// 示例：使用硬编码红色
		//FVector3f Color(1.0, 0.0, 0.0);  
		
		// 绑定到Shader
		ShaderBindings.Add(OutlineColor, Color);
	}
	
	// ★ 对应HLSL中的float3 OutlineColor变量
	LAYOUT_FIELD(FShaderParameter, OutlineColor);
};