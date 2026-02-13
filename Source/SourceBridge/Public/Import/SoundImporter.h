#pragma once

#include "CoreMinimal.h"

class USoundWave;

/**
 * Imports Source engine sounds (.wav) into UE as USoundWave assets.
 *
 * Usage:
 *   FSoundImporter::ImportSoundsFromDirectory(extractedDir);
 *   USoundWave* Sound = FSoundImporter::ImportSound("sound/soccer/crowd_1.wav", diskPath);
 */
class SOURCEBRIDGE_API FSoundImporter
{
public:
	/**
	 * Import a single WAV file as a USoundWave asset.
	 *
	 * @param SourcePath Source-relative path (e.g., "sound/soccer/crowd_1.wav")
	 * @param DiskPath Absolute path to WAV file on disk
	 * @return The created USoundWave, or nullptr on failure
	 */
	static USoundWave* ImportSound(const FString& SourcePath, const FString& DiskPath);

	/**
	 * Batch import all WAV files found in a directory tree.
	 * Scans for sound/ subdirectory and imports all .wav files found.
	 *
	 * @param ExtractedDir Root directory containing sound/ folder
	 * @return Number of sounds imported
	 */
	static int32 ImportSoundsFromDirectory(const FString& ExtractedDir);

	/**
	 * Scan entity keyvalues for sound path references.
	 * Known sound properties: message, StartSound, StopSound, MoveSound, etc.
	 *
	 * @param EntityKeyValues Map of keyvalue pairs from entity
	 * @return Array of Source sound paths found
	 */
	static TArray<FString> ExtractSoundReferences(const TMap<FString, FString>& EntityKeyValues);
};
