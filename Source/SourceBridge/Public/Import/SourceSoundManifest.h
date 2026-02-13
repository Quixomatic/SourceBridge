#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SourceSoundManifest.generated.h"

class USoundWave;

/** How this sound was created / where it came from. */
UENUM()
enum class ESourceSoundType : uint8
{
	/** Exists in game VPK archives or base game — no export needed. */
	Stock,

	/** Imported from a BSP (custom server content) — needs re-packing on export. */
	Imported,

	/** Added manually by user — needs packing on export. */
	Custom
};

/** One entry per Source sound tracked by the manifest. */
USTRUCT()
struct FSourceSoundEntry
{
	GENERATED_BODY()

	/** Original Source engine sound path, e.g. "sound/soccer/crowd_1.wav" */
	UPROPERTY(EditAnywhere)
	FString SourcePath;

	/** How this sound was created / where it came from */
	UPROPERTY(EditAnywhere)
	ESourceSoundType Type = ESourceSoundType::Stock;

	/** UE sound asset: /Game/SourceBridge/Sounds/... */
	UPROPERTY(EditAnywhere)
	FSoftObjectPath SoundAsset;

	/** Whether this sound exists in game VPK archives */
	UPROPERTY(EditAnywhere)
	bool bIsStock = false;

	/** Absolute path to WAV file on disk (for export packing) */
	UPROPERTY(EditAnywhere)
	FString DiskPath;

	/** Duration in seconds */
	UPROPERTY(EditAnywhere)
	float Duration = 0.0f;

	/** Sample rate (Hz) */
	UPROPERTY(EditAnywhere)
	int32 SampleRate = 0;

	/** Number of audio channels */
	UPROPERTY(EditAnywhere)
	int32 NumChannels = 0;

	/** When this sound was last imported/updated */
	UPROPERTY(EditAnywhere)
	FDateTime LastImported;
};

/**
 * Central manifest tracking every Source sound the plugin has touched.
 * Saved as a UDataAsset at /Game/SourceBridge/SoundManifest.
 * One manifest per project. Access via USourceSoundManifest::Get().
 */
UCLASS()
class SOURCEBRIDGE_API USourceSoundManifest : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Get the singleton manifest instance. Creates or loads it on first call. */
	static USourceSoundManifest* Get();

	/** All tracked sound entries */
	UPROPERTY(EditAnywhere)
	TArray<FSourceSoundEntry> Entries;

	// ---- Lookup API ----

	/** Find an entry by Source sound path. Returns nullptr if not found. */
	FSourceSoundEntry* FindBySourcePath(const FString& SourcePath);
	const FSourceSoundEntry* FindBySourcePath(const FString& SourcePath) const;

	/** Reverse lookup: find entry by UE sound asset. */
	FSourceSoundEntry* FindByUESound(const USoundWave* Sound);

	/** The critical reverse mapping for export: UE sound → Source path. Returns empty if not found. */
	FString GetSourcePath(const USoundWave* Sound) const;

	// ---- Registration API ----

	/** Add or update an entry. If SourcePath already exists, updates it. Marks manifest dirty. */
	void Register(const FSourceSoundEntry& Entry);

	/** Remove an entry by Source path. */
	void Remove(const FString& SourcePath);

	// ---- Query API ----

	/** Get all entries of a given type. */
	TArray<FSourceSoundEntry*> GetAllOfType(ESourceSoundType Type);

	/** Get total number of entries. */
	int32 Num() const { return Entries.Num(); }

	// ---- Persistence ----

	/** Save the manifest asset to disk. */
	void SaveManifest();

	/** Mark the manifest as needing save. */
	void MarkDirty();

private:
	void RebuildIndex();
	void EnsureIndex();

	TMap<FString, int32> SourcePathIndex;
	TMap<FString, int32> SoundAssetIndex;
	bool bIndexBuilt = false;

	static TWeakObjectPtr<USourceSoundManifest> CachedManifest;
	static const TCHAR* ManifestAssetPath;
};
