#include "SToonLightingDebugger.h"
#include "Components/DeferredToonVolumeComponent.h"
#include "DeferredToonDebugPreview.h"
#include "DeferredToonDebugViews.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "Misc/ConfigCacheIni.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "HAL/IConsoleManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "InputCoreTypes.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const TCHAR* ConfigSection = TEXT("DeferredToon.Debugger");
const DeferredToonDebugViews::FEntry* FindView(int32 Index)
{
    for (const auto& Entry : DeferredToonDebugViews::Entries)
        if (Entry.PublicIndex == Index) { return &Entry; }
    return nullptr;
}
}

FText SToonLightingDebugger::T(const TCHAR* Chinese, const TCHAR* English) const
{ return FText::FromString(bChinese ? Chinese : English); }

void SToonLightingDebugger::Construct(const FArguments& Args)
{
    OwnerId = DeferredToonDebugPreview::AllocateOwnerId();
    bChinese = FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName() == TEXT("zh");
    GConfig->GetBool(TEXT("DeferredToon.AuthoringUI"), TEXT("Chinese"), bChinese, GEditorPerProjectIni);
    GConfig->GetBool(ConfigSection, TEXT("Chinese"), bChinese, GEditorPerProjectIni);
    TArray<FString> Saved;
    GConfig->GetArray(ConfigSection, TEXT("Favorites"), Saved, GEditorPerProjectIni);
    bool bSavedFavorites = false;
    GConfig->GetBool(ConfigSection, TEXT("FavoritesSaved"), bSavedFavorites, GEditorPerProjectIni);
    if (!bSavedFavorites)
        Saved = { TEXT("EffectiveDirectLight"), TEXT("IndirectOcclusionBand"), TEXT("IndirectSpecular"), TEXT("Emissive") };
    for (const FString& Name : Saved) { Favorites.Add(FName(*Name)); }
    MapHandle = FEditorDelegates::MapChange.AddSP(this, &SToonLightingDebugger::OnMapChange);
    PIEHandle = FEditorDelegates::PreBeginPIE.AddSP(this, &SToonLightingDebugger::OnBeginPIE);
    FollowSelection();
    Build();
}

SToonLightingDebugger::~SToonLightingDebugger()
{
    FEditorDelegates::MapChange.Remove(MapHandle);
    FEditorDelegates::PreBeginPIE.Remove(PIEHandle);
    StopPreview();
}

