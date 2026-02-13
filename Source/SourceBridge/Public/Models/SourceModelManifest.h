#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SourceModelManifest.generated.h"

class UStaticMesh;

/** How this model was created / where it came from. */
UENUM()
enum class ESourceModelType : uint8
{
	/** Exists in game VPK archives — no export needed, engine can find it. */
	Stock,

	/** Imported from a BSP/VMF (custom server content, not in VPK) — needs re-packing on export. */
	Imported,

	/** Created by user in UE — needs full SMD/QC export + studiomdl compile. */
	Custom
};

/** One entry per Source model tracked by the manifest. */
USTRUCT()
struct FSourceModelEntry
{
	GENERATED_BODY()

	/** Original Source engine model path, e.g. "models/props/cs_office/light_inset.mdl" */
	UPROPERTY(EditAnywhere)
	FString SourcePath;

	/** How this model was created / where it came from */
	UPROPERTY(EditAnywhere)
	ESourceModelType Type = ESourceModelType::Stock;

	/** UE static mesh asset: /Game/SourceBridge/Models/... */
	UPROPERTY(EditAnywhere)
	FSoftObjectPath MeshAsset;

	/** Whether this model exists in game VPK archives */
	UPROPERTY(EditAnywhere)
	bool bIsStock = false;

	/** Disk paths for companion files (extension → absolute disk path).
	 *  e.g. ".mdl" → "C:/path/models/foo.mdl", ".vvd" → "C:/path/models/foo.vvd"
	 *  Used by export pipeline to pack custom model files into BSP. */
	UPROPERTY(EditAnywhere)
	TMap<FString, FString> DiskPaths;

	/** Surface property from MDL header (e.g. "metal", "wood") */
	UPROPERTY(EditAnywhere)
	FString SurfaceProp;

	/** Whether the MDL had $staticprop flag */
	UPROPERTY(EditAnywhere)
	bool bIsStaticProp = true;

	/** Mass from MDL header in kg */
	UPROPERTY(EditAnywhere)
	float ModelMass = 0.0f;

	/** Material search dirs from MDL ($cdmaterials) */
	UPROPERTY(EditAnywhere)
	TArray<FString> CDMaterials;

	/** When this model was last imported/updated */
	UPROPERTY(EditAnywhere)
	FDateTime LastImported;
};

/**
 * Central manifest tracking every Source model the plugin has touched.
 * Saved as a UDataAsset at /Game/SourceBridge/ModelManifest.
 * One manifest per project. Access via USourceModelManifest::Get().
 */
UCLASS()
class SOURCEBRIDGE_API USourceModelManifest : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Get the singleton manifest instance. Creates or loads it on first call. */
	static USourceModelManifest* Get();

	/** All tracked model entries */
	UPROPERTY(EditAnywhere)
	TArray<FSourceModelEntry> Entries;

	// ---- Lookup API ----

	/** Find an entry by Source model path (e.g. "models/props/barrel.mdl"). Returns nullptr if not found. */
	FSourceModelEntry* FindBySourcePath(const FString& SourcePath);
	const FSourceModelEntry* FindBySourcePath(const FString& SourcePath) const;

	/** Reverse lookup: find entry by UE static mesh asset. */
	FSourceModelEntry* FindByUEMesh(const UStaticMesh* Mesh);

	/** The critical reverse mapping for export: UE mesh → Source path. Returns empty if not found. */
	FString GetSourcePath(const UStaticMesh* Mesh) const;

	// ---- Registration API ----

	/** Add or update an entry. If SourcePath already exists, updates it. Marks manifest dirty. */
	void Register(const FSourceModelEntry& Entry);

	/** Remove an entry by Source path. */
	void Remove(const FString& SourcePath);

	// ---- Query API ----

	/** Get all entries of a given type. */
	TArray<FSourceModelEntry*> GetAllOfType(ESourceModelType Type);

	/** Get total number of entries. */
	int32 Num() const { return Entries.Num(); }

	// ---- Persistence ----

	/** Save the manifest asset to disk. */
	void SaveManifest();

	/** Mark the manifest as needing save. */
	void MarkDirty();

private:
	/** Build the lookup index from Entries array. */
	void RebuildIndex();

	/** Ensure index is built before lookup. */
	void EnsureIndex();

	/** Source path (lowercase) → index into Entries array */
	TMap<FString, int32> SourcePathIndex;

	/** UE mesh asset path → index into Entries array */
	TMap<FString, int32> MeshAssetIndex;

	/** Whether the index has been built */
	bool bIndexBuilt = false;

	/** Singleton instance */
	static TWeakObjectPtr<USourceModelManifest> CachedManifest;

	/** The asset path where the manifest lives */
	static const TCHAR* ManifestAssetPath;
};
