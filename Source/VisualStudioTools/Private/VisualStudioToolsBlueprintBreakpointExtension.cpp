#include "VisualStudioToolsBlueprintBreakpointExtension.h"
#include "FSmartBSTR.h"
#include <Modules/ModuleManager.h>
#include <Framework/MultiBox/MultiBoxBuilder.h>
#include <BlueprintGraphClasses.h>
#include <EditorSubsystem.h>
#include <unknwn.h>
#include <Windows/WindowsPlatformStackWalk.h>
#include <Subsystems/Subsystem.h>
#include <Windows/WindowsPlatformMisc.h>
#include <SourceCodeNavigation.h>
#include <GraphEditorModule.h>
#include <Containers/Array.h>
#include <EdGraph/EdGraph.h>
#include <EdGraph/EdGraphNode.h>
#include <EdGraph/EdGraphPin.h>
#include <Framework/Commands/UIAction.h>
#include <Widgets/Notifications/SNotificationList.h>
#include <Framework/Notifications/NotificationManager.h>
#include <Misc/FileHelper.h>
#include <Interfaces/IProjectManager.h>
#include <Misc/UProjectInfo.h>
#include <ProjectDescriptor.h>
#include <Misc/App.h>
#include <Runtime/Launch/Resources/Version.h>
#include <EditorStyleSet.h>

DEFINE_LOG_CATEGORY(LogUVisualStudioToolsBlueprintBreakpointExtension);

#if ENGINE_MAJOR_VERSION >= 5

static const FName GraphEditorModuleName(TEXT("GraphEditor"));

void UVisualStudioToolsBlueprintBreakpointExtension::Initialize(FSubsystemCollectionBase& Collection)
{
	FGraphEditorModule& GraphEditorModule = FModuleManager::LoadModuleChecked<FGraphEditorModule>(GraphEditorModuleName);
	GraphEditorModule.GetAllGraphEditorContextMenuExtender().Add(
		FGraphEditorModule::FGraphEditorMenuExtender_SelectedNode::CreateUObject(this, &ThisClass::HandleOnExtendGraphEditorContextMenu));
}

void UVisualStudioToolsBlueprintBreakpointExtension::Deinitialize()
{
	FGraphEditorModule* GraphEditorModule = FModuleManager::GetModulePtr<FGraphEditorModule>(GraphEditorModuleName);
	if (!GraphEditorModule)
	{
		return;
	}

	GraphEditorModule->GetAllGraphEditorContextMenuExtender().RemoveAll(
		[](const FGraphEditorModule::FGraphEditorMenuExtender_SelectedNode& Delegate) {
			FName LocalFunction = GET_FUNCTION_NAME_CHECKED(ThisClass, HandleOnExtendGraphEditorContextMenu);
			return Delegate.TryGetBoundFunctionName() == LocalFunction;
		});
}

TSharedRef<FExtender> UVisualStudioToolsBlueprintBreakpointExtension::HandleOnExtendGraphEditorContextMenu(
	const TSharedRef<FUICommandList> CommandList,
	const UEdGraph* Graph,
	const UEdGraphNode* Node,
	const UEdGraphPin* Pin,
	bool /* bIsConst */)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();
	if (!CanAddVisualStudioBreakpoint(Node, nullptr, nullptr))
	{
		return Extender;
	}

	const FName ExtensionHook(TEXT("EdGraphSchemaNodeActions"));
	Extender->AddMenuExtension(
		ExtensionHook,
		EExtensionHook::After,
		CommandList,
		FMenuExtensionDelegate::CreateUObject(this, &ThisClass::AddVisualStudioBlueprintBreakpointMenuOption, Node));

	return Extender;
}

