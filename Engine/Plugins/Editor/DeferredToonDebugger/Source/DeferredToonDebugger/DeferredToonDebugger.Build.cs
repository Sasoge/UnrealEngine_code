using UnrealBuildTool;
// The editor module also contains the non-destructive panel integration test.
public class DeferredToonDebugger : ModuleRules
{
    public DeferredToonDebugger(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Slate", "SlateCore", "UnrealEd", "LevelEditor", "ToolMenus", "InputCore", "RenderCore", "RHI", "UMG" });
    }
}
