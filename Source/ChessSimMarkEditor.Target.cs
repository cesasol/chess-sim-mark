// SPDX-License-Identifier: Apache-2.0
using UnrealBuildTool;

public class ChessSimMarkEditorTarget : TargetRules
{
	public ChessSimMarkEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("ChessSimMark");
	}
}
