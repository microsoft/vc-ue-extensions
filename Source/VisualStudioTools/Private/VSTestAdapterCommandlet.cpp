// Copyright 2022 (c) Microsoft. All rights reserved.

#include "VSTestAdapterCommandlet.h"

#include <Runtime/Core/Public/Async/TaskGraphInterfaces.h>
#include <Runtime/Core/Public/Containers/Ticker.h>
#include <fstream>
#include <string>

#include "VisualStudioTools.h"

int32 UVSTestAdapterCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;

	// Functionality for Unreal Engine Test Adapter.
	ParseCommandLine(*Params, Tokens, Switches, ParamVals);
	FAutomationTestFramework::GetInstance().SetRequestedTestFilter( EAutomationTestFlags::PriorityMask | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::SmokeFilter | EAutomationTestFlags::PerfFilter | EAutomationTestFlags::StressFilter | EAutomationTestFlags::NegativeFilter );	
	if ( ParamVals.Contains( TEXT( "listtests" ) ) )
	{
		return ListTests( ParamVals[TEXT( "listtests" )] );
	}
	else if ( ParamVals.Contains( TEXT( "runtests" ) ) && ParamVals.Contains( TEXT( "testresultfile" ) ) )
	{
		return RunTests( ParamVals[TEXT( "runtests" )], ParamVals[TEXT( "testresultfile" )] );
	}

	return 0;
}


int32 UVSTestAdapterCommandlet::ListTests( const FString& TargetFile )
{
	std::wofstream OutFile( *TargetFile );
	if ( !OutFile.good() )
	{
		UE_LOG( LogVisualStudioTools, Error, TEXT( "Failed to open file at path: %s" ), *TargetFile );
		return 1;
	}

	FAutomationTestFramework& Framework = FAutomationTestFramework::GetInstance();

	TArray< FAutomationTestInfo > TestInfos;
	GetAllTests( TestInfos );

	for ( const auto& TestInfo : TestInfos )
	{
		const FString TestCommand = TestInfo.GetTestName();
		const FString DisplayName = TestInfo.GetDisplayName();
		const FString SourceFile = TestInfo.GetSourceFile();
		const int32 Line = TestInfo.GetSourceFileLine();

		OutFile << *TestCommand << TEXT( "|" ) << *DisplayName << TEXT( "|" ) << Line << TEXT( "|" ) << *SourceFile << std::endl;
	}

	UE_LOG( LogVisualStudioTools, Display, TEXT( "Found %d tests" ), TestInfos.Num() );

	OutFile.close();
	return 0;
}

int32 UVSTestAdapterCommandlet::RunTests( const FString& TestListFile, const FString& ResultsFile )
{
	std::wofstream OutFile( *ResultsFile );
	if ( !OutFile.good() )
	{
		UE_LOG( LogVisualStudioTools, Error, TEXT( "Failed to open file at path: %s" ), *ResultsFile );
		return 1;
	}

	TArray< FAutomationTestInfo > TestInfos;
	if ( TestListFile.Equals( TEXT( "All" ), ESearchCase::IgnoreCase ) )
	{
		GetAllTests( TestInfos );
	}
	else
	{
		ReadTestsFromFile( TestListFile, TestInfos );
	}

	bool AllSuccessful = true;

	FAutomationTestFramework& Framework = FAutomationTestFramework::GetInstance();

	for ( const FAutomationTestInfo& TestInfo : TestInfos )
	{
		const FString TestCommand = TestInfo.GetTestName();
		const FString DisplayName = TestInfo.GetDisplayName();

		UE_LOG( LogVisualStudioTools, Log, TEXT( "Running %s" ), *DisplayName );

		const int32 RoleIndex = 0; // always default to "local" role index.  Only used for multi-participant tests
		Framework.StartTestByName( TestCommand, RoleIndex );

		FDateTime Last = FDateTime::UtcNow();

		while ( !Framework.ExecuteLatentCommands() )
		{
			// Because we are not 'ticked' by the Engine we need to pump the TaskGraph
			FTaskGraphInterface::Get().ProcessThreadUntilIdle( ENamedThreads::GameThread );

			const FDateTime Now = FDateTime::UtcNow();
			const FTimespan Delta = Now - Last;

			// .. and the core FTicker
			FTSTicker::GetCoreTicker().Tick( Delta.GetTotalSeconds() );

			Last = Now;
		}

		FAutomationTestExecutionInfo ExecutionInfo;
		const bool CurrentTestSuccessful = Framework.StopTest( ExecutionInfo ) && ExecutionInfo.GetErrorTotal() == 0;
		AllSuccessful = AllSuccessful && CurrentTestSuccessful;

		const FString Result = CurrentTestSuccessful ? TEXT( "OK" ) : TEXT( "FAIL" );

		OutFile << TEXT( "[RUNTEST]" ) << *TestCommand << TEXT( "|" ) << *DisplayName << TEXT( "|" ) << *Result << TEXT( "|" ) << ExecutionInfo.Duration << std::endl;

		if ( !CurrentTestSuccessful )
		{
			for ( const auto& Entry : ExecutionInfo.GetEntries() )
			{
				if ( Entry.Event.Type == EAutomationEventType::Error )
				{
					OutFile << *Entry.Event.Message << std::endl;
					UE_LOG( LogVisualStudioTools, Error, TEXT( "%s" ), *Entry.Event.Message );
				}
			}

			UE_LOG( LogVisualStudioTools, Log, TEXT( "Failed  %s" ), *DisplayName );
		}

		OutFile.flush();
	}

	return AllSuccessful ? 0 : 1;
}


void UVSTestAdapterCommandlet::ReadTestsFromFile( const FString& InFile, TArray< FAutomationTestInfo >& OutTestList )
{
	TSet< FString > TestCommands;

	std::wifstream InStream( *InFile );
	if ( !InStream.good() )
	{
		UE_LOG( LogVisualStudioTools, Error, TEXT( "Failed to open file at path: %s" ), *InFile );
		return;
	}

	std::wstring Line;
	while ( std::getline( InStream, Line ) )
	{
		if ( Line.length() > 0 )
		{
			TestCommands.Add( FString( Line.c_str() ) );
		}
	}
	InStream.close();

	GetAllTests( OutTestList );
	for ( int32 Idx = OutTestList.Num() - 1; Idx >= 0; Idx-- )
	{
		if ( !TestCommands.Contains( OutTestList[Idx].GetTestName() ) )
		{
			OutTestList.RemoveAt( Idx );
		}
	}
}

void UVSTestAdapterCommandlet::GetAllTests( TArray< FAutomationTestInfo >& OutTestList )
{
	FAutomationTestFramework& Framework = FAutomationTestFramework::GetInstance();
	Framework.GetValidTestNames( OutTestList );
}