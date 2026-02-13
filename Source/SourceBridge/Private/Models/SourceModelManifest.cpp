#include "Models/SourceModelManifest.h"
#include "Engine/StaticMesh.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

TWeakObjectPtr<USourceModelManifest> USourceModelManifest::CachedManifest;
const TCHAR* USourceModelManifest::ManifestAssetPath = TEXT("/Game/SourceBridge/ModelManifest");

USourceModelManifest* USourceModelManifest::Get()
{
	if (CachedManifest.IsValid())
	{
		return CachedManifest.Get();
	}

	FString FullAssetPath = FString(ManifestAssetPath) + TEXT(".ModelManifest");
	USourceModelManifest* Manifest = LoadObject<USourceModelManifest>(nullptr, *FullAssetPath);

	if (Manifest)
	{
		UE_LOG(LogTemp, Log, TEXT("SourceModelManifest: Loaded existing manifest (%d entries)"), Manifest->Entries.Num());
		CachedManifest = Manifest;
		Manifest->RebuildIndex();
		return Manifest;
	}

	FString PackageName = FPackageName::ObjectPathToPackageName(FString(ManifestAssetPath));
	FString AssetName = TEXT("ModelManifest");

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("SourceModelManifest: Failed to create package: %s"), *PackageName);
		return nullptr;
	}

	Manifest = NewObject<USourceModelManifest>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!Manifest)
	{
		UE_LOG(LogTemp, Error, TEXT("SourceModelManifest: Failed to create manifest object"));
		return nullptr;
	}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Manifest);
	Manifest->SaveManifest();

	UE_LOG(LogTemp, Log, TEXT("SourceModelManifest: Created new manifest at %s"), ManifestAssetPath);
	CachedManifest = Manifest;
	return Manifest;
}

// ---- Index Management ----

void USourceModelManifest::EnsureIndex()
{
	if (!bIndexBuilt)
	{
		RebuildIndex();
	}
}

void USourceModelManifest::RebuildIndex()
{
	SourcePathIndex.Empty(Entries.Num());
	MeshAssetIndex.Empty(Entries.Num());

	for (int32 i = 0; i < Entries.Num(); i++)
	{
		FString LowerPath = Entries[i].SourcePath.ToLower();
		LowerPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		SourcePathIndex.Add(LowerPath, i);

		FString MeshPath = Entries[i].MeshAsset.GetAssetPathString();
		if (!MeshPath.IsEmpty())
		{
			MeshAssetIndex.Add(MeshPath, i);
		}
	}

	bIndexBuilt = true;
}

// ---- Lookup API ----

FSourceModelEntry* USourceModelManifest::FindBySourcePath(const FString& SourcePath)
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

const FSourceModelEntry* USourceModelManifest::FindBySourcePath(const FString& SourcePath) const
{
	return const_cast<USourceModelManifest*>(this)->FindBySourcePath(SourcePath);
}

FSourceModelEntry* USourceModelManifest::FindByUEMesh(const UStaticMesh* Mesh)
{
	if (!Mesh) return nullptr;
	EnsureIndex();

	FSoftObjectPath MeshPath(Mesh);
	FString PathStr = MeshPath.GetAssetPathString();
	if (const int32* Idx = MeshAssetIndex.Find(PathStr))
	{
		if (Entries.IsValidIndex(*Idx))
		{
			return &Entries[*Idx];
		}
	}
	return nullptr;
}

FString USourceModelManifest::GetSourcePath(const UStaticMesh* Mesh) const
{
	if (!Mesh) return FString();
	USourceModelManifest* MutableThis = const_cast<USourceModelManifest*>(this);
	FSourceModelEntry* Entry = MutableThis->FindByUEMesh(Mesh);
	if (Entry)
	{
		return Entry->SourcePath;
	}
	return FString();
}

// ---- Registration API ----

void USourceModelManifest::Register(const FSourceModelEntry& Entry)
{
	FString Key = Entry.SourcePath.ToLower();
	Key.ReplaceInline(TEXT("\\"), TEXT("/"));

	EnsureIndex();

	if (const int32* ExistingIdx = SourcePathIndex.Find(Key))
	{
		if (Entries.IsValidIndex(*ExistingIdx))
		{
			FSourceModelEntry& Existing = Entries[*ExistingIdx];

			FString OldMeshPath = Existing.MeshAsset.GetAssetPathString();
			if (!OldMeshPath.IsEmpty())
			{
				MeshAssetIndex.Remove(OldMeshPath);
			}

			Existing = Entry;

			FString NewMeshPath = Entry.MeshAsset.GetAssetPathString();
			if (!NewMeshPath.IsEmpty())
			{
				MeshAssetIndex.Add(NewMeshPath, *ExistingIdx);
			}

			UE_LOG(LogTemp, Verbose, TEXT("SourceModelManifest: Updated entry '%s'"), *Entry.SourcePath);
			MarkDirty();
			return;
		}
	}

	int32 NewIdx = Entries.Add(Entry);
	SourcePathIndex.Add(Key, NewIdx);

	FString NewMeshPath = Entry.MeshAsset.GetAssetPathString();
	if (!NewMeshPath.IsEmpty())
	{
		MeshAssetIndex.Add(NewMeshPath, NewIdx);
	}

	UE_LOG(LogTemp, Verbose, TEXT("SourceModelManifest: Registered '%s' (type=%d)"),
		*Entry.SourcePath, (int32)Entry.Type);
	MarkDirty();
}

void USourceModelManifest::Remove(const FString& SourcePath)
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

TArray<FSourceModelEntry*> USourceModelManifest::GetAllOfType(ESourceModelType Type)
{
	TArray<FSourceModelEntry*> Result;
	for (FSourceModelEntry& Entry : Entries)
	{
		if (Entry.Type == Type)
		{
			Result.Add(&Entry);
		}
	}
	return Result;
}

// ---- Persistence ----

void USourceModelManifest::MarkDirty()
{
	if (UPackage* Package = GetOutermost())
	{
		Package->MarkPackageDirty();
	}
}

void USourceModelManifest::SaveManifest()
{
	UPackage* Package = GetOutermost();
	if (!Package) return;

	FString PackageFileName;
	if (!FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		UE_LOG(LogTemp, Error, TEXT("SourceModelManifest: Failed to resolve save path for %s"), *Package->GetName());
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
		UE_LOG(LogTemp, Log, TEXT("SourceModelManifest: Saved manifest (%d entries) to %s"),
			Entries.Num(), *PackageFileName);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("SourceModelManifest: Failed to save manifest to %s"), *PackageFileName);
	}
}
