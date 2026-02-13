#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UTexture2D;

/**
 * Result of analyzing a UE material for Source engine export.
 * Contains extracted textures and properties needed to generate VTF/VMT files.
 */
struct FSourceMaterialAnalysis
{
	/** Base color / diffuse texture (the primary texture for $basetexture) */
	UTexture2D* BaseColorTexture = nullptr;

	/** Normal / bump map texture (for $bumpmap) */
	UTexture2D* NormalMapTexture = nullptr;

	/** Emissive texture (for $selfillum) */
	UTexture2D* EmissiveTexture = nullptr;

	/** Whether the material uses masked transparency ($alphatest) */
	bool bIsMasked = false;

	/** Whether the material uses smooth transparency ($translucent) */
	bool bIsTranslucent = false;

	/** Whether the material is two-sided ($nocull) */
	bool bTwoSided = false;

	/** Opacity value for partially transparent materials ($alpha) */
	float Opacity = 1.0f;

	/** Tint / color multiplier applied to the base texture */
	FLinearColor TintColor = FLinearColor::White;

	/** Whether analysis found at least a usable base texture */
	bool bHasValidTexture = false;
};

/**
 * Analyzes UE materials to extract textures and properties for Source engine export.
 *
 * For UMaterialInstance: reads texture, scalar, and vector parameters by common names.
 * For UMaterial: uses GetUsedTextures() + heuristics to identify base color and normal.
 * Always reads blend mode and two-sided flag from the material interface.
 */
class SOURCEBRIDGE_API FMaterialAnalyzer
{
public:
	/**
	 * Analyze a UE material and extract all exportable properties.
	 * Works with any UMaterialInterface (UMaterial, UMaterialInstanceConstant, etc.)
	 */
	static FSourceMaterialAnalysis Analyze(UMaterialInterface* Material);

private:
	/** Try to find a texture by trying multiple common parameter names. */
	static UTexture2D* FindTextureParameter(UMaterialInterface* Material, const TArray<FName>& Names);

	/** Try to find a scalar parameter value. Returns true if found. */
	static bool FindScalarParameter(UMaterialInterface* Material, const TArray<FName>& Names, float& OutValue);

	/** Try to find a vector parameter value. Returns true if found. */
	static bool FindVectorParameter(UMaterialInterface* Material, const TArray<FName>& Names, FLinearColor& OutValue);

	/** Fallback: use GetUsedTextures() to find base color and normal textures. */
	static void FallbackTextureDetection(UMaterialInterface* Material, FSourceMaterialAnalysis& Result);
};