void UVisualStudioToolsBlueprintBreakpointExtension::AddVisualStudioBlueprintBreakpointMenuOption(FMenuBuilder& MenuBuilder, const UEdGraphNode *Node)
{
	MenuBuilder.BeginSection(TEXT("VisualStudioTools"), FText::FromString("Visual Studio Tools"));
	MenuBuilder.AddMenuEntry(
		FText::FromString("Set breakpoint in Visual Studio"),
		FText::FromString("This will set a breakpoint in Visual Studio so the native debugger can break the execution"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateUObject(this, &ThisClass::AddVisualStudioBreakpoint, Node)));
	MenuBuilder.EndSection();
}

FString UVisualStudioToolsBlueprintBreakpointExtension::GetProjectPath(const FString &ProjectDir)
{
	FString ProjectPath;
	if (!FFileHelper::LoadFileToString(ProjectPath, *(FPaths::EngineIntermediateDir() / TEXT("ProjectFiles") / TEXT("PrimaryProjectPath.txt"))))
	{
		const FProjectDescriptor* CurrentProject = IProjectManager::Get().GetCurrentProject();

		if ((CurrentProject == nullptr || CurrentProject->Modules.Num() == 0) || !FUProjectDictionary::GetDefault().IsForeignProject(ProjectDir))
		{
			ProjectPath = FPaths::RootDir() / TEXT("UE5");
		}
		else
		{
			const FString BaseName = FApp::HasProjectName() ? FApp::GetProjectName() : FPaths::GetBaseFilename(ProjectDir);
			ProjectPath = ProjectDir / BaseName;
		}
	}

	ProjectPath = ProjectPath + TEXT(".sln");

	FPaths::NormalizeFilename(ProjectPath);

	return ProjectPath;
}

bool UVisualStudioToolsBlueprintBreakpointExtension::GetRunningVisualStudioDTE(TComPtr<EnvDTE::_DTE>& OutDTE)
{
	IRunningObjectTable* RunningObjectTable;
	bool bResult = false;
	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(ProjectDir);
	FString SolutionPath = GetProjectPath(ProjectDir);
	
	if (SUCCEEDED(GetRunningObjectTable(0, &RunningObjectTable)) && RunningObjectTable)
	{
		IEnumMoniker* MonikersTable;
		if (SUCCEEDED(RunningObjectTable->EnumRunning(&MonikersTable)))
		{
			MonikersTable->Reset();

			// Look for all visual studio instances in the ROT
			IMoniker* CurrentMoniker;
			while (MonikersTable->Next(1, &CurrentMoniker, NULL) == S_OK)
			{
				IBindCtx* BindContext;
				LPOLESTR OutName;
				if (SUCCEEDED(CreateBindCtx(0, &BindContext)) && SUCCEEDED(CurrentMoniker->GetDisplayName(BindContext, NULL, &OutName)))
				{
					TComPtr<IUnknown> ComObject;
					if (SUCCEEDED(RunningObjectTable->GetObject(CurrentMoniker, &ComObject)))
					{
						TComPtr<EnvDTE::_DTE> TempDTE;
						if (SUCCEEDED(TempDTE.FromQueryInterface(__uuidof(EnvDTE::_DTE), ComObject)))
						{
							TComPtr<EnvDTE::_Solution> Solution;
							BSTR OutPath = nullptr;
							if (SUCCEEDED(TempDTE->get_Solution(&Solution)) &&
								SUCCEEDED(Solution->get_FullName(&OutPath)))
							{
								FString Filename(OutPath);
								FPaths::NormalizeFilename(Filename);
								if (Filename == SolutionPath || Filename == ProjectDir)
								{
									OutDTE = TempDTE;
									bResult = true;
								}
							}
							SysFreeString(OutPath);
						}
					}
				}
				BindContext->Release();
				CurrentMoniker->Release();
				if (bResult) break;
			}
			MonikersTable->Release();
			RunningObjectTable->Release();
		}
	}

	return bResult;
}

