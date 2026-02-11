#pragma once

#include "CoreMinimal.h"
#include "VMF/VMFKeyValues.h"

class ABrush;

/** Result of converting a UE brush to VMF solids. */
struct SOURCEBRIDGE_API FBrushConversionResult
{
	TArray<FVMFKeyValues> Solids;
	TArray<FString> Warnings;
};

/**
 * Converts UE ABrush geometry into VMF solid blocks.
 *
 * Each ABrush contains a UModel with FPoly faces. This class:
 * 1. Extracts polygons from the brush model
 * 2. Transforms vertices from local to world space
 * 3. Converts to Source engine coordinates (scale 0.525, negate Y)
 * 4. Reverses winding order (left-handed -> right-handed)
 * 5. Picks 3 plane-defining points per face
 * 6. Builds VMF side nodes with default UV axes
 */
class SOURCEBRIDGE_API FBrushConverter
{
public:
	/**
	 * Convert a single ABrush actor to VMF solid(s).
	 * Increments SolidIdCounter and SideIdCounter as IDs are consumed.
	 */
	static FBrushConversionResult ConvertBrush(
		ABrush* Brush,
		int32& SolidIdCounter,
		int32& SideIdCounter,
		const FString& DefaultMaterial = TEXT("DEV/DEV_MEASUREWALL01A"));

	/**
	 * Validate that a set of face planes form a convex solid.
	 * For each plane, all vertices of all other faces must be on or behind the plane.
	 * Returns true if convex.
	 */
	static bool ValidateConvexity(
		const TArray<FPlane>& Planes,
		const TArray<TArray<FVector>>& FaceVertices,
		float Tolerance = 1.0f);

private:
	/**
	 * Pick 3 non-collinear points from a vertex array for VMF plane definition.
	 * Returns false if all points are collinear (degenerate face).
	 */
	static bool Pick3PlanePoints(
		const TArray<FVector>& Vertices,
		FVector& OutP1,
		FVector& OutP2,
		FVector& OutP3);

	/** Get default UV axes for a face based on its normal direction. */
	static void GetDefaultUVAxes(
		const FVector& Normal,
		FString& OutUAxis,
		FString& OutVAxis);
};
