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
		TEXT("Deferred toon volume debug view: 0 NativeComposite, 1 weight, 2 volume color, 3 priority, 4-11 semantic layers."),
		ECVF_RenderThreadSafe);

	// [DeferredToon] VSM/SMRT uses View.StateFrameIndex for its blue-noise
	// sequence. Keep a small history in the custom pre-tonemap path instead of
	// disabling temporal sampling globally.
	TAutoConsoleVariable<float> CVarDeferredToonShadowTemporalWeight(
		TEXT("r.Toon.DeferredVolumes.TemporalWeight"),
		0.20f,
		TEXT("Current-frame weight for the deferred toon ShadowBuffer temporal filter."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarDeferredToonShadowTemporalFilter(
		TEXT("r.Toon.DeferredVolumes.TemporalFilter"),
		1,
		TEXT("Enables temporal filtering of the deferred toon ShadowBuffer."),
		ECVF_RenderThreadSafe);

	// [DeferredToon] Resolve the temporal ShadowBuffer with a compute pass.  A
	// fullscreen graphics PSO was rejected by D3D12 for this custom scalar
	// target on the editor's SM6 path, while the result is naturally a UAV write.
	class FDeferredToonShadowTemporalCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FDeferredToonShadowTemporalCS);
		SHADER_USE_PARAMETER_STRUCT(FDeferredToonShadowTemporalCS, FGlobalShader);

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
				&& !IsForwardShadingEnabled(Parameters.Platform);
		}

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FIntPoint, ViewRectMin)
			SHADER_PARAMETER(FIntPoint, ViewRectSize)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CurrentShadowBuffer)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousShadowBuffer)
			SHADER_PARAMETER(float, CurrentFrameWeight)
			SHADER_PARAMETER(uint32, bHistoryValid)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, StableShadowBuffer)
		END_SHADER_PARAMETER_STRUCT()
	};

	IMPLEMENT_GLOBAL_SHADER(FDeferredToonShadowTemporalCS, "/Engine/Private/PostProcessDeferredToonShadowTemporal.usf", "MainCS", SF_Compute);

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
			: FMath::Min<uint32>(Volume.DebugView, 11u);
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
		PassParameters->ShadowBufferTexture = View.DeferredToonStableShadowBuffer
			? View.DeferredToonStableShadowBuffer
			: (*SceneTextures)->ShadowBufferTexture;
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

