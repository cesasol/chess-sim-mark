// SPDX-License-Identifier: Apache-2.0
using UnrealBuildTool;

public class ChessSimMarkTarget : TargetRules
{
	public ChessSimMarkTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("ChessSimMark");
	}
}
