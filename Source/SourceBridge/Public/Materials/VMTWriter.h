#pragma once

#include "CoreMinimal.h"

/**
 * Generates Source VMT (Valve Material Type) files.
 *
 * VMT format is Valve KeyValues defining shader and texture parameters:
 *   LightmappedGeneric
 *   {
 *       $basetexture "custom/mymap/floor"
 *       $surfaceprop "concrete"
 *   }
 */
class SOURCEBRIDGE_API FVMTWriter
{
public:
	/** Shader type for this material. */
	FString ShaderName;

	/** Material parameters ($basetexture, $surfaceprop, $bumpmap, etc.) */
	TMap<FString, FString> Parameters;

	FVMTWriter();

	/** Set the shader (LightmappedGeneric, VertexLitGeneric, UnlitGeneric, etc.) */
	void SetShader(const FString& Shader);

	/** Set the base texture path (relative to materials/) */
	void SetBaseTexture(const FString& TexturePath);

	/** Set the surface property for physics */
	void SetSurfaceProp(const FString& SurfaceProp);

	/** Set the bump/normal map path */
	void SetBumpMap(const FString& BumpPath);

	/** Set an arbitrary parameter */
	void SetParameter(const FString& Key, const FString& Value);

	/** Serialize to VMT text format. */
	FString Serialize() const;

	/**
	 * Generate a basic LightmappedGeneric VMT for a brush material.
	 * This is the most common shader for world geometry.
	 */
	static FString GenerateBrushVMT(
		const FString& BaseTexturePath,
		const FString& SurfaceProp = TEXT("concrete"));

	/**
	 * Generate a VertexLitGeneric VMT for a model material.
	 */
	static FString GenerateModelVMT(
		const FString& BaseTexturePath,
		const FString& SurfaceProp = TEXT("metal"));
};
