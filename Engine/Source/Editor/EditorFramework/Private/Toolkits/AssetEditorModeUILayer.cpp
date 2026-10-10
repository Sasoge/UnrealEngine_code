// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/AssetEditorModeUILayer.h"

#include "Framework/Docking/WorkspaceItem.h"
#include "HAL/Platform.h"
#include "Misc/Attribute.h"
#include "Textures/SlateIcon.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SNullWidget.h"
#include "Toolkits/IToolkit.h"

#define LOCTEXT_NAMESPACE "SLevelEditorToolBox"

const FName UAssetEditorUISubsystem::TopLeftTabID(TEXT("TopLeftModeTab"));
const FName UAssetEditorUISubsystem::BottomLeftTabID(TEXT("BottomLeftModeTab"));
const FName UAssetEditorUISubsystem::TopRightTabID(TEXT("TopRightModeTab"));
const FName UAssetEditorUISubsystem::BottomRightTabID(TEXT("BottomRightModeTab"));
const FName UAssetEditorUISubsystem::VerticalToolbarID = TEXT("VerticalModeToolbar");

FAssetEditorModeUILayer::FAssetEditorModeUILayer(const IToolkitHost* InToolkitHost)
	: ToolkitHost(InToolkitHost)
	, ModeCommands( new FUICommandList() )

{
	RequestedTabInfo.Add(UAssetEditorUISubsystem::VerticalToolbarID, FMinorTabConfig(UAssetEditorUISubsystem::VerticalToolbarID));
	RequestedTabInfo.Add(UAssetEditorUISubsystem::TopLeftTabID, FMinorTabConfig(UAssetEditorUISubsystem::TopLeftTabID));
	RequestedTabInfo.Add(UAssetEditorUISubsystem::BottomLeftTabID, FMinorTabConfig(UAssetEditorUISubsystem::BottomLeftTabID));
	RequestedTabInfo.Add(UAssetEditorUISubsystem::TopRightTabID, FMinorTabConfig(UAssetEditorUISubsystem::TopRightTabID));
	RequestedTabInfo.Add(UAssetEditorUISubsystem::BottomRightTabID, FMinorTabConfig(UAssetEditorUISubsystem::BottomRightTabID));
}

void FAssetEditorModeUILayer::OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit)
{
	const TSharedPtr<FTabManager> TabManager = GetTabManager();
	if (!TabManager.IsValid())
	{
		return;
	}

	TabManager->UnregisterTabSpawner(UAssetEditorUISubsystem::TopLeftTabID);
	TabManager->UnregisterTabSpawner(UAssetEditorUISubsystem::BottomLeftTabID);
	TabManager->UnregisterTabSpawner(UAssetEditorUISubsystem::VerticalToolbarID);
	TabManager->UnregisterTabSpawner(UAssetEditorUISubsystem::TopRightTabID);
	TabManager->UnregisterTabSpawner(UAssetEditorUISubsystem::BottomRightTabID);
}

void FAssetEditorModeUILayer::OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit)
{
	OnToolkitHostShutdownUI.ExecuteIfBound();
	if (HostedToolkit.IsValid() && HostedToolkit.Pin() == Toolkit)
	{
		const TSharedPtr<FTabManager> TabManager = GetTabManager();

		for (TPair<FName, TWeakPtr<SDockTab>>& SpawnedTab : SpawnedTabs)
		{
			if (SpawnedTab.Value.IsValid())
			{
				SpawnedTab.Value.Pin()->SetContent(SNullWidget::NullWidget);
				SpawnedTab.Value.Pin()->RequestCloseTab();
			}
		}
		for (TPair<FName, FMinorTabConfig>& TabSpawnerInfo : RequestedTabInfo)
		{
			TabSpawnerInfo.Value = FMinorTabConfig(TabSpawnerInfo.Key);
			if (TabManager.IsValid())
			{
				TabManager->UnregisterTabSpawner(TabSpawnerInfo.Key);
			}
		}
		SpawnedTabs.Empty();
	}
}

TSharedPtr<FTabManager> FAssetEditorModeUILayer::GetTabManager()
{
	return ToolkitHost ? ToolkitHost->GetTabManager() : TSharedPtr<FTabManager>();
}


TSharedPtr<FWorkspaceItem> FAssetEditorModeUILayer::GetModeMenuCategory() const
{
	return TSharedPtr<FWorkspaceItem>();
}

void FAssetEditorModeUILayer::RegisterModeTabSpawners()
{
	RegisterModeTabSpawner(UAssetEditorUISubsystem::TopLeftTabID);
	RegisterModeTabSpawner(UAssetEditorUISubsystem::BottomLeftTabID);
	RegisterModeTabSpawner(UAssetEditorUISubsystem::VerticalToolbarID);
	RegisterModeTabSpawner(UAssetEditorUISubsystem::TopRightTabID);
	RegisterModeTabSpawner(UAssetEditorUISubsystem::BottomRightTabID);

}

void FAssetEditorModeUILayer::RegisterModeTabSpawner(const FName TabID)
{
	const TSharedPtr<FTabManager> TabManager = GetTabManager();
	if (!TabManager.IsValid())
	{
		return;
	}

	TSharedPtr<FWorkspaceItem> MenuGroup = GetModeMenuCategory();
	bool bShowMenuOption = GetStoredSpawner(TabID).IsBound();
	FTabSpawnerEntry& TabSpawnerEntry = TabManager->RegisterTabSpawner(TabID,
		FOnSpawnTab::CreateSP(this, &FAssetEditorModeUILayer::SpawnStoredTab, TabID),
		FCanSpawnTab::CreateSP(this, &FAssetEditorModeUILayer::CanSpawnStoredTab, TabID))
		.SetDisplayNameAttribute(MakeAttributeSP(this, &FAssetEditorModeUILayer::GetTabSpawnerName, TabID))
		.SetTooltipTextAttribute(MakeAttributeSP(this, &FAssetEditorModeUILayer::GetTabSpawnerTooltip, TabID))
		.SetIcon(GetTabSpawnerIcon(TabID))
		.SetAutoGenerateMenuEntry(bShowMenuOption);

	if (MenuGroup.IsValid())
	{
		TabSpawnerEntry.SetGroup(MenuGroup.ToSharedRef());
	}
}

void FAssetEditorModeUILayer::SetModePanelInfo(const FName InTabSpawnerID, const FMinorTabConfig& InTabInfo)
{
	RequestedTabInfo.Emplace(InTabSpawnerID, InTabInfo);
}

void FAssetEditorModeUILayer::SetSecondaryModeToolbarName(FName InName)
{
	SecondaryModeToolbarName = InName;
}

TMap<FName, TWeakPtr<SDockTab>> FAssetEditorModeUILayer::GetSpawnedTabs()
{
	return SpawnedTabs;
}

const FOnSpawnTab& FAssetEditorModeUILayer::GetStoredSpawner(const FName TabID)
{
	return RequestedTabInfo[TabID].OnSpawnTab;
}

TSharedRef<SDockTab> FAssetEditorModeUILayer::SpawnStoredTab(const FSpawnTabArgs& Args, const FName TabID)
{
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab);
	if (GetStoredSpawner(TabID).IsBound())
	{
		FOnSpawnTab& StoredTabSpawner = RequestedTabInfo[TabID].OnSpawnTab;
		SpawnedTab = StoredTabSpawner.Execute(Args);
	}
	SpawnedTabs.Emplace(TabID, SpawnedTab);
	return SpawnedTab;
}

bool FAssetEditorModeUILayer::CanSpawnStoredTab(const FSpawnTabArgs& Args, const FName TabID)
{
	// Always allow spawning a placeholder tab for known mode slots.
	// This prevents layout restore from producing missing-tab chains that can
	// later trigger null shared pointer dereferences in editor UI code paths.
	bool bCanSpawnTab = RequestedTabInfo.Contains(TabID);
	if (RequestedTabInfo[TabID].CanSpawnTab.IsBound())
	{
		bCanSpawnTab = RequestedTabInfo[TabID].CanSpawnTab.Execute(Args);
	}
	return bCanSpawnTab;
}

FText FAssetEditorModeUILayer::GetTabSpawnerName(const FName TabID) const
{
	if (!RequestedTabInfo[TabID].TabLabel.IsEmpty())
	{
		return RequestedTabInfo[TabID].TabLabel;
	}
	return FText::GetEmpty();
}

FText FAssetEditorModeUILayer::GetTabSpawnerTooltip(const FName TabID) const
{
	if (!RequestedTabInfo[TabID].TabTooltip.IsEmpty())
	{
		return RequestedTabInfo[TabID].TabTooltip;
	}
	return FText::GetEmpty();
}

const FSlateIcon& FAssetEditorModeUILayer::GetTabSpawnerIcon(const FName TabID) const
{
	if (RequestedTabInfo[TabID].TabIcon.IsSet())
	{
		return RequestedTabInfo[TabID].TabIcon;
	}
	if (TSharedPtr<FWorkspaceItem> ModeMenuCategory = GetModeMenuCategory())
	{
		return ModeMenuCategory->GetIcon();
	}

	static const FSlateIcon DefaultIcon;
	return DefaultIcon;
}

#undef LOCTEXT_NAMESPACE

