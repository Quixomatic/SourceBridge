#include "Import/SourceResourceManifest.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

TWeakObjectPtr<USourceResourceManifest> USourceResourceManifest::CachedManifest;
const TCHAR* USourceResourceManifest::ManifestAssetPath = TEXT("/Game/SourceBridge/ResourceManifest");

USourceResourceManifest* USourceResourceManifest::Get()
{
	if (CachedManifest.IsValid())
	{
		return CachedManifest.Get();
	}

	FString FullAssetPath = FString(ManifestAssetPath) + TEXT(".ResourceManifest");
	USourceResourceManifest* Manifest = LoadObject<USourceResourceManifest>(nullptr, *FullAssetPath);

	if (Manifest)
	{
		UE_LOG(LogTemp, Log, TEXT("SourceResourceManifest: Loaded existing manifest (%d entries)"), Manifest->Entries.Num());
		CachedManifest = Manifest;
		Manifest->RebuildIndex();
		return Manifest;
	}

	FString PackageName = FPackageName::ObjectPathToPackageName(FString(ManifestAssetPath));
	FString AssetName = TEXT("ResourceManifest");

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("SourceResourceManifest: Failed to create package: %s"), *PackageName);
		return nullptr;
	}

	Manifest = NewObject<USourceResourceManifest>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!Manifest)
	{
		UE_LOG(LogTemp, Error, TEXT("SourceResourceManifest: Failed to create manifest object"));
		return nullptr;
	}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Manifest);
	Manifest->SaveManifest();

	UE_LOG(LogTemp, Log, TEXT("SourceResourceManifest: Created new manifest at %s"), ManifestAssetPath);
	CachedManifest = Manifest;
	return Manifest;
}

// ---- Index Management ----

void USourceResourceManifest::EnsureIndex()
{
	if (!bIndexBuilt)
	{
		RebuildIndex();
	}
}

void USourceResourceManifest::RebuildIndex()
{
	SourcePathIndex.Empty(Entries.Num());

	for (int32 i = 0; i < Entries.Num(); i++)
	{
		FString LowerPath = Entries[i].SourcePath.ToLower();
		LowerPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		SourcePathIndex.Add(LowerPath, i);
	}

	bIndexBuilt = true;
}

// ---- Lookup API ----

FSourceResourceEntry* USourceResourceManifest::FindBySourcePath(const FString& SourcePath)
{
	EnsureIndex();

	FString Key = SourcePath.ToLower();
	Key.ReplaceInline(TEXT("\\"), TEXT("/"));

	if (const int32* Idx = SourcePathIndex.Find(Key))
	{
		if (Entries.IsValidIndex(*Idx))
		{
			return &Entries[*Idx];
		}
	}
	return nullptr;
}

const FSourceResourceEntry* USourceResourceManifest::FindBySourcePath(const FString& SourcePath) const
{
	return const_cast<USourceResourceManifest*>(this)->FindBySourcePath(SourcePath);
}

// ---- Registration API ----

void USourceResourceManifest::Register(const FSourceResourceEntry& Entry)
{
	FString Key = Entry.SourcePath.ToLower();
	Key.ReplaceInline(TEXT("\\"), TEXT("/"));

	EnsureIndex();

	if (const int32* ExistingIdx = SourcePathIndex.Find(Key))
	{
		if (Entries.IsValidIndex(*ExistingIdx))
		{
			Entries[*ExistingIdx] = Entry;
			UE_LOG(LogTemp, Verbose, TEXT("SourceResourceManifest: Updated entry '%s'"), *Entry.SourcePath);
			MarkDirty();
			return;
		}
	}

	int32 NewIdx = Entries.Add(Entry);
	SourcePathIndex.Add(Key, NewIdx);

	UE_LOG(LogTemp, Verbose, TEXT("SourceResourceManifest: Registered '%s' (type=%d)"),
		*Entry.SourcePath, (int32)Entry.ResourceType);
	MarkDirty();
}

void USourceResourceManifest::Remove(const FString& SourcePath)
{
	FString Key = SourcePath.ToLower();
	Key.ReplaceInline(TEXT("\\"), TEXT("/"));

	EnsureIndex();

	if (const int32* Idx = SourcePathIndex.Find(Key))
	{
		if (Entries.IsValidIndex(*Idx))
		{
			Entries.RemoveAt(*Idx);
			RebuildIndex();
			MarkDirty();
		}
	}
}

// ---- Query API ----

TArray<FSourceResourceEntry*> USourceResourceManifest::GetAllOfType(ESourceResourceType Type)
{
	TArray<FSourceResourceEntry*> Result;
	for (FSourceResourceEntry& Entry : Entries)
	{
		if (Entry.ResourceType == Type)
		{
			Result.Add(&Entry);
		}
	}
	return Result;
}

// ---- Persistence ----

void USourceResourceManifest::MarkDirty()
{
	if (UPackage* Package = GetOutermost())
	{
		Package->MarkPackageDirty();
	}
}

void USourceResourceManifest::SaveManifest()
{
	UPackage* Package = GetOutermost();
	if (!Package) return;

	FString PackageFileName;
	if (!FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		UE_LOG(LogTemp, Error, TEXT("SourceResourceManifest: Failed to resolve save path for %s"), *Package->GetName());
		return;
	}

	FString Dir = FPaths::GetPath(PackageFileName);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*Dir);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	bool bSaved = UPackage::SavePackage(Package, this, *PackageFileName, SaveArgs);

	if (bSaved)
	{
		UE_LOG(LogTemp, Log, TEXT("SourceResourceManifest: Saved manifest (%d entries) to %s"),
			Entries.Num(), *PackageFileName);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("SourceResourceManifest: Failed to save manifest to %s"), *PackageFileName);
	}
}
