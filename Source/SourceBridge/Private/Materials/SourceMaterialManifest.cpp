#include "Materials/SourceMaterialManifest.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

TWeakObjectPtr<USourceMaterialManifest> USourceMaterialManifest::CachedManifest;
const TCHAR* USourceMaterialManifest::ManifestAssetPath = TEXT("/Game/SourceBridge/MaterialManifest");

USourceMaterialManifest* USourceMaterialManifest::Get()
{
	// Return cached if still valid
	if (CachedManifest.IsValid())
	{
		return CachedManifest.Get();
	}

	// Try to load existing manifest
	FString FullAssetPath = FString(ManifestAssetPath) + TEXT(".MaterialManifest");
	USourceMaterialManifest* Manifest = LoadObject<USourceMaterialManifest>(nullptr, *FullAssetPath);

	if (Manifest)
	{
		UE_LOG(LogTemp, Log, TEXT("SourceMaterialManifest: Loaded existing manifest (%d entries)"), Manifest->Entries.Num());
		CachedManifest = Manifest;
		Manifest->RebuildIndex();
		return Manifest;
	}

	// Create new manifest
	FString PackagePath = FString(ManifestAssetPath);
	FString PackageName = FPackageName::ObjectPathToPackageName(PackagePath);
	FString AssetName = TEXT("MaterialManifest");

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("SourceMaterialManifest: Failed to create package: %s"), *PackageName);
		return nullptr;
	}

	Manifest = NewObject<USourceMaterialManifest>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!Manifest)
	{
		UE_LOG(LogTemp, Error, TEXT("SourceMaterialManifest: Failed to create manifest object"));
		return nullptr;
	}

	// Mark package dirty so it can be saved
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Manifest);

	Manifest->SaveManifest();

	UE_LOG(LogTemp, Log, TEXT("SourceMaterialManifest: Created new manifest at %s"), ManifestAssetPath);
	CachedManifest = Manifest;
	return Manifest;
}

// ---- Index Management ----

void USourceMaterialManifest::EnsureIndex()
{
	if (!bIndexBuilt)
	{
		RebuildIndex();
	}
}

void USourceMaterialManifest::RebuildIndex()
{
	SourcePathIndex.Empty(Entries.Num());
	MaterialAssetIndex.Empty(Entries.Num());

	for (int32 i = 0; i < Entries.Num(); i++)
	{
		FString LowerPath = Entries[i].SourcePath.ToLower();
		LowerPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		SourcePathIndex.Add(LowerPath, i);

		FString MatPath = Entries[i].MaterialAsset.GetAssetPathString();
		if (!MatPath.IsEmpty())
		{
			MaterialAssetIndex.Add(MatPath, i);
		}
	}

	bIndexBuilt = true;
}

// ---- Lookup API ----

FSourceMaterialEntry* USourceMaterialManifest::FindBySourcePath(const FString& SourcePath)
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

const FSourceMaterialEntry* USourceMaterialManifest::FindBySourcePath(const FString& SourcePath) const
{
	return const_cast<USourceMaterialManifest*>(this)->FindBySourcePath(SourcePath);
}

FSourceMaterialEntry* USourceMaterialManifest::FindByUEMaterial(const UMaterialInterface* Material)
{
	if (!Material) return nullptr;

	FSoftObjectPath MatPath(Material);
	return FindByUEMaterial(MatPath);
}

FSourceMaterialEntry* USourceMaterialManifest::FindByUEMaterial(const FSoftObjectPath& MaterialAssetPath)
{
	EnsureIndex();

	FString PathStr = MaterialAssetPath.GetAssetPathString();
	if (const int32* Idx = MaterialAssetIndex.Find(PathStr))
	{
		if (Entries.IsValidIndex(*Idx))
		{
			return &Entries[*Idx];
		}
	}
	return nullptr;
}

FSourceMaterialEntry* USourceMaterialManifest::FindByUETexture(const UTexture2D* Texture)
{
	if (!Texture) return nullptr;
	EnsureIndex();

	FSoftObjectPath TexPath(Texture);
	FString TexPathStr = TexPath.GetAssetPathString();

	for (int32 i = 0; i < Entries.Num(); i++)
	{
		if (Entries[i].TextureAsset.GetAssetPathString() == TexPathStr)
		{
			return &Entries[i];
		}
	}
	return nullptr;
}