void SToonLightingDebugger::SavePreferences()
{
    TArray<FString> Saved;
    for (const FName& Name : Favorites) { Saved.Add(Name.ToString()); }
    Saved.Sort();
    GConfig->SetArray(ConfigSection, TEXT("Favorites"), Saved, GEditorPerProjectIni);
    GConfig->SetBool(ConfigSection, TEXT("FavoritesSaved"), true, GEditorPerProjectIni);
    GConfig->SetBool(ConfigSection, TEXT("Chinese"), bChinese, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
}

bool SToonLightingDebugger::CanPreview() const
{
    const auto* C = Target.Get();
    return GEditor && !GEditor->PlayWorld && C && C->GetWorld() == GEditor->GetEditorWorldContext().World()
        && C->IsRegistered() && C->bEnabled;
}

void SToonLightingDebugger::RefreshViewportDisplay()
{
    if (!GEditor || !Target.IsValid()) { return; }
    for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
    {
        if (Client && Client->GetWorld() == Target->GetWorld())
        {
            // Preview only changes display pixels. Invalidating hit proxies forces
            // expensive picking renders/readbacks when the mouse next queries them.
            // Preserve the user's realtime settings, including inactive viewports.
            Client->Invalidate(false, false);
        }
    }
}

void SToonLightingDebugger::StopPreview()
{
    if (!bPreviewActive) { return; }
    bPreviewActive = false;
    DeferredToonDebugPreview::Clear(OwnerId);
    RefreshViewportDisplay();
}

void SToonLightingDebugger::RefreshPreview()
{
    if (bPreviewActive && CanPreview())
    {
        DeferredToonDebugPreview::Set(OwnerId, Target.Get(), CurrentIndex, AOFilterMode);
        RefreshViewportDisplay();
    }
}

void SToonLightingDebugger::Preview(int32 Index)
{
    if (!CanPreview() || !FindView(Index)) { return; }
    if (bPreviewActive && CurrentIndex != Index) { PreviousIndex = CurrentIndex; }
    CurrentIndex = Index;
    bPreviewActive = true;
    RefreshPreview();
}

void SToonLightingDebugger::SetTarget(UDeferredToonVolumeComponent* Component)
{
    if (Target.Get() == Component) { return; }
    const bool bWasActive = bPreviewActive;
    StopPreview();
    Target = Component;
    if (bWasActive && CanPreview()) { bPreviewActive = true; RefreshPreview(); }
}

void SToonLightingDebugger::FollowSelection()
{
    if (!GEditor || GEditor->PlayWorld) { return; }
    if (auto* C = GEditor->GetSelectedComponents()->GetTop<UDeferredToonVolumeComponent>()) { SetTarget(C); return; }
    for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
    {
        if (auto* Actor = Cast<AActor>(*It))
            if (auto* C = Actor->FindComponentByClass<UDeferredToonVolumeComponent>()) { SetTarget(C); return; }
    }
    // Selecting an unrelated actor does not discard the last useful target.
}

void SToonLightingDebugger::Tick(const FGeometry& Geometry, double Time, float Delta)
{
    SCompoundWidget::Tick(Geometry, Time, Delta);
    if (Time - LastRefresh < 0.25) { return; }
    LastRefresh = Time;
    if (bPreviewActive && !CanPreview()) { StopPreview(); }
    if (Target.IsValid() && GEditor && Target->GetWorld() != GEditor->GetEditorWorldContext().World()) { Target.Reset(); }
    if (bFollowSelection) { FollowSelection(); }
    // Idle polling must not redraw viewports or invalidate picking caches.
}

void SToonLightingDebugger::OnMapChange(uint32 Flags) { StopPreview(); Target.Reset(); PreviousIndex = -1; }
void SToonLightingDebugger::OnBeginPIE(bool bSimulating) { StopPreview(); }
FReply SToonLightingDebugger::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape) { StopPreview(); return FReply::Handled(); }
    return SCompoundWidget::OnKeyDown(Geometry, Event);
}

FText SToonLightingDebugger::TargetLabel() const
{
    if (const auto* C = Target.Get())
    {
        const FString Owner = C->GetOwner() ? C->GetOwner()->GetActorLabel() : C->GetName();
        return FText::FromString(Owner + TEXT(" / ") + C->GetName());
    }
    return T(TEXT("选择目标盒体…"), TEXT("Choose a target volume…"));
}

TSharedRef<SWidget> SToonLightingDebugger::TargetMenu()
{
    FMenuBuilder Menu(true, nullptr);
    int32 Count = 0;
    if (GEditor && !GEditor->PlayWorld)
    {
        UWorld* World = GEditor->GetEditorWorldContext().World();
        TArray<UDeferredToonVolumeComponent*> Components;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            TArray<UDeferredToonVolumeComponent*> ActorComponents;
            It->GetComponents(ActorComponents);
            Components.Append(ActorComponents);
        }
        Components.Sort([](const auto& A, const auto& B) { return A.GetPathName() < B.GetPathName(); });
        for (auto* C : Components)
        {
            TWeakObjectPtr<UDeferredToonVolumeComponent> Weak(C);
            const FString Label = C->GetOwner()->GetActorLabel() + TEXT(" / ") + C->GetName();
            Menu.AddMenuEntry(FText::FromString(Label), FText::FromString(C->GetPathName()), FSlateIcon(),
                FUIAction(FExecuteAction::CreateSPLambda(this, [this, Weak]() { bFollowSelection = false; SetTarget(Weak.Get()); })));
            ++Count;
        }
    }
    if (!Count) { Menu.AddMenuEntry(T(TEXT("当前编辑器关卡中没有可选盒体"), TEXT("No volume in the current editor level")), FText::GetEmpty(), FSlateIcon(), FUIAction()); }
    return Menu.MakeWidget();
}

