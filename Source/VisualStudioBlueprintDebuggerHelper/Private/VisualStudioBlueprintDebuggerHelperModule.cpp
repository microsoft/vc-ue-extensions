// Copyright 2022 (c) Microsoft. All rights reserved.

#include "VisualStudioBlueprintDebuggerHelperModule.h"
#include <Modules/ModuleManager.h>
#include <UObject/Script.h>
#include <UObject/Stack.h>
#include <UObject/Object.h>
#include <Templates/Casts.h>
#include <Kismet2/KismetDebugUtilities.h>
#include <Containers/Array.h>
#include <UObject/Class.h>
#include <Engine/BlueprintGeneratedClass.h>
#include <Engine/Blueprint.h>
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
#include <Blueprint/BlueprintExceptionInfo.h>
#endif
#include <Internationalization/Text.h>
#include <HAL/Platform.h>
#include <EdGraph/EdGraphNode.h>
#include <EdGraph/EdGraphPin.h>
#include <Templates/SharedPointer.h>
#include <Templates/Tuple.h>
#include <CoreGlobals.h>
#include <map>

IMPLEMENT_MODULE(FVisualStudioBlueprintDebuggerHelper, VisualStudioBlueprintDebuggerHelper);

DEFINE_LOG_CATEGORY(LogVisualStudioBlueprintDebuggerHelper);

#if ENGINE_MAJOR_VERSION >= 5
#define FCustomBlueprintPropertyInfo TSharedPtr<FPropertyInstanceInfo>
#else
#define FCustomBlueprintPropertyInfo FDebugInfo
#endif

struct FVSNodePinRuntimeInformation
{
	UEdGraphPin* Pin;
	FCustomBlueprintPropertyInfo Property;

	FVSNodePinRuntimeInformation(UEdGraphPin* InPin, FCustomBlueprintPropertyInfo InProperty)
		: Pin(InPin)
		, Property(InProperty)
	{
	}
};

struct FVSNodeData
{
	FText NodeName;
	TArray<TSharedPtr<FVSNodePinRuntimeInformation>> Properties;
	int32 ScriptEntryTag;
	const UEdGraphNode* Node;
};

struct FVSNodesRuntimeInformation
{
	TArray<TSharedPtr<FVSNodeData>> Nodes;
};

struct FVSBlueprintRuntimeInformation
{
	TArray<TTuple<UBlueprint*, TSharedPtr<FVSNodesRuntimeInformation>>> RunningBlueprints;
};

struct StackTraceHelper
{
	int32 ScriptEntryTag;
	FString NodeName;
};

// Keep exported so we can read it.
VISUALSTUDIOBLUEPRINTDEBUGGERHELPER_API FVSBlueprintRuntimeInformation BlueprintsRuntimeInformation;

VISUALSTUDIOBLUEPRINTDEBUGGERHELPER_API std::map<void*, StackTraceHelper> StackFrameInformation;

VISUALSTUDIOBLUEPRINTDEBUGGERHELPER_API const char* DebuggerHelperVersion = "1.0.0";

void FVisualStudioBlueprintDebuggerHelper::StartupModule()
{
	CurrentScriptEntryTag = 0;

	FBlueprintContextTracker::OnEnterScriptContext.AddRaw(
		this,
		&FVisualStudioBlueprintDebuggerHelper::OnEnterScriptContext);

	FBlueprintContextTracker::OnExitScriptContext.AddRaw(
		this,
		&FVisualStudioBlueprintDebuggerHelper::OnExitScriptContext);

	FBlueprintCoreDelegates::OnScriptException.AddRaw(
		this,
		&FVisualStudioBlueprintDebuggerHelper::OnScriptException);
}

void FVisualStudioBlueprintDebuggerHelper::ShutdownModule()
{
	FBlueprintCoreDelegates::OnScriptException.RemoveAll(this);
	FBlueprintContextTracker::OnExitScriptContext.RemoveAll(this);
	FBlueprintContextTracker::OnEnterScriptContext.RemoveAll(this);
}

void FVisualStudioBlueprintDebuggerHelper::OnEnterScriptContext(
	const struct FBlueprintContextTracker& Context,
	const UObject* SourceObject,
	const UFunction* Function)
{
	if (!IsInGameThread())
	{
		return;
	}

	CurrentScriptEntryTag = Context.GetScriptEntryTag();
}

