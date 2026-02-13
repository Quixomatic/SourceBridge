#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SourceMaterialManifest.generated.h"

class UMaterialInterface;
class UTexture2D;

/** How this material was created / where it came from. */
UENUM()
enum class ESourceMaterialType : uint8
{
	/** Exists in game VPK archives — no export needed, vbsp can find it. */
	Stock,

	/** Imported from a BSP/VMF (custom server content, not in VPK) — needs re-export. */
	Imported,

	/** Created by user in UE — needs full VTF/VMT export. */
	Custom
};

/** One entry per Source material tracked by the manifest. */
USTRUCT()
struct FSourceMaterialEntry
{
	GENERATED_BODY()

	/** Original Source engine material path, e.g. "concrete/concretefloor001a" */
	UPROPERTY(EditAnywhere)
	FString SourcePath;

	/** How this material was created / where it came from */
	UPROPERTY(EditAnywhere)
	ESourceMaterialType Type = ESourceMaterialType::Stock;

	/** UE texture asset: /Game/SourceBridge/Textures/concrete/concretefloor001a */
	UPROPERTY(EditAnywhere)
	FSoftObjectPath TextureAsset;

	/** UE material asset: /Game/SourceBridge/Materials/concrete/concretefloor001a */
	UPROPERTY(EditAnywhere)
	FSoftObjectPath MaterialAsset;

	/** Optional normal/bump map texture */
	UPROPERTY(EditAnywhere)
	FSoftObjectPath NormalMapAsset;

	/** VMT shader name: "LightmappedGeneric", "VertexLitGeneric", etc. */
	UPROPERTY(EditAnywhere)
	FString VMTShader;

	/** All original VMT parameters (for lossless re-export) */
	UPROPERTY(EditAnywhere)
	TMap<FString, FString> VMTParams;

	/** Base texture width in pixels (for UV computation) */
	UPROPERTY(EditAnywhere)
	int32 TextureWidth = 0;

	/** Base texture height in pixels (for UV computation) */
	UPROPERTY(EditAnywhere)
	int32 TextureHeight = 0;

	/** Whether the base texture has alpha */
	UPROPERTY(EditAnywhere)
	bool bHasAlpha = false;

	/** Quick check: does this material exist in game VPK? */
	UPROPERTY(EditAnywhere)
	bool bIsInVPK = false;

	/** When this material was last imported/updated */
	UPROPERTY(EditAnywhere)
	FDateTime LastImported;
};

/**
 * Central manifest tracking every Source material the plugin has touched.
 * Saved as a UDataAsset at /Game/SourceBridge/MaterialManifest.
 * One manifest per project. Access via USourceMaterialManifest::Get().
 */
UCLASS()
class SOURCEBRIDGE_API USourceMaterialManifest : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Get the singleton manifest instance. Creates or loads it on first call. */
	static USourceMaterialManifest* Get();

	/** All tracked material entries */
	UPROPERTY(EditAnywhere)
	TArray<FSourceMaterialEntry> Entries;

	// ---- Lookup API ----

	/** Find an entry by Source material path (e.g. "concrete/concretefloor001a"). Returns nullptr if not found. */
	FSourceMaterialEntry* FindBySourcePath(const FString& SourcePath);
	const FSourceMaterialEntry* FindBySourcePath(const FString& SourcePath) const;

	/** Reverse lookup: find entry by UE material asset path. */
	FSourceMaterialEntry* FindByUEMaterial(const UMaterialInterface* Material);
	FSourceMaterialEntry* FindByUEMaterial(const FSoftObjectPath& MaterialAssetPath);

	/** Reverse lookup: find entry by UE texture asset path. */
	FSourceMaterialEntry* FindByUETexture(const UTexture2D* Texture);

	/** The critical reverse mapping for export: UE material → Source path. Returns empty if not found. */
	FString GetSourcePath(const UMaterialInterface* Material) const;

	// ---- Registration API ----

	/** Add or update an entry. If SourcePath already exists, updates it. Marks manifest dirty. */
	void Register(const FSourceMaterialEntry& Entry);

	/** Remove an entry by Source path. */
	void Remove(const FString& SourcePath);

	// ---- Query API ----

	/** Get all entries of a given type. */
	TArray<FSourceMaterialEntry*> GetAllOfType(ESourceMaterialType Type);

	/** Quick check: is this Source path a stock material (in VPK)? Caches result in entry. */
	bool IsStock(const FString& SourcePath) const;

	/** Get total number of entries. */
	int32 Num() const { return Entries.Num(); }

	// ---- Persistence ----

	/** Save the manifest asset to disk. */
	void SaveManifest();

	/** Mark the manifest as needing save. Call SaveManifest() or rely on auto-save. */
	void MarkDirty();

private:
	/** Build the lookup index from Entries array (called after load). */
	void RebuildIndex();

	/** Source path (lowercase) → index into Entries array */
	TMap<FString, int32> SourcePathIndex;

	/** UE material asset path → index into Entries array */
	TMap<FString, int32> MaterialAssetIndex;

	/** Whether the index has been built */
	bool bIndexBuilt = false;

	/** Ensure index is built before lookup */
	void EnsureIndex();

	/** Singleton instance */
	static TWeakObjectPtr<USourceMaterialManifest> CachedManifest;

	/** The asset path where the manifest lives */
	static const TCHAR* ManifestAssetPath;
};