FText SToonLightingDebugger::Status() const
{
    if (GEditor && GEditor->PlayWorld) { return T(TEXT("运行期间暂停编辑器预览。"), TEXT("Editor preview paused during PIE.")); }
    if (!Target.IsValid()) { return T(TEXT("从上方选择盒体，或在场景中选中一个盒体。"), TEXT("Choose a volume above, or select one in the scene.")); }
    if (!CanPreview()) { return T(TEXT("目标盒体未启用或尚未注册，请先在组件中启用。"), TEXT("Target disabled or unregistered. Enable its component first.")); }
    const auto* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Toon.DeferredVolumes"));
    if (Enabled && Enabled->GetInt() == 0) { return T(TEXT("盒体渲染被全局关闭：r.Toon.DeferredVolumes = 0。"), TEXT("Volume rendering disabled globally: r.Toon.DeferredVolumes = 0.")); }
    if (!bPreviewActive) { return T(TEXT("未接管预览 · 当前使用场景原有设置"), TEXT("Preview released · original scene settings in use")); }
    const auto* Entry = FindView(CurrentIndex);
    return FText::FromString(FString::Printf(TEXT("%s%d · %s"), bChinese ? TEXT("临时预览：") : TEXT("Temporary preview: "), CurrentIndex, bChinese ? Entry->Chinese : Entry->English));
}

FText SToonLightingDebugger::Description() const
{
    if (!bPreviewActive) { return T(TEXT("单击视图按钮开始。只接管显示，不写入组件或全局 CVar。关闭面板／退出调试将恢复原设置。"), TEXT("Click a view to preview. Component properties and global CVars stay untouched. Close the tab or release preview to restore original settings.")); }
    if (CurrentIndex == 51) { return T(TEXT("二分 AO：白为无遮挡，黑为遮蔽。先看 53 确认数据有效，再看 54 判断来源；下方可以切换滤波与历史诊断。"), TEXT("Binary AO: white = unoccluded, black = occluded. Check availability in 53 and provider in 54. Filter/history diagnostics are below.")); }
    if (CurrentIndex == 54) { return T(TEXT("来源：青色 = Lumen 短程 AO；橙色 = 普通天光综合遮蔽；黑色 = 没有受支持的数据。"), TEXT("Provider: cyan = Lumen short AO; orange = dynamic sky occlusion; black = no supported input.")); }
    if (CurrentIndex == 41 || CurrentIndex == 53 || CurrentIndex == 62 || CurrentIndex == 72 || CurrentIndex == 81 || CurrentIndex == 91)
        return T(TEXT("数据有效性：白为已提供，黑为未提供。有效的光照仍可能是黑色，不能用 RGB 是否明亮判断接口有效。"), TEXT("Availability: white = provided, black = unavailable. Valid lighting can still be black; RGB brightness is not a validity test."));
    if (CurrentIndex == 50 || CurrentIndex == 55 || CurrentIndex == 61 || CurrentIndex == 71)
        return T(TEXT("原始可见度：白为无遮挡，暗色为遮蔽。Lumen 与普通天光走不同来源；空白不能单独证明无遮挡，请同时检查对应有效性视图。"), TEXT("Raw visibility: white = unoccluded, dark = occluded. Lumen and sky use different providers. Inspect availability before interpreting an empty view."));
    if (CurrentIndex == 42) { return T(TEXT("整层间接漫反射的明暗二分，包含照明与材质响应；它不是 51 的 AO 二分。"), TEXT("Two-tone selection of the entire indirect diffuse layer, including lighting/material response. This is not the AO mask in 51.")); }
    if (CurrentIndex == 22) { return T(TEXT("直接漫反射所选明暗档位：0 为最暗、1 为最亮。使用有效受光响应选择，不是材质底色亮度。"), TEXT("Selected direct-light band: 0 = darkest, 1 = brightest. Selected from effective illumination, not base-color brightness.")); }
    if (CurrentIndex == 80) { return T(TEXT("原生环境镜面反射颜色，保留当前路径的材质和遮蔽响应；不是直接光源高光。81 用于确认导出有效性。"), TEXT("Native environment reflection color with material/occlusion response. This is separate from direct highlights. View 81 checks availability.")); }
    if (CurrentIndex == 90) { return T(TEXT("Base Pass 导出的材质自发光；不等于 Bloom，也不包括它照亮别处的 GI。91 查看数据有效性。"), TEXT("Material emission exported by the Base Pass, excluding bloom and its GI contribution elsewhere. View 91 checks availability.")); }
    return T(TEXT("在场景视口中查看当前分量。预览沿用已有提取逻辑；选择目标决定盒体参数，通常显示整屏缓冲，不是盒体内裁剪。"), TEXT("Inspect this layer in the scene viewport. Uses existing exports; the target supplies volume parameters. Most debug views show the fullscreen buffer, not a box crop."));
}

