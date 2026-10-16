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
		// [DeferredToon] View 20 previews the new independent two-band output mask.
		TEXT("Deferred toon volume debug view: 0 selected output mode, 1 weight, 2 volume color, 3 priority, 4-13 semantic layers, 14-20 binary reference masks, 21 raw DirectLightFacing (max N.L), 22 effective diffuse, 23 effective light response, 24 selected light band (0 dark / 1 light), 25 indirect diffuse (Lumen or dynamic sky), 26 indirect availability, 27 sky diffuse before AO, 28 sky combined AO (white=unoccluded), 29 sky split availability, 30 Lumen short AO, 31 Lumen diffuse before short AO, 32 Lumen short AO availability."),
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
			// [DeferredToon] Preserve the original residual across overlapping volumes.
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OriginalSceneColorTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ShadowBufferTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, ShadowBufferSampler)
			// [DeferredToon] Explicit binding for the split direct-specular semantic.
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SpecularBufferTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, SpecularBufferSampler)
			SHADER_PARAMETER_RDG_TEXTURE(Texture3D, ShadowColorVolume)
			SHADER_PARAMETER_SAMPLER(SamplerState, ShadowColorVolumeSampler)
			SHADER_PARAMETER(FMatrix44f, WorldToLocal)
			SHADER_PARAMETER(FVector3f, LocalBoxExtent)
			SHADER_PARAMETER(float, EdgeFalloff)
			SHADER_PARAMETER(float, ShadowColorVolumeWeight)
			SHADER_PARAMETER(float, BlendWeight)
			SHADER_PARAMETER(uint32, OutputMode)
			// [DeferredToon] Linear authored endpoints for independent output.
			SHADER_PARAMETER(float, BaseColorWeight)
			SHADER_PARAMETER(float, ToonShadowThreshold)
			SHADER_PARAMETER(FVector4f, ToonLitColor)
			SHADER_PARAMETER(FVector4f, ToonDarkColor)
			// [DeferredToon] Visible direct layer and box-local texture mapping.
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ShadowTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, ShadowTextureSampler)
			SHADER_PARAMETER(uint32, bShadowTextureValid)
			SHADER_PARAMETER(uint32, bEffectiveLightAvailable)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EffectiveLightBufferTexture) // [DeferredToon] Explicit provider binding avoids changing every scene-texture shader.
			SHADER_PARAMETER(float, EffectiveLightThreshold)
			// [DeferredToon] Sorted discrete palette, no per-band shader permutations.
			SHADER_PARAMETER(uint32, LightBandCount)
			SHADER_PARAMETER_ARRAY(FVector4f, LightBandParams, [FDeferredToonVolumeSceneProxy::MaxLightBands])
			SHADER_PARAMETER_ARRAY(FVector4f, LightBandColors, [FDeferredToonVolumeSceneProxy::MaxLightBands])
			SHADER_PARAMETER(float, LitColorReplaceWeight)
			SHADER_PARAMETER(float, ShadowColorReplaceWeight)
			SHADER_PARAMETER(float, OtherLightingWeight)
			// [DeferredToon] Explicit optional provider; no global scene uniform ABI change.
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IndirectDiffuseTexture)
			SHADER_PARAMETER(uint32, bIndirectDiffuseAvailable)
			SHADER_PARAMETER(uint32, bOverrideIndirectDiffuse)
			SHADER_PARAMETER(float, IndirectDiffuseIntensity)
			SHADER_PARAMETER(FVector4f, IndirectDiffuseColor)
			SHADER_PARAMETER(float, IndirectDiffuseColorReplaceWeight)
			// [DeferredToon] RGB before AO, alpha visibility or -1 invalid.
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SkyDiffuseBeforeAOTexture)
			SHADER_PARAMETER(uint32, bSkyOcclusionAvailable)
			SHADER_PARAMETER(uint32, bOverrideSkyOcclusion)
			SHADER_PARAMETER(float, SkyOcclusionStrength)
			SHADER_PARAMETER(FVector4f, SkyOcclusionColor)
			SHADER_PARAMETER(float, SkyOcclusionColorReplaceWeight)
			// [DeferredToon] One packed, shared RT for native Lumen short-range AO.
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, LumenDiffuseBeforeShortAOTexture)
			SHADER_PARAMETER(uint32, bLumenShortAOAvailable)
			SHADER_PARAMETER(uint32, bOverrideLumenShortRangeAO)
			SHADER_PARAMETER(float, LumenShortRangeAOStrength)
			SHADER_PARAMETER(float, ShadowTextureWeight)
			SHADER_PARAMETER(FVector2f, ShadowTextureTiling)
			SHADER_PARAMETER(FVector2f, ShadowTextureOffset)
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
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()
	};

	IMPLEMENT_GLOBAL_SHADER(FDeferredToonVolumePS, "/Engine/Private/PostProcessDeferredToonVolumes.usf", "MainPS", SF_Pixel);

	// [DeferredToon] Only authored layers enter the style evaluator. Canonical
	// diagnostics and diffuse mapping bind providers independently of this list.
	static void FillInputBinding(FDeferredToonVolumePS::FParameters& Parameters, const FDeferredToonVolumeSceneProxy& Volume)
	{
		// [DeferredToon] Both authoring paths feed the same fixed renderer ABI:
		// the component inline stack is only a convenience for the details panel.
		const bool bUseProfile = (Volume.bUseStyleProfile || Volume.bUseInlineStyleLayers) && Volume.InputStack.NumLayers > 0;
		Parameters.InputNumLayers = bUseProfile ? FMath::Min<uint32>(Volume.InputStack.NumLayers, InputLayerCount) : 0u;
		for (int32 Index = 0; Index < InputLayerCount; ++Index)
		{
			const FDeferredToonInputLayer Layer = bUseProfile
				? Volume.InputStack.Layers[Index]
				: FDeferredToonInputRegistry::MakeFallbackLayer(EDeferredToonInputId::Unknown);
			const FDeferredToonInputDescriptor* Descriptor = FDeferredToonInputRegistry::ResolveDescriptor(Layer.InputId);
			GET_SCALAR_ARRAY_ELEMENT(Parameters.InputIds, Index) = static_cast<uint32>(Layer.InputId);
			GET_SCALAR_ARRAY_ELEMENT(Parameters.InputChannels, Index) = static_cast<uint32>(Layer.Channel);
			GET_SCALAR_ARRAY_ELEMENT(Parameters.InputAvailability, Index) = Descriptor && Descriptor->bAvailable && Layer.Weight > 0.0f
				&& (Descriptor->Source != EDeferredToonInputSource::EffectiveLightBuffer || Parameters.bEffectiveLightAvailable != 0u)
                && (Descriptor->Source != EDeferredToonInputSource::IndirectDiffuseBuffer || Parameters.bIndirectDiffuseAvailable != 0u)
                && (Descriptor->Source != EDeferredToonInputSource::SkyDiffuseBeforeAOBuffer || Parameters.bSkyOcclusionAvailable != 0u) ? 1u : 0u; // [DeferredToon]
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
		const FScreenPassTexture& OriginalSceneColor,
		const FDeferredToonVolumeSceneProxy& Volume,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
	{
		const FScreenPassTexture Input = FScreenPassTexture::CopyFromSlice(GraphBuilder, SceneColor);
		const FScreenPassTextureViewport InputViewport(Input);
		// [DeferredToon] The details-panel selector is a per-volume remote
		// control.  255 preserves the legacy global console variable path.
		const uint32 DebugView = Volume.DebugView == 255u
			? uint32(GetDeferredToonVolumeDebugView())
			: FMath::Min<uint32>(Volume.DebugView, 32u); // [DeferredToon] Band preview appended; existing IDs preserved.
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
		PassParameters->OriginalSceneColorTexture = OriginalSceneColor.Texture; // [DeferredToon]
		PassParameters->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		// [DeferredToon] Consume the native ShadowBuffer directly.  The custom
		// history path was removed because it accumulated camera-space pixels
		// without reprojection and produced severe moving-camera ghosting.
		PassParameters->ShadowBufferTexture = (*SceneTextures)->ShadowBufferTexture;
		PassParameters->ShadowBufferSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SpecularBufferTexture = (*SceneTextures)->SpecularBufferTexture;
		PassParameters->SpecularBufferSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
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
		// [DeferredToon] Colors are scene-linear, pre-exposed once in the shader.
		PassParameters->BaseColorWeight = Volume.BaseColorWeight;
		PassParameters->ToonShadowThreshold = Volume.ToonShadowThreshold;
		PassParameters->ToonLitColor = FVector4f(Volume.ToonLitColor.R, Volume.ToonLitColor.G, Volume.ToonLitColor.B, Volume.ToonLitColor.A);
		PassParameters->ToonDarkColor = FVector4f(Volume.ToonDarkColor.R, Volume.ToonDarkColor.G, Volume.ToonDarkColor.B, Volume.ToonDarkColor.A);
		// [DeferredToon] Raw inputs stay independent of authored style stack order.
		PassParameters->EffectiveLightThreshold = Volume.EffectiveLightThreshold;
		// [DeferredToon] Copy every slot, including zeroed inactive entries.
		PassParameters->LightBandCount = Volume.LightBandCount;
		for (int32 Index = 0; Index < FDeferredToonVolumeSceneProxy::MaxLightBands; ++Index)
		{
			PassParameters->LightBandParams[Index] = Volume.LightBandParams[Index];
			PassParameters->LightBandColors[Index] = Volume.LightBandColors[Index];
		}
		PassParameters->LitColorReplaceWeight = Volume.LitColorReplaceWeight;
		PassParameters->ShadowColorReplaceWeight = Volume.ShadowColorReplaceWeight;
		PassParameters->OtherLightingWeight = Volume.OtherLightingWeight;
        // [DeferredToon] Alpha coverage distinguishes unsupported pixels from valid zero GI.
        const FRDGTextureRef IndirectTexture = View.GetSceneTextures().ToonIndirectDiffuse;
        PassParameters->bIndirectDiffuseAvailable = IndirectTexture && HasBeenProduced(IndirectTexture) ? 1u : 0u;
        PassParameters->IndirectDiffuseTexture = PassParameters->bIndirectDiffuseAvailable ? IndirectTexture : GSystemTextures.GetBlackDummy(GraphBuilder);
        PassParameters->bOverrideIndirectDiffuse = Volume.bOverrideIndirectDiffuse ? 1u : 0u;
        PassParameters->IndirectDiffuseIntensity = Volume.IndirectDiffuseIntensity;
        PassParameters->IndirectDiffuseColor = FVector4f(Volume.IndirectDiffuseColor.R, Volume.IndirectDiffuseColor.G, Volume.IndirectDiffuseColor.B, 1);
        PassParameters->IndirectDiffuseColorReplaceWeight = Volume.IndirectDiffuseColorReplaceWeight;
        // [DeferredToon] Allocation alone does not imply valid pixels; alpha is checked in HLSL.
        const FRDGTextureRef SkySplitTexture = View.GetSceneTextures().ToonSkyDiffuseBeforeAO;
        PassParameters->bSkyOcclusionAvailable = SkySplitTexture && HasBeenProduced(SkySplitTexture) ? 1u : 0u;
        PassParameters->SkyDiffuseBeforeAOTexture = PassParameters->bSkyOcclusionAvailable ? SkySplitTexture : GSystemTextures.GetBlackDummy(GraphBuilder);
        PassParameters->bOverrideSkyOcclusion = Volume.bOverrideSkyOcclusion ? 1u : 0u;
        PassParameters->SkyOcclusionStrength = Volume.SkyOcclusionStrength;
        PassParameters->SkyOcclusionColor = FVector4f(Volume.SkyOcclusionColor.R, Volume.SkyOcclusionColor.G, Volume.SkyOcclusionColor.B, 1);
        PassParameters->SkyOcclusionColorReplaceWeight = Volume.SkyOcclusionColorReplaceWeight;
        // [DeferredToon] Both resource presence and pixel alpha are required.
        const FRDGTextureRef LumenSplitTexture = View.GetSceneTextures().ToonLumenDiffuseBeforeShortAO;
        PassParameters->bLumenShortAOAvailable = LumenSplitTexture && HasBeenProduced(LumenSplitTexture) ? 1u : 0u;
        PassParameters->LumenDiffuseBeforeShortAOTexture = PassParameters->bLumenShortAOAvailable ? LumenSplitTexture : GSystemTextures.GetBlackDummy(GraphBuilder);
        PassParameters->bOverrideLumenShortRangeAO = Volume.bOverrideLumenShortRangeAO ? 1u : 0u;
        PassParameters->LumenShortRangeAOStrength = Volume.LumenShortRangeAOStrength;
		PassParameters->ShadowTextureWeight = Volume.ShadowTextureWeight;
		PassParameters->ShadowTextureTiling = FVector2f(Volume.ShadowTextureTiling);
		PassParameters->ShadowTextureOffset = FVector2f(Volume.ShadowTextureOffset);
        PassParameters->bEffectiveLightAvailable = View.GetSceneTextures().EffectiveLightBuffer && HasBeenProduced(View.GetSceneTextures().EffectiveLightBuffer) ? 1u : 0u;
        PassParameters->EffectiveLightBufferTexture = PassParameters->bEffectiveLightAvailable ? View.GetSceneTextures().EffectiveLightBuffer : GSystemTextures.GetBlackDummy(GraphBuilder);
        PassParameters->bShadowTextureValid = Volume.bShadowTextureValid ? 1u : 0u;
        PassParameters->ShadowTexture = Volume.bShadowTextureValid
            ? RegisterExternalTexture(GraphBuilder, Volume.ShadowTextureResource.GetReference(), TEXT("DeferredToonVolume.ShadowTexture"))
            : GSystemTextures.GetBlackDummy(GraphBuilder);
        PassParameters->ShadowTextureSampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		PassParameters->Priority = float(Volume.GetPriority());
		PassParameters->bShadowColorVolumeValid = Volume.bShadowColorVolumeValid ? 1u : 0u;
		PassParameters->DebugView = DebugView;
		FillInputBinding(*PassParameters, Volume);
		for (int32 Index = 0; Index < CurveSampleCount; ++Index)
		{
			// Scalar shader arrays use UE's packed layout; write them through the matching helper.
			GET_SCALAR_ARRAY_ELEMENT(PassParameters->FalloffSamples, Index) = Volume.FalloffSamples[Index];
			const FLinearColor Lighting = Volume.LightingColorSamples[Index];
			PassParameters->LightingColorSamples[Index] = FVector4f(Lighting.R, Lighting.G, Lighting.B, Lighting.A);
		}
		PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

		TShaderMapRef<FDeferredToonVolumePS> PixelShader(View.ShaderMap);
		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("DeferredToonVolume"), View, OutputViewport, InputViewport, PixelShader, PassParameters);
		return FScreenPassTextureSlice::CreateFromScreenPassTexture(GraphBuilder, FScreenPassTexture(Output));
	}
}

int32 GetDeferredToonVolumeDebugView()
{
	return FMath::Clamp(CVarDeferredToonVolumesDebugView.GetValueOnRenderThread(), 0, 32); // [DeferredToon] Appended indirect diffuse diagnostics.
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
	// [DeferredToon] Resolve once, not once per box, to avoid repeated residual subtraction.
	const FScreenPassTexture OriginalSceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.SceneColor);
	for (const FDeferredToonVolumeSceneProxy* Volume : Volumes)
	{
		if (Volume)
		{
			Current = AddVolumePass(GraphBuilder, View, Current, OriginalSceneColor, *Volume, Inputs.SceneTextures);
		}
	}
	return Current;
}
