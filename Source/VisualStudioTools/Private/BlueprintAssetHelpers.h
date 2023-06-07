// Copyright 2022 (c) Microsoft. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include "CoreMinimal.h"

namespace VisualStudioTools 
{
namespace AssetHelpers
{
void SetBlueprintClassFilter(FARFilter& InOutFilter);

void ForEachAsset(
	const TArray<FAssetData>& TargetAssets,
	TFunctionRef<void(UBlueprintGeneratedClass*, const FAssetData& AssetData)> Callback);

} // namespace AssetHelpers
} // namespace VisualStudioTools