void SToonLightingDebugger::Build()
{
    TSharedPtr<SVerticalBox> Categories;
    TSharedPtr<SHorizontalBox> AOButtons;
    ChildSlot
    [
        SNew(SBorder).Padding(12)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(T(TEXT("Toon 光照调试"), TEXT("Toon Lighting Debugger"))).Font(FAppStyle::GetFontStyle(TEXT("HeadingExtraSmall")))]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(T(TEXT("English"),TEXT("中文"))).OnClicked_Lambda([this](){ bChinese=!bChinese; SavePreferences(); Build(); return FReply::Handled(); })]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,8,0)[SNew(SComboButton).OnGetMenuContent(this,&SToonLightingDebugger::TargetMenu).ButtonContent()[SNew(STextBlock).Text(this,&SToonLightingDebugger::TargetLabel).AutoWrapText(true)]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SCheckBox).IsChecked_Lambda([this](){return bFollowSelection?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this](ECheckBoxState S){bFollowSelection=S==ECheckBoxState::Checked; if(bFollowSelection)FollowSelection();}).ToolTipText(T(TEXT("勾选：跟随选中的盒体。取消：锁定当前目标。选择普通物体时保留上一个盒体。"),TEXT("Checked: follow selected volumes. Unchecked: lock target. Selecting unrelated actors retains the last target.")))[SNew(STextBlock).Text_Lambda([this](){return bFollowSelection?T(TEXT("跟随选择"),TEXT("Follow selection")):T(TEXT("目标已锁定"),TEXT("Target locked"));})]]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[SNew(SButton).Text(T(TEXT("正常画面"),TEXT("Normal output"))).IsEnabled(this,&SToonLightingDebugger::CanPreview).OnClicked_Lambda([this](){Preview(0);return FReply::Handled();})]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[SNew(SButton).Text(T(TEXT("上一视图"),TEXT("Previous view"))).IsEnabled_Lambda([this](){return CanPreview()&&PreviousIndex>=0;}).OnClicked_Lambda([this](){Preview(PreviousIndex);return FReply::Handled();})]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(T(TEXT("退出调试"),TEXT("Release preview"))).ToolTipText(T(TEXT("恢复组件与控制台原设置；Esc 同效。"),TEXT("Restore original component/console settings; also Esc."))).OnClicked_Lambda([this](){StopPreview();return FReply::Handled();})]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(STextBlock).Text(this,&SToonLightingDebugger::Status).AutoWrapText(true)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(SSearchBox).HintText(T(TEXT("搜索名称、英文或编号（跨分类）"),TEXT("Search name or index across categories"))).InitialText(FText::FromString(Search)).OnTextChanged_Lambda([this](const FText& Text){Search=Text.ToString();RebuildViews();})]
            + SVerticalBox::Slot().FillHeight(1)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)[SNew(SBox).WidthOverride(165)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(Categories,SVerticalBox)]]]
                + SHorizontalBox::Slot().FillWidth(1)[SAssignNew(Views,SScrollBox)]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,10,0,6)[SNew(STextBlock).Text(this,&SToonLightingDebugger::Description).AutoWrapText(true)]
            + SVerticalBox::Slot().AutoHeight()[SAssignNew(AOButtons,SHorizontalBox).Visibility_Lambda([this](){return bPreviewActive&&CurrentIndex==51?EVisibility::Visible:EVisibility::Collapsed;})]
        ]
    ];
    struct FGroup {int32 Id; const TCHAR* Chinese; const TCHAR* English;};
    const FGroup Groups[] = {
        {-2,TEXT("★ 收藏"),TEXT("★ Favorites")},{-1,TEXT("全部视图"),TEXT("All views")},
        {0,TEXT("00 · 输出与盒体"),TEXT("00 · Output / volume")},{1,TEXT("10 · 表面数据"),TEXT("10 · Surface")},
        {2,TEXT("20 · 直接漫反射"),TEXT("20 · Direct diffuse")},{3,TEXT("30 · 直接高光"),TEXT("30 · Direct specular")},
        {4,TEXT("40 · 间接漫反射"),TEXT("40 · Indirect diffuse")},{5,TEXT("50 · 综合遮蔽"),TEXT("50 · Occlusion")},
        {6,TEXT("60 · 天光遮蔽"),TEXT("60 · Sky occlusion")},{7,TEXT("70 · Lumen 短程 AO"),TEXT("70 · Lumen short AO")},
        {8,TEXT("80 · 环境反射"),TEXT("80 · Reflections")},{9,TEXT("90 · 自发光"),TEXT("90 · Emission")},
        {10,TEXT("100 · 阴影输入"),TEXT("100 · Shadow inputs")},{11,TEXT("110 · 实验分层"),TEXT("110 · Style layers")}};
    for (const FGroup& G : Groups)
    {
        Categories->AddSlot().AutoHeight().Padding(0,0,0,4)
        [SNew(SCheckBox).Style(FAppStyle::Get(),TEXT("ToggleButtonCheckbox"))
            .IsChecked_Lambda([this,Id=G.Id](){return Group==Id?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this,Id=G.Id](ECheckBoxState){Group=Id;RebuildViews();})
            [SNew(STextBlock).Text(T(G.Chinese,G.English))]];
    }
    const TCHAR* ChineseAO[] = {TEXT("最终遮罩"),TEXT("滤波结果"),TEXT("样本支持"),TEXT("几何法线"),TEXT("历史接受"),TEXT("拒绝原因")};
    const TCHAR* EnglishAO[] = {TEXT("Mask"),TEXT("Filtered"),TEXT("Support"),TEXT("Normals"),TEXT("History"),TEXT("Rejection")};
    for (int32 Mode=0;Mode<6;++Mode)
    {
        AOButtons->AddSlot().FillWidth(1).Padding(1)
        [SNew(SCheckBox).Style(FAppStyle::Get(),TEXT("ToggleButtonCheckbox"))
            .IsChecked_Lambda([this,Mode](){return AOFilterMode==Mode?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this,Mode](ECheckBoxState){AOFilterMode=Mode;RefreshPreview();})
            [SNew(STextBlock).Text(T(ChineseAO[Mode],EnglishAO[Mode])).AutoWrapText(true)]];
    }
    RebuildViews();
}

