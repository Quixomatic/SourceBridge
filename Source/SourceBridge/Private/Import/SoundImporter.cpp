#include "Import/SoundImporter.h"
#include "Import/SourceSoundManifest.h"
#include "Sound/SoundWave.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetImportTask.h"

USoundWave* FSoundImporter::ImportSound(const FString& SourcePath, const FString& DiskPath)
{
	// Check if already registered in manifest
	USourceSoundManifest* Manifest = USourceSoundManifest::Get();
	if (Manifest)
	{
		FSourceSoundEntry* Existing = Manifest->FindBySourcePath(SourcePath);
		if (Existing)
		{
			USoundWave* ExistingSound = Cast<USoundWave>(Existing->SoundAsset.TryLoad());
			if (ExistingSound)
			{
				UE_LOG(LogTemp, Verbose, TEXT("SoundImporter: '%s' already imported"), *SourcePath);
				return ExistingSound;
			}
		}
	}

	// Validate the WAV file exists and is reasonable size
	if (!FPaths::FileExists(DiskPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("SoundImporter: WAV file not found: %s"), *DiskPath);
		return nullptr;
	}

	int64 FileSize = IFileManager::Get().FileSize(*DiskPath);
	if (FileSize <= 44 || FileSize > 256 * 1024 * 1024)
	{
		UE_LOG(LogTemp, Warning, TEXT("SoundImporter: Skipping WAV with bad size (%lld bytes): %s"),
			FileSize, *DiskPath);
		return nullptr;
	}

	// Build asset destination path: /Game/SourceBridge/Sounds/<relative_path_without_extension>
	FString CleanPath = SourcePath.ToLower();
	CleanPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	CleanPath.RemoveFromStart(TEXT("sound/"));
	CleanPath = FPaths::GetPath(CleanPath) / FPaths::GetBaseFilename(CleanPath);
	CleanPath = CleanPath.Replace(TEXT(" "), TEXT("_"));

	FString DestinationPath = FString::Printf(TEXT("/Game/SourceBridge/Sounds/%s"), *FPaths::GetPath(CleanPath));
	FString AssetName = FPaths::GetCleanFilename(CleanPath);

	// Check if asset already exists
	FString FullAssetPath = DestinationPath / AssetName;
	FString FullObjectPath = FullAssetPath + TEXT(".") + AssetName;
	USoundWave* ExistingAsset = LoadObject<USoundWave>(nullptr, *FullObjectPath);
	if (ExistingAsset)
	{
		if (Manifest)
		{
			FSourceSoundEntry Entry;
			Entry.SourcePath = SourcePath;
			Entry.Type = ESourceSoundType::Imported;
			Entry.SoundAsset = FSoftObjectPath(ExistingAsset);
			Entry.DiskPath = DiskPath;
			Entry.Duration = ExistingAsset->Duration;
			Entry.SampleRate = ExistingAsset->GetSampleRateForCurrentPlatform();
			Entry.NumChannels = ExistingAsset->NumChannels;
			Entry.LastImported = FDateTime::Now();
			Manifest->Register(Entry);
		}
		return ExistingAsset;
	}

	// Use UE's built-in asset import pipeline (UAssetImportTask) instead of manual
	// USoundWave creation. Manual RawData.UpdatePayload causes heap corruption.
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UAssetImportTask* ImportTask = NewObject<UAssetImportTask>();
	ImportTask->Filename = DiskPath;
	ImportTask->DestinationPath = DestinationPath;
	ImportTask->DestinationName = AssetName;
	ImportTask->bAutomated = true;
	ImportTask->bReplaceExisting = true;
	ImportTask->bSave = true;

	TArray<UAssetImportTask*> ImportTasks;
	ImportTasks.Add(ImportTask);
	AssetTools.ImportAssetTasks(ImportTasks);

	USoundWave* SoundWave = nullptr;
	for (UObject* Asset : ImportTask->GetObjects())
	{
		SoundWave = Cast<USoundWave>(Asset);
		if (SoundWave)
		{
			break;
		}
	}

	if (!SoundWave)
	{
		UE_LOG(LogTemp, Warning, TEXT("SoundImporter: UE import pipeline failed for '%s'"), *DiskPath);
		return nullptr;
	}

	UE_LOG(LogTemp, Log, TEXT("SoundImporter: Imported '%s' -> %s (%.1fs, %dch)"),
		*SourcePath, *FullAssetPath, SoundWave->Duration, (int32)SoundWave->NumChannels);

	// Register in manifest
	if (Manifest)
	{
		FSourceSoundEntry Entry;
		Entry.SourcePath = SourcePath;
		Entry.Type = ESourceSoundType::Imported;
		Entry.SoundAsset = FSoftObjectPath(SoundWave);
		Entry.DiskPath = DiskPath;
		Entry.Duration = SoundWave->Duration;
		Entry.SampleRate = SoundWave->GetSampleRateForCurrentPlatform();
		Entry.NumChannels = SoundWave->NumChannels;
		Entry.LastImported = FDateTime::Now();
		Manifest->Register(Entry);
	}

	return SoundWave;
}

