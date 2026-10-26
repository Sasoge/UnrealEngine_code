#include "Modules/ModuleManager.h"
#include "SToonLightingDebugger.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"
#include "HAL/IConsoleManager.h"

class FDeferredToonDebuggerModule : public IModuleInterface
{
    TWeakPtr<SToonLightingDebugger> Panel;
    TWeakPtr<SDockTab> Tab;
    TUniquePtr<FAutoConsoleCommand> OpenCommand;
    const FName TabName = TEXT("ToonLightingDebugger");
public:
    virtual void StartupModule() override
    {
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName,
            FOnSpawnTab::CreateRaw(this, &FDeferredToonDebuggerModule::Spawn))
            .SetDisplayName(FText::FromString(TEXT("Toon 光照调试 / Lighting Debugger")))
            .SetMenuType(ETabSpawnerMenuType::Hidden);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDeferredToonDebuggerModule::RegisterMenus));
        OpenCommand = MakeUnique<FAutoConsoleCommand>(TEXT("Toon.Debug.Open"), TEXT("Open the Toon Lighting Debugger editor tab."),
            FConsoleCommandDelegate::CreateRaw(this, &FDeferredToonDebuggerModule::Open));
    }
    void Open() { FGlobalTabmanager::Get()->TryInvokeTab(TabName); }
    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
        Menu->FindOrAddSection(TEXT("WindowLayout")).AddMenuEntry(TEXT("ToonLightingDebugger"),
            FText::FromString(TEXT("Toon 光照调试 / Lighting Debugger")),
            FText::FromString(TEXT("Preview lighting layers without changing saved volume settings.")),
            FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FDeferredToonDebuggerModule::Open)));
    }
    TSharedRef<SDockTab> Spawn(const FSpawnTabArgs& Args)
    {
        TSharedRef<SToonLightingDebugger> NewPanel = SNew(SToonLightingDebugger);
        Panel = NewPanel;
        TSharedRef<SDockTab> NewTab = SNew(SDockTab).TabRole(ETabRole::NomadTab)
            .OnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([Weak = Panel](TSharedRef<SDockTab>)
            { if (auto Widget = Weak.Pin()) { Widget->StopPreview(); } }))
            [NewPanel];
        Tab = NewTab;
        return NewTab;
    }
    virtual void ShutdownModule() override
    {
        OpenCommand.Reset();
        if (auto Widget = Panel.Pin()) { Widget->StopPreview(); }
        if (auto OpenTab = Tab.Pin()) { OpenTab->RequestCloseTab(); }
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
    }
};
IMPLEMENT_MODULE(FDeferredToonDebuggerModule, DeferredToonDebugger)