void SToonLightingDebugger::RebuildViews()
{
    if (!Views.IsValid()) {return;}
    Views->ClearChildren();
    int32 Count=0;
    for (const auto& Entry : DeferredToonDebugViews::Entries)
    {
        if (Entry.bHidden) {continue;}
        const bool bSearch = !Search.TrimStartAndEnd().IsEmpty();
        if (bSearch)
        {
            const FString Haystack=FString::Printf(TEXT("%d %s %s %s"),Entry.PublicIndex,Entry.Name,Entry.English,Entry.Chinese);
            if (!Haystack.Contains(Search.TrimStartAndEnd())) {continue;}
        }
        else if (Group==-2 ? !Favorites.Contains(FName(Entry.Name)) : (Group>=0 && Entry.PublicIndex/10!=Group)) {continue;}
        const FString Label=FString::Printf(TEXT("%d · %s"),Entry.PublicIndex,bChinese?Entry.Chinese:Entry.English);
        Views->AddSlot().Padding(0,0,0,5)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
            [SNew(SButton).Text_Lambda([this,Name=FName(Entry.Name)](){return FText::FromString(Favorites.Contains(Name)?TEXT("★"):TEXT("☆"));})
                .ToolTipText(T(TEXT("收藏 / 取消收藏"),TEXT("Toggle favorite")))
                .OnClicked_Lambda([this,Name=FName(Entry.Name)](){if(Favorites.Contains(Name))Favorites.Remove(Name);else Favorites.Add(Name);SavePreferences();RebuildViews();return FReply::Handled();})]
            + SHorizontalBox::Slot().FillWidth(1)
            [SNew(SCheckBox).Style(FAppStyle::Get(),TEXT("ToggleButtonCheckbox")).IsEnabled(this,&SToonLightingDebugger::CanPreview)
                .IsChecked_Lambda([this,Index=Entry.PublicIndex](){return bPreviewActive&&CurrentIndex==Index?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this,Index=Entry.PublicIndex](ECheckBoxState){Preview(Index);})
                [SNew(STextBlock).Text(FText::FromString(Label)).AutoWrapText(true)]]
        ];
        ++Count;
    }
    if (!Count) {Views->AddSlot()[SNew(STextBlock).Text(T(TEXT("没有匹配视图。可清空搜索，或用 ☆ 添加收藏。"),TEXT("No matching views. Clear search or use ☆ to add favorites."))).AutoWrapText(true)];}
}
