// Copyright 2022 (c) Microsoft. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "VisualStudioToolsCommandletBase.h"

#include "BlueprintReferencesCommandlet.generated.h"

UCLASS()
class UVsBlueprintReferencesCommandlet
	: public UVisualStudioToolsCommandletBase
{
	GENERATED_BODY()

public:
	UVsBlueprintReferencesCommandlet();

	int32 Run(
		TArray<FString>& Tokens,
		TArray<FString>& Switches,
		TMap<FString, FString>& ParamVals,
		FArchive& OutArchive) override;
};
