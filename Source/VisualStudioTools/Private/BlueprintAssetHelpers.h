// Copyright 2022 (c) Microsoft. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"

class UBlueprintGeneratedClass;

namespace VisualStudioTools 
{
namespace AssetHelpers
{
void SetBlueprintClassFilter(FARFilter& InOutFilter);

/**
* Loads each blueprint asset and invokes the callback with the resulting blueprint generated class.
* Each iteration will load the asset using a FStreamableHandle and verify that is a valid blueprint
* before invoking the callback.
*/
void ForEachAsset(
	const TArray<FAssetData>& TargetAssets,
	TFunctionRef<void(UBlueprintGeneratedClass*, const FAssetData& AssetData)> Callback);

} // namespace AssetHelpers
} // namespace VisualStudioTools
