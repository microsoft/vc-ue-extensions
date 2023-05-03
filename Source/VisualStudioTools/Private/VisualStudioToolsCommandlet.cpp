// Copyright 2022 (c) Microsoft. All rights reserved.

#include "VisualStudioToolsCommandlet.h"

#include "Algo/Transform.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/BlueprintCore.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "SourceCodeNavigation.h"
#include "UObject/UObjectIterator.h"
#include "VisualStudioTools.h"

namespace VisualStudioTools
{
static const FName CategoryFName = TEXT("Category");
static const FName ModuleNameFName = TEXT("ModuleName");

namespace AssetHelpers
{
/*
* These helpers handle the usage of some APIs that were deprecated in 5.1
* but the replacements are not available in older versions.
* Might be overridden by the `Build.cs` rules
*/
#if FILTER_ASSETS_BY_CLASS_PATH

static void SetBlueprintClassFilter(FARFilter& InOutFilter)
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

static void SetBlueprintClassFilter(FARFilter& InOutFilter)
{
	InOutFilter.ClassNames.Add(UBlueprintCore::StaticClass()->GetFName());
}

static FString GetObjectPathString(const FAssetData& InAssetData)
{
	return InAssetData.ObjectPath.ToString();
}

#endif // FILTER_ASSETS_BY_CLASS_PATH
}

static TArray<FProperty*> GetChangedPropertiesList(
	UStruct* InStruct, const uint8* DataPtr, const uint8* DefaultDataPtr)
{
	TArray<FProperty*> Result;

	const UClass* OwnerClass = Cast<UClass>(InStruct);

	// Walk only in the properties defined in the current class, the super classes are processed individually
	for (TFieldIterator<FProperty> It(OwnerClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		FProperty* Property = *It;
		for (int32 Idx = 0; Idx < Property->ArrayDim; Idx++)
		{
			const uint8* PropertyValue = Property->ContainerPtrToValuePtr<uint8>(DataPtr, Idx);
			const uint8* DefaultPropertyValue = Property->ContainerPtrToValuePtrForDefaults<uint8>(InStruct, DefaultDataPtr, Idx);

			if (!Property->Identical(PropertyValue, DefaultPropertyValue))
			{
				Result.Add(Property);
				break;
			}
		}
	}

	return Result;
}

static bool FindBlueprintNativeParents(
	const UClass* BlueprintGeneratedClass, TFunctionRef<void(UClass*)> Callback)
{
	bool bAnyNativeParent = false;
	for (UClass* Super = BlueprintGeneratedClass->GetSuperClass(); Super; Super = Super->GetSuperClass())
	{
		// Ignore the root `UObject` class and non-native parents.
		if (Super->HasAnyClassFlags(CLASS_Native) && Super->GetFName() != NAME_Object)
		{
			bAnyNativeParent = true;
			Callback(Super);
		}
	}

	return bAnyNativeParent;
}

struct FPropertyEntry
{
	FProperty* Property;
	TArray<int32> Blueprints;
};

struct FFunctionEntry
{
	UFunction* Function;
	TArray<int32> Blueprints;
};

struct FClassEntry
{
	const UClass* Class;
	TArray<int32> Blueprints;
	TMap<FString, FPropertyEntry> Properties;
	TMap<FString, FFunctionEntry> Functions;
};

using ClassMap = TMap<FString, FClassEntry>;

struct FAssetIndex
{
	TSet<FString> AssetPathCache;
	ClassMap Classes;
	TArray<const UClass*> Blueprints;

	void ProcessBlueprint(const UBlueprintGeneratedClass* GeneratedClass)
	{
		if (GeneratedClass == nullptr)
		{
			return;
		}

		int32 BlueprintIndex = Blueprints.Num();

		bool bHasAnyParent = FindBlueprintNativeParents(GeneratedClass, [&](UClass* Parent) {
			FString ParentName = Parent->GetFName().ToString();
			if (!Classes.Contains(ParentName))
			{
				Classes.Add(ParentName).Class = Parent;
			}

			FClassEntry& Entry = Classes[ParentName];

			Entry.Blueprints.Add(BlueprintIndex);

			UObject* GeneratedClassDefault = GeneratedClass->ClassDefaultObject;
			UObject* SuperClassDefault = Parent->GetDefaultObject(false);
			TArray<FProperty*> ChangedProperties = GetChangedPropertiesList(Parent, (uint8*)GeneratedClassDefault, (uint8*)SuperClassDefault);

			for (FProperty* Property : ChangedProperties)
			{
				FString PropertyName = Property->GetFName().ToString();
				if (!Entry.Properties.Contains(PropertyName))
				{
					Entry.Properties.Add(PropertyName).Property = Property;
				}

				FPropertyEntry& PropEntry = Entry.Properties[PropertyName];
				PropEntry.Blueprints.Add(BlueprintIndex);
			}
			});

		bool bHasAnyFunctions = false;
		for (UFunction* Fn : GeneratedClass->CalledFunctions)
		{
			if (!Fn->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native))
			{
				continue;
			}

			bHasAnyFunctions = true;

			UClass* Owner = Fn->GetOwnerClass();
			FString OwnerName = Owner->GetFName().ToString();
			if (!Classes.Contains(OwnerName))
			{
				Classes.Add(OwnerName).Class = Owner;
			}

			FClassEntry& Entry = Classes[OwnerName];

			FString FnName = Fn->GetFName().ToString();
			if (!Entry.Functions.Contains(FnName))
			{
				Entry.Functions.Add(FnName).Function = Fn;
			}

			FFunctionEntry& FuncEntry = Entry.Functions[FnName];
			FuncEntry.Blueprints.Add(BlueprintIndex);
		}

		if (bHasAnyParent || bHasAnyFunctions)
		{
			check(Blueprints.Add(GeneratedClass) == BlueprintIndex);
		}

		return;
	}
};

using JsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

static bool ShouldSerializePropertyValue(FProperty* Property)
{
	if (Property->ArrayDim > 1) // Skip properties that are not scalars
	{
		return false;
	}

	if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		return true;
	}

