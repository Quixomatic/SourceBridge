#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
 * Maps UE materials to Source engine material paths.
 *
 * Priority:
 * 1. Manual overrides (user-defined UE material name -> Source path)
 * 2. Tool texture detection (materials named Tool_* map to tools/*)
 * 3. Name-based auto-mapping (strips prefixes, lowercases)
 * 4. Default fallback material
 */
class SOURCEBRIDGE_API FMaterialMapper
{
public:
	FMaterialMapper();

	/** Resolve a UE material to a Source material path string. */
	FString MapMaterial(UMaterialInterface* Material) const;

	/** Resolve a UE material name to a Source material path. */
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