bool UVisualStudioToolsBlueprintBreakpointExtension::CanAddVisualStudioBreakpoint(const UEdGraphNode* Node, UClass **OutOwnerClass, UFunction **OutFunction)
{
	const UK2Node_CallFunction* K2Node = Cast<const UK2Node_CallFunction>(Node);
	if (!K2Node)
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Verbose, TEXT("Node is not a UK2Node_CallFunction"));
		return false;
	}

	UFunction* Function = K2Node->GetTargetFunction();
	if (!Function || !Function->IsNative())
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Verbose, TEXT("Function is not native"));
		return false;
	}

	UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Log, TEXT("Trying to get function definition for %s"), *Function->GetName());

	UClass* OwnerClass = Function->GetOwnerClass();
	if (!OwnerClass->HasAllClassFlags(CLASS_Native))
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Verbose, TEXT("Owning class is not native"));
		return false;
	}

	if (OutOwnerClass) *OutOwnerClass = OwnerClass;
	if (OutFunction) *OutFunction = Function;
	return true;
}

bool UVisualStudioToolsBlueprintBreakpointExtension::GetFunctionDefinitionLocation(const UEdGraphNode* Node, FString& SourceFilePath, FString& SymbolName, uint32& SourceLineNumber)
{
	UClass* OwningClass;
	UFunction* Function;
	if (!CanAddVisualStudioBreakpoint(Node, &OwningClass, &Function))
	{
		return false;
	}

	FString ModuleName;

	// Find module name for class
	if (!FSourceCodeNavigation::FindClassModuleName(OwningClass, ModuleName))
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Failed to find module name for class"));
		return false;
	}

	SymbolName = FString::Printf(
		TEXT("%s%s::%s"),
		OwningClass->GetPrefixCPP(),
		*OwningClass->GetName(),
		*Function->GetName());

	UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Log, TEXT("Symbol %s is defined in module %s"), *SymbolName, *ModuleName);

	uint32 SourceColumnNumber = 0;
	return FPlatformStackWalk::GetFunctionDefinitionLocation(
		SymbolName,
		ModuleName,
		SourceFilePath,
		SourceLineNumber,
		SourceColumnNumber);
}

bool UVisualStudioToolsBlueprintBreakpointExtension::SetVisualStudioBreakpoint(const UEdGraphNode* Node, const FString& SourceFilePath, const FString& SymbolName, uint32 SourceLineNumber)
{		
	TComPtr<EnvDTE::_DTE> DTE;
	bool bBreakpointAdded = false;
	if (!GetRunningVisualStudioDTE(DTE))
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Failed to access Visual Studio via DTE"));
		return bBreakpointAdded;
	}
	
	TComPtr<EnvDTE::Debugger> Debugger;
	TComPtr<EnvDTE::Breakpoints> Breakpoints;
	if (SUCCEEDED(DTE->get_Debugger(&Debugger)) && SUCCEEDED(Debugger->get_Breakpoints(&Breakpoints)))
	{
		FSmartBSTR BSTREmptyStr;
		FSmartBSTR BSTRFilePath(SourceFilePath);
		HRESULT Result = Breakpoints->Add(
			*BSTREmptyStr,
			*BSTRFilePath,
			SourceLineNumber,
			1,
			*BSTREmptyStr,
			EnvDTE::dbgBreakpointConditionType::dbgBreakpointConditionTypeWhenTrue,
			*BSTREmptyStr,
			*BSTREmptyStr,
			0,
			*BSTREmptyStr,
			0,
			EnvDTE::dbgHitCountType::dbgHitCountTypeNone,
			&Breakpoints);
		
		if (FAILED(Result))
		{
			UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Failed to add breakpoint"));
		}
		else
		{
			bBreakpointAdded = true;
			UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Log, TEXT("Breakpoint set for %s"), *SymbolName);
		}
	}
	else
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Failed to get debugger or breakpoints"));
	}

	return bBreakpointAdded;
}

