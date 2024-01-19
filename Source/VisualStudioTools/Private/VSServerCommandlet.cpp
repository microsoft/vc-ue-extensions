// Copyright 2022 (c) Microsoft. All rights reserved.

#include "VSServerCommandlet.h"
#include "VSTestAdapterCommandlet.h"

#include "Windows/AllowWindowsPlatformTypes.h"

#include "HAL/PlatformNamedPipe.h"
#include "Runtime/Core/Public/Async/TaskGraphInterfaces.h"
#include "Runtime/Core/Public/Containers/Ticker.h"
#include "Runtime/Engine/Classes/Engine/World.h"
#include "Runtime/Engine/Public/TimerManager.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Runtime\CoreUObject\Public\UObject\UObjectGlobals.h"
#include <chrono>
#include <codecvt>
#include <fstream>
#include <string>
#include <thread>
#include <windows.h>

#include "Windows/HideWindowsPlatformTypes.h"

#include "VisualStudioTools.h"

static constexpr auto NamedPipeParam = TEXT("NamedPipe");
static constexpr auto KillServerParam = TEXT("KillVSServer");

UVSServerCommandlet::UVSServerCommandlet()
{
	HelpDescription = TEXT("Commandlet for Unreal Engine server mode.");
	HelpUsage = TEXT("<Editor-Cmd.exe> <path_to_uproject> -run=VSServer [-stdout -multiprocess -silent -unattended -AllowStdOutLogVerbosity -NoShaderCompile]");

	HelpParamNames.Add(NamedPipeParam);
	HelpParamDescriptions.Add(TEXT("[Required] The name of the named pipe used to communicate with Visual Studio."));

	HelpParamNames.Add(KillServerParam);
	HelpParamDescriptions.Add(TEXT("[Optional] Quit the server mode commandlet immediately."));
}

void UVSServerCommandlet::ExecuteSubCommandlet(FString ueServerNamedPipe)
{
	char buffer[1024];
	DWORD dwRead;
	std::string result = "0";

	// Open the named pipe.
	std::wstring pipeName = L"\\\\.\\pipe\\";
	pipeName.append(ueServerNamedPipe.GetCharArray().GetData());
	HANDLE HPipe = CreateFile(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (HPipe != INVALID_HANDLE_VALUE)
	{
		ConnectNamedPipe(HPipe, NULL);
		DWORD dwState;
		BOOL bSuccess = GetNamedPipeHandleState(HPipe, &dwState, NULL, NULL, NULL, NULL, 0);
		if (bSuccess)
		{
			// Read data from the named pipe.
			ReadFile(HPipe, buffer, sizeof(buffer) - 1, &dwRead, NULL);
			buffer[dwRead] = '\0';
			std::string strSubCommandletParams(buffer, dwRead);
			FString SubCommandletParams = FString(strSubCommandletParams.c_str());

			// Determine which sub-commandlet to invoke, and write back result response.
			if (SubCommandletParams.Contains("VSTestAdapter"))
			{
				UVSTestAdapterCommandlet *Commandlet = NewObject<UVSTestAdapterCommandlet>();
				try
				{
					int32 subCommandletResult = Commandlet->Main(SubCommandletParams);
				}
				catch (const std::exception &ex)
				{
					UE_LOG(LogVisualStudioTools, Display, TEXT("Exception invoking VSTestAdapter commandlet: %s"), UTF8_TO_TCHAR(ex.what()));
					result = "0";
				}
			}
			else if (SubCommandletParams.Contains("KillVSServer"))
			{
				// When KillVSServer is passed in, then kill the Unreal Editor process to end server mode.
				exit(0);
			}
			else
			{
				// If cannot find which sub-commandlet to run, then return error.
				result = "1";
			}

			WriteFile(HPipe, result.c_str(), result.size(), &dwRead, NULL);
		}
	}
}

int32 UVSServerCommandlet::Main(const FString &ServerParams)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;

	ParseCommandLine(*ServerParams, Tokens, Switches, ParamVals);
	if (ParamVals.Contains(NamedPipeParam))
	{
		FString ueServerNamedPipe = ParamVals[NamedPipeParam];

		// Infinite loop that listens to requests every second.
		while (true)
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			ExecuteSubCommandlet(ueServerNamedPipe);
		}
	}
	else
	{
		UE_LOG(LogVisualStudioTools, Display, TEXT("Missing named pipe parameter."));
	}

	return 1;
}