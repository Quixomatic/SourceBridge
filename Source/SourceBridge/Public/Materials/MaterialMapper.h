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
 * 4. Default fallback material
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

private:
	/** User-defined overrides: UE material name -> Source path */
	TMap<FString, FString> ManualOverrides;

	/** Built-in tool texture mappings */
	TMap<FString, FString> ToolTextureMappings;

	/** Fallback material when no mapping is found */
	FString DefaultMaterial;

	void InitToolTextureMappings();
};
