#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SourceResourceManifest.generated.h"

/** Type of Source resource file. */
UENUM()
enum class ESourceResourceType : uint8
{
	/** Radar/minimap overview image */
	Overview,

	/** Radar config .txt file */
	OverviewConfig,

	/** Detail sprite textures */
	DetailSprites,

	/** Custom loading screen */
	LoadingScreen,

	/** Catch-all for other resource files */
	Other
};

/** How this resource was created / where it came from. */
UENUM()
enum class ESourceResourceOrigin : uint8
{
	/** Exists in game install — no export needed. */
	Stock,

	/** Imported from a BSP (custom content) — needs re-packing on export. */
	Imported,

	/** Added manually by user. */
	Custom
};

/** One entry per Source resource tracked by the manifest. */
USTRUCT()
struct FSourceResourceEntry
{
	GENERATED_BODY()

	/** Source-relative path, e.g. "resource/overviews/mapname.txt" */
	UPROPERTY(EditAnywhere)
	FString SourcePath;

	/** Type of resource */
	UPROPERTY(EditAnywhere)
	ESourceResourceType ResourceType = ESourceResourceType::Other;

	/** Origin of this resource */
	UPROPERTY(EditAnywhere)
	ESourceResourceOrigin Origin = ESourceResourceOrigin::Imported;

	/** UE asset (UTexture2D for images, nullptr for text files) */
	UPROPERTY(EditAnywhere)
	FSoftObjectPath Asset;

	/** Absolute path to file on disk (for export packing) */
	UPROPERTY(EditAnywhere)
	FString DiskPath;

	/** Raw text content for config files (overview .txt) */
	UPROPERTY(EditAnywhere)
	FString TextContent;

	/** When this resource was last imported/updated */
	UPROPERTY(EditAnywhere)
	FDateTime LastImported;

	/** Force this resource to be packed into every export, regardless of auto-detect */
	UPROPERTY(EditAnywhere)
	bool bForcePack = false;
};

/**
 * Central manifest tracking Source resource files (overviews, configs, etc.).
 * Saved as a UDataAsset at /Game/SourceBridge/ResourceManifest.
 * One manifest per project. Access via USourceResourceManifest::Get().
 */
UCLASS()
class SOURCEBRIDGE_API USourceResourceManifest : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Get the singleton manifest instance. Creates or loads it on first call. */
	static USourceResourceManifest* Get();

	/** All tracked resource entries */
	UPROPERTY(EditAnywhere)
	TArray<FSourceResourceEntry> Entries;

	// ---- Lookup API ----

	/** Find an entry by Source path. Returns nullptr if not found. */
	FSourceResourceEntry* FindBySourcePath(const FString& SourcePath);
	const FSourceResourceEntry* FindBySourcePath(const FString& SourcePath) const;

	// ---- Registration API ----

	/** Add or update an entry. If SourcePath already exists, updates it. */
	void Register(const FSourceResourceEntry& Entry);

	/** Remove an entry by Source path. */
	void Remove(const FString& SourcePath);

	// ---- Query API ----

	/** Get all entries of a given type. */
	TArray<FSourceResourceEntry*> GetAllOfType(ESourceResourceType Type);

	/** Get total number of entries. */
	int32 Num() const { return Entries.Num(); }

	// ---- Persistence ----

	void SaveManifest();
	void MarkDirty();

private:
	void RebuildIndex();
	void EnsureIndex();

	TMap<FString, int32> SourcePathIndex;
	bool bIndexBuilt = false;

	static TWeakObjectPtr<USourceResourceManifest> CachedManifest;
	static const TCHAR* ManifestAssetPath;
};
