#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;

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
	 * Find or create a UE material for a Source material path.
	 * First checks the cache, then tries reverse name mapping,
	 * then creates a placeholder material.
	 */
	static UMaterialInterface* ResolveSourceMaterial(const FString& SourceMaterialPath);

	/**
	 * Try to find an existing UE material asset by reversing the Source path.
	 * Returns null if no match found.
	 */
	static UMaterialInterface* FindExistingMaterial(const FString& SourceMaterialPath);

	/**
	 * Create a simple placeholder material with a color based on the material name.
	 * The placeholder is a dynamic material instance that can be applied to brushes.
	 */
	static UMaterialInterface* CreatePlaceholderMaterial(const FString& SourceMaterialPath);

	/** Clear the material cache. Call when starting a new import. */
	static void ClearCache();

private:
	/** Cache of resolved materials (Source path → UE material) */
	static TMap<FString, UMaterialInterface*> MaterialCache;

	/** Reverse tool texture mapping (Source path → UE tool material name) */
	static TMap<FString, FString> ReverseToolMappings;

	/** Generate a deterministic color from a material name (for placeholders). */
	static FLinearColor ColorFromName(const FString& Name);

	/** Initialize reverse mappings if not already done. */
	static void EnsureReverseToolMappings();
};
