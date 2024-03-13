// Copyright 2022 (c) Microsoft. All rights reserved.
// Licensed under the MIT License.

#include "VisualStudioToolsCommandletBase.h"

#include "Windows/AllowWindowsPlatformTypes.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "VisualStudioTools.h"

#include "Windows/HideWindowsPlatformTypes.h"

static constexpr auto HelpSwitch = TEXT("help");
static constexpr auto OutputSwitch = TEXT("output");

UVisualStudioToolsCommandletBase::UVisualStudioToolsCommandletBase()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = false;
	ShowErrorCount = false;

	HelpParamNames.Add(OutputSwitch);
	HelpParamDescriptions.Add(TEXT("[Required] The file path to write the command output."));

	HelpParamNames.Add(HelpSwitch);
	HelpParamDescriptions.Add(TEXT("[Optional] Print this help message and quit the commandlet immediately."));
}

void UVisualStudioToolsCommandletBase::PrintHelp() const
{
	UE_LOG(LogVisualStudioTools, Display, TEXT("%s"), *HelpDescription);
	UE_LOG(LogVisualStudioTools, Display, TEXT("Usage: %s"), *HelpUsage);
	UE_LOG(LogVisualStudioTools, Display, TEXT("Parameters:"));
	for (int32 i = 0; i < HelpParamNames.Num(); ++i)
	{
		UE_LOG(LogVisualStudioTools, Display, TEXT("\t-%s: %s"), *HelpParamNames[i], *HelpParamDescriptions[i]);
	}
}

int32 UVisualStudioToolsCommandletBase::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;

	ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	if (Switches.Contains(HelpSwitch))
	{
		PrintHelp();
		return 0;
	}

	UE_LOG(LogVisualStudioTools, Display, TEXT("Init VS Tools cmdlet."));

	if (!FPaths::IsProjectFilePathSet())
	{
		UE_LOG(LogVisualStudioTools, Error, TEXT("You must invoke this commandlet with a project file."));
		return -1;
	}

	FString FullPath = ParamVals.FindRef(OutputSwitch);

	if (FullPath.IsEmpty() && !FParse::Value(*Params, TEXT("output "), FullPath))
	{
		// VS:1678426 - Initial version was using `-output "path-to-file"` (POSIX style).
		// However, that does not support paths with spaces, even when surrounded with
		// quotes because `FParse::Value` only handles that case when there's no space
		// between the parameter name and quoted value.
		// For back-compatibility reasons, parse that style by including the space in
		// the parameter token like it's usually done for the `=` sign.
		UE_LOG(LogVisualStudioTools, Error, TEXT("Missing file output parameter."));
		PrintHelp();
		return -1;
	}

	TUniquePtr<FArchive> OutArchive{ IFileManager::Get().CreateFileWriter(*FullPath) };
	if (!OutArchive)
	{
		UE_LOG(LogVisualStudioTools, Error, TEXT("Failed to create index with path: %s."), *FullPath);
		return -1;
	}

    return this->Run(Tokens, Switches, ParamVals, *OutArchive);
}