	if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
	{
		UEnum* EnumDef = NumericProperty->GetIntPropertyEnum();
		if (EnumDef != NULL)
		{
			return true;
		}

		if (NumericProperty->IsFloatingPoint())
		{
			return true;
		}

		if (NumericProperty->IsInteger())
		{
			return true;
		}
	}

	if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		return true;
	}

	if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		return true;
	}

	return false;
}

static void SerializeBlueprints(TSharedRef<JsonWriter>& Json, TArray<const UClass*> Items)
{
	Json->WriteArrayStart();
	for (const UClass* Blueprint : Items)
	{
		Json->WriteObjectStart();

		Json->WriteValue(TEXT("name"), Blueprint->GetName());
		Json->WriteValue(TEXT("path"), Blueprint->GetPathName());
		Json->WriteObjectEnd();
	}
	Json->WriteArrayEnd();
}

static void SerializeProperties(TSharedRef<JsonWriter>& Json, FClassEntry& Entry, TArray<const UClass*>& Blueprints)
{
	Json->WriteArrayStart();
	for (auto& Item : Entry.Properties)
	{
		auto& PropName = Item.Key;
		auto& PropEntry = Item.Value;
		FProperty* Property = PropEntry.Property;

		Json->WriteObjectStart();

		Json->WriteValue(TEXT("name"), PropName);

		Json->WriteIdentifierPrefix(TEXT("metadata"));
		{
			Json->WriteObjectStart();
			if (Property->HasMetaData(CategoryFName))
			{
				Json->WriteValue(TEXT("categories"), Property->GetMetaData(CategoryFName));
			}
			Json->WriteObjectEnd();
		}

		Json->WriteIdentifierPrefix(TEXT("values"));
		{
			Json->WriteArrayStart();
			for (auto& BlueprintEntry : PropEntry.Blueprints)
			{
				Json->WriteObjectStart();

				Json->WriteValue(TEXT("blueprint"), BlueprintEntry);

				UObject* GeneratedClassDefaultObject = Blueprints[BlueprintEntry]->ClassDefaultObject;
				const uint8* PropData = PropEntry.Property->ContainerPtrToValuePtr<uint8>(GeneratedClassDefaultObject);

				if (ShouldSerializePropertyValue(PropEntry.Property))
				{
					TSharedPtr<FJsonValue> JsonValue = FJsonObjectConverter::UPropertyToJsonValue(Property, PropData);
					FJsonSerializer::Serialize(JsonValue.ToSharedRef(), TEXT("value"), Json);
				}

				Json->WriteObjectEnd();
			}
			Json->WriteArrayEnd();
		}

		Json->WriteObjectEnd();
	}
	Json->WriteArrayEnd();
}

static void SerializeFunctions(TSharedRef<JsonWriter>& Json, FClassEntry& Entry)
{
	Json->WriteArrayStart();
	for (auto& Item : Entry.Functions)
	{
		auto& Name = Item.Key;
		auto& FnEntry = Item.Value;
		Json->WriteObjectStart();
		Json->WriteValue(TEXT("name"), Name);
		Json->WriteValue(TEXT("blueprints"), FnEntry.Blueprints);
		Json->WriteObjectEnd();
	}
	Json->WriteArrayEnd();
}

