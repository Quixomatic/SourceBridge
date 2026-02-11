#pragma once

#include "CoreMinimal.h"

/**
 * Settings for QC file generation.
 */
struct FQCSettings
{
	/** Model path relative to models/ (e.g., "props/mymodel") */
	FString ModelName;

	/** Reference mesh SMD filename (e.g., "mymodel_ref.smd") */
	FString BodySMD;

	/** Physics collision SMD filename (e.g., "mymodel_phys.smd") */
	FString CollisionSMD;

	/** Idle animation SMD filename */
	FString IdleSMD;

	/** Material search path relative to materials/ (e.g., "models/props/mymodel") */
	FString CDMaterials;

	/** Surface property (e.g., "metal", "wood", "concrete", "rubber") */
	FString SurfaceProp = TEXT("default");

	/** Model scale factor (applied on top of SMD coordinates) */
	float Scale = 1.0f;

	/** Whether the model is static (no animations beyond idle) */
	bool bStaticProp = true;

	/** Whether to generate a $collisionmodel block */
	bool bHasCollision = true;

	/** Whether collision should be concave (multiple convex pieces) */
	bool bConcaveCollision = false;

	/** Mass override in kg (0 = auto from volume * surfaceprop density) */
	float MassOverride = 0.0f;
};

/**
 * Generates studiomdl QC compile files for Source engine models.
 *
 * QC files are plain text instructions that tell studiomdl how to
 * assemble SMD files into a compiled .mdl model.
 */
class SOURCEBRIDGE_API FQCWriter
{
public:
	/**
	 * Generate a complete QC file from settings.
	 */
	static FString GenerateQC(const FQCSettings& Settings);

	/**
	 * Generate default QC settings from a mesh name.
	 * Sets up reasonable defaults for a static prop.
	 */
	static FQCSettings MakeDefaultSettings(const FString& MeshName);
};
