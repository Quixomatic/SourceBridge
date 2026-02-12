#pragma once

#include "CoreMinimal.h"
#include "Import/VPKReader.h"

class UStaticMesh;
class UMaterialInterface;
struct FSourceModelData;

/**
 * Imports Source engine models (.mdl/.vvd/.vtx) into UE as UStaticMesh assets.
 *
 * Follows the same pattern as FMaterialImporter:
 * - Static search path configuration
 * - VPK archive fallback
 * - Per-model caching (parse each unique .mdl once)
 * - Transient package storage
 *
 * Usage:
 *   FModelImporter::SetAssetSearchPath(extractDir);
 *   FModelImporter::SetupGameSearchPaths("cstrike");
 *   UStaticMesh* Mesh = FModelImporter::ResolveModel("models/props/barrel.mdl", 0);
 */
class SOURCEBRIDGE_API FModelImporter
{
public:
	/**
	 * Set the primary directory to search for extracted model files.
	 * Typically the BSPSource output directory containing models/ subdirectory.
	 */
	static void SetAssetSearchPath(const FString& Path);

	/**
	 * Set up additional search paths from the Source game install directory.
	 * Searches game directories and opens VPK archives for model files.
	 */
	static void SetupGameSearchPaths(const FString& GameName = TEXT("cstrike"));

	/**
	 * Find or create a UStaticMesh for a Source model path.
	 *
	 * @param SourceModelPath Source-relative model path (e.g., "models/props/barrel.mdl")
	 * @param SkinIndex Which skin family to use for material assignment (0 = default)
	 * @return The created/cached UStaticMesh, or nullptr if model not found/failed
	 */
	static UStaticMesh* ResolveModel(const FString& SourceModelPath, int32 SkinIndex = 0);

	/**
	 * Get materials for a specific skin of a previously resolved model.
	 * Call after ResolveModel to get material array for a different skin.
	 */
	static TArray<UMaterialInterface*> GetMaterialsForSkin(const FString& SourceModelPath, int32 SkinIndex);

	/** Clear per-import transient state. Does NOT clear VPK archives. */
	static void ClearCache();

private:
	/** Cache of resolved models (Source path → UStaticMesh) */
	static TMap<FString, UStaticMesh*> ModelCache;

	/** Cache of parsed model data for skin/material lookups */
	static TMap<FString, TSharedPtr<FSourceModelData>> ParsedModelCache;

	/** Primary search directory (BSP extracted assets) */
	static FString AssetSearchPath;

	/** Additional search paths (game install directories) */
	static TArray<FString> AdditionalSearchPaths;

	/** VPK archives for model file access */
	static TArray<TSharedPtr<FVPKReader>> VPKArchives;

	/** Whether game search paths have been initialized */
	static bool bGamePathsInitialized;

	/**
	 * Find companion model files (.mdl, .vvd, .vtx) on disk or in VPK archives.
	 * @param SourceModelPath Source-relative path (e.g., "models/props/barrel.mdl")
	 * @param OutMDL Raw bytes of .mdl file
	 * @param OutVVD Raw bytes of .vvd file
	 * @param OutVTX Raw bytes of .vtx file
	 * @return true if all required files found
	 */
	static bool FindModelFiles(const FString& SourceModelPath,
		TArray<uint8>& OutMDL, TArray<uint8>& OutVVD, TArray<uint8>& OutVTX);

	/** Try to read a file from disk search paths. */
	static bool ReadFileFromDisk(const FString& RelativePath, TArray<uint8>& OutData);

	/** Try to read a file from VPK archives. */
	static bool ReadFileFromVPK(const FString& RelativePath, TArray<uint8>& OutData);

	/**
	 * Create a UStaticMesh from parsed model data.
	 * @param ModelData Parsed model geometry/materials
	 * @param SourceModelPath Original path for naming
	 * @param SkinIndex Skin family to use for material assignment
	 */
	static UStaticMesh* CreateStaticMesh(const FSourceModelData& ModelData,
		const FString& SourceModelPath, int32 SkinIndex);

	/** Resolve material names from MDL data using MaterialImporter. */
	static TArray<UMaterialInterface*> ResolveMaterials(const FSourceModelData& ModelData, int32 SkinIndex);
};
