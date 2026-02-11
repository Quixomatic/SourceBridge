#pragma once

#include "CoreMinimal.h"
#include "VMF/VMFKeyValues.h"

class UWorld;

/**
 * Settings for skybox export.
 */
struct FSkyboxSettings
{
	/** 2D sky texture name for worldspawn (e.g., "sky_day01_01") */
	FString SkyName = TEXT("sky_day01_01");

	/** Whether to generate a 3D skybox room */
	bool bGenerate3DSkybox = false;

	/** Scale of 3D skybox (Source uses 1/16) */
	float SkyboxScale = 1.0f / 16.0f;

	/** Size of the generated skybox room in Source units */
	float SkyboxRoomSize = 16384.0f;
};

/**
 * Skybox data ready for VMF integration.
 */
struct FSkyboxData
{
	/** sky_camera entity */
	FVMFKeyValues SkyCameraEntity;

	/** Skybox brush solids (6 faces forming the sky shell) */
	TArray<FVMFKeyValues> SkyboxBrushes;

	/** The skyname value for worldspawn */
	FString SkyName;

	bool bHasSkyCamera = false;
};

/**
 * Exports skybox configuration for Source engine maps.
 *
 * Source skybox system:
 * - worldspawn "skyname" keyvalue sets the 2D skybox texture
 * - 3D skybox uses a miniature room at 1/16 scale
 * - sky_camera entity marks the origin of the 3D skybox
 * - Skybox brush faces use tools/toolsskybox material
 */
class SOURCEBRIDGE_API FSkyboxExporter
{
public:
	/**
	 * Generate skybox shell brushes (6 brushes forming a sealed room).
	 * These use tools/toolsskybox material and seal the map.
	 */
	static TArray<FVMFKeyValues> GenerateSkyboxShell(
		int32& SolidIdCounter,
		int32& SideIdCounter,
		float RoomSize = 16384.0f,
		float WallThickness = 16.0f);

	/**
	 * Generate a sky_camera entity for 3D skybox.
	 */
	static FVMFKeyValues GenerateSkyCamera(
		int32 EntityId,
		const FVector& Position,
		float Scale = 16.0f);

	/**
	 * Detect skybox-related actors in the UE scene and export.
	 * Looks for actors tagged with "skybox" or "sky_camera".
	 */
	static FSkyboxData ExportSkybox(
		UWorld* World,
		int32& EntityIdCounter,
		int32& SolidIdCounter,
		int32& SideIdCounter,
		const FSkyboxSettings& Settings = FSkyboxSettings());
};
