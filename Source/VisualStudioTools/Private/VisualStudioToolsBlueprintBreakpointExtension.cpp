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
#include <Windows/WindowsPlatformProcess.h>
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

#if ENGINE_MAJOR_VERSION < 5
#include <DbgHelp.h>
#include <Psapi.h>
#endif

DEFINE_LOG_CATEGORY(LogUVisualStudioToolsBlueprintBreakpointExtension);

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
							else
							{
								UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Could not get solution from DTE"));
							}

							SysFreeString(OutPath);
						}
					}
				}
				else
				{
					UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Could not get display name for moniker"));
				}
				BindContext->Release();
				CurrentMoniker->Release();
				if (bResult) break;
			}
			MonikersTable->Release();
			RunningObjectTable->Release();
		}
		else
		{
			UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Could not enumerate Running Object Table"));
		}
	}
	else
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Could not get Running Object Table"));
	}

	return bResult;
}

bool UVisualStudioToolsBlueprintBreakpointExtension::CanAddVisualStudioBreakpoint(const UEdGraphNode* Node, UClass **OutOwnerClass, UFunction **OutFunction)
{
	const UK2Node_CallFunction* K2Node = Cast<const UK2Node_CallFunction>(Node);
	if (!K2Node)
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Log, TEXT("Node is not a UK2Node_CallFunction"));
		return false;
	}

	UFunction* Function = K2Node->GetTargetFunction();
	if (!Function || !Function->IsNative())
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Log, TEXT("Function is not native"));
		return false;
	}

	UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Log, TEXT("Trying to get function definition for %s"), *Function->GetName());

	UClass* OwnerClass = Function->GetOwnerClass();
	if (!OwnerClass->HasAllClassFlags(CLASS_Native))
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Log, TEXT("Owning class is not native"));
		return false;
	}

	if (OutOwnerClass) *OutOwnerClass = OwnerClass;
	if (OutFunction) *OutFunction = Function;
	return true;
}

#if ENGINE_MAJOR_VERSION < 5

#define PRINT_PLATFORM_ERROR_MSG(_TXT) \
	do { \
		TCHAR _ErrorBuffer[MAX_SPRINTF] = { 0 }; \
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("" #_TXT ": [%s]"), \
			FPlatformMisc::GetSystemErrorMessage(_ErrorBuffer, MAX_SPRINTF, 0)); \
	} while (0)

bool UVisualStudioToolsBlueprintBreakpointExtension::PreloadModule(HANDLE ProcessHandle, HMODULE ModuleHandle, const FString& RemoteStorage)
{
	int32 ErrorCode = 0;
	MODULEINFO ModuleInfo = { 0 };
	WCHAR ModuleName[FProgramCounterSymbolInfo::MAX_NAME_LENGTH] = { 0 };
	WCHAR ImageName[FProgramCounterSymbolInfo::MAX_NAME_LENGTH] = { 0 };
#if PLATFORM_64BITS
	static_assert(sizeof(MODULEINFO) == 24, "Broken alignment for 64bit Windows include.");
#else
	static_assert(sizeof(MODULEINFO) == 12, "Broken alignment for 32bit Windows include.");
#endif

	if (!GetModuleInformation(ProcessHandle, ModuleHandle, &ModuleInfo, sizeof(ModuleInfo)))
	{
		PRINT_PLATFORM_ERROR_MSG("Could not read GetModuleInformation");
		return false;
	}

	IMAGEHLP_MODULE64 ImageHelpModule = { 0 };
	ImageHelpModule.SizeOfStruct = sizeof(ImageHelpModule);
	if (!SymGetModuleInfo64(ProcessHandle, (DWORD64)ModuleInfo.EntryPoint, &ImageHelpModule))
	{
		PRINT_PLATFORM_ERROR_MSG("Could not SymGetModuleInfo64 from module");
		return false;
	}

	if (ImageHelpModule.SymType != SymDeferred && ImageHelpModule.SymType != SymNone)
	{
		return true;
	}

	if (!GetModuleFileNameExW(ProcessHandle, ModuleHandle, ImageName, 1024))
	{
		PRINT_PLATFORM_ERROR_MSG("Could not GetModuleFileNameExW");
		return false;
	}

	if (!GetModuleBaseNameW(ProcessHandle, ModuleHandle, ModuleName, 1024))
	{
		PRINT_PLATFORM_ERROR_MSG("Could not GetModuleBaseNameW");
		return false;
	}

	WCHAR SearchPath[MAX_PATH] = { 0 };
	WCHAR* FileName = NULL;
	const auto Result = GetFullPathNameW(ImageName, MAX_PATH, SearchPath, &FileName);

	FString SearchPathList;
	if (Result != 0 && Result < MAX_PATH)
	{
		*FileName = 0;
		SearchPathList = SearchPath;
	}

	if (!RemoteStorage.IsEmpty())
	{
		if (!SearchPathList.IsEmpty())
		{
			SearchPathList.AppendChar(TEXT(';'));
		}
		SearchPathList.Append(RemoteStorage);
	}

	if (!SymSetSearchPathW(ProcessHandle, *SearchPathList))
	{
		PRINT_PLATFORM_ERROR_MSG("Could not SymSetSearchPathW");
		return false;
	}

	const DWORD64 BaseAddress = SymLoadModuleExW(
		ProcessHandle,
		ModuleHandle,
		ImageName,
		ModuleName,
		(DWORD64)ModuleInfo.lpBaseOfDll,
		ModuleInfo.SizeOfImage,
		NULL,
		0);
	if (!BaseAddress)
	{
		PRINT_PLATFORM_ERROR_MSG("Could not load the module");
		return false;
	}

	return true;
}

