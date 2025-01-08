#pragma once

#include <CoreMinimal.h>
#include <Modules/ModuleInterface.h>
#include <Modules/ModuleManager.h>
#include <UObject/Script.h>
#include <UObject/Stack.h>
#include <UObject/Object.h>
#include <Logging/LogMacros.h>
#include <UObject/Class.h>
#include <HAL/Platform.h>

DECLARE_LOG_CATEGORY_EXTERN(LogVisualStudioBlueprintDebuggerHelper, Log, All);

class FVisualStudioBlueprintDebuggerHelper : public FDefaultModuleImpl
{
private:
	void OnScriptException(const UObject* Owner, const struct FFrame& Stack, const FBlueprintExceptionInfo& ExceptionInfo);
	void OnEnterScriptContext(const struct FBlueprintContextTracker& Context, const UObject* SourceObject, const UFunction* Function);
	void OnExitScriptContext(const struct FBlueprintContextTracker& Context);

	int32 CurrentScriptEntryTag;

public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
