#pragma once

#include "CoreMinimal.h"
#include "Import/VPKReader.h"

class UMaterial;
class UMaterialInterface;
class UMaterialInstanceConstant;
class UTexture2D;

/** How a Source material handles transparency. */
enum class ESourceAlphaMode : uint8
{
	Opaque,       // No transparency (default)
	Masked,       // Binary alpha test ($alphatest) - pixels fully opaque or fully transparent
	Translucent   // Smooth partial opacity ($translucent) - pixels can be semi-transparent
};

/** Parsed data from a VMT (Valve Material Type) file. */
struct FVMTParsedMaterial
{
	FString ShaderName;
	TMap<FString, FString> Parameters;

	FString GetBaseTexture() const { return Parameters.FindRef(TEXT("$basetexture")); }
	FString GetBumpMap() const { return Parameters.FindRef(TEXT("$bumpmap")); }
	FString GetSurfaceProp() const { return Parameters.FindRef(TEXT("$surfaceprop")); }
	bool IsTranslucent() const { return Parameters.Contains(TEXT("$translucent")) || Parameters.Contains(TEXT("$alpha")); }
};

/**
 * Imports Source engine materials into UE as persistent assets.
 *
 * All imported textures are saved as UTexture2D assets in Content/SourceBridge/Textures/.
 * All imported materials are saved as UMaterialInstanceConstant in Content/SourceBridge/Materials/.
 * Everything is tracked in the central USourceMaterialManifest.
 *
 * On subsequent imports, existing assets are reused (manifest lookup).
 * Assets survive editor restart (persistent .uasset files).
 */
class SOURCEBRIDGE_API FMaterialImporter
{
public:
	/** Parse a VMT file into structured data. */
	static FVMTParsedMaterial ParseVMT(const FString& VMTContent);

	/** Parse a VMT file from disk. */
	static FVMTParsedMaterial ParseVMTFile(const FString& FilePath);

	/**
	 * Set the directory to search for extracted VMT/VTF files.
	 * Called by BSPImporter after extracting BSP pakfile contents.
	 */
	static void SetAssetSearchPath(const FString& Path);

	/** Set up additional search paths from the Source game install directory. */
	static void SetupGameSearchPaths(const FString& GameName = TEXT("cstrike"));

	/**
	 * Find or create a persistent UE material for a Source material path.
	 * Search order:
	 * 1. Runtime pointer cache
	 * 2. Material manifest (loads existing persistent asset)
	 * 3. VMT/VTF on disk or in VPK → creates persistent texture + material + registers in manifest
	 * 4. Placeholder persistent material
	 */
	static UMaterialInterface* ResolveSourceMaterial(const FString& SourceMaterialPath);

	/** Try to find an existing UE material (manifest first, then asset registry). */
	static UMaterialInterface* FindExistingMaterial(const FString& SourceMaterialPath);

	/** Create a persistent material from VMT/VTF data. */
	static UMaterialInterface* CreateMaterialFromVMT(const FString& SourceMaterialPath);

	/** Create a persistent placeholder material with a deterministic color. */
	static UMaterialInterface* CreatePlaceholderMaterial(const FString& SourceMaterialPath);

	/**
	 * Clear the runtime pointer cache. Does NOT delete persistent assets.
	 * Call when starting a new import session.
	 */
	static void ClearCache();

	/** Get texture dimensions for a Source material path. Returns (512,512) if unknown. */
	static FIntPoint GetTextureSize(const FString& SourceMaterialPath);

private:
	/** Runtime pointer cache (Source path → loaded persistent UMaterialInterface*) */
	static TMap<FString, UMaterialInterface*> MaterialCache;

	/** Texture info cached during VTF decode */
	struct FTextureCacheEntry
	{
		FIntPoint Size = FIntPoint(512, 512);
		bool bHasAlpha = false;
	};
	static TMap<FString, FTextureCacheEntry> TextureInfoCache;

	/** Reverse tool texture mapping (Source path → UE tool material name) */
	static TMap<FString, FString> ReverseToolMappings;

	/** Directory containing extracted VMT/VTF files from BSP pakfile */
	static FString AssetSearchPath;

	/** Additional search paths (game materials dir, custom dirs) */
	static TArray<FString> AdditionalSearchPaths;

	/** Opened VPK archives for game material access */
	static TArray<TSharedPtr<FVPKReader>> VPKArchives;

	// ---- Persistent Asset Creation ----

	/** Create a persistent UTexture2D from BGRA pixel data. */
	static UTexture2D* CreatePersistentTexture(const TArray<uint8>& BGRAData, int32 Width, int32 Height,
		const FString& SourceTexturePath, bool bIsNormalMap = false);

	/** Create a persistent UMaterialInstanceConstant from a texture + VMT data. */
	static UMaterialInstanceConstant* CreatePersistentMaterial(UTexture2D* BaseTexture, UTexture2D* NormalMap,
		ESourceAlphaMode AlphaMode, const FString& SourceMaterialPath, const FVMTParsedMaterial& VMTData);

	/** Create a persistent color-only material (for placeholders). */
	static UMaterialInstanceConstant* CreatePersistentColorMaterial(const FLinearColor& Color,
		const FString& SourceMaterialPath);

	// ---- Persistent Base Materials ----

	/** Get or create the persistent base material for a given alpha mode. */
	static UMaterial* GetOrCreateBaseMaterial(ESourceAlphaMode AlphaMode);

	/** Get or create the persistent color base material (for placeholders). */
	static UMaterial* GetOrCreateColorBaseMaterial();

	static UMaterial* CachedOpaqueMaterial;
	static UMaterial* CachedMaskedMaterial;
	static UMaterial* CachedTranslucentMaterial;
	static UMaterial* CachedColorMaterial;

	// ---- VTF Loading ----

	/** Find raw VTF file bytes from disk or VPK. */
	static bool FindVTFBytes(const FString& TexturePath, TArray<uint8>& OutFileData);

	/** Try to find VTF bytes in VPK archives only. */
	static bool FindVTFBytesFromVPK(const FString& TexturePath, TArray<uint8>& OutData);

	// ---- VMT Search ----

	/** Try to find and read VMT content from VPK archives. */
	static FString FindVMTInVPK(const FString& SourceMaterialPath);

	// ---- Helpers ----

	static void EnsureReverseToolMappings();
	static void EnsureVPKArchivesLoaded();
	static FLinearColor ColorFromName(const FString& Name);

	/** Convert a Source path to a UE asset path. E.g. ("Textures", "concrete/floor") → "/Game/SourceBridge/Textures/concrete/floor" */
	static FString SourcePathToAssetPath(const FString& Category, const FString& SourcePath);

	/** Save a UObject asset to disk. Returns true on success. */
	static bool SaveAsset(UObject* Asset);
};