static void SerializeClasses(TSharedRef<JsonWriter>& Json, ClassMap& Items, TArray<const UClass*> Blueprints)
{
	Json->WriteArrayStart();
	for (auto& Item : Items)
	{
		auto& ClassName = Item.Key;
		auto& Entry = Item.Value;
		Json->WriteObjectStart();
		Json->WriteValue(TEXT("name"), FString::Printf(TEXT("%s%s"), Entry.Class->GetPrefixCPP(), *Entry.Class->GetName()));

		Json->WriteValue(TEXT("blueprints"), Entry.Blueprints);

		Json->WriteIdentifierPrefix(TEXT("properties"));
		SerializeProperties(Json, Entry, Blueprints);

		Json->WriteIdentifierPrefix(TEXT("functions"));
		SerializeFunctions(Json, Entry);

		Json->WriteObjectEnd();
	}
	Json->WriteArrayEnd();
}

static void SerializeToIndex(FAssetIndex Index, FArchive& IndexFile)
{
	TSharedRef<JsonWriter> Json = JsonWriter::Create(&IndexFile);

	Json->WriteObjectStart();

	Json->WriteIdentifierPrefix(TEXT("blueprints"));
	SerializeBlueprints(Json, Index.Blueprints);

	Json->WriteIdentifierPrefix(TEXT("classes"));
	SerializeClasses(Json, Index.Classes, Index.Blueprints);

	Json->WriteObjectEnd();
	Json->Close();
}

static TArray<FString> GetModulesByPath(const FString& InDir)
{
	TArray<FString> OutResult;
	Algo::TransformIf(
		FSourceCodeNavigation::GetSourceFileDatabase().GetModuleNames(),
		OutResult,
		[&](const FString& Module) {
			return FPaths::IsUnderDirectory(Module, InDir);
		},
		[](const FString& Module) {
			return FPaths::GetBaseFilename(FPaths::GetPath(*Module));
		});

	return OutResult;
}

static void GetNativeClassesByPath(const FString& InDir, TArray<TWeakObjectPtr<UClass>>& OutClasses)
{
	TArray<FString> Modules = GetModulesByPath(InDir);

	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* TestClass = *ClassIt;
		if (!TestClass->HasAnyClassFlags(CLASS_Native))
		{
			continue;
		}

		FAssetData ClassAssetData(TestClass);
		FString ModuleName = ClassAssetData.GetTagValueRef<FString>(ModuleNameFName);

		if (!ModuleName.IsEmpty() && Modules.Contains(ModuleName))
		{
			OutClasses.Add(TestClass);
		}
	}
}

static void ProcessAssets(
	FAssetIndex& Index,
	const TArray<FAssetData>& TargetAssets)
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
		FSoftClassPath GenClassPath = TargetAssets[Idx].GetTagValueRef<FString>(FBlueprintTags::GeneratedClassPath);

		UE_LOG(LogVisualStudioTools, Display, TEXT("Processing blueprints [%d/%d]: %s"), Idx + 1, TargetAssets.Num(), *GenClassPath.ToString());

		TSharedPtr<FStreamableHandle> Handle = AssetLoader.RequestSyncLoad(GenClassPath);
		ON_SCOPE_EXIT
		{
			// We're done, notify an unload.
			Handle->ReleaseHandle();
		};

		if (!Handle.IsValid())
		{
			continue;
		}

		if (auto BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(Handle->GetLoadedAsset()))
		{
			Index.ProcessBlueprint(BlueprintGeneratedClass);
		}
		else
		{
			FString ObjectPathString = AssetHelpers::GetObjectPathString(TargetAssets[Idx]);
			if (!GenClassPath.ToString().Contains(ObjectPathString))
			{
				UE_LOG(LogVisualStudioTools, Warning,
					TEXT("blueprint's ObjectPath is not compatible with GenClassPath, consider re-save it to avoid future issues: \n ObjectPath is: %s \n while GenClassPath is: %s"),
					*ObjectPathString,
					*GenClassPath.ToString());
			}
		}
	}
}

static void RunAssetScan(
	FAssetIndex& Index,
	const TArray<TWeakObjectPtr<UClass>>& FilterBaseClasses)
{
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	AssetHelpers::SetBlueprintClassFilter(Filter);

	// Add all base classes to the tag filter for native parent
	Algo::Transform(FilterBaseClasses, Filter.TagsAndValues, [](const TWeakObjectPtr<UClass>& Class) {
		return MakeTuple(
			FBlueprintTags::NativeParentClassPath,
			FObjectPropertyBase::GetExportPath(Class.Get(), nullptr /*Parent*/, nullptr /*ExportRootScope*/, 0 /*PortFlags*/));
		});

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> TargetAssets;
	AssetRegistry.GetAssets(Filter, TargetAssets);

	ProcessAssets(Index, TargetAssets);
}
} // namespace VS