bool UVisualStudioToolsBlueprintBreakpointExtension::GetFunctionDefinitionLocation(const FString& FunctionSymbolName, const FString& FunctionModuleName, FString& SourceFilePath, uint32& SourceLineNumber)
{
	const HANDLE ProcessHandle = GetCurrentProcess();
	HMODULE ModuleHandle = GetModuleHandle(*FunctionModuleName);
	if (!ModuleHandle || !PreloadModule(ProcessHandle, ModuleHandle, FPlatformStackWalk::GetDownstreamStorage()))
	{
		return false;
	}

	ANSICHAR SymbolInfoBuffer[sizeof(IMAGEHLP_SYMBOL64) + MAX_SYM_NAME];
	PIMAGEHLP_SYMBOL64 SymbolInfoPtr = reinterpret_cast<IMAGEHLP_SYMBOL64*>(SymbolInfoBuffer);
	SymbolInfoPtr->SizeOfStruct = sizeof(SymbolInfoBuffer);
	SymbolInfoPtr->MaxNameLength = MAX_SYM_NAME;

	FString FullyQualifiedSymbolName = FunctionSymbolName;
	if (!FunctionModuleName.IsEmpty())
	{
		FullyQualifiedSymbolName = FString::Printf(TEXT("%s!%s"), *FunctionModuleName, *FunctionSymbolName);
	}

	if (!SymGetSymFromName64(ProcessHandle, TCHAR_TO_ANSI(*FullyQualifiedSymbolName), SymbolInfoPtr))
	{
		PRINT_PLATFORM_ERROR_MSG("Could not load module symbol information");
		return false;
	}

	IMAGEHLP_LINE64 FileAndLineInfo;
	FileAndLineInfo.SizeOfStruct = sizeof(FileAndLineInfo);

	uint32 SourceColumnNumber = 0;
	if (!SymGetLineFromAddr64(ProcessHandle, SymbolInfoPtr->Address, (::DWORD*)&SourceColumnNumber, &FileAndLineInfo))
	{
		PRINT_PLATFORM_ERROR_MSG("Could not query module file and line number");
		return false;
	}

	SourceLineNumber = FileAndLineInfo.LineNumber;
	SourceFilePath = FString((const ANSICHAR*)(FileAndLineInfo.FileName));
	return true;
}

#endif

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

#if ENGINE_MAJOR_VERSION >= 5
	uint32 SourceColumnNumber = 0;
	return FPlatformStackWalk::GetFunctionDefinitionLocation(
		SymbolName,
		ModuleName,
		SourceFilePath,
		SourceLineNumber,
		SourceColumnNumber);
#else
	return GetFunctionDefinitionLocation(SymbolName, ModuleName, SourceFilePath, SourceLineNumber);
#endif
}

bool UVisualStudioToolsBlueprintBreakpointExtension::GetProcessById(const TComPtr<EnvDTE::Processes>& Processes, DWORD CurrentProcessId, TComPtr<EnvDTE::Process>& OutProcess)
{
	long Count = 0;
	if (FAILED(Processes->get_Count(&Count)))
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Could not get the process count"));
		return false;
	}

	TComPtr<EnvDTE::Process> Process;
	for (long i = 1; i <= Count; i++)
	{
		VARIANT Index;
		Index.vt = VT_I4;
		Index.lVal = i;
		if (SUCCEEDED(Processes->Item(Index, &Process)))
		{
			long PID = 0;
			if (SUCCEEDED(Process->get_ProcessID(&PID)) && CurrentProcessId == PID)
			{
				OutProcess = Process;
				return true;
			}

			Process.Reset();
		}
	}

	return true;
}

void UVisualStudioToolsBlueprintBreakpointExtension::AttachDebuggerIfNecessary(const TComPtr<EnvDTE::Debugger>& Debugger)
{
	TComPtr<EnvDTE::Processes> Processes;
	if (FAILED(Debugger->get_DebuggedProcesses(&Processes)))
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Failed to get debugging proccess"));
		return;
	}

	TComPtr<EnvDTE::Process> Process;
	DWORD CurrentProcessId = GetCurrentProcessId();
	if (!GetProcessById(Processes, CurrentProcessId, Process))
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Failed to check if UE is already in debug mode"));
		return;
	}

	// currently debugging this process
	if (Process.Get() != nullptr)
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Log, TEXT("Already debugging UE."));
		return;
	}

	Processes.Reset();
	if (FAILED(Debugger->get_LocalProcesses(&Processes)))
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Failed to attach to process"));
		return;
	}

	Process.Reset();
	if (!GetProcessById(Processes, CurrentProcessId, Process))
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Failed to get all process"));
		return;
	}

	if (Process.Get() == nullptr)
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Log, TEXT("No UE proccess running."));
		return;
	}

	if (FAILED(Process->Attach()))
	{
		UE_LOG(LogUVisualStudioToolsBlueprintBreakpointExtension, Error, TEXT("Failed to attach to process"));
	}
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
			AttachDebuggerIfNecessary(Debugger);
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
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
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
