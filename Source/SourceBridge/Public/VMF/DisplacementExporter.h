#pragma once

#include "CoreMinimal.h"
#include "VMF/VMFKeyValues.h"

class ALandscapeProxy;
class ULandscapeComponent;

/**
 * Settings for displacement export.
 */
struct FDisplacementSettings
{
	/** Power level: 2 (5x5=25 verts), 3 (9x9=81 verts), 4 (17x17=289 verts) */
	int32 Power = 3;

	/** Material to apply to the displacement base brush face */
	FString Material = TEXT("nature/blendgrassgravel001a");

	/** Elevation scale multiplier */
	float ElevationScale = 1.0f;

	/** Subdivision of landscape into this many displacements per component */
	int32 SubdivisionsPerComponent = 1;
};

/**
 * A single displacement surface ready for VMF serialization.
 */
struct FDisplacementData
{
	/** The base brush solid that the displacement is painted on (top face) */
	FVMFKeyValues BrushSolid;

	/** The dispinfo block to attach to the top face's side */
	FVMFKeyValues DispInfo;

	/** Displacement power level */
	int32 Power = 3;

	/** Grid dimensions (2^Power + 1) */
	int32 GridSize = 9;
};

/**
 * Exports UE landscape data to Source engine displacement surfaces.
 *
 * Source displacements are painted onto a brush face:
 * - A flat brush is created as the base
 * - The top face gets a "dispinfo" block with vertex offsets
 * - Each vertex stores: distance from base face, normal direction, alpha blend
 *
 * Power levels:
 *   2 = 5x5 vertices (25 total)
 *   3 = 9x9 vertices (81 total)
 *   4 = 17x17 vertices (289 total)
 *
 * VMF format:
 *   side { ...
 *     dispinfo {
 *       "power" "3"
 *       "startposition" "[x y z]"
 *       "elevation" "0"
 *       "subdiv" "0"
 *       normals { "row0" "..." ... }
 *       distances { "row0" "..." ... }
 *       offsets { "row0" "..." ... }
 *       offset_normals { "row0" "..." ... }
 *       alphas { "row0" "..." ... }
 *     }
 *   }
 */
class SOURCEBRIDGE_API FDisplacementExporter
{
public:
	/**
	 * Export all landscape components from a world to displacement data.
	 * Each landscape component becomes one or more displacement surfaces.
	 */
	static TArray<FDisplacementData> ExportLandscapes(
		UWorld* World,
		int32& SolidIdCounter,
		int32& SideIdCounter,
		const FDisplacementSettings& Settings = FDisplacementSettings());

	/**
	 * Export a single landscape component to displacement data.
	 */
	static TArray<FDisplacementData> ExportLandscapeComponent(
		ULandscapeComponent* Component,
		int32& SolidIdCounter,
		int32& SideIdCounter,
		const FDisplacementSettings& Settings);

	/**
	 * Build a dispinfo block from heightmap data.
	 * @param Heights 2D grid of heights (GridSize x GridSize)
	 * @param BaseHeight Height of the base brush face
	 * @param StartPos Start position of the displacement in Source coords
	 * @param Power Displacement power level
	 */
	static FVMFKeyValues BuildDispInfo(
		const TArray<TArray<float>>& Heights,
		float BaseHeight,
		const FVector& StartPos,
		int32 Power);

	/**
	 * Build the base brush solid for a displacement.
	 * Creates a thin flat brush with the top face ready for displacement.
	 */
	static FVMFKeyValues BuildDisplacementBrush(
		int32 SolidId,
		int32& SideIdCounter,
		const FVector& Min,
		const FVector& Max,
		const FString& Material,
		float BaseHeight);

private:
	/** Convert UE landscape heights to Source displacement distances. */
	static void SampleLandscapeHeights(
		ULandscapeComponent* Component,
		int32 GridSize,
		TArray<TArray<float>>& OutHeights,
		float& OutMinHeight,
		float& OutMaxHeight);
};