void UVisualStudioToolsBlueprintBreakpointExtension::AddVisualStudioBreakpoint(const UEdGraphNode* Node)
{
	FWindowsPlatformMisc::CoInitialize();
	FPlatformStackWalk::InitStackWalking();
	FString SourceFilePath;
	FString SymbolName;
	uint32 SourceLineNumber;
	bool bBreakpointAdded = false;
	
	if (GetFunctionDefinitionLocation(Node, SourceFilePath, SymbolName, SourceLineNumber))
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Log, TEXT("Method defined in %s at line %d"), *SourceFilePath, SourceLineNumber);
		bBreakpointAdded = SetVisualStudioBreakpoint(Node, SourceFilePath, SymbolName, SourceLineNumber);
	}
	else
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Failed to get function definition location"));
	}

	ShowOperationResultNotification(bBreakpointAdded, SymbolName);
	FWindowsPlatformMisc::CoUninitialize();
}

void UVisualStudioToolsBlueprintBreakpointExtension::ShowOperationResultNotification(bool bBreakpointAdded, const FString &SymbolName)
{
	FNotificationInfo Info(bBreakpointAdded ? FText::FromString(FString::Printf(TEXT("Breakpoint added at %s"), *SymbolName)) : FText::FromString("Could not add Breakpoint in Visual Studio"));
#if ENGINE_MAJOR_VERSION >=5 && ENGINE_MINOR_VERSION >= 1
	Info.Image = FAppStyle::GetBrush(TEXT("LevelEditor.RecompileGameCode"));
#else
	Info.Image = FEditorStyle::GetBrush(TEXT("LevelEditor.RecompileGameCode"));
#endif
	Info.FadeInDuration = 0.1f;
	Info.FadeOutDuration = 0.5f;
	Info.ExpireDuration = 3.0f;
	Info.bUseThrobber = false;
	Info.bUseSuccessFailIcons = true;
	Info.bUseLargeFont = true;
	Info.bFireAndForget = false;
	Info.bAllowThrottleWhenFrameRateIsLow = false;
	Info.WidthOverride = 400.0f;
	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
	NotificationItem->SetCompletionState(bBreakpointAdded ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	NotificationItem->ExpireAndFadeout();
}

#else

void UVisualStudioToolsBlueprintBreakpointExtension::Initialize(FSubsystemCollectionBase& Collection)
{
}

void UVisualStudioToolsBlueprintBreakpointExtension::Deinitialize()
{
}

TSharedRef<FExtender> HandleOnExtendGraphEditorContextMenu(
	const TSharedRef<FUICommandList> CommandList,
	const UEdGraph* Graph,
	const UEdGraphNode* Node,
	const UEdGraphPin* Pin,
	bool bIsConst)
{
    return MakeShared<FExtender>();
}

void UVisualStudioToolsBlueprintBreakpointExtension::AddVisualStudioBlueprintBreakpointMenuOption(FMenuBuilder& MenuBuilder, const UEdGraphNode* node)
{
}

void UVisualStudioToolsBlueprintBreakpointExtension::AddVisualStudioBreakpoint(const UEdGraphNode* Node)
{
}

bool UVisualStudioToolsBlueprintBreakpointExtension::GetFunctionDefinitionLocation(const UEdGraphNode* Node, FString& SourceFilePath, FString& SymbolName, uint32& SourceLineNumber)
{
    return false;
}

bool UVisualStudioToolsBlueprintBreakpointExtension::SetVisualStudioBreakpoint(const UEdGraphNode* Node, const FString& SourceFilePath, const FString& SymbolName, uint32 SourceLineNumber)
{
    return false;
}

bool UVisualStudioToolsBlueprintBreakpointExtension::CanAddVisualStudioBreakpoint(const UEdGraphNode* Node, UClass** OutOwnerClass, UFunction** OutFunction)
{
    return false;
}

void UVisualStudioToolsBlueprintBreakpointExtension::ShowOperationResultNotification(bool bBreakpointAdded, const FString& SymbolName)
{
}

FString UVisualStudioToolsBlueprintBreakpointExtension::GetProjectPath(const FString& ProjectDir)
{
    return FString();
}

bool UVisualStudioToolsBlueprintBreakpointExtension::GetRunningVisualStudioDTE(TComPtr<EnvDTE::_DTE>& OutDTE)
{
    return false;
}

#endif