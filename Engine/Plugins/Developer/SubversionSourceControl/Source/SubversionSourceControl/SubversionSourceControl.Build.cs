// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SubversionSourceControl : ModuleRules
{
	public SubversionSourceControl(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			 new string[] {
				"Core",
				"Slate",
				"SlateCore",
				"SourceControl",
				"XmlParser",
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Mac)
		{
			// Add contents of binaries directory as runtime dependencies
			string SvnBinDir = Target.UEThirdPartyBinariesDirectory + "svn/" + Target.Platform.ToString() + "/";
			if (Directory.Exists(SvnBinDir))
			{
				foreach (string FilePath in Directory.EnumerateFiles(SvnBinDir, "*", SearchOption.AllDirectories))
				{
					RuntimeDependencies.Add(FilePath);
				}
			}
		}

		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
