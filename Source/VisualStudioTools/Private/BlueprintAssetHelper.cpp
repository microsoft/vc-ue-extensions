// Copyright 2022 (c) Microsoft. All rights reserved.
// Licensed under the MIT License.

#include "BlueprintAssetHelpers.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/BlueprintCore.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Misc/ScopeExit.h"
#include "VisualStudioTools.h"

namespace VisualStudioTools
{
namespace AssetHelpers
{
/*
* These helpers handle the usage of some APIs that were deprecated in 5.1
* but the replacements are not available in older versions.
* Might be overridden by the `Build.cs` rules
*/
#if FILTER_ASSETS_BY_CLASS_PATH

void SetBlueprintClassFilter(FARFilter& InOutFilter)
{
	// UE5.1 deprecated the API to filter using class names
	InOutFilter.ClassPaths.Add(UBlueprintCore::StaticClass()->GetClassPathName());
}

static FString GetObjectPathString(const FAssetData& InAssetData)
{
	// UE5.1 deprecated 'FAssetData::ObjectPath' in favor of 'FAssetData::GetObjectPathString()'
	return InAssetData.GetObjectPathString();
}

#else // FILTER_ASSETS_BY_CLASS_PATH

void SetBlueprintClassFilter(FARFilter& InOutFilter)
{
	InOutFilter.ClassNames.Add(UBlueprintCore::StaticClass()->GetFName());
}

static FString GetObjectPathString(const FAssetData& InAssetData)
{
	return InAssetData.ObjectPath.ToString();
}

#endif // FILTER_ASSETS_BY_CLASS_PATH

void ForEachAsset(
	const TArray<FAssetData>& TargetAssets,
	TFunctionRef<void(UBlueprintGeneratedClass*, const FAssetData& AssetData)> Callback)
{
	// Show a simpler logging output.
	// LogTimes are still useful to tell how long it takes to process each asset.
	TGuardValue<bool> DisableLogVerbosity(GPrintLogVerbosity, false);
	TGuardValue<bool> DisableLogCategory(GPrintLogCategory, false);

	// We're about to load the assets which might trigger a ton of log messages
	// Temporarily suppress them during this stage.
	GEngine->Exec(nullptr, TEXT("log LogVisualStudioTools only"));
	ON_SCOPE_EXIT
	{
		GEngine->Exec(nullptr, TEXT("log reset"));
	};

	FStreamableManager AssetLoader;

	for (int32 Idx = 0; Idx < TargetAssets.Num(); Idx++)
	{
		const FAssetData AssetData = TargetAssets[Idx];
		FSoftClassPath GenClassPath = AssetData.GetTagValueRef<FString>(FBlueprintTags::GeneratedClassPath);
		UE_LOG(LogVisualStudioTools, Display, TEXT("Processing blueprints [%d/%d]: %s"), Idx + 1, TargetAssets.Num(), *GenClassPath.ToString());

		TSharedPtr<FStreamableHandle> Handle = AssetLoader.RequestSyncLoad(GenClassPath);
		ON_SCOPE_EXIT
		{
			// We're done, notify an unload.
			Handle->ReleaseHandle();
		};

		if (!Handle.IsValid())
		{
			UE_LOG(LogVisualStudioTools, Warning, TEXT("Failed to get a streamable handle for Blueprint. Skipping. GenClassPath: %s"), *GenClassPath.ToString());
			continue;
		}

		if (auto BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(Handle->GetLoadedAsset()))
		{
			Callback(BlueprintGeneratedClass, AssetData);
		}
		else
		{
			// Log some extra information to help the user understand why the asset failed to load.

			FString ObjectPathString = AssetHelpers::GetObjectPathString(AssetData);

			FString Msg = !GenClassPath.ToString().Contains(ObjectPathString)
				? FString::Printf(
					TEXT("ObjectPath is not compatible with GenClassPath, consider re-saving it to avoid future issues. { ObjectPath: %s, GenClassPath: %s }"),
					*ObjectPathString,
					*GenClassPath.ToString())
				: FString::Printf(TEXT("ClassPath: %s"), *GenClassPath.ToString());

			UE_LOG(LogVisualStudioTools, Warning, TEXT("Failed to load Blueprint. Skipping. %s"), *Msg);
		}
	}
}

}
}