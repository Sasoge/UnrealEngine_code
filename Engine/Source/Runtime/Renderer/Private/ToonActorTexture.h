// ----------------------------------YK Engine Start-----------------------------------------
// ToonActorTexture
#pragma once

#include "CoreMinimal.h"
#include "RenderGraphUtils.h"

class FViewInfo;
// ToonActorTexture
FRDGTextureRef CreateToonActorTexture(FRDGBuilder& GraphBuilder);

// ToonActorTextureCSPass用于把ToonActor上的数据保存到一张ToonActorTexture上
void AddToonActorTextureCSPass(FRDGBuilder& GraphBuilder, FScene* Scene,FViewInfo& View, FSceneTextures& SceneTextures);

// ----------------------------------YK Engine End-------------------------------------------