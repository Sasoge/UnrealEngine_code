#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
class UDeferredToonVolumeComponent;
class SVerticalBox;
class SScrollBox;

class SToonLightingDebugger : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SToonLightingDebugger) {} SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual ~SToonLightingDebugger();
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    void StopPreview();
private:
    friend class FToonDebuggerPanelTest;
    FText T(const TCHAR* Chinese, const TCHAR* English) const;
    void Build();
    void RebuildViews();
    void RefreshPreview();
    void Preview(int32 Index);
    void SetTarget(UDeferredToonVolumeComponent* Component);
    void FollowSelection();
    void SetRealtime(bool bEnable);
    void SavePreferences();
    void OnMapChange(uint32 Flags);
    void OnBeginPIE(bool bSimulating);
    TSharedRef<SWidget> TargetMenu();
    FText TargetLabel() const;
    FText Status() const;
    FText Description() const;
    bool CanPreview() const;
    TWeakObjectPtr<UDeferredToonVolumeComponent> Target;
    TSharedPtr<SScrollBox> Views;
    TSet<FName> Favorites;
    FString Search;
    uint64 OwnerId = 0;
    double LastRefresh = 0;
    int32 Group = 2;
    int32 CurrentIndex = 0;
    int32 PreviousIndex = -1;
    int32 AOFilterMode = 0;
    bool bChinese = true;
    bool bFollowSelection = true;
    bool bPreviewActive = false;
    FDelegateHandle MapHandle;
    FDelegateHandle PIEHandle;
};
