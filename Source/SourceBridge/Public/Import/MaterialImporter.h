#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;

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
 * Imports Source engine materials into UE.
 *
 * Handles:
 * - Parsing VMT files (KeyValues format)
 * - Searching extracted asset directories for VMT/VTF files
 * - Reverse material name mapping (Source path → UE material)
 * - Creating placeholder UE materials for unmapped Source materials
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

	/**
	 * Find or create a UE material for a Source material path.
	 * Search order:
	 * 1. Material cache
	 * 2. Extracted VMT file in asset search path → parse and create material
	 * 3. Existing UE material in asset registry (reverse name mapping)
	 * 4. Create placeholder material with deterministic color
	 */
	static UMaterialInterface* ResolveSourceMaterial(const FString& SourceMaterialPath);

	/**
	 * Try to find an existing UE material asset by reversing the Source path.
	 * Returns null if no match found.
	 */
	static UMaterialInterface* FindExistingMaterial(const FString& SourceMaterialPath);

	/**
	 * Try to find and parse a VMT file from the asset search path.
	 * If found, creates a material based on VMT properties.
	 */
	static UMaterialInterface* CreateMaterialFromVMT(const FString& SourceMaterialPath);

	/**
	 * Create a simple placeholder material with a color based on the material name.
	 * The placeholder is a dynamic material instance that can be applied to brushes.
	 */
	static UMaterialInterface* CreatePlaceholderMaterial(const FString& SourceMaterialPath);

	/** Set up additional search paths from the Source game install directory. */
	static void SetupGameSearchPaths(const FString& GameName = TEXT("cstrike"));

	/** Clear the material cache. Call when starting a new import. */
	static void ClearCache();

private:
	/** Cache of resolved materials (Source path → UE material) */
	static TMap<FString, UMaterialInterface*> MaterialCache;

	/** Reverse tool texture mapping (Source path → UE tool material name) */
	static TMap<FString, FString> ReverseToolMappings;

	/** Directory containing extracted VMT/VTF files from BSP pakfile */
	static FString AssetSearchPath;

	/** Additional search paths (game materials dir, custom dirs) */
	static TArray<FString> AdditionalSearchPaths;

	/** Generate a deterministic color from a material name (for placeholders). */
	static FLinearColor ColorFromName(const FString& Name);

	/** Create a UMaterial with a Constant3Vector expression for the given color. */
	static UMaterial* CreateColorMaterial(const FLinearColor& Color, const FString& SourceMaterialPath);

	/** Create a UMaterial with a TextureSample expression for an imported VTF texture. */
	static UMaterial* CreateTexturedMaterial(UTexture2D* Texture, const FString& SourceMaterialPath);

	/** Find and load a VTF texture from the asset search path. */
	static UTexture2D* FindAndLoadVTF(const FString& TexturePath);

	/** Initialize reverse mappings if not already done. */
	static void EnsureReverseToolMappings();
};
