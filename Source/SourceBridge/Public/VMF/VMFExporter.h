#pragma once

#include "CoreMinimal.h"
#include "VMF/VMFKeyValues.h"

/**
 * Builds and exports complete VMF documents.
 * Phase 1: Generates hardcoded test geometry (box rooms).
 * Later phases will read from UE scene data.
 */
class SOURCEBRIDGE_API FVMFExporter
{
public:
	/**
	 * Generate a complete VMF string for a sealed box room.
	 * Room is 512x512x256 Source units with 16-unit thick walls.
	 * Material: DEV/DEV_MEASUREWALL01A on inner faces, TOOLS/TOOLSNODRAW on outer.
	 */
	static FString GenerateBoxRoom();

	/** Build a VMF solid (brush) from an axis-aligned bounding box. */
	static FVMFKeyValues BuildAABBSolid(
		int32 SolidId,
		int32& SideIdCounter,
		const FVector& Min,
		const FVector& Max,
		const FString& Material);

private:
	static FVMFKeyValues BuildVersionInfo();
	static FVMFKeyValues BuildVisGroups();
	static FVMFKeyValues BuildViewSettings();
	static FVMFKeyValues BuildCameras();
	static FVMFKeyValues BuildCordon();

	/** Build a single side (face) of a brush solid. */
	static FVMFKeyValues BuildSide(
		int32 SideId,
		const FString& PlaneStr,
		const FString& Material,
		const FString& UAxis,
		const FString& VAxis);

	/** Get default UV axes for an axis-aligned face given its outward normal direction. */
	static void GetDefaultUVAxes(const FVector& Normal, FString& OutUAxis, FString& OutVAxis);
};
