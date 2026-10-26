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

    // [DeferredToon] Spatial filtering only; zero allows comparison with raw thresholding.
    TAutoConsoleVariable<float> CVarDeferredToonAOFilterConservatism(
        TEXT("r.Toon.DeferredVolumes.AOFilterConservatism"), 0.5f,
        TEXT("Reject noisy AO fringe before hard thresholding by adding local standard deviation to visibility (0-2, default 0.5). Higher values shrink uncertain occlusion; uniform AO is unchanged."),
        ECVF_RenderThreadSafe);

    TAutoConsoleVariable<float> CVarDeferredToonAOFilterRadius(
        TEXT("r.Toon.DeferredVolumes.AOFilterRadius"), 8.0f,
        TEXT("Geometry-aware AO filter radius in input render pixels before hard thresholding (0-8, default 8). Zero disables. Zero also bypasses temporal stabilization; raw AO views remain unchanged."),
        ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarDeferredToonVolumesDebugView(
		TEXT("r.Toon.DeferredVolumes.DebugView"),
		0,
		// [DeferredToon] View 20 previews the new independent two-band output mask.
		TEXT("Deferred toon volume debug view: 0 selected output mode, 1 weight, 2 volume color, 3 priority, 4-13 semantic layers, 14-20 binary reference masks, 21 raw DirectLightFacing (max N.L), 22 effective diffuse, 23 effective light response, 24 selected light band (0 dark / 1 light), 25 indirect diffuse (Lumen or dynamic sky), 26 indirect availability, 27 sky diffuse before AO, 28 sky combined AO (white=unoccluded), 29 sky split availability, 30 Lumen short AO, 31 Lumen diffuse before short AO, 32 Lumen short AO availability, 33 auto AO, 34 auto diffuse before AO, 35 auto availability, 36 provider (cyan=Lumen/orange=sky/black=none), 37 indirect two-tone mask, 38 AO two-tone mask (white=unoccluded), 39 direct specular hard mask, 40 styled direct specular, 41 native indirect specular (environment reflections), 42 indirect specular availability, 43 material emission, 44 emission availability. 18 shows effective shadowed direct specular."),
		ECVF_RenderThreadSafe);

    TAutoConsoleVariable<int32> CVarDeferredToonAOCleanup(
        TEXT("r.Toon.DeferredVolumes.AOCleanup"), 1,
        TEXT("Remove tiny dark AO features with geometry-aware hard-mask opening. 0 disables; 1 enables (default)."), ECVF_RenderThreadSafe);
    TAutoConsoleVariable<int32> CVarDeferredToonAOFilterDebug(
        TEXT("r.Toon.DeferredVolumes.AOFilterDebug"), 0,
        TEXT("Override view 38 only: 0 hard AO, 1 filtered continuous visibility, 2 filter support, 3 depth-derived normals, 4 accepted temporal history, 5 history rejection reason (white=accepted, gray=reset, cyan=outside, yellow=source, magenta=normal, red=plane, blue=motion, orange=insufficient coverage)."), ECVF_RenderThreadSafe);

    TAutoConsoleVariable<float> CVarDeferredToonAOHysteresis(
        TEXT("r.Toon.DeferredVolumes.AOHysteresis"), 0.04f,
        TEXT("Hard AO temporal hysteresis half-width (0-0.2, default 0.04). Zero disables history. Reprojects surface state; does not blend RGB or binary masks."), ECVF_RenderThreadSafe);

    TAutoConsoleVariable<float> CVarDeferredToonAOTemporalWeight(
        TEXT("r.Toon.DeferredVolumes.AOTemporalWeight"), 0.85f,
        TEXT("Continuous AO history weight before binary threshold (0-0.95, default 0.85). Zero disables scalar accumulation, keeping reprojection and hysteresis."), ECVF_RenderThreadSafe);

    TAutoConsoleVariable<int32> CVarDeferredToonAODarkConfirmationFrames(
        TEXT("r.Toon.DeferredVolumes.AODarkConfirmationFrames"), 3,
        TEXT("Consecutive reprojected dark observations before hard AO appears (1-8, default 3). One restores immediate entry. Requires temporal stabilization."), ECVF_RenderThreadSafe);
    TAutoConsoleVariable<int32> CVarDeferredToonAOMinDarkSupport(
        TEXT("r.Toon.DeferredVolumes.AOMinDarkSupport"), 5,
        TEXT("Minimum dark same-surface taps in a 3x3 neighborhood (0-9, default 5), also requiring 50 percent coverage. Zero disables the conservative fringe gate. Requires temporal stabilization."), ECVF_RenderThreadSafe);

    class FDeferredToonAOFilterPS : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FDeferredToonAOFilterPS);
        SHADER_USE_PARAMETER_STRUCT(FDeferredToonAOFilterPS, FGlobalShader);
        class FStage : SHADER_PERMUTATION_INT("AO_FILTER_STAGE", 7);
        using FPermutationDomain = TShaderPermutationDomain<FStage>;
        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsForwardShadingEnabled(Parameters.Platform);
        }
        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
            SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
            SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IndirectDiffuseTexture)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SkyDiffuseBeforeAOTexture)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, LumenDiffuseBeforeShortAOTexture)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AOGeometryTexture)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AOMomentsTexture)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AOMaskTexture)
            SHADER_PARAMETER(uint32, bSkyOcclusionAvailable)
            SHADER_PARAMETER(uint32, bLumenShortAOAvailable)
            SHADER_PARAMETER(float, AOFilterRadius)
            SHADER_PARAMETER(float, AOFilterConservatism)
            SHADER_PARAMETER(float, AOThreshold)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AOHistoryTexture)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AOHistoryGeometryTexture)
            SHADER_PARAMETER(uint32, bAOHistoryValid)
            SHADER_PARAMETER(float, AOHysteresis)
            SHADER_PARAMETER(float, AOTemporalWeight)
            SHADER_PARAMETER(uint32, AODarkConfirmationFrames)
            SHADER_PARAMETER(uint32, AOMinDarkSupport)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AOCurrentVisibilityTexture)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AOHistoryVisibilityTexture)
            SHADER_PARAMETER(FIntPoint, AOFilterDirection)
            RENDER_TARGET_BINDING_SLOTS()
        END_SHADER_PARAMETER_STRUCT()
    };
    IMPLEMENT_GLOBAL_SHADER(FDeferredToonAOFilterPS, "/Engine/Private/DeferredToonAOFilter.usf", "MainPS", SF_Pixel);

    struct FDeferredToonAOTextures
    {
        FRDGTextureRef Geometry = nullptr;
        FRDGTextureRef Moments = nullptr;
    };

    static FRDGTextureRef AddAOStage(FRDGBuilder& GraphBuilder, const FViewInfo& View,
        const FScreenPassTexture& Input, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures,
        int32 Stage, const FDeferredToonAOTextures& AO, FRDGTextureRef Mask = nullptr,
        FIntPoint Direction = FIntPoint::ZeroValue, float Threshold = 0.8f, FRDGTextureRef SecondTarget = nullptr, const FDeferredToonAOHistory* History = nullptr, FRDGTextureRef CurrentVisibility = nullptr)
    {
        const auto& Textures = View.GetSceneTextures();
        const FRDGTextureRef Black = GSystemTextures.GetBlackDummy(GraphBuilder);
        auto Produced = [](FRDGTextureRef Texture) { return Texture && HasBeenProduced(Texture); };
        auto* P = GraphBuilder.AllocParameters<FDeferredToonAOFilterPS::FParameters>();
        P->View = View.ViewUniformBuffer;
        const FScreenPassTextureViewport Viewport(Input);
        P->Input = GetScreenPassTextureViewportParameters(Viewport);
        P->SceneTextures = SceneTextures;
        P->IndirectDiffuseTexture = Produced(Textures.ToonIndirectDiffuse) ? Textures.ToonIndirectDiffuse : Black;
        P->bSkyOcclusionAvailable = Produced(Textures.ToonSkyDiffuseBeforeAO);
        P->bLumenShortAOAvailable = Produced(Textures.ToonLumenDiffuseBeforeShortAO);
        P->SkyDiffuseBeforeAOTexture = P->bSkyOcclusionAvailable ? Textures.ToonSkyDiffuseBeforeAO : Black;
        P->LumenDiffuseBeforeShortAOTexture = P->bLumenShortAOAvailable ? Textures.ToonLumenDiffuseBeforeShortAO : Black;
        P->AOGeometryTexture = AO.Geometry ? AO.Geometry : Black;
        P->AOMomentsTexture = AO.Moments ? AO.Moments : Black;
        P->AOMaskTexture = Mask ? Mask : Black;
        P->bAOHistoryValid = History != nullptr;
        P->AODarkConfirmationFrames = FMath::Clamp(CVarDeferredToonAODarkConfirmationFrames.GetValueOnRenderThread(), 1, 8);
        P->AOMinDarkSupport = FMath::Clamp(CVarDeferredToonAOMinDarkSupport.GetValueOnRenderThread(), 0, 9);
        P->AOCurrentVisibilityTexture = CurrentVisibility ? CurrentVisibility : Black;
        P->AOTemporalWeight = FMath::Clamp(CVarDeferredToonAOTemporalWeight.GetValueOnRenderThread(), 0.0f, 0.95f);
        P->AOHistoryVisibilityTexture = History ? GraphBuilder.RegisterExternalTexture(History->Visibility) : Black;
        P->AOHistoryTexture = History ? GraphBuilder.RegisterExternalTexture(History->State) : Black;
        P->AOHistoryGeometryTexture = History ? GraphBuilder.RegisterExternalTexture(View.PrevViewInfo.DeferredToonAOGeometry) : Black;
        P->AOHysteresis = FMath::Clamp(CVarDeferredToonAOHysteresis.GetValueOnRenderThread(), 0.0f, 0.2f);
        P->AOFilterDirection = Direction;
        P->AOFilterRadius = FMath::Clamp(CVarDeferredToonAOFilterRadius.GetValueOnRenderThread(), 0.0f, 8.0f);
        P->AOFilterConservatism = FMath::Clamp(CVarDeferredToonAOFilterConservatism.GetValueOnRenderThread(), 0.0f, 2.0f);
        P->AOThreshold = Threshold;
        const FRDGTextureRef Output = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
            Input.Texture->Desc.Extent, Stage == 0 ? PF_FloatRGBA : ((Stage == 1 || Stage == 5 || Stage == 6) ? PF_A32B32G32R32F : PF_R16F), FClearValueBinding::None,
            TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("DeferredToon.AOSpatial"));
        P->RenderTargets[0] = FRenderTargetBinding(Output, ERenderTargetLoadAction::ENoAction);
        if (SecondTarget) { P->RenderTargets[1] = FRenderTargetBinding(SecondTarget, ERenderTargetLoadAction::ENoAction); }
        FDeferredToonAOFilterPS::FPermutationDomain Permutation;
        Permutation.Set<FDeferredToonAOFilterPS::FStage>(Stage);
        TShaderMapRef<FDeferredToonAOFilterPS> Shader(View.ShaderMap, Permutation);
        AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("DeferredToon AO Stage %d (%d,%d)", Stage, Direction.X, Direction.Y),
            View, Viewport, Viewport, Shader, P);
        return Output;
    }

    static FDeferredToonAOTextures AddAOFilter(FRDGBuilder& GraphBuilder, const FViewInfo& View,
        const FScreenPassTexture& Input, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
    {
        FDeferredToonAOTextures AO;
        const auto& Textures = View.GetSceneTextures();
        if (CVarDeferredToonAOFilterRadius.GetValueOnRenderThread() <= 0 || !Textures.ToonIndirectDiffuse
            || !HasBeenProduced(Textures.ToonIndirectDiffuse)) { return AO; }
        const bool bSky = Textures.ToonSkyDiffuseBeforeAO && HasBeenProduced(Textures.ToonSkyDiffuseBeforeAO);
        const bool bLumen = Textures.ToonLumenDiffuseBeforeShortAO && HasBeenProduced(Textures.ToonLumenDiffuseBeforeShortAO);
        if (!bSky && !bLumen) { return AO; }
        // FP32 moments avoid FP16 rounding turning a flat field into apparent variance.
        const FRDGTextureRef Seed = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
            Input.Texture->Desc.Extent, PF_A32B32G32R32F, FClearValueBinding::None,
            TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("DeferredToon.AOSeedMoments"));
        AO.Geometry = AddAOStage(GraphBuilder, View, Input, SceneTextures, 0, AO, nullptr, FIntPoint::ZeroValue, 0, Seed);
        AO.Moments = Seed;
        AO.Moments = AddAOStage(GraphBuilder, View, Input, SceneTextures, 1, AO, nullptr, FIntPoint(1,0));
        AO.Moments = AddAOStage(GraphBuilder, View, Input, SceneTextures, 1, AO, nullptr, FIntPoint(0,1));
        return AO;
    }

    // Continuous history must be resolved BEFORE thresholding and morphology.
    static FRDGTextureRef AddAOHardMask(FRDGBuilder& GraphBuilder, const FViewInfo& View,
        const FScreenPassTexture& Input, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures,
        const FDeferredToonVolumeSceneProxy& Volume, FDeferredToonAOTextures& AO)
    {
        const float Hysteresis = FMath::Clamp(CVarDeferredToonAOHysteresis.GetValueOnRenderThread(), 0.0f, 0.2f);
        const float TemporalWeight = FMath::Clamp(CVarDeferredToonAOTemporalWeight.GetValueOnRenderThread(), 0.0f, 0.95f);
        const bool bTemporal = View.ViewState && Hysteresis > 0;
        const FVector2f ConfirmationSettings(
            float(FMath::Clamp(CVarDeferredToonAODarkConfirmationFrames.GetValueOnRenderThread(), 1, 8)),
            float(FMath::Clamp(CVarDeferredToonAOMinDarkSupport.GetValueOnRenderThread(), 0, 9)));
        const FVector4f Settings(CVarDeferredToonAOFilterRadius.GetValueOnRenderThread(),
            CVarDeferredToonAOFilterConservatism.GetValueOnRenderThread(), Hysteresis,
            float(CVarDeferredToonAOCleanup.GetValueOnRenderThread() != 0));
        const auto* Previous = View.PrevViewInfo.DeferredToonAOHistories.Find(Volume.GetStableSortPath());
        const FDeferredToonAOHistory* History = Previous ? Previous->Get() : nullptr;
        if (!bTemporal || View.bCameraCut || View.bPrevTransformsReset || !History || !History->State || !History->Visibility
            || !View.PrevViewInfo.DeferredToonAOGeometry || History->ViewRect != Input.ViewRect
            || History->State->GetDesc().Extent != Input.Texture->Desc.Extent
            || History->Visibility->GetDesc().Extent != Input.Texture->Desc.Extent
            || History->FilterSettings != Settings || History->Threshold != Volume.IndirectOcclusionThreshold
            || History->TemporalWeight != TemporalWeight || History->ConfirmationSettings != ConfirmationSettings)
        {
            History = nullptr;
        }
        FRDGTextureRef Visibility = nullptr;
        if (bTemporal)
        {
            Visibility = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
                Input.Texture->Desc.Extent, PF_G16R16F, FClearValueBinding::None,
                TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("DeferredToon.AOCurrentVisibility"));
            AO.Moments = AddAOStage(GraphBuilder, View, Input, SceneTextures, 6, AO, nullptr,
                FIntPoint::ZeroValue, Volume.IndirectOcclusionThreshold, Visibility, History);
        }
        FRDGTextureRef Mask = AddAOStage(GraphBuilder, View, Input, SceneTextures, 2, AO, nullptr,
            FIntPoint::ZeroValue, Volume.IndirectOcclusionThreshold);
        if (CVarDeferredToonAOCleanup.GetValueOnRenderThread() != 0)
        {
            Mask = AddAOStage(GraphBuilder, View, Input, SceneTextures, 3, AO, Mask);
            Mask = AddAOStage(GraphBuilder, View, Input, SceneTextures, 4, AO, Mask,
                FIntPoint::ZeroValue, Volume.IndirectOcclusionThreshold);
        }
        if (!bTemporal) { return Mask; }
        const FRDGTextureRef ConfirmedVisibility = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
            Input.Texture->Desc.Extent, PF_FloatRGBA, FClearValueBinding::None,
            TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("DeferredToon.AOConfirmedVisibilityHistory"));
        const FRDGTextureRef State = AddAOStage(GraphBuilder, View, Input, SceneTextures, 5, AO, Mask,
            FIntPoint::ZeroValue, Volume.IndirectOcclusionThreshold, ConfirmedVisibility, History, Visibility);
        if (!View.bStatePrevViewInfoIsReadOnly)
        {
            auto Next = MakeShared<FDeferredToonAOHistory>();
            Next->ViewRect = Input.ViewRect;
            Next->FilterSettings = Settings;
            Next->Threshold = Volume.IndirectOcclusionThreshold;
            Next->TemporalWeight = TemporalWeight;
            Next->ConfirmationSettings = ConfirmationSettings;
            View.ViewState->PrevFrameViewInfo.DeferredToonAOHistories.Add(Volume.GetStableSortPath(), Next);
            GraphBuilder.QueueTextureExtraction(State, &Next->State);
            GraphBuilder.QueueTextureExtraction(ConfirmedVisibility, &Next->Visibility);
        }
        return State;
    }

    static bool NeedsHardAO(const FDeferredToonVolumeSceneProxy& Volume, uint32 DebugView)
    {
        return DebugView == 38u || (Volume.bHardIndirectOcclusion
            && (DebugView == 37u || (DebugView == 0u && uint32(Volume.OutputMode) == 4u)));
    }

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
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IndirectSpecularTexture)
            SHADER_PARAMETER(uint32, bIndirectSpecularAvailable)
            SHADER_PARAMETER(uint32, bOverrideIndirectSpecular)
            SHADER_PARAMETER(float, IndirectSpecularIntensity)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EmissiveTexture)
            SHADER_PARAMETER(uint32, bEmissiveAvailable)
            SHADER_PARAMETER(uint32, bOverrideEmissive)
            SHADER_PARAMETER(float, EmissiveIntensity)
            // [DeferredToon] Visible direct-specular band controls.
            SHADER_PARAMETER(uint32, bSpecularAvailable)
            SHADER_PARAMETER(uint32, DirectSpecularStyle)
            SHADER_PARAMETER(float, DirectSpecularThreshold)
            SHADER_PARAMETER(uint32, DirectSpecularColorMode)
            SHADER_PARAMETER(FVector4f, DirectSpecularColor)
            SHADER_PARAMETER(float, DirectSpecularIntensity)
			SHADER_PARAMETER(uint32, bOverrideIndirectDiffuse)
			SHADER_PARAMETER(uint32, IndirectDiffuseStyle)
            SHADER_PARAMETER(float, IndirectDiffuseThreshold)
            SHADER_PARAMETER(FVector4f, IndirectDiffuseDarkColor)
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
			SHADER_PARAMETER(uint32, bHardIndirectOcclusion)
            SHADER_PARAMETER(float, IndirectOcclusionThreshold)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AOMomentsTexture)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AOGeometryTexture)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AOMaskTexture)
            SHADER_PARAMETER(uint32, bFilteredAOAvailable)
            SHADER_PARAMETER(uint32, AOFilterDebug)
            SHADER_PARAMETER(uint32, bUnifiedIndirectOcclusion) // [DeferredToon]
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
                && (Descriptor->Source != EDeferredToonInputSource::SkyDiffuseBeforeAOBuffer || Parameters.bSkyOcclusionAvailable != 0u)
                && (Descriptor->Source != EDeferredToonInputSource::IndirectOcclusionBuffers || (Parameters.bSkyOcclusionAvailable != 0u || Parameters.bLumenShortAOAvailable != 0u)) ? 1u : 0u; // [DeferredToon]
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
        const FDeferredToonAOTextures& SharedAO,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
	{
		const FScreenPassTexture Input = FScreenPassTexture::CopyFromSlice(GraphBuilder, SceneColor);
		const FScreenPassTextureViewport InputViewport(Input);
		// [DeferredToon] The details-panel selector is a per-volume remote
		// control.  255 preserves the legacy global console variable path.
		const uint32 DebugView = Volume.DebugView == 255u
			? uint32(GetDeferredToonVolumeDebugView())
			: FMath::Min<uint32>(Volume.DebugView, 44u); // [DeferredToon] Band preview appended; existing IDs preserved.
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
        const FRDGTextureRef IndirectSpecularTexture = View.GetSceneTextures().ToonIndirectSpecular;
        PassParameters->bIndirectSpecularAvailable = IndirectSpecularTexture && HasBeenProduced(IndirectSpecularTexture) ? 1u : 0u;
        PassParameters->IndirectSpecularTexture = PassParameters->bIndirectSpecularAvailable ? IndirectSpecularTexture : GSystemTextures.GetBlackDummy(GraphBuilder);
        // [DeferredToon] Missing exports retain the native residual, never synthesize a band.
        PassParameters->bSpecularAvailable = View.GetSceneTextures().SpecularBuffer && HasBeenProduced(View.GetSceneTextures().SpecularBuffer) ? 1u : 0u;
        PassParameters->DirectSpecularStyle = Volume.DirectSpecularStyle;
        PassParameters->DirectSpecularThreshold = Volume.DirectSpecularThreshold;
        PassParameters->DirectSpecularColorMode = Volume.DirectSpecularColorMode;
        PassParameters->DirectSpecularColor = FVector4f(Volume.DirectSpecularColor);
        PassParameters->DirectSpecularIntensity = Volume.DirectSpecularIntensity;
        const FRDGTextureRef EmissiveTexture = View.GetSceneTextures().ToonEmissive;
        PassParameters->bEmissiveAvailable = EmissiveTexture && HasBeenProduced(EmissiveTexture) ? 1u : 0u;
        PassParameters->EmissiveTexture = PassParameters->bEmissiveAvailable ? EmissiveTexture : GSystemTextures.GetBlackDummy(GraphBuilder);
        PassParameters->bOverrideEmissive = Volume.bOverrideEmissive ? 1u : 0u;
        PassParameters->EmissiveIntensity = Volume.EmissiveIntensity;
        PassParameters->bOverrideIndirectSpecular = Volume.bOverrideIndirectSpecular ? 1u : 0u;
        PassParameters->IndirectSpecularIntensity = Volume.IndirectSpecularIntensity;
        PassParameters->bOverrideIndirectDiffuse = Volume.bOverrideIndirectDiffuse ? 1u : 0u;
        PassParameters->IndirectDiffuseIntensity = Volume.IndirectDiffuseIntensity;
        PassParameters->IndirectDiffuseColor = FVector4f(Volume.IndirectDiffuseColor.R, Volume.IndirectDiffuseColor.G, Volume.IndirectDiffuseColor.B, 1);
        PassParameters->IndirectDiffuseColorReplaceWeight = Volume.IndirectDiffuseColorReplaceWeight;
        PassParameters->IndirectDiffuseStyle = Volume.IndirectDiffuseStyle;
        PassParameters->IndirectDiffuseThreshold = Volume.IndirectDiffuseThreshold;
        PassParameters->IndirectDiffuseDarkColor = FVector4f(Volume.IndirectDiffuseDarkColor.R, Volume.IndirectDiffuseDarkColor.G, Volume.IndirectDiffuseDarkColor.B, 1);
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
        PassParameters->bHardIndirectOcclusion = Volume.bHardIndirectOcclusion ? 1u : 0u;
        PassParameters->IndirectOcclusionThreshold = Volume.IndirectOcclusionThreshold;
        FRDGTextureRef HardMask = nullptr;
        FDeferredToonAOTextures VolumeAO = SharedAO;
        if (SharedAO.Moments && NeedsHardAO(Volume, DebugView))
        {
            HardMask = AddAOHardMask(GraphBuilder, View, Input, SceneTextures, Volume, VolumeAO);
        }
        PassParameters->bFilteredAOAvailable = HardMask ? 1u : 0u;
        PassParameters->AOMaskTexture = HardMask ? HardMask : GSystemTextures.GetBlackDummy(GraphBuilder);
        PassParameters->AOMomentsTexture = VolumeAO.Moments ? VolumeAO.Moments : GSystemTextures.GetBlackDummy(GraphBuilder);
        PassParameters->AOGeometryTexture = SharedAO.Geometry ? SharedAO.Geometry : GSystemTextures.GetBlackDummy(GraphBuilder);
        PassParameters->AOFilterDebug = FMath::Clamp(CVarDeferredToonAOFilterDebug.GetValueOnRenderThread(), 0, 5);
        PassParameters->bUnifiedIndirectOcclusion = Volume.bUnifiedIndirectOcclusion ? 1u : 0u; // [DeferredToon]
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
	return FMath::Clamp(CVarDeferredToonVolumesDebugView.GetValueOnRenderThread(), 0, 44); // [DeferredToon] Appended indirect diffuse diagnostics.
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
    // Shared continuous filtering is independent of a box's threshold/palette.
    FDeferredToonAOTextures SharedAO;
    for (const FDeferredToonVolumeSceneProxy* Volume : Volumes)
    {
        if (Volume && NeedsHardAO(*Volume, Volume->DebugView == 255u ? uint32(GetDeferredToonVolumeDebugView()) : Volume->DebugView))
        {
            SharedAO = AddAOFilter(GraphBuilder, View, OriginalSceneColor, Inputs.SceneTextures);
            if (SharedAO.Geometry && View.ViewState && !View.bStatePrevViewInfoIsReadOnly
                && CVarDeferredToonAOHysteresis.GetValueOnRenderThread() > 0)
            {
                GraphBuilder.QueueTextureExtraction(SharedAO.Geometry, &View.ViewState->PrevFrameViewInfo.DeferredToonAOGeometry);
            }
            break;
        }
    }
	for (const FDeferredToonVolumeSceneProxy* Volume : Volumes)
	{
		if (Volume)
		{
			Current = AddVolumePass(GraphBuilder, View, Current, OriginalSceneColor, *Volume, SharedAO, Inputs.SceneTextures);
		}
	}
	return Current;
}
