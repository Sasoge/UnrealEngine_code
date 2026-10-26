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

// [DeferredToon] Translates the grouped console index to the stable shader ID used by the
// pre-tonemap volume pass.  Post-processing uses it to present the debug image
// at the final view-family resolution, matching UE's buffer visualization path.
RENDERER_API int32 GetDeferredToonVolumeDebugView();

FScreenPassTextureSlice AddDeferredToonVolumePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FScene* Scene,
	const FDeferredToonVolumePassInputs& Inputs);
