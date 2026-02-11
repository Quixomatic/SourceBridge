#pragma once

#include "CoreMinimal.h"
#include "VMF/VMFKeyValues.h"

class UWorld;

/**
 * Builds and exports complete VMF documents.
 *
 * Two modes:
 * - GenerateBoxRoom(): Hardcoded test geometry for validation
 * - ExportScene(): Reads ABrush actors from a UE world and converts them
 */
class SOURCEBRIDGE_API FVMFExporter
{
public:
	/**
	 * Export the current UE scene to a VMF string.
	 * Iterates all ABrush actors, converts geometry, builds complete VMF.
	 * Skips the default builder brush and volume actors.
	 * Warnings are logged via UE_LOG.
	 */
	static FString ExportScene(UWorld* World);

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
