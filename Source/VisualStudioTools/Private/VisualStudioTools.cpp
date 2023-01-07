// Copyright 2022 (c) Microsoft. All rights reserved.
// Licensed under the MIT License.

#include "VisualStudioTools.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogVisualStudioTools);

class FVisualStudioToolsModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FVisualStudioToolsModule, VisualStudioTools)
