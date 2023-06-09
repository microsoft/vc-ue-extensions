// Copyright 2022 (c) Microsoft. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "Commandlets/Commandlet.h"

#include "VisualStudioToolsCommandletBase.generated.h"

UCLASS(Abstract)
class UVisualStudioToolsCommandletBase
	: public UCommandlet
{
	GENERATED_BODY()

public:
	int32 Main(const FString& Params) override;

protected:
	UVisualStudioToolsCommandletBase();
	
	void PrintHelp() const;

	virtual int32 Run(
		TArray<FString>& Tokens,
		TArray<FString>& Switches,
		TMap<FString, FString>& ParamVals,
		FArchive& OutArchive) PURE_VIRTUAL(UVisualStudioToolsCommandletBase::Run, return 0;);
};