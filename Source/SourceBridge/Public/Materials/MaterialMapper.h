#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
 * Maps UE materials to Source engine material paths.
 *
 * Priority:
 * 1. Material manifest lookup (Source path from import/previous export)
 * 2. Manual overrides (user-defined UE material name -> Source path)
 * 3. Tool texture detection (materials named Tool_* map to tools/*)
 * 4. Material analysis (analyze UE material, assign custom Source path, register in manifest)
 * 5. Default fallback material
 */
class SOURCEBRIDGE_API FMaterialMapper
{
public:
	FMaterialMapper();

	/** Resolve a UE material to a Source material path string. */
	FString MapMaterial(UMaterialInterface* Material) const;

	/** Resolve a UE material name to a Source material path (fallback only, no manifest). */
	FString MapMaterialName(const FString& MaterialName) const;

	/** Add a manual mapping override. */
	void AddOverride(const FString& UEMaterialName, const FString& SourceMaterialPath);

	/** Set the default fallback material. */
	void SetDefaultMaterial(const FString& SourceMaterialPath);

	/** Get the default fallback material. */
	const FString& GetDefaultMaterial() const { return DefaultMaterial; }

	/** Set the map name used for custom material Source paths (e.g. "custom/<mapname>/<material>"). */
	void SetMapName(const FString& InMapName) { MapName = InMapName; }

	/** Get all Source material paths that were used during mapping (populated by MapMaterial calls). */
	const TSet<FString>& GetUsedPaths() const { return UsedMaterialPaths; }

	/** Clear the used paths set. */
	void ClearUsedPaths() { UsedMaterialPaths.Empty(); }

private:
	/** User-defined overrides: UE material name -> Source path */
	TMap<FString, FString> ManualOverrides;

	/** Built-in tool texture mappings */
	TMap<FString, FString> ToolTextureMappings;

	/** Fallback material when no mapping is found */
	FString DefaultMaterial;

	/** Map name for custom material path prefix */
	FString MapName;

	/** All Source paths returned by MapMaterial (for tracking which materials need export) */
	mutable TSet<FString> UsedMaterialPaths;

	void InitToolTextureMappings();

	/** Analyze a custom UE material, assign a Source path, register in manifest. Returns Source path or empty. */
	FString AnalyzeAndRegisterCustomMaterial(UMaterialInterface* Material) const;

	/** Generate a clean Source-engine-safe material name from a UE material. */
	static FString CleanMaterialName(const FString& UEName);
};