FString USourceMaterialManifest::GetSourcePath(const UMaterialInterface* Material) const
{
	if (!Material) return FString();

	// Use const_cast since EnsureIndex/FindByUEMaterial don't modify logical state
	USourceMaterialManifest* MutableThis = const_cast<USourceMaterialManifest*>(this);
	FSourceMaterialEntry* Entry = MutableThis->FindByUEMaterial(Material);
	if (Entry)
	{
		return Entry->SourcePath;
	}
	return FString();
}

// ---- Registration API ----

void USourceMaterialManifest::Register(const FSourceMaterialEntry& Entry)
{
	FString Key = Entry.SourcePath.ToLower();
	Key.ReplaceInline(TEXT("\\"), TEXT("/"));

	EnsureIndex();

	if (const int32* ExistingIdx = SourcePathIndex.Find(Key))
	{
		if (Entries.IsValidIndex(*ExistingIdx))
		{
			// Update existing entry
			FSourceMaterialEntry& Existing = Entries[*ExistingIdx];

			// Remove old material path from index
			FString OldMatPath = Existing.MaterialAsset.GetAssetPathString();
			if (!OldMatPath.IsEmpty())
			{
				MaterialAssetIndex.Remove(OldMatPath);
			}

			Existing = Entry;

			// Add new material path to index
			FString NewMatPath = Entry.MaterialAsset.GetAssetPathString();
			if (!NewMatPath.IsEmpty())
			{
				MaterialAssetIndex.Add(NewMatPath, *ExistingIdx);
			}

			UE_LOG(LogTemp, Verbose, TEXT("SourceMaterialManifest: Updated entry '%s'"), *Entry.SourcePath);
			MarkDirty();
			return;
		}
	}

	// Add new entry
	int32 NewIdx = Entries.Add(Entry);
	SourcePathIndex.Add(Key, NewIdx);

	FString NewMatPath = Entry.MaterialAsset.GetAssetPathString();
	if (!NewMatPath.IsEmpty())
	{
		MaterialAssetIndex.Add(NewMatPath, NewIdx);
	}

	UE_LOG(LogTemp, Verbose, TEXT("SourceMaterialManifest: Registered new entry '%s' (type=%d)"),
		*Entry.SourcePath, (int32)Entry.Type);
	MarkDirty();
}

void USourceMaterialManifest::Remove(const FString& SourcePath)
{
	FString Key = SourcePath.ToLower();
	Key.ReplaceInline(TEXT("\\"), TEXT("/"));

	EnsureIndex();

	if (const int32* Idx = SourcePathIndex.Find(Key))
	{
		if (Entries.IsValidIndex(*Idx))
		{
			FString MatPath = Entries[*Idx].MaterialAsset.GetAssetPathString();
			if (!MatPath.IsEmpty())
			{
				MaterialAssetIndex.Remove(MatPath);
			}

			Entries.RemoveAt(*Idx);
			RebuildIndex(); // Indices shifted, need full rebuild
			MarkDirty();
		}
	}
}

// ---- Query API ----

TArray<FSourceMaterialEntry*> USourceMaterialManifest::GetAllOfType(ESourceMaterialType Type)
{
	TArray<FSourceMaterialEntry*> Result;
	for (FSourceMaterialEntry& Entry : Entries)
	{
		if (Entry.Type == Type)
		{
			Result.Add(&Entry);
		}
	}
	return Result;
}

bool USourceMaterialManifest::IsStock(const FString& SourcePath) const
{
	const FSourceMaterialEntry* Entry = FindBySourcePath(SourcePath);
	if (Entry)
	{
		return Entry->bIsInVPK;
	}
	// If not in manifest, we don't know — caller should check VPK
	return false;
}

// ---- Persistence ----

void USourceMaterialManifest::MarkDirty()
{
	if (UPackage* Package = GetOutermost())
	{
		Package->MarkPackageDirty();
	}
}

void USourceMaterialManifest::SaveManifest()
{
	UPackage* Package = GetOutermost();
	if (!Package) return;

	FString PackageFileName;
	if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		UE_LOG(LogTemp, Error, TEXT("SourceMaterialManifest: Failed to resolve save path for %s"), *Package->GetName());
		return;
	}

	// Ensure the directory exists
	FString Dir = FPaths::GetPath(PackageFileName);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*Dir);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	bool bSaved = UPackage::SavePackage(Package, this, *PackageFileName, SaveArgs);

	if (bSaved)
	{
		UE_LOG(LogTemp, Log, TEXT("SourceMaterialManifest: Saved manifest (%d entries) to %s"),
			Entries.Num(), *PackageFileName);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("SourceMaterialManifest: Failed to save manifest to %s"), *PackageFileName);
	}
}
