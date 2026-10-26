// [DeferredToon] Public index / serialized ID compatibility and reflection metadata contract.
#include "DeferredToonDebugViews.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeferredToonDebugIndicesTest,
    "Engine.DeferredToon.DebugViewIndices", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeferredToonDebugIndicesTest::RunTest(const FString& Parameters)
{
    using namespace DeferredToonDebugViews;
    const UEnum* Enum = StaticEnum<EDeferredToonDebugView>();
    TSet<int32> PublicIndices;
    TSet<int32> StableIds;
    int32 PreviousPublic = -1;
    int32 PreviousEnumIndex = -1;
    for (const FEntry& Entry : Entries)
    {
        const int32 StableId = static_cast<int32>(Entry.View);
        const FString Context = Entry.Name;
        TestFalse(Context + TEXT(" duplicate public index"), PublicIndices.Contains(Entry.PublicIndex));
        TestFalse(Context + TEXT(" duplicate serialized ID"), StableIds.Contains(StableId));
        PublicIndices.Add(Entry.PublicIndex);
        StableIds.Add(StableId);
        TestEqual(Context + TEXT(" public -> shader"), ToShaderId(Entry.PublicIndex), StableId);
        TestTrue(Context + TEXT(" menu order"), Entry.PublicIndex > PreviousPublic);
        const int32 EnumIndex = Enum->GetIndexByValue(StableId);
        TestTrue(Context + TEXT(" reflected order"), EnumIndex > PreviousEnumIndex);
        TestEqual(Context + TEXT(" reflected name"), Enum->GetNameStringByIndex(EnumIndex), Context);
        if (!Entry.bHidden)
        {
            // English and Chinese metadata must have the same public prefix.
            const FString Label = Enum->GetDisplayNameTextByIndex(EnumIndex).ToString();
            TestTrue(Context + TEXT(" displayed public number"), Label.StartsWith(FString::Printf(TEXT("%d - "), Entry.PublicIndex)));
        }
        TestEqual(Context + TEXT(" visibility"), Enum->HasMetaData(TEXT("Hidden"), EnumIndex), Entry.bHidden);
        PreviousPublic = Entry.PublicIndex;
        PreviousEnumIndex = EnumIndex;
    }
    TestEqual(TEXT("All existing shader views represented"), StableIds.Num(), 45);
    for (int32 Id = 0; Id <= 44; ++Id) { TestTrue(TEXT("Existing shader ID covered"), StableIds.Contains(Id)); }
    for (int32 Invalid : {-1, 4, 9, 19, 29, 39, 49, 59, 69, 79, 89, 99, 109, 112, 255, 256, MAX_int32})
    {
        TestEqual(TEXT("Reserved/invalid index -> selected output"), ToShaderId(Invalid), 0);
    }
    // Fixed snapshots of the historical asset values. Never replace these with public indices.
    TestEqual(TEXT("Saved NativeComposite"), static_cast<int32>(EDeferredToonDebugView::NativeComposite), 0);
    TestEqual(TEXT("Saved BoxInfluence"), static_cast<int32>(EDeferredToonDebugView::BoxInfluence), 1);
    TestEqual(TEXT("Saved Priority"), static_cast<int32>(EDeferredToonDebugView::Priority), 3);
    TestEqual(TEXT("Saved ShadowComposite"), static_cast<int32>(EDeferredToonDebugView::ShadowComposite), 2);
    TestEqual(TEXT("Saved BaseColor"), static_cast<int32>(EDeferredToonDebugView::BaseColor), 7);
    TestEqual(TEXT("Saved WorldNormal"), static_cast<int32>(EDeferredToonDebugView::WorldNormal), 8);
    TestEqual(TEXT("Saved Depth"), static_cast<int32>(EDeferredToonDebugView::Depth), 12);
    TestEqual(TEXT("Saved Velocity"), static_cast<int32>(EDeferredToonDebugView::Velocity), 13);
    TestEqual(TEXT("Saved EffectiveDirectLight"), static_cast<int32>(EDeferredToonDebugView::EffectiveDirectLight), 22);
    TestEqual(TEXT("Saved EffectiveLightResponse"), static_cast<int32>(EDeferredToonDebugView::EffectiveLightResponse), 23);
    TestEqual(TEXT("Saved LightBandValue"), static_cast<int32>(EDeferredToonDebugView::LightBandValue), 24);
    TestEqual(TEXT("Saved DirectLightMask"), static_cast<int32>(EDeferredToonDebugView::DirectLightMask), 14);
    TestEqual(TEXT("Saved ToonShadowMask"), static_cast<int32>(EDeferredToonDebugView::ToonShadowMask), 19);
    TestEqual(TEXT("Saved ToonBinaryShadowMask"), static_cast<int32>(EDeferredToonDebugView::ToonBinaryShadowMask), 20);
    TestEqual(TEXT("Saved DirectLight"), static_cast<int32>(EDeferredToonDebugView::DirectLight), 4);
    TestEqual(TEXT("Saved DirectLightFacing"), static_cast<int32>(EDeferredToonDebugView::DirectLightFacing), 21);
    TestEqual(TEXT("Saved SpecularDirectLight"), static_cast<int32>(EDeferredToonDebugView::SpecularDirectLight), 18);
    TestEqual(TEXT("Saved DirectSpecularMask"), static_cast<int32>(EDeferredToonDebugView::DirectSpecularMask), 39);
    TestEqual(TEXT("Saved StyledDirectSpecular"), static_cast<int32>(EDeferredToonDebugView::StyledDirectSpecular), 40);
    TestEqual(TEXT("Saved IndirectDiffuse"), static_cast<int32>(EDeferredToonDebugView::IndirectDiffuse), 25);
    TestEqual(TEXT("Saved IndirectDiffuseCoverage"), static_cast<int32>(EDeferredToonDebugView::IndirectDiffuseCoverage), 26);
    TestEqual(TEXT("Saved IndirectDiffuseBand"), static_cast<int32>(EDeferredToonDebugView::IndirectDiffuseBand), 37);
    TestEqual(TEXT("Saved IndirectOcclusion"), static_cast<int32>(EDeferredToonDebugView::IndirectOcclusion), 33);
    TestEqual(TEXT("Saved IndirectOcclusionBand"), static_cast<int32>(EDeferredToonDebugView::IndirectOcclusionBand), 38);
    TestEqual(TEXT("Saved IndirectDiffuseBeforeOcclusion"), static_cast<int32>(EDeferredToonDebugView::IndirectDiffuseBeforeOcclusion), 34);
    TestEqual(TEXT("Saved IndirectOcclusionCoverage"), static_cast<int32>(EDeferredToonDebugView::IndirectOcclusionCoverage), 35);
    TestEqual(TEXT("Saved IndirectOcclusionSource"), static_cast<int32>(EDeferredToonDebugView::IndirectOcclusionSource), 36);
    TestEqual(TEXT("Saved AmbientOcclusion"), static_cast<int32>(EDeferredToonDebugView::AmbientOcclusion), 9);
    TestEqual(TEXT("Saved SkyDiffuseBeforeAO"), static_cast<int32>(EDeferredToonDebugView::SkyDiffuseBeforeAO), 27);
    TestEqual(TEXT("Saved SkyOcclusion"), static_cast<int32>(EDeferredToonDebugView::SkyOcclusion), 28);
    TestEqual(TEXT("Saved SkyOcclusionCoverage"), static_cast<int32>(EDeferredToonDebugView::SkyOcclusionCoverage), 29);
    TestEqual(TEXT("Saved LumenDiffuseBeforeShortAO"), static_cast<int32>(EDeferredToonDebugView::LumenDiffuseBeforeShortAO), 31);
    TestEqual(TEXT("Saved LumenShortRangeAO"), static_cast<int32>(EDeferredToonDebugView::LumenShortRangeAO), 30);
    TestEqual(TEXT("Saved LumenShortRangeAOCoverage"), static_cast<int32>(EDeferredToonDebugView::LumenShortRangeAOCoverage), 32);
    TestEqual(TEXT("Saved IndirectSpecular"), static_cast<int32>(EDeferredToonDebugView::IndirectSpecular), 41);
    TestEqual(TEXT("Saved IndirectSpecularCoverage"), static_cast<int32>(EDeferredToonDebugView::IndirectSpecularCoverage), 42);
    TestEqual(TEXT("Saved Emissive"), static_cast<int32>(EDeferredToonDebugView::Emissive), 43);
    TestEqual(TEXT("Saved EmissiveCoverage"), static_cast<int32>(EDeferredToonDebugView::EmissiveCoverage), 44);
    TestEqual(TEXT("Saved NativeShadow"), static_cast<int32>(EDeferredToonDebugView::NativeShadow), 5);
    TestEqual(TEXT("Saved ShadowChannel"), static_cast<int32>(EDeferredToonDebugView::ShadowChannel), 6);
    TestEqual(TEXT("Saved CombinedShadowMask"), static_cast<int32>(EDeferredToonDebugView::CombinedShadowMask), 17);
    TestEqual(TEXT("Saved NativeShadowMask"), static_cast<int32>(EDeferredToonDebugView::NativeShadowMask), 15);
    TestEqual(TEXT("Saved ShadowChannelMask"), static_cast<int32>(EDeferredToonDebugView::ShadowChannelMask), 16);
    TestEqual(TEXT("Saved StyleMask"), static_cast<int32>(EDeferredToonDebugView::StyleMask), 10);
    TestEqual(TEXT("Saved StyleColor"), static_cast<int32>(EDeferredToonDebugView::StyleColor), 11);
    TestEqual(TEXT("Saved follow global"), static_cast<int32>(EDeferredToonDebugView::ConsoleGlobal), 255);
    return true;
}
#endif