void FVisualStudioBlueprintDebuggerHelper::OnExitScriptContext(const struct FBlueprintContextTracker& Context)
{
	if (!IsInGameThread())
	{
		return;
	}

	for (auto ItRunningBlueprints = BlueprintsRuntimeInformation.RunningBlueprints.CreateIterator(); ItRunningBlueprints; ++ItRunningBlueprints)
	{
		auto& RunningBlueprint = ItRunningBlueprints->Value;
		for (auto ItNodeData = RunningBlueprint->Nodes.CreateIterator(); ItNodeData; ++ItNodeData)
		{
			if ((*ItNodeData)->ScriptEntryTag == Context.GetScriptEntryTag())
			{
				ItNodeData.RemoveCurrent();
			}
		}

		if (!RunningBlueprint->Nodes.Num())
		{
			ItRunningBlueprints.RemoveCurrent();
		}
	}

	for (auto ItStackFrameInfo = StackFrameInformation.begin(); ItStackFrameInfo != StackFrameInformation.end();)
	{
		if (ItStackFrameInfo->second.ScriptEntryTag == Context.GetScriptEntryTag())
		{
			ItStackFrameInfo = StackFrameInformation.erase(ItStackFrameInfo);
		}
		else
		{
			++ItStackFrameInfo;
		}
	}

	CurrentScriptEntryTag--;
}

void FVisualStudioBlueprintDebuggerHelper::OnScriptException(
	const UObject* Owner,
	const struct FFrame& Stack,
	const FBlueprintExceptionInfo& ExceptionInfo)
{
	EBlueprintExceptionType::Type ExceptionType = ExceptionInfo.GetType();
	if (ExceptionType != EBlueprintExceptionType::Type::Tracepoint &&
		ExceptionType != EBlueprintExceptionType::Type::WireTracepoint &&
		ExceptionType != EBlueprintExceptionType::Type::Breakpoint)
	{
		return;
	}

	UFunction* NodeFunction = Cast<UFunction>(Stack.Node);
	if (!NodeFunction)
	{
		return;
	}

	UBlueprintGeneratedClass* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(NodeFunction->GetOuter());
	if (!BlueprintGeneratedClass)
	{
		return;
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintGeneratedClass->ClassGeneratedBy);
	if (!Blueprint)
	{
		return;
	}

	const int32 BreakpointOffset = Stack.Code - Stack.Node->Script.GetData() - 1;
	const UEdGraphNode* NodeStoppedAt = FKismetDebugUtilities::FindSourceNodeForCodeLocation(Owner, Stack.Node, BreakpointOffset, /*bAllowImpreciseHit=*/ true);
	if (!NodeStoppedAt)
	{
		return;
	}

	StackFrameInformation[NodeFunction] = { CurrentScriptEntryTag, FString::Printf(TEXT("%s::%s"), *Blueprint->GetFriendlyName(), *NodeStoppedAt->GetNodeTitle(ENodeTitleType::Type::ListView).ToString()) };
	TTuple<UBlueprint*, TSharedPtr<FVSNodesRuntimeInformation>>* ExistingNodesRuntimeInformationTuple = BlueprintsRuntimeInformation.RunningBlueprints.FindByPredicate([&Blueprint](const TTuple<UBlueprint*, TSharedPtr<FVSNodesRuntimeInformation>>& Tuple) {
		return Tuple.Key == Blueprint;
	});

	TSharedPtr<FVSNodesRuntimeInformation> NodesRuntimeInformation;
	if (!ExistingNodesRuntimeInformationTuple)
	{
		NodesRuntimeInformation = MakeShared<FVSNodesRuntimeInformation>();
		BlueprintsRuntimeInformation.RunningBlueprints.Add(MakeTuple(Blueprint, NodesRuntimeInformation));
	}
	else
	{
		NodesRuntimeInformation = ExistingNodesRuntimeInformationTuple->Value;
	}

	TSharedPtr<FVSNodeData> CurrentNodeData;
	if (NodesRuntimeInformation->Nodes.Num() == 0 || NodeStoppedAt != NodesRuntimeInformation->Nodes.Top()->Node)
	{
		CurrentNodeData = MakeShared<FVSNodeData>();
		CurrentNodeData->Node = NodeStoppedAt;
		CurrentNodeData->NodeName = NodeStoppedAt->GetNodeTitle(ENodeTitleType::Type::ListView);
		CurrentNodeData->ScriptEntryTag = CurrentScriptEntryTag;
		NodesRuntimeInformation->Nodes.Push(CurrentNodeData);
	}
	else
	{
		CurrentNodeData = NodesRuntimeInformation->Nodes.Top();
	}

	FCustomBlueprintPropertyInfo PinInstanceInfo;
	for (auto GraphPin : NodeStoppedAt->Pins)
	{
		FKismetDebugUtilities::EWatchTextResult DebugResult = FKismetDebugUtilities::GetDebugInfo(PinInstanceInfo, Blueprint, (UObject*)Owner, GraphPin);
		if (DebugResult != FKismetDebugUtilities::EWTR_Valid)
		{
			continue;
		}

		TSharedPtr<FVSNodePinRuntimeInformation>* Existing = CurrentNodeData->Properties.FindByPredicate([&GraphPin](TSharedPtr<FVSNodePinRuntimeInformation>& PinInfo) {
			return PinInfo->Pin == GraphPin;
		});

		if (!Existing)
		{
			CurrentNodeData->Properties.Add(MakeShared<FVSNodePinRuntimeInformation>(GraphPin, PinInstanceInfo));
		}
		else
		{
			(*Existing)->Property = PinInstanceInfo;
		}
	}
}
