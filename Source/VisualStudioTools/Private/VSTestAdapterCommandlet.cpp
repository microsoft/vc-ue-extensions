// Copyright 2022 (c) Microsoft. All rights reserved.

#include "VSTestAdapterCommandlet.h"

#include "Runtime/Core/Public/Async/TaskGraphInterfaces.h"
#include "Runtime/Core/Public/Containers/Ticker.h"
#include "Runtime/Launch/Resources/Version.h"
#include <string>
#include <fstream>

#include "VisualStudioTools.h"

static constexpr auto FiltersParam = TEXT("filters");
static constexpr auto ListTestsParam = TEXT("listtests");
static constexpr auto RunTestsParam = TEXT("runtests");
static constexpr auto TestResultsFileParam = TEXT("testresultfile");
static constexpr auto HelpParam = TEXT("help");

static void GetAllTests(TArray<FAutomationTestInfo>& OutTestList)
{
	FAutomationTestFramework& Framework = FAutomationTestFramework::GetInstance();
	Framework.GetValidTestNames(OutTestList);
}

static void ReadTestsFromFile(const FString& InFile, TArray<FAutomationTestInfo>& OutTestList)
{
	TSet<FString> TestCommands;

	// Wrapping in an inner scope to ensure automatic destruction of InStream object without explicitly calling .close().
	{
		std::wifstream InStream(*InFile);
		if (!InStream.good())
		{
			UE_LOG(LogVisualStudioTools, Error, TEXT("Failed to open file at path: %s"), *InFile);
			return;
		}

		std::wstring Line;
		while (std::getline(InStream, Line))
		{
			if (Line.length() > 0)
			{
				TestCommands.Add(FString(Line.c_str()));
			}
		}
	}

	GetAllTests(OutTestList);
	for (int32 Idx = OutTestList.Num() - 1; Idx >= 0; Idx--)
	{
		if (!TestCommands.Contains(OutTestList[Idx].GetTestName()))
		{
			OutTestList.RemoveAt(Idx);
		}
	}
}

static int32 ListTests(const FString& TargetFile)
{
	std::wofstream OutFile(*TargetFile);
	if (!OutFile.good())
	{
		UE_LOG(LogVisualStudioTools, Error, TEXT("Failed to open file at path: %s"), *TargetFile);
		return 1;
	}

	FAutomationTestFramework& Framework = FAutomationTestFramework::GetInstance();

	TArray<FAutomationTestInfo> TestInfos;
	GetAllTests(TestInfos);

	for (const auto& TestInfo : TestInfos)
	{
		const FString TestCommand = TestInfo.GetTestName();
		const FString DisplayName = TestInfo.GetDisplayName();
		const FString SourceFile = TestInfo.GetSourceFile();
		const int32 Line = TestInfo.GetSourceFileLine();

		OutFile << *TestCommand << TEXT("|") << *DisplayName << TEXT("|") << Line << TEXT("|") << *SourceFile << std::endl;
	}

	UE_LOG(LogVisualStudioTools, Display, TEXT("Found %d tests"), TestInfos.Num());

	OutFile.close();
	return 0;
}

static int32 RunTests(const FString& TestListFile, const FString& ResultsFile)
{
	std::wofstream OutFile(*ResultsFile);
	if (!OutFile.good())
	{
		UE_LOG(LogVisualStudioTools, Error, TEXT("Failed to open file at path: %s"), *ResultsFile);
		return 1;
	}

	TArray<FAutomationTestInfo> TestInfos;
	if (TestListFile.Equals(TEXT("All"), ESearchCase::IgnoreCase))
	{
		GetAllTests(TestInfos);
	}
	else
	{
		ReadTestsFromFile(TestListFile, TestInfos);
	}

	bool AllSuccessful = true;

	FAutomationTestFramework& Framework = FAutomationTestFramework::GetInstance();

	for (const FAutomationTestInfo& TestInfo : TestInfos)
	{
		const FString TestCommand = TestInfo.GetTestName();
		const FString DisplayName = TestInfo.GetDisplayName();

		UE_LOG(LogVisualStudioTools, Log, TEXT("Running %s"), *DisplayName);

		const int32 RoleIndex = 0; // always default to "local" role index.  Only used for multi-participant tests
		Framework.StartTestByName(TestCommand, RoleIndex);

		FDateTime Last = FDateTime::UtcNow();

		while (!Framework.ExecuteLatentCommands())
		{
			// Because we are not 'ticked' by the Engine we need to pump the TaskGraph
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

			const FDateTime Now = FDateTime::UtcNow();
			const float Delta = static_cast<float>((Now - Last).GetTotalSeconds());

			// .. and the core FTicker
#if ENGINE_MAJOR_VERSION >= 5
			FTSTicker::GetCoreTicker().Tick(Delta);
#else
			FTicker::GetCoreTicker().Tick(Delta);
#endif

			Last = Now;
		}

		FAutomationTestExecutionInfo ExecutionInfo;
		const bool CurrentTestSuccessful = Framework.StopTest(ExecutionInfo) && ExecutionInfo.GetErrorTotal() == 0;
		AllSuccessful = AllSuccessful && CurrentTestSuccessful;

		const FString Result = CurrentTestSuccessful ? TEXT("OK") : TEXT("FAIL");

		// [RUNTEST] is part of the protocol, so do not remove.
		OutFile << TEXT("[RUNTEST]") << *TestCommand << TEXT("|") << *DisplayName << TEXT("|") << *Result << TEXT("|") << ExecutionInfo.Duration << std::endl;

		if (!CurrentTestSuccessful)
		{
			for (const auto& Entry : ExecutionInfo.GetEntries())
			{
				if (Entry.Event.Type == EAutomationEventType::Error)
				{
					OutFile << *Entry.Event.Message << std::endl;
					UE_LOG(LogVisualStudioTools, Error, TEXT("%s"), *Entry.Event.Message);
				}
			}

			UE_LOG(LogVisualStudioTools, Log, TEXT("Failed  %s"), *DisplayName);
		}

		OutFile.flush();
	}

	return AllSuccessful ? 0 : 1;
}

UVSTestAdapterCommandlet::UVSTestAdapterCommandlet()
{
	HelpDescription = TEXT("Commandlet for generating data used by Blueprint support in Visual Studio.");
	HelpUsage = TEXT("<Editor-Cmd.exe> <path_to_uproject> -run=VSTestAdapter [-stdout -multiprocess -silent -unattended -AllowStdOutLogVerbosity -NoShaderCompile]");

	HelpParamNames.Add(ListTestsParam);
	HelpParamDescriptions.Add(TEXT("[Required] The file path to write the test cases retrieved from FAutomationTestFramework"));

	HelpParamNames.Add(RunTestsParam);
	HelpParamDescriptions.Add(TEXT("[Required] The test cases that will be sent to FAutomationTestFramework to run."));

	HelpParamNames.Add(TestResultsFileParam);
	HelpParamDescriptions.Add(TEXT("[Required] The output file from running test cases that we parse to retrieve test case results."));

	HelpParamNames.Add(FiltersParam);
	HelpParamDescriptions.Add(TEXT("[Optional] List of test filters to enable separated by '+'. Default is 'smoke+product+perf+stress+negative'"));

	HelpParamNames.Add(HelpParam);
	HelpParamDescriptions.Add(TEXT("[Optional] Print this help message and quit the commandlet immediately."));
}

void UVSTestAdapterCommandlet::PrintHelp() const
{
	UE_LOG(LogVisualStudioTools, Display, TEXT("%s"), *HelpDescription);
	UE_LOG(LogVisualStudioTools, Display, TEXT("Usage: %s"), *HelpUsage);
	UE_LOG(LogVisualStudioTools, Display, TEXT("Parameters:"));
	for (int32 Idx = 0; Idx < HelpParamNames.Num(); ++Idx)
	{
		UE_LOG(LogVisualStudioTools, Display, TEXT("\t-%s: %s"), *HelpParamNames[Idx], *HelpParamDescriptions[Idx]);
	}
}

int32 UVSTestAdapterCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;

	// Functionality for Unreal Engine Test Adapter.
	ParseCommandLine(*Params, Tokens, Switches, ParamVals);
	if (ParamVals.Contains(HelpParam))
	{
		PrintHelp();
		return 0;
	}

	// Default to all the test filters on except for engine tests.
	uint32 filter = EAutomationTestFlags::ProductFilter | EAutomationTestFlags::SmokeFilter |
					EAutomationTestFlags::PerfFilter | EAutomationTestFlags::StressFilter | EAutomationTestFlags::NegativeFilter;
	if (ParamVals.Contains(FiltersParam))
	{
		FString filters = ParamVals[FiltersParam];
		if (filters.Contains("smoke"))
		{
			filter |= EAutomationTestFlags::SmokeFilter;
		}
		else
		{
			filter &= ~EAutomationTestFlags::SmokeFilter;
		}

		if (filters.Contains("engine"))
		{
			filter |= EAutomationTestFlags::EngineFilter;
		}
		else
		{
			filter &= ~EAutomationTestFlags::EngineFilter;
		}

		if (filters.Contains("product"))
		{
			filter |= EAutomationTestFlags::ProductFilter;
		}
		else
		{
			filter &= ~EAutomationTestFlags::ProductFilter;
		}

		if (filters.Contains("perf"))
		{
			filter |= EAutomationTestFlags::PerfFilter;
		}
		else
		{
			filter &= ~EAutomationTestFlags::PerfFilter;
		}

		if (filters.Contains("stress"))
		{
			filter |= EAutomationTestFlags::StressFilter;
		}
		else
		{
			filter &= ~EAutomationTestFlags::StressFilter;
		}

		if (filters.Contains("negative"))
		{
			filter |= EAutomationTestFlags::NegativeFilter;
		}
		else
		{
			filter &= ~EAutomationTestFlags::NegativeFilter;
		}
	}

	FAutomationTestFramework::GetInstance().SetRequestedTestFilter(filter);
	if (ParamVals.Contains(ListTestsParam))
	{
		return ListTests(ParamVals[ListTestsParam]);
	}
	else if (ParamVals.Contains(RunTestsParam) && ParamVals.Contains(TestResultsFileParam))
	{
		return RunTests(ParamVals[RunTestsParam], ParamVals[TestResultsFileParam]);
	}

	PrintHelp();
	return 1;
}