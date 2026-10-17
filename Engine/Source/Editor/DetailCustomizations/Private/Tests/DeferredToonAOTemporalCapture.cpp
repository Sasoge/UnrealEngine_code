// Copyright Epic Games, Inc. All Rights Reserved.

// Ordinary PIE screenshots retain temporal histories; HighResShot does not.
// An offscreen embedded PIE viewport otherwise inherits a tiny Slate layout size.
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Slate/SceneViewport.h"

static FAutoConsoleCommand DeferredToonTestViewportSize(
    TEXT("Toon.Test.ViewportSize"),
    TEXT("PIE AO regression capture size: width height. Use 0 0 to release the fixed size."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        if (Args.Num() == 2 && GEngine && GEngine->GameViewport)
        {
            if (FSceneViewport* Viewport = GEngine->GameViewport->GetGameViewport())
            {
                const int32 Width = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 4096);
                const int32 Height = FMath::Clamp(FCString::Atoi(*Args[1]), 0, 4096);
                Viewport->SetFixedViewportSize(Width, Height);
            }
        }
    }));
#endif
