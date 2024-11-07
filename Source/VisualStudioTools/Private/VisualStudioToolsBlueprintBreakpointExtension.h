#pragma once

#include <CoreMinimal.h>
#include <EditorSubsystem.h>
#include <EdGraph/EdGraph.h>
#include <EdGraph/EdGraphNode.h>
#include <EdGraph/EdGraphPin.h>
#include <GraphEditorModule.h>
#include <VisualStudioDTE.h>
#include <Microsoft/COMPointer.h>
#include <Runtime/Launch/Resources/Version.h>
#include "VisualStudioToolsBlueprintBreakpointExtension.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUVisualStudioToolsBlueprintBreakpointExtension, Log, All);

UCLASS()
class UVisualStudioToolsBlueprintBreakpointExtension : public UEditorSubsystem
{
	GENERATED_BODY()
	
public:
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnNodeMenuExtensionHookRequestDelegate, const UEdGraphNode*, const UEdGraph*, TSet<FName>&);

	void Initialize(FSubsystemCollectionBase& Collection) override;
	void Deinitialize() override;

	FOnNodeMenuExtensionHookRequestDelegate& OnNodeMenuExtensionHookRequest() { return OnNodeMenuExtensionHookRequestDelegate; }

private:
	FOnNodeMenuExtensionHookRequestDelegate OnNodeMenuExtensionHookRequestDelegate;

	TSharedRef<FExtender> HandleOnExtendGraphEditorContextMenu(
		const TSharedRef<FUICommandList> CommandList,
		const UEdGraph* Graph,
		const UEdGraphNode* Node,
		const UEdGraphPin* Pin,
		bool bIsConst);

	void AddVisualStudioBlueprintBreakpointMenuOption(FMenuBuilder& MenuBuilder, const UEdGraphNode* node);

	void AddVisualStudioBreakpoint(const UEdGraphNode* Node);

	bool GetFunctionDefinitionLocation(const UEdGraphNode* Node, FString& SourceFilePath, FString& SymbolName, uint32& SourceLineNumber);

	bool SetVisualStudioBreakpoint(const UEdGraphNode* Node, const FString& SourceFilePath, const FString& SymbolName, uint32 SourceLineNumber);

	bool CanAddVisualStudioBreakpoint(const UEdGraphNode* Node, UClass** OutOwnerClass, UFunction** OutFunction);

	void ShowOperationResultNotification(bool bBreakpointAdded, const FString& SymbolName);

	FString GetProjectPath(const FString& ProjectDir);

	bool GetRunningVisualStudioDTE(TComPtr<EnvDTE::_DTE>& OutDTE);

	void AttachDebuggerIfNecessary(const TComPtr<EnvDTE::Debugger>& Debugger);

	bool GetProcessById(const TComPtr<EnvDTE::Processes>& Processes, DWORD CurrentProcessId, TComPtr<EnvDTE::Process>& OutProcess);

#if ENGINE_MAJOR_VERSION < 5
	bool PreloadModule(HANDLE ProcessHandle, HMODULE ModuleHandle, const FString& RemoteStorage);

	bool GetFunctionDefinitionLocation(const FString& FunctionSymbolName, const FString& FunctionModuleName, FString& SourceFilePath, uint32& SourceLineNumber);
#endif
};