int32 FSoundImporter::ImportSoundsFromDirectory(const FString& ExtractedDir)
{
	FString SoundDir = ExtractedDir / TEXT("sound");
	if (!FPaths::DirectoryExists(SoundDir))
	{
		UE_LOG(LogTemp, Log, TEXT("SoundImporter: No sound/ directory found in %s"), *ExtractedDir);
		return 0;
	}

	// Find all WAV files recursively
	TArray<FString> WAVFiles;
	IFileManager::Get().FindFilesRecursive(WAVFiles, *SoundDir, TEXT("*.wav"), true, false);

	if (WAVFiles.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("SoundImporter: No WAV files found in %s"), *SoundDir);
		return 0;
	}

	UE_LOG(LogTemp, Log, TEXT("SoundImporter: Found %d WAV files in %s"), WAVFiles.Num(), *SoundDir);

	int32 ImportCount = 0;
	for (const FString& WAVPath : WAVFiles)
	{
		// Compute Source-relative path: sound/subfolder/file.wav
		FString RelPath = WAVPath;
		FPaths::MakePathRelativeTo(RelPath, *(ExtractedDir + TEXT("/")));
		RelPath.ReplaceInline(TEXT("\\"), TEXT("/"));

		USoundWave* Sound = ImportSound(RelPath, WAVPath);
		if (Sound)
		{
			ImportCount++;
		}
	}

	// Save manifest after batch import
	USourceSoundManifest* Manifest = USourceSoundManifest::Get();
	if (Manifest && ImportCount > 0)
	{
		Manifest->SaveManifest();
	}

	UE_LOG(LogTemp, Log, TEXT("SoundImporter: Imported %d/%d sounds from %s"),
		ImportCount, WAVFiles.Num(), *SoundDir);

	return ImportCount;
}

TArray<FString> FSoundImporter::ExtractSoundReferences(const TMap<FString, FString>& EntityKeyValues)
{
	TArray<FString> SoundPaths;

	// Known sound-type keyvalue names across Source entities
	static const TArray<FString> SoundKeys = {
		TEXT("message"),       // ambient_generic
		TEXT("StartSound"),    // various
		TEXT("StopSound"),     // various
		TEXT("MoveSound"),     // func_door, func_movelinear
		TEXT("OpenSound"),     // func_door
		TEXT("CloseSound"),    // func_door
		TEXT("LockedSound"),   // func_door
		TEXT("UnlockedSound"), // func_door
		TEXT("soundstart"),    // ambient_generic alternate
		TEXT("soundstop"),     // ambient_generic alternate
	};

	// Also check soundscape position sounds (scape0-scape7)
	for (const auto& KV : EntityKeyValues)
	{
		bool bIsSound = false;

		for (const FString& SoundKey : SoundKeys)
		{
			if (KV.Key.Equals(SoundKey, ESearchCase::IgnoreCase))
			{
				bIsSound = true;
				break;
			}
		}

		// Check for scape0-scape7 pattern
		if (!bIsSound && KV.Key.Len() >= 5)
		{
			FString Lower = KV.Key.ToLower();
			if (Lower.StartsWith(TEXT("scape")) && Lower.Len() == 6 && FChar::IsDigit(Lower[5]))
			{
				bIsSound = true;
			}
		}

		if (bIsSound && !KV.Value.IsEmpty())
		{
			// Source sound paths may or may not start with "sound/"
			FString SoundPath = KV.Value;
			SoundPath.ReplaceInline(TEXT("\\"), TEXT("/"));

			// Normalize: ensure it starts with "sound/" for consistent lookup
			if (!SoundPath.StartsWith(TEXT("sound/"), ESearchCase::IgnoreCase))
			{
				SoundPath = TEXT("sound/") + SoundPath;
			}

			SoundPaths.AddUnique(SoundPath);
		}
	}

	return SoundPaths;
}
