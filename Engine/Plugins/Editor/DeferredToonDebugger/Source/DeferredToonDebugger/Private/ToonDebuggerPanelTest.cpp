#include "SToonLightingDebugger.h"
#include "Components/DeferredToonVolumeComponent.h"
#include "DeferredToonDebugPreview.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "EngineUtils.h"
#include "Engine/Selection.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Widgets/Layout/SScrollBox.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "UObject/Package.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FToonDebuggerPanelTest, "Editor.DeferredToon.DebuggerPanel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FToonDebuggerPanelTest::RunTest(const FString& Parameters)
{
    auto Read = []()
    {
        DeferredToonDebugPreview::FState State;
        ENQUEUE_RENDER_COMMAND(ReadToonPanelTest)([&State](FRHICommandListImmediate&) { State = DeferredToonDebugPreview::Get_RenderThread(); });
        FlushRenderingCommands();
        return State;
    };
    if (Read().bActive) { AddError(TEXT("Release interactive preview before running panel test.")); return false; }
    UDeferredToonVolumeComponent* Target = nullptr;
    for (TActorIterator<AActor> It(GEditor->GetEditorWorldContext().World()); It; ++It)
    {
        auto* Candidate = It->FindComponentByClass<UDeferredToonVolumeComponent>();
        if (Candidate && Candidate->bEnabled) { Target = Candidate; break; }
    }
    if (!Target) { AddError(TEXT("Open /Game/NewMap with an enabled DeferredToonVolume for this editor integration test.")); return false; }
    const auto SavedView = Target->DebugView;
    const bool bWasDirty = Target->GetOutermost()->IsDirty();
    auto* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Toon.DeferredVolumes.DebugView"));
    auto* AOVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Toon.DeferredVolumes.AOFilterDebug"));
    const int32 SavedCVar = CVar->GetInt(), SavedAO = AOVar->GetInt();
    TSharedRef<SToonLightingDebugger> Panel = SNew(SToonLightingDebugger);
    FWidgetRenderer* WidgetRenderer = new FWidgetRenderer(true);
    Panel->bFollowSelection = false;
    Panel->SetTarget(Target);
    TestTrue(TEXT("Target is usable"), Panel->CanPreview());
    // A paused viewport catches accidental force-realtime regressions even when
    // every viewport in the test map normally uses realtime rendering.
    const FText TestRealtimeOwner = FText::FromString(TEXT("Toon debugger realtime regression test"));
    FLevelEditorViewportClient* PausedClient = nullptr;
    for (auto* Client : GEditor->GetLevelViewportClients())
    {
        if (Client && Client->GetWorld() == Target->GetWorld())
        {
            PausedClient = Client;
            Client->AddRealtimeOverride(false, TestRealtimeOwner);
            break;
        }
    }
    TestNotNull(TEXT("Target scene viewport exists"), PausedClient);
    TArray<TPair<FLevelEditorViewportClient*, bool>> RealtimeStates;
    for (auto* Client : GEditor->GetLevelViewportClients())
    {
        if (Client) { RealtimeStates.Emplace(Client, Client->IsRealtime()); }
    }
    TestTrue(TEXT("Editor viewport available for realtime preservation check"), !RealtimeStates.IsEmpty());
    Panel->Preview(80);
    Panel->Tick(FGeometry(), FPlatformTime::Seconds(), 0.3f);
    for (const auto& State : RealtimeStates)
    {
        TestEqual(TEXT("Preview and idle tick preserve viewport realtime"), State.Key->IsRealtime(), State.Value);
    }
    TestTrue(TEXT("Panel locks the exact component"), Read().Matches(FObjectKey(Target)));
    TestEqual(TEXT("Reflection route"), Read().ShaderId, 41);
    Panel->Preview(51);
    TestEqual(TEXT("Previous view recorded"), Panel->PreviousIndex, 80);
    Panel->AOFilterMode = 4;
    Panel->RefreshPreview();
    TestEqual(TEXT("AO history route"), Read().AOFilterMode, 4);
    Panel->Preview(Panel->PreviousIndex);
    TestEqual(TEXT("Back button toggles view"), Panel->CurrentIndex, 80);
    TestEqual(TEXT("Back button retains return point"), Panel->PreviousIndex, 51);
    Panel->Group=5; Panel->Search=TEXT("80"); Panel->RebuildViews();
    TestEqual(TEXT("Cross-category numeric search has one result"), Panel->Views->NumSlots(), 1);
    Panel->Search=TEXT("自发光");Panel->RebuildViews();
    TestEqual(TEXT("Chinese search finds color and availability"), Panel->Views->NumSlots(), 2);
    Panel->Search=TEXT("emissive");Panel->RebuildViews();
    TestEqual(TEXT("English search works in Chinese UI"), Panel->Views->NumSlots(), 2);
    Panel->Search.Empty();Panel->Group=-2;Panel->Favorites.Empty();Panel->Favorites.Add(TEXT("Emissive"));Panel->RebuildViews();
    TestEqual(TEXT("Favorite filter"), Panel->Views->NumSlots(), 1);
    Panel->Group=5;Panel->Preview(51);
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Automation/ToonDebugger");
    IFileManager::Get().MakeDirectory(*Directory,true);
    for (bool bChinese : {true,false})
    {
        Panel->bChinese=bChinese;Panel->Build();
        TestEqual(TEXT("AO group has six views"), Panel->Views->NumSlots(), 6);
        // Render the real Slate widget into a transient RT, without a native window or focus changes.
        // Auto-wrapped Slate text needs one layout frame before capture.
        WidgetRenderer->DrawWidget(Panel, FVector2D(720,640));
        FlushRenderingCommands();
        UTextureRenderTarget2D* RT = WidgetRenderer->DrawWidget(Panel, FVector2D(720,640));
        FlushRenderingCommands();
        TArray<FColor> Pixels;
        const bool bRead = RT && RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TestTrue(TEXT("Offscreen panel render"), bRead);
        if (bRead)
        {
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(720,640,Pixels,PNG);
            TestTrue(TEXT("Save layout evidence"), FFileHelper::SaveArrayToFile(PNG,*(Directory/(bChinese?TEXT("panel-zh.png"):TEXT("panel-en.png")))));
        }
    }
    Panel->Preview(0);
    TestTrue(TEXT("Normal is an explicit temporary override"), Read().bActive);
    TestEqual(TEXT("Normal route"), Read().ShaderId, 0);
    Panel->OnBeginPIE(false);
    TestFalse(TEXT("PIE releases preview"), Read().bActive);
    Panel->Preview(20);
    Panel->OnMapChange(0);
    TestFalse(TEXT("Map change releases preview"), Read().bActive);
    TestFalse(TEXT("Map change clears target"), Panel->Target.IsValid());
    Panel->SetTarget(Target);Panel->Preview(90);Panel->StopPreview();
    TestFalse(TEXT("Close handler releases preview"), Read().bActive);
    TestEqual(TEXT("Component selection unchanged"), static_cast<int32>(Target->DebugView), static_cast<int32>(SavedView));
    TestEqual(TEXT("Map dirtiness unchanged"), Target->GetOutermost()->IsDirty(), bWasDirty);
    TestEqual(TEXT("Global view unchanged"), CVar->GetInt(), SavedCVar);
    TestEqual(TEXT("Global AO unchanged"), AOVar->GetInt(), SavedAO);
    for (const auto& State : RealtimeStates)
    {
        TestEqual(TEXT("Release preserves viewport realtime"), State.Key->IsRealtime(), State.Value);
    }
    if (PausedClient) { PausedClient->RemoveRealtimeOverride(TestRealtimeOwner, false); }
    BeginCleanup(WidgetRenderer);
    return true;
}
#endif
