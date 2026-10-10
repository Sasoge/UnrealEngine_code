// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class UnrealEditorTarget : TargetRules
{
	public UnrealEditorTarget( TargetInfo Target ) : base(Target)
	{
		Type = TargetType.Editor;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		BuildEnvironment = TargetBuildEnvironment.Shared;
		bBuildAllModules = true;
		ExtraModuleNames.Add("UnrealGame");

		// Optional hardware SDK: skip when Engine/Binaries/ThirdParty Blackmagic libs are not present (LNK1181 on libBlackmagicLib.lib).
		DisablePlugins.Add("BlackmagicMedia");
	}
}