namespace
{
	FRDGTextureRef AddDeferredToonShadowBufferTemporalPassInternal(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FRDGTextureRef ShadowBuffer)
	{
		if (!ShadowBuffer || !View.ViewState || CVarDeferredToonShadowTemporalFilter.GetValueOnRenderThread() <= 0)
		{
			return ShadowBuffer;
		}

		// [DeferredToon] Reuse the result if both the volume pass and the debug
		// presentation request it in the same frame.
		if (View.DeferredToonStableShadowBuffer)
		{
			return View.DeferredToonStableShadowBuffer;
		}

		// [DeferredToon] ViewState::PrevFrameViewInfo is replaced with the current
		// frame's base view data during PrepareViewStateForVisibility.  The history
		// that belongs to the previous rendered frame is therefore exposed through
		// View.PrevViewInfo, while the extraction target for this frame must remain
		// ViewState::PrevFrameViewInfo.  Reading and writing the same object here
		// would invalidate the history on every frame and make the temporal pass a
		// current-frame copy.
		const FTemporalAAHistory& PreviousHistory = View.PrevViewInfo.DeferredToonShadowBufferHistory;
		FTemporalAAHistory& OutputHistory = View.ViewState->PrevFrameViewInfo.DeferredToonShadowBufferHistory;
		const FIntPoint BufferExtent = ShadowBuffer->Desc.Extent;
		const bool bMatchingHistory = PreviousHistory.IsValid()
			&& PreviousHistory.ReferenceBufferSize == BufferExtent
			&& PreviousHistory.ViewportRect == View.ViewRect;

		// Do not blend across camera cuts or transform resets. Temporal jitter only
		// changes the projection matrix, so a static editor camera keeps its history.
		const FVector CurrentViewOrigin = View.ViewMatrices.GetViewOrigin();
		const FVector PreviousViewOrigin = View.PrevViewInfo.ViewMatrices.GetViewOrigin();
		const FVector CurrentViewDirection = View.ViewMatrices.GetViewMatrix().GetColumn(2);
		const FVector PreviousViewDirection = View.PrevViewInfo.ViewMatrices.GetViewMatrix().GetColumn(2);
		const bool bCameraStable = !View.bCameraCut && !View.bPrevTransformsReset
			&& (CurrentViewOrigin - PreviousViewOrigin).SizeSquared() < FMath::Square(0.5)
			&& (CurrentViewDirection | PreviousViewDirection) > 0.9999;
		const bool bHistoryValid = bMatchingHistory && bCameraStable;

		const FRDGTextureRef PreviousShadowBuffer = bHistoryValid
			? TryRegisterExternalTexture(GraphBuilder, PreviousHistory.RT[0])
			: GSystemTextures.GetBlackDummy(GraphBuilder);

		const FRDGTextureRef StableShadowBuffer = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				BufferExtent,
				// [DeferredToon] Keep the temporal resolve in a standard RGBA16F
				// render target.  The source ShadowBuffer is scalar R16F on D3D12,
				// but using R16F as the fullscreen pass target makes graphics PSO
				// creation fail with E_INVALIDARG (RTV format 0x57).  Only the red
				// channel is consumed by the volume shader, so the wider target does
				// not change the data contract and is suitable for the compute UAV.
				PF_FloatRGBA,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("DeferredToon.StableShadowBuffer"));
		// [DeferredToon] UAV dispatches cover only the primary view rect.  TSR
		// can enlarge the texture or use a non-zero view origin, so initialize
		// the complete allocation first and never expose pooled pixels outside
		// the written rect as rectangular history artifacts.
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StableShadowBuffer), FLinearColor::Black);

		FDeferredToonShadowTemporalCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDeferredToonShadowTemporalCS::FParameters>();
		PassParameters->ViewRectMin = View.ViewRect.Min;
		PassParameters->ViewRectSize = View.ViewRect.Size();
		PassParameters->CurrentShadowBuffer = ShadowBuffer;
		PassParameters->PreviousShadowBuffer = PreviousShadowBuffer;
		PassParameters->CurrentFrameWeight = FMath::Clamp(CVarDeferredToonShadowTemporalWeight.GetValueOnRenderThread(), 0.0f, 1.0f);
		PassParameters->bHistoryValid = bHistoryValid ? 1u : 0u;
		PassParameters->StableShadowBuffer = GraphBuilder.CreateUAV(StableShadowBuffer);

		TShaderMapRef<FDeferredToonShadowTemporalCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("DeferredToon ShadowBuffer Temporal"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), 8));

		if (!View.bStatePrevViewInfoIsReadOnly)
		{
			GraphBuilder.QueueTextureExtraction(StableShadowBuffer, &OutputHistory.RT[0]);
			OutputHistory.ReferenceBufferSize = BufferExtent;
			OutputHistory.ViewportRect = View.ViewRect;
		}

		View.DeferredToonStableShadowBuffer = StableShadowBuffer;
		return StableShadowBuffer;
	}
}

int32 GetDeferredToonVolumeDebugView()
{
	return FMath::Clamp(CVarDeferredToonVolumesDebugView.GetValueOnRenderThread(), 0, 11);
}

FRDGTextureRef AddDeferredToonShadowBufferTemporalPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef ShadowBuffer)
{
	return AddDeferredToonShadowBufferTemporalPassInternal(GraphBuilder, View, ShadowBuffer);
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

	// [DeferredToon] Stabilize the native shadow scalar once per view so every
	// volume samples the same filtered buffer.
	AddDeferredToonShadowBufferTemporalPass(GraphBuilder, View, (*Inputs.SceneTextures)->ShadowBufferTexture);

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
