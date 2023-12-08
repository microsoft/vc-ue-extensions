// Copyright 2022 (c) Microsoft. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "Commandlets/Commandlet.h"
#include <string>

#include <Runtime/Core/Public/Misc/AutomationTest.h>
#include <Runtime/CoreUObject/Public/UObject/ObjectMacros.h>
#include <Runtime/Engine/Classes/Commandlets/Commandlet.h>

#include "VSServerCommandlet.generated.h"

UCLASS()
class UVSServerCommandlet
	: public UCommandlet
{
	GENERATED_BODY()

public:
	UVSServerCommandlet();

public:
	virtual int32 Main(const FString& Params) override;

private:
	void ExecuteSubCommandlet(FString ueServerNamedPipe);
};