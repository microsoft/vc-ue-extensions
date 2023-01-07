// Copyright 2022 (c) Microsoft. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "Commandlets/Commandlet.h"

#include "VisualStudioToolsCommandlet.generated.h"

UCLASS()
class UVisualStudioToolsCommandlet
	: public UCommandlet
{
	GENERATED_BODY()

public:
	UVisualStudioToolsCommandlet();

public:
	virtual int32 Main(const FString& Params) override;

private:
	void PrintHelp() const;
};
