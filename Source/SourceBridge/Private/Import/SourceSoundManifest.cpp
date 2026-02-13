#include "Import/SourceSoundManifest.h"
#include "Sound/SoundWave.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

TWeakObjectPtr<USourceSoundManifest> USourceSoundManifest::CachedManifest;
const TCHAR* USourceSoundManifest::ManifestAssetPath = TEXT("/Game/SourceBridge/SoundManifest");

USourceSoundManifest* USourceSoundManifest::Get()
{
	if (CachedManifest.IsValid())
	{
		return CachedManifest.Get();
	}

	FString FullAssetPath = FString(ManifestAssetPath) + TEXT(".SoundManifest");
	USourceSoundManifest* Manifest = LoadObject<USourceSoundManifest>(nullptr, *FullAssetPath);

	if (Manifest)
	{
		UE_LOG(LogTemp, Log, TEXT("SourceSoundManifest: Loaded existing manifest (%d entries)"), Manifest->Entries.Num());
		CachedManifest = Manifest;
		Manifest->RebuildIndex();
		return Manifest;
	}

	FString PackageName = FPackageName::ObjectPathToPackageName(FString(ManifestAssetPath));
	FString AssetName = TEXT("SoundManifest");

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("SourceSoundManifest: Failed to create package: %s"), *PackageName);
		return nullptr;
	}

	Manifest = NewObject<USourceSoundManifest>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!Manifest)
	{
		UE_LOG(LogTemp, Error, TEXT("SourceSoundManifest: Failed to create manifest object"));
		return nullptr;
	}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Manifest);
	Manifest->SaveManifest();

	UE_LOG(LogTemp, Log, TEXT("SourceSoundManifest: Created new manifest at %s"), ManifestAssetPath);
	CachedManifest = Manifest;
	return Manifest;
}

// ---- Index Management ----

void USourceSoundManifest::EnsureIndex()
{
	if (!bIndexBuilt)
	{
		RebuildIndex();
	}
}

void USourceSoundManifest::RebuildIndex()
{
	SourcePathIndex.Empty(Entries.Num());
	SoundAssetIndex.Empty(Entries.Num());

	for (int32 i = 0; i < Entries.Num(); i++)
	{
		FString LowerPath = Entries[i].SourcePath.ToLower();
		LowerPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		SourcePathIndex.Add(LowerPath, i);

		FString SoundPath = Entries[i].SoundAsset.GetAssetPathString();
		if (!SoundPath.IsEmpty())
		{
			SoundAssetIndex.Add(SoundPath, i);
		}
	}

	bIndexBuilt = true;
}

// ---- Lookup API ----

FSourceSoundEntry* USourceSoundManifest::FindBySourcePath(const FString& SourcePath)
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

const FSourceSoundEntry* USourceSoundManifest::FindBySourcePath(const FString& SourcePath) const
{
	return const_cast<USourceSoundManifest*>(this)->FindBySourcePath(SourcePath);
}

FSourceSoundEntry* USourceSoundManifest::FindByUESound(const USoundWave* Sound)
{
	if (!Sound) return nullptr;
	EnsureIndex();

	FSoftObjectPath SoundPath(Sound);
	FString PathStr = SoundPath.GetAssetPathString();
	if (const int32* Idx = SoundAssetIndex.Find(PathStr))
	{
		if (Entries.IsValidIndex(*Idx))
		{
			return &Entries[*Idx];
		}
	}
	return nullptr;
}

FString USourceSoundManifest::GetSourcePath(const USoundWave* Sound) const
{
	if (!Sound) return FString();
	USourceSoundManifest* MutableThis = const_cast<USourceSoundManifest*>(this);
	FSourceSoundEntry* Entry = MutableThis->FindByUESound(Sound);
	if (Entry)
	{
		return Entry->SourcePath;
	}
	return FString();
}

// ---- Registration API ----

void USourceSoundManifest::Register(const FSourceSoundEntry& Entry)
{
	FString Key = Entry.SourcePath.ToLower();
	Key.ReplaceInline(TEXT("\\"), TEXT("/"));

	EnsureIndex();

	if (const int32* ExistingIdx = SourcePathIndex.Find(Key))
	{
		if (Entries.IsValidIndex(*ExistingIdx))
		{
			FSourceSoundEntry& Existing = Entries[*ExistingIdx];

			FString OldSoundPath = Existing.SoundAsset.GetAssetPathString();
			if (!OldSoundPath.IsEmpty())
			{
				SoundAssetIndex.Remove(OldSoundPath);
			}

			Existing = Entry;

			FString NewSoundPath = Entry.SoundAsset.GetAssetPathString();
			if (!NewSoundPath.IsEmpty())
			{
				SoundAssetIndex.Add(NewSoundPath, *ExistingIdx);
			}

			UE_LOG(LogTemp, Verbose, TEXT("SourceSoundManifest: Updated entry '%s'"), *Entry.SourcePath);
			MarkDirty();
			return;
		}
	}

	int32 NewIdx = Entries.Add(Entry);
	SourcePathIndex.Add(Key, NewIdx);

	FString NewSoundPath = Entry.SoundAsset.GetAssetPathString();
	if (!NewSoundPath.IsEmpty())
	{
		SoundAssetIndex.Add(NewSoundPath, NewIdx);
	}

	UE_LOG(LogTemp, Verbose, TEXT("SourceSoundManifest: Registered '%s' (type=%d)"),
		*Entry.SourcePath, (int32)Entry.Type);
	MarkDirty();
}

void USourceSoundManifest::Remove(const FString& SourcePath)
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

TArray<FSourceSoundEntry*> USourceSoundManifest::GetAllOfType(ESourceSoundType Type)
{
	TArray<FSourceSoundEntry*> Result;
	for (FSourceSoundEntry& Entry : Entries)
	{
		if (Entry.Type == Type)
		{
			Result.Add(&Entry);
		}
	}
	return Result;
}

// ---- Persistence ----

void USourceSoundManifest::MarkDirty()
{
	if (UPackage* Package = GetOutermost())
	{
		Package->MarkPackageDirty();
	}
}

void USourceSoundManifest::SaveManifest()
{
	UPackage* Package = GetOutermost();
	if (!Package) return;

	FString PackageFileName;
	if (!FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		UE_LOG(LogTemp, Error, TEXT("SourceSoundManifest: Failed to resolve save path for %s"), *Package->GetName());
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
		UE_LOG(LogTemp, Log, TEXT("SourceSoundManifest: Saved manifest (%d entries) to %s"),
			Entries.Num(), *PackageFileName);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("SourceSoundManifest: Failed to save manifest to %s"), *PackageFileName);
	}
}