static constexpr auto HelpSwitch = TEXT("help");
static constexpr auto FilterSwitch = TEXT("filter");
static constexpr auto FullSwitch = TEXT("full");
static constexpr auto OutputSwitch = TEXT("output");

UVisualStudioToolsCommandlet::UVisualStudioToolsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = false;
	ShowErrorCount = false;

	HelpDescription = TEXT("Commandlet for generating data used by Blueprint support in Visual Studio.");

	HelpParamNames.Add(OutputSwitch);
	HelpParamDescriptions.Add(TEXT("[Required] The file path to write the command output."));

	HelpParamNames.Add(FilterSwitch);
	HelpParamDescriptions.Add(TEXT("[Optional] Scan only blueprints derived from native classes under the provided path. Defaults to `FPaths::ProjectDir`. Incompatible with `-full`."));

	HelpParamNames.Add(FullSwitch);
	HelpParamDescriptions.Add(TEXT("[Optional] Scan blueprints derived from native classes from ALL modules, include the Engine. This can be _very slow_ for large projects. Incompatible with `-filter`."));

	HelpParamNames.Add(HelpSwitch);
	HelpParamDescriptions.Add(TEXT("[Optional] Print this help message and quit the commandlet immediately."));

	HelpUsage = TEXT("<Editor-Cmd.exe> <path_to_uproject> -run=VisualStudioTools -output=<path_to_output_file> [-filter=<subdir_native_classes>|-full] [-unattended -noshadercompile -nosound -nullrhi -nocpuprofilertrace -nocrashreports -nosplash]");
}

void UVisualStudioToolsCommandlet::PrintHelp() const
{
	UE_LOG(LogVisualStudioTools, Display, TEXT("%s"), *HelpDescription);
	UE_LOG(LogVisualStudioTools, Display, TEXT("Usage: %s"), *HelpUsage);
	UE_LOG(LogVisualStudioTools, Display, TEXT("Parameters:"));
	for (int32 Idx = 0; Idx < HelpParamNames.Num(); ++Idx)
	{
		UE_LOG(LogVisualStudioTools, Display, TEXT("\t-%s: %s"), *HelpParamNames[Idx], *HelpParamDescriptions[Idx]);
	}
}

int32 UVisualStudioToolsCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;

	ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	if (Switches.Contains(HelpSwitch))
	{
		PrintHelp();
		return 0;
	}

	UE_LOG(LogVisualStudioTools, Display, TEXT("Init VS Tools cmdlet."));

	if (!FPaths::IsProjectFilePathSet())
	{
		UE_LOG(LogVisualStudioTools, Error, TEXT("You must invoke this commandlet with a project file."));
		return -1;
	}

	FString FullPath = ParamVals.FindRef(OutputSwitch);

	if (FullPath.IsEmpty() && !FParse::Value(*Params, TEXT("output "), FullPath))
	{
		// VS:1678426 - Initial version was using `-output "path-to-file"` (POSIX style).
		// However, that does not support paths with spaces, even when surrounded with
		// quotes because `FParse::Value` only handles that case when there's no space
		// between the parameter name and quoted value.
		// For back-compatibility reasons, parse that style by including the space in
		// the parameter token like it's usually done for the `=` sign.
		UE_LOG(LogVisualStudioTools, Error, TEXT("Missing file output parameter."));
		PrintHelp();
		return -1;
	}

	TUniquePtr<FArchive> OutArchive{ IFileManager::Get().CreateFileWriter(*FullPath) };
	if (!OutArchive)
	{
		UE_LOG(LogVisualStudioTools, Error, TEXT("Failed to create index with path: %s."), *FullPath);
		return -1;
	}

	using namespace VisualStudioTools;

	FString* Filter = ParamVals.Find(FilterSwitch);
	const bool bFullScan = Switches.Contains(FullSwitch);

	if (Filter != nullptr && bFullScan)
	{
		UE_LOG(LogVisualStudioTools, Error, TEXT("Incompatible scan options."));
		PrintHelp();
		return -1;
	}

	TArray<TWeakObjectPtr<UClass>> FilterBaseClasses;
	if (!bFullScan)
	{
		if (Filter)
		{
			FPaths::NormalizeDirectoryName(*Filter);
			GetNativeClassesByPath(*Filter, FilterBaseClasses);
		}
		else
		{
			GetNativeClassesByPath(FPaths::ProjectDir(), FilterBaseClasses);
		}
	}

	FAssetIndex Index;
	RunAssetScan(Index, FilterBaseClasses);
	SerializeToIndex(Index, *OutArchive);

	UE_LOG(LogVisualStudioTools, Display, TEXT("Found %d blueprints."), Index.Blueprints.Num());

	return 0;
}
