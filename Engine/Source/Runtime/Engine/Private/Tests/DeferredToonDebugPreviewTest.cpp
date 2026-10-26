// [DeferredToon] Non-destructive temporary preview and stale-owner cleanup.
#include "DeferredToonDebugPreview.h"
#include "Components/DeferredToonVolumeComponent.h"
#include "Misc/AutomationTest.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "UObject/Package.h"
#include "HAL/IConsoleManager.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeferredToonDebugPreviewTest, "Engine.DeferredToon.DebugPreviewOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDeferredToonDebugPreviewTest::RunTest(const FString& Parameters)
{
    using namespace DeferredToonDebugPreview;
    auto Read = []()
    {
        FState Snapshot;
        ENQUEUE_RENDER_COMMAND(ReadToonDebugTest)([&Snapshot](FRHICommandListImmediate&) { Snapshot = Get_RenderThread(); });
        FlushRenderingCommands();
        return Snapshot;
    };
    // Do not interrupt an interactive session if the test is invoked manually.
    if (Read().bActive) { AddWarning(TEXT("Close the active Toon preview before running this test.")); return false; }
    const uint64 First = AllocateOwnerId(), Second = AllocateOwnerId();
    UPackage* Package = CreatePackage(TEXT("/Temp/DeferredToonPreviewOwnership"));
    auto* A = NewObject<UDeferredToonVolumeComponent>(Package);
    auto* B = NewObject<UDeferredToonVolumeComponent>(Package);
    A->DebugView = EDeferredToonDebugView::SpecularDirectLight;
    Package->SetDirtyFlag(false);
    auto* GlobalView = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Toon.DeferredVolumes.DebugView"));
    auto* GlobalAO = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Toon.DeferredVolumes.AOFilterDebug"));
    const int32 ViewBefore = GlobalView ? GlobalView->GetInt() : 0;
    const int32 AOBefore = GlobalAO ? GlobalAO->GetInt() : 0;
    Set(First, A, 80, 4);
    auto State = Read();
    TestTrue(TEXT("Exact target matches"), State.Matches(FObjectKey(A)));
    TestFalse(TEXT("Another component cannot inherit preview"), State.Matches(FObjectKey(B)));
    TestEqual(TEXT("Public 80 maps to stable reflection ID"), State.ShaderId, 41);
    TestEqual(TEXT("Local AO submode"), State.AOFilterMode, 4);
    Set(Second, B, 51, 999);
    Clear(First);
    State = Read();
    TestTrue(TEXT("Late old tab close cannot clear new preview"), State.Matches(FObjectKey(B)));
    TestEqual(TEXT("AO two-tone requests stable shader 38"), State.ShaderId, 38);
    TestEqual(TEXT("AO submode clamped"), State.AOFilterMode, 5);
    Set(Second, B, 0, 0);
    TestTrue(TEXT("Normal output remains an explicit override"), Read().bActive);
    TestEqual(TEXT("Normal output shader"), Read().ShaderId, 0);
    Clear(Second);
    TestFalse(TEXT("Release removes override"), Read().bActive);
    TestEqual(TEXT("Saved component view unchanged"), static_cast<int32>(A->DebugView), 18);
    TestFalse(TEXT("Preview never dirties the asset"), Package->IsDirty());
    if (GlobalView) { TestEqual(TEXT("Global view untouched"), GlobalView->GetInt(), ViewBefore); }
    if (GlobalAO) { TestEqual(TEXT("Global AO untouched"), GlobalAO->GetInt(), AOBefore); }
    return true;
}
#endif
