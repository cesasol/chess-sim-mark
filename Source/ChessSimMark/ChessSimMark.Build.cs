// SPDX-License-Identifier: Apache-2.0
using UnrealBuildTool;

public class ChessSimMark : ModuleRules
{
	public ChessSimMark(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"UMG", "Slate", "SlateCore", "DeveloperSettings"
		});

		PublicIncludePaths.Add(ModuleDirectory);
	}
}
