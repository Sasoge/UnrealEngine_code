#pragma once

#include "ScreenPass.h"

// [DeferredToon] Pre-tonemap RDG pass interface.
class FScene;
class FDeferredToonVolumeSceneProxy;
class FSceneTextureUniformParameters;
class FViewInfo;

struct FDeferredToonVolumePassInputs
{
	FScreenPassTextureSlice SceneColor;
	FScreenPassTexture SceneDepth;
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures;
};

// [DeferredToon] Returns the clamped debug presentation mode used by the
// pre-tonemap volume pass.  Post-processing uses it to present the debug image
// at the final view-family resolution, matching UE's buffer visualization path.
RENDERER_API int32 GetDeferredToonVolumeDebugView();

// [DeferredToon] Smooth the pre-tonemap native ShadowBuffer with a small
// per-view history.  The returned texture is also cached on the current view
// so the debug presentation and all volume passes consume the same result.
RENDERER_API FRDGTextureRef AddDeferredToonShadowBufferTemporalPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef ShadowBuffer);

FScreenPassTextureSlice AddDeferredToonVolumePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FScene* Scene,
	const FDeferredToonVolumePassInputs& Inputs);
