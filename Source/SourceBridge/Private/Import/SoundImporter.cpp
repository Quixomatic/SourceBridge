#include "Import/SoundImporter.h"
#include "Import/SourceSoundManifest.h"
#include "Sound/SoundWave.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/PlatformFileManager.h"

// Simple WAV header parsing
struct FWAVHeader
{
	int32 SampleRate = 0;
	int16 NumChannels = 0;
	int16 BitsPerSample = 0;
	int32 DataSize = 0;
	int32 DataOffset = 0;
	bool bValid = false;
};

static FWAVHeader ParseWAVHeader(const TArray<uint8>& Data)
{
	FWAVHeader Header;

	if (Data.Num() < 44) return Header;

	// Check RIFF header
	if (Data[0] != 'R' || Data[1] != 'I' || Data[2] != 'F' || Data[3] != 'F')
		return Header;

	// Check WAVE format
	if (Data[8] != 'W' || Data[9] != 'A' || Data[10] != 'V' || Data[11] != 'E')
		return Header;

	// Parse chunks
	int32 Offset = 12;
	while (Offset + 8 <= Data.Num())
	{
		FString ChunkID = FString::Printf(TEXT("%c%c%c%c"),
			(char)Data[Offset], (char)Data[Offset + 1],
			(char)Data[Offset + 2], (char)Data[Offset + 3]);
		int32 ChunkSize = *(const int32*)&Data[Offset + 4];

		if (ChunkID == TEXT("fmt ") && ChunkSize >= 16)
		{
			Header.NumChannels = *(const int16*)&Data[Offset + 10];
			Header.SampleRate = *(const int32*)&Data[Offset + 12];
			Header.BitsPerSample = *(const int16*)&Data[Offset + 22];
		}
		else if (ChunkID == TEXT("data"))
		{
			Header.DataSize = ChunkSize;
			Header.DataOffset = Offset + 8;
			Header.bValid = true;
		}

		Offset += 8 + ChunkSize;
		// Align to word boundary
		if (ChunkSize % 2 != 0) Offset++;
	}

	return Header;
}

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

	// Load the WAV file
	TArray<uint8> WAVData;
	if (!FFileHelper::LoadFileToArray(WAVData, *DiskPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("SoundImporter: Failed to load WAV: %s"), *DiskPath);
		return nullptr;
	}

	// Parse WAV header for metadata
	FWAVHeader Header = ParseWAVHeader(WAVData);
	if (!Header.bValid)
	{
		UE_LOG(LogTemp, Warning, TEXT("SoundImporter: Invalid WAV format: %s"), *DiskPath);
		return nullptr;
	}

	// Build asset path: /Game/SourceBridge/Sounds/<relative_path_without_extension>
	FString CleanPath = SourcePath.ToLower();
	CleanPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	CleanPath.RemoveFromStart(TEXT("sound/"));
	CleanPath = FPaths::GetPath(CleanPath) / FPaths::GetBaseFilename(CleanPath);
	CleanPath = CleanPath.Replace(TEXT(" "), TEXT("_"));

	FString AssetPath = FString::Printf(TEXT("/Game/SourceBridge/Sounds/%s"), *CleanPath);
	FString AssetName = FPaths::GetCleanFilename(AssetPath);

	// Check if asset already exists on disk
	FString FullObjectPath = AssetPath + TEXT(".") + AssetName;
	USoundWave* ExistingAsset = LoadObject<USoundWave>(nullptr, *FullObjectPath);
	if (ExistingAsset)
	{
		// Register in manifest even if already exists (might be re-import)
		if (Manifest)
		{
			FSourceSoundEntry Entry;
			Entry.SourcePath = SourcePath;
			Entry.Type = ESourceSoundType::Imported;
			Entry.SoundAsset = FSoftObjectPath(ExistingAsset);
			Entry.DiskPath = DiskPath;
			Entry.Duration = ExistingAsset->Duration;
			Entry.SampleRate = Header.SampleRate;
			Entry.NumChannels = Header.NumChannels;
			Entry.LastImported = FDateTime::Now();
			Manifest->Register(Entry);
		}
		return ExistingAsset;
	}

	// Create package
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("SoundImporter: Failed to create package: %s"), *AssetPath);
		return nullptr;
	}
	Package->FullyLoad();

	// Create the USoundWave
	USoundWave* SoundWave = NewObject<USoundWave>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!SoundWave)
	{
		UE_LOG(LogTemp, Error, TEXT("SoundImporter: Failed to create USoundWave for '%s'"), *SourcePath);
		return nullptr;
	}

	// Set properties from WAV header
	SoundWave->SetSampleRate(Header.SampleRate);
	SoundWave->NumChannels = Header.NumChannels;

	if (Header.DataSize > 0 && Header.SampleRate > 0 && Header.NumChannels > 0 && Header.BitsPerSample > 0)
	{
		int32 BytesPerSample = Header.BitsPerSample / 8;
		int32 TotalSamples = Header.DataSize / (BytesPerSample * Header.NumChannels);
		SoundWave->Duration = (float)TotalSamples / (float)Header.SampleRate;
	}

	// Store the raw WAV data as bulk data so UE can decode it
	FSharedBuffer SharedBuf = FSharedBuffer::Clone(WAVData.GetData(), WAVData.Num());
	SoundWave->RawData.UpdatePayload(MoveTemp(SharedBuf), SoundWave);

	// Save the asset
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(SoundWave);

	FString PackageFileName;
	if (FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		FString Dir = FPaths::GetPath(PackageFileName);
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*Dir);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, SoundWave, *PackageFileName, SaveArgs);
	}

	UE_LOG(LogTemp, Log, TEXT("SoundImporter: Imported '%s' → %s (%.1fs, %dHz, %dch)"),
		*SourcePath, *AssetPath, SoundWave->Duration, Header.SampleRate, Header.NumChannels);

	// Register in manifest
	if (Manifest)
	{
		FSourceSoundEntry Entry;
		Entry.SourcePath = SourcePath;
		Entry.Type = ESourceSoundType::Imported;
		Entry.SoundAsset = FSoftObjectPath(SoundWave);
		Entry.DiskPath = DiskPath;
		Entry.Duration = SoundWave->Duration;
		Entry.SampleRate = Header.SampleRate;
		Entry.NumChannels = Header.NumChannels;
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
