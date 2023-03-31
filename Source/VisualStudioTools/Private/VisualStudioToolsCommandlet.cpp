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
#include "Launch/Resources/Version.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "SourceCodeNavigation.h"
#include "UObject/UObjectIterator.h"
#include "VisualStudioTools.h"

namespace VSTools
{
static const FName NAME_Category = TEXT("Category");
static const FName ModuleNameFName = TEXT("ModuleName");

// Wrapper for conditional compilation of deprecated API usage
// Might be overridden by the `Build.cs` rules.
static void SetBlueprintClassFilter(FARFilter& InOutFilter)
{
	// UE5.1 deprecated the API to filter using class names
#if !defined(FILTER_ASSETS_BY_CLASS_PATH)
#define FILTER_ASSETS_BY_CLASS_PATH ((ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1) || ENGINE_MAJOR_VERSION > 5)
#endif

#if FILTER_ASSETS_BY_CLASS_PATH
	InOutFilter.ClassPaths.Add(UBlueprintCore::StaticClass()->GetClassPathName());
#else
	InOutFilter.ClassNames.Add(UBlueprintCore::StaticClass()->GetFName());
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
	bool anyNativeParent = false;
	for (UClass* Super = BlueprintGeneratedClass->GetSuperClass(); Super; Super = Super->GetSuperClass())
	{
		// Ignore the root `UObject` class and non-native parents.
		if (Super->HasAnyClassFlags(CLASS_Native) && Super->GetFName() != NAME_Object)
		{
			anyNativeParent = true;
			Callback(Super);
		}
	}

	return anyNativeParent;
}

struct PropertyEntry
{
	FProperty* Property;
	TArray<int32> Blueprints;
};

struct FunctionEntry
{
	UFunction* Function;
	TArray<int32> Blueprints;
};

struct ClassEntry
{
	const UClass* Class;
	TArray<int32> Blueprints;
	TMap<FString, PropertyEntry> Properties;
	TMap<FString, FunctionEntry> Functions;
};

using ClassMap = TMap<FString, ClassEntry>;

struct AssetIndex
{
	TSet<FString> AssetPathCache;
	ClassMap Classes;
	TArray<const UClass*> Blueprints;

	void ProcessBlueprint(const UBlueprintGeneratedClass* BPGC)
	{
		int32 BlueprintIndex = Blueprints.Num();

		bool hasAnyParent = FindBlueprintNativeParents(BPGC, [&](UClass* Parent) {
			FString ParentName = Parent->GetFName().ToString();
		if (!Classes.Contains(ParentName))
		{
			Classes.Add(ParentName).Class = Parent;
		}

		ClassEntry& entry = Classes[ParentName];

		entry.Blueprints.Add(BlueprintIndex);

		UObject* GeneratedClassCDO = BPGC->ClassDefaultObject;
		UObject* SuperClassCDO = Parent->GetDefaultObject(false);
		TArray<FProperty*> ChangedProperties = GetChangedPropertiesList(Parent, (uint8*)GeneratedClassCDO, (uint8*)SuperClassCDO);

		for (FProperty* Property : ChangedProperties)
		{
			FString PropertyName = Property->GetFName().ToString();
			if (!entry.Properties.Contains(PropertyName))
			{
				entry.Properties.Add(PropertyName).Property = Property;
			}

			PropertyEntry& propEntry = entry.Properties[PropertyName];
			propEntry.Blueprints.Add(BlueprintIndex);
		}
			});

		bool hasAnyFunctions = false;
		for (UFunction* Fn : BPGC->CalledFunctions)
		{
			if (!Fn->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native))
			{
				continue;
			}

			hasAnyFunctions = true;

			UClass* Owner = Fn->GetOwnerClass();
			FString OwnerName = Owner->GetFName().ToString();
			if (!Classes.Contains(OwnerName))
			{
				Classes.Add(OwnerName).Class = Owner;
			}

			ClassEntry& entry = Classes[OwnerName];

			FString FnName = Fn->GetFName().ToString();
			if (!entry.Functions.Contains(FnName))
			{
				entry.Functions.Add(FnName).Function = Fn;
			}

			FunctionEntry& funcEntry = entry.Functions[FnName];
			funcEntry.Blueprints.Add(BlueprintIndex);
		}

		if (hasAnyParent || hasAnyFunctions)
		{
			check(Blueprints.Add(BPGC) == BlueprintIndex);
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

static void SerializeProperties(TSharedRef<JsonWriter>& Json, ClassEntry& Entry, TArray<const UClass*>& Blueprints)
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
			if (Property->HasMetaData(NAME_Category))
			{
				Json->WriteValue(TEXT("categories"), Property->GetMetaData(NAME_Category));
			}
			Json->WriteObjectEnd();
		}

		Json->WriteIdentifierPrefix(TEXT("values"));
		{
			Json->WriteArrayStart();
			for (auto& BPEntry : PropEntry.Blueprints)
			{
				Json->WriteObjectStart();

				Json->WriteValue(TEXT("blueprint"), BPEntry);

				UObject* GeneratedClassCDO = Blueprints[BPEntry]->ClassDefaultObject;
				const uint8* PropData = PropEntry.Property->ContainerPtrToValuePtr<uint8>(GeneratedClassCDO);

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

static void SerializeFunctions(TSharedRef<JsonWriter>& Json, ClassEntry& Entry)
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

static void SerializeToIndex(AssetIndex Index, FArchive& IndexFile)
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
	AssetIndex& Index,
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

	for (int32 i = 0; i < TargetAssets.Num(); i++)
	{
		FSoftClassPath GenClassPath = TargetAssets[i].GetTagValueRef<FString>(FBlueprintTags::GeneratedClassPath);

		UE_LOG(LogVisualStudioTools, Display, TEXT("Processing blueprints [%d/%d]: %s"), i+1, TargetAssets.Num(), *GenClassPath.ToString());

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

		Index.ProcessBlueprint(Cast<UBlueprintGeneratedClass>(Handle->GetLoadedAsset()));
	}
}

static void RunAssetScan(
	AssetIndex& Index,
	const TArray<TWeakObjectPtr<UClass>>& FilterBaseClasses)
{
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	SetBlueprintClassFilter(Filter);

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
	for (int32 i = 0; i < HelpParamNames.Num(); ++i)
	{
		UE_LOG(LogVisualStudioTools, Display, TEXT("\t-%s: %s"), *HelpParamNames[i], *HelpParamDescriptions[i]);
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

	using namespace VSTools;

	FString* Filter = ParamVals.Find(FilterSwitch);
	bool bFullScan = Switches.Contains(FilterSwitch);

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
	
	AssetIndex Index;
	RunAssetScan(Index, FilterBaseClasses);
	SerializeToIndex(Index, *OutArchive);

	UE_LOG(LogVisualStudioTools, Display, TEXT("Found %d blueprints."), Index.Blueprints.Num());

	return 0;
}

