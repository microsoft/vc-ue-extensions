// Copyright 2022 (c) Microsoft. All rights reserved.

using UnrealBuildTool;

public class VisualStudioBlueprintDebuggerHelper: ModuleRules
{
    public VisualStudioBlueprintDebuggerHelper(ReadOnlyTargetRules Target) : base(Target)
    {
        OptimizeCode = CodeOptimization.Never;
        PrivateDependencyModuleNames.AddRange(new string[] {
                "Core",
                "ApplicationCore",
                "AssetRegistry",
                "CoreUObject",
                "Engine",
                "Json",
                "JsonUtilities",
                "Kismet",
                "UnrealEd",
                "Slate",
                "SlateCore",
                "ToolMenus",
                "EditorSubsystem",
                "MainFrame",
                "BlueprintGraph",
                "VisualStudioDTE",
                "EditorStyle",
                "Projects"
        });
    }
}
