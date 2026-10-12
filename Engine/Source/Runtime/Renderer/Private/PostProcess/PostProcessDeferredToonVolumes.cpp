#include "PostProcess/PostProcessDeferredToonVolumes.h"

#include "DeferredToonVolumeSceneProxy.h"
#include "Engine/DeferredToonInputRegistry.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalRenderResources.h"
#include "PixelShaderUtils.h"
#include "RenderGraphUtils.h"
#include "ScenePrivate.h"
#include "SceneTextureParameters.h"

// [DeferredToon] One fullscreen ping-pong pass is emitted per volume.
namespace
{
	static constexpr int32 CurveSampleCount = FDeferredToonVolumeSceneProxy::CurveSampleCount;
	static constexpr int32 InputLayerCount = FDeferredToonInputStack::MaxInputLayers;

	TAutoConsoleVariable<int32> CVarDeferredToonVolumes(
		TEXT("r.Toon.DeferredVolumes"),
		1,
		TEXT("Enables pre-tonemap deferred toon color volumes."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarDeferredToonVolumesDebugView(
		TEXT("r.Toon.DeferredVolumes.DebugView"),
		0,
		TEXT("Deferred toon volume debug view: 0 NativeComposite, 1 weight, 2 volume color, 3 priority, 4-13 semantic layers, 14-17 independent masks."),
		ECVF_RenderThreadSafe);

	class FDeferredToonVolumePS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FDeferredToonVolumePS);
		SHADER_USE_PARAMETER_STRUCT(FDeferredToonVolumePS, FGlobalShader);

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
				&& !IsForwardShadingEnabled(Parameters.Platform);
		}

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
			SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
			SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ShadowBufferTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, ShadowBufferSampler)
			SHADER_PARAMETER_RDG_TEXTURE(Texture3D, ShadowColorVolume)
			SHADER_PARAMETER_SAMPLER(SamplerState, ShadowColorVolumeSampler)
			SHADER_PARAMETER(FMatrix44f, WorldToLocal)
			SHADER_PARAMETER(FVector3f, LocalBoxExtent)
			SHADER_PARAMETER(float, EdgeFalloff)
			SHADER_PARAMETER(float, ShadowColorVolumeWeight)
			// [DeferredToon] DirectLight mask controls are separate from future
			// style texture/color parameters.
			SHADER_PARAMETER(float, DirectLightMaskThreshold)
			SHADER_PARAMETER(float, DirectLightMaskSoftness)
			SHADER_PARAMETER(float, BlendWeight)
			SHADER_PARAMETER(uint32, OutputMode)
			SHADER_PARAMETER(float, Priority)
			SHADER_PARAMETER(uint32, bShadowColorVolumeValid)
			SHADER_PARAMETER(uint32, DebugView)
			// [DeferredToon] Keep semantic input binding fixed-size.  The registry
			// may grow without changing this shader parameter layout.
			SHADER_PARAMETER_SCALAR_ARRAY(uint32, InputIds, [InputLayerCount])
			SHADER_PARAMETER_SCALAR_ARRAY(uint32, InputChannels, [InputLayerCount])
			SHADER_PARAMETER_SCALAR_ARRAY(uint32, InputAvailability, [InputLayerCount])
			SHADER_PARAMETER_SCALAR_ARRAY(float, InputWeights, [InputLayerCount])
			SHADER_PARAMETER_SCALAR_ARRAY(uint32, InputInverts, [InputLayerCount])
			SHADER_PARAMETER_SCALAR_ARRAY(uint32, InputOperations, [InputLayerCount])
			SHADER_PARAMETER_ARRAY(FVector4f, InputDefaults, [InputLayerCount])
			SHADER_PARAMETER(uint32, InputNumLayers)
			SHADER_PARAMETER_SCALAR_ARRAY(float, FalloffSamples, [CurveSampleCount])
			SHADER_PARAMETER_ARRAY(FVector4f, LightingColorSamples, [CurveSampleCount])
			SHADER_PARAMETER_ARRAY(FVector4f, ShadowColorSamples, [CurveSampleCount])
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()
	};

	IMPLEMENT_GLOBAL_SHADER(FDeferredToonVolumePS, "/Engine/Private/PostProcessDeferredToonVolumes.usf", "MainPS", SF_Pixel);

	// [DeferredToon] This first-stage stack is intentionally data-driven but
	// fixed in capacity.  It exercises the scene-input contract before profiles
	// are introduced; profile layers will replace this list without changing the
	// shader ABI.
	static void FillInputBinding(FDeferredToonVolumePS::FParameters& Parameters, const FDeferredToonVolumeSceneProxy& Volume)
	{
		static constexpr EDeferredToonInputId DefaultInputs[InputLayerCount] =
		{
			EDeferredToonInputId::DirectLight,
			EDeferredToonInputId::NativeShadow,
			EDeferredToonInputId::ShadowChannel,
			EDeferredToonInputId::AmbientOcclusion,
			EDeferredToonInputId::BaseColor,
			EDeferredToonInputId::WorldNormal,
			EDeferredToonInputId::Roughness,
			EDeferredToonInputId::ShadingModel,
			EDeferredToonInputId::Depth,
			EDeferredToonInputId::Velocity,
		};

		// [DeferredToon] Both authoring paths feed the same fixed renderer ABI:
		// the component inline stack is only a convenience for the details panel.
		const bool bUseProfile = (Volume.bUseStyleProfile || Volume.bUseInlineStyleLayers) && Volume.InputStack.NumLayers > 0;
		Parameters.InputNumLayers = bUseProfile ? FMath::Min<uint32>(Volume.InputStack.NumLayers, InputLayerCount) : InputLayerCount;
		for (int32 Index = 0; Index < InputLayerCount; ++Index)
		{
			const FDeferredToonInputLayer Layer = bUseProfile
				? Volume.InputStack.Layers[Index]
				: FDeferredToonInputRegistry::MakeFallbackLayer(DefaultInputs[Index]);
			const FDeferredToonInputDescriptor* Descriptor = FDeferredToonInputRegistry::ResolveDescriptor(Layer.InputId);
			GET_SCALAR_ARRAY_ELEMENT(Parameters.InputIds, Index) = static_cast<uint32>(Layer.InputId);
			GET_SCALAR_ARRAY_ELEMENT(Parameters.InputChannels, Index) = static_cast<uint32>(Descriptor ? Descriptor->Channel : Layer.Channel);
			GET_SCALAR_ARRAY_ELEMENT(Parameters.InputAvailability, Index) = Descriptor && Descriptor->bAvailable && Layer.Weight > 0.0f ? 1u : 0u;
			GET_SCALAR_ARRAY_ELEMENT(Parameters.InputWeights, Index) = Layer.Weight;
			GET_SCALAR_ARRAY_ELEMENT(Parameters.InputInverts, Index) = Layer.bInvert;
			GET_SCALAR_ARRAY_ELEMENT(Parameters.InputOperations, Index) = static_cast<uint32>(Layer.Operation);
			Parameters.InputDefaults[Index] = Descriptor ? Descriptor->DefaultValue : FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
		}
	}

	static FScreenPassTextureSlice AddVolumePass(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FScreenPassTextureSlice& SceneColor,
		const FDeferredToonVolumeSceneProxy& Volume,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
	{
		const FScreenPassTexture Input = FScreenPassTexture::CopyFromSlice(GraphBuilder, SceneColor);
		const FScreenPassTextureViewport InputViewport(Input);
		// [DeferredToon] The details-panel selector is a per-volume remote
		// control.  255 preserves the legacy global console variable path.
		const uint32 DebugView = Volume.DebugView == 255u
			? uint32(GetDeferredToonVolumeDebugView())
			: FMath::Min<uint32>(Volume.DebugView, 17u);
		// [DeferredToon] A pooled post-process target can retain pixels outside a
		// partial view rect. Those pixels become visible as rectangular blocks when
		// the editor viewport or screen percentage changes. Clear every partial
		// output, not only DebugView output, and also replace EClear because
		// CreateFromInput intentionally removes the descriptor clear binding.
		const bool bPartialViewRect =
			Input.ViewRect.Min != FIntPoint::ZeroValue || Input.ViewRect.Max != Input.Texture->Desc.Extent;
		const bool bNeedsExplicitClear = bPartialViewRect || View.GetOverwriteLoadAction() == ERenderTargetLoadAction::EClear;
		// [DeferredToon] Do not use EClear for this temporary target. CreateFromInput
		// resets the descriptor's clear binding, so D3D12 cannot perform a fast clear
		// and asserts when the render pass begins. Clear explicitly with a quad, then
		// load that initialized texture while the fullscreen volume pass overwrites
		// the current view rect.
		FScreenPassRenderTarget Output = FScreenPassRenderTarget::CreateFromInput(
			GraphBuilder, Input, bNeedsExplicitClear ? ERenderTargetLoadAction::ELoad : View.GetOverwriteLoadAction(), TEXT("DeferredToonVolume"));
		if (bNeedsExplicitClear)
		{
			AddClearRenderTargetPass(GraphBuilder, Output.Texture, FLinearColor::Black);
		}
		const FScreenPassTextureViewport OutputViewport(Output);

		FDeferredToonVolumePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDeferredToonVolumePS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->Input = GetScreenPassTextureViewportParameters(InputViewport);
		PassParameters->SceneTextures = SceneTextures;
		PassParameters->SceneColorTexture = Input.Texture;
		PassParameters->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		// [DeferredToon] Consume the native ShadowBuffer directly.  The custom
		// history path was removed because it accumulated camera-space pixels
		// without reprojection and produced severe moving-camera ghosting.
		PassParameters->ShadowBufferTexture = (*SceneTextures)->ShadowBufferTexture;
		PassParameters->ShadowBufferSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->ShadowColorVolume = Volume.bShadowColorVolumeValid
			? RegisterExternalTexture(GraphBuilder, Volume.ShadowColorVolumeResource.GetReference(), TEXT("DeferredToonVolume.ShadowColorVolume"))
			: GBlackVolumeTexture->TextureRHI.IsValid()
				? RegisterExternalTexture(GraphBuilder, GBlackVolumeTexture->TextureRHI.GetReference(), TEXT("DeferredToonVolume.BlackVolume"))
				: nullptr;
		PassParameters->ShadowColorVolumeSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->WorldToLocal = FMatrix44f(Volume.GetInverseTransform().ToMatrixWithScale());
		PassParameters->LocalBoxExtent = FVector3f(Volume.LocalBoxExtent);
		PassParameters->EdgeFalloff = Volume.EdgeFalloff;
		PassParameters->ShadowColorVolumeWeight = Volume.ShadowColorVolumeWeight;
		// [DeferredToon] Bind only the independent DirectLight mask controls.
		PassParameters->DirectLightMaskThreshold = Volume.DirectLightMaskThreshold;
		PassParameters->DirectLightMaskSoftness = Volume.DirectLightMaskSoftness;
		PassParameters->BlendWeight = Volume.BlendWeight;
		PassParameters->OutputMode = uint32(Volume.OutputMode);
		PassParameters->Priority = float(Volume.GetPriority());
		PassParameters->bShadowColorVolumeValid = Volume.bShadowColorVolumeValid ? 1u : 0u;
		PassParameters->DebugView = DebugView;
		FillInputBinding(*PassParameters, Volume);
		for (int32 Index = 0; Index < CurveSampleCount; ++Index)
		{
			// Scalar shader arrays use UE's packed layout; write them through the matching helper.
			GET_SCALAR_ARRAY_ELEMENT(PassParameters->FalloffSamples, Index) = Volume.FalloffSamples[Index];
			const FLinearColor Lighting = Volume.LightingColorSamples[Index];
			const FLinearColor Shadow = Volume.ShadowColorSamples[Index];
			PassParameters->LightingColorSamples[Index] = FVector4f(Lighting.R, Lighting.G, Lighting.B, Lighting.A);
			PassParameters->ShadowColorSamples[Index] = FVector4f(Shadow.R, Shadow.G, Shadow.B, Shadow.A);
		}
		PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

		TShaderMapRef<FDeferredToonVolumePS> PixelShader(View.ShaderMap);
		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("DeferredToonVolume"), View, OutputViewport, InputViewport, PixelShader, PassParameters);
		return FScreenPassTextureSlice::CreateFromScreenPassTexture(GraphBuilder, FScreenPassTexture(Output));
	}
}

int32 GetDeferredToonVolumeDebugView()
{
	return FMath::Clamp(CVarDeferredToonVolumesDebugView.GetValueOnRenderThread(), 0, 17);
}

FScreenPassTextureSlice AddDeferredToonVolumePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FScene* Scene,
	const FDeferredToonVolumePassInputs& Inputs)
{
	if (CVarDeferredToonVolumes.GetValueOnRenderThread() <= 0 || !Scene || !Inputs.SceneColor.IsValid() || !Inputs.SceneTextures || !Scene->HasAnyDeferredToonVolume())
	{
		return Inputs.SceneColor;
	}

	TArray<FDeferredToonVolumeSceneProxy*> Volumes;
	Scene->CollectDeferredToonVolumes(FBox(EForceInit::ForceInit), Volumes);
	FScreenPassTextureSlice Current = Inputs.SceneColor;
	for (const FDeferredToonVolumeSceneProxy* Volume : Volumes)
	{
		if (Volume)
		{
			Current = AddVolumePass(GraphBuilder, View, Current, *Volume, Inputs.SceneTextures);
		}
	}
	return Current;
}
