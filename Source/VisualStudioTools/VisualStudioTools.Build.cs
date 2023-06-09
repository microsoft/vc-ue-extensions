// Copyright 2022 (c) Microsoft. All rights reserved.

using UnrealBuildTool;

public class VisualStudioTools : ModuleRules
{
    public VisualStudioTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // When debugging the commandlet code, you can uncomment the line below
        // to get proper local variable inspection and less inlined stack frames
        // OptimizeCode = CodeOptimization.Never;

        // To support UE5.1+, the code is using the new FTopLevelAssetPath API
        // with a detection of support via version numbers.
        // If the check is producing a false positive/negative in your version of the engine
        // you can change the block below and force the check as enabled/disabled.
        if ((Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 1) || Target.Version.MajorVersion > 5)
        {
            PrivateDefinitions.Add("FILTER_ASSETS_BY_CLASS_PATH=1");
        }
        else
        {
            PrivateDefinitions.Add("FILTER_ASSETS_BY_CLASS_PATH=0");
        }

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "AssetRegistry",
                "CoreUObject",
                "Engine",
                "Json",
                "JsonUtilities",
                "Kismet",
                "UnrealEd",
            }
        );
    }
}
