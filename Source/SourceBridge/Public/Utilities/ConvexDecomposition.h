#pragma once

#include "CoreMinimal.h"

/**
 * A single convex hull from decomposition.
 */
struct FConvexHull
{
	TArray<FVector> Vertices;
	TArray<int32> Indices; // Triangulated face indices (groups of 3)
};

/**
 * Settings for convex decomposition.
 */
struct FConvexDecompositionSettings
{
	/** Maximum number of convex hulls to generate. */
	int32 MaxHulls = 8;

	/** Maximum vertices per hull. */
	int32 MaxVerticesPerHull = 32;

	/** Concavity threshold (0-1). Lower = more hulls, better fit. */
	float ConcavityThreshold = 0.05f;

	/** Minimum hull volume (Source units cubed). Hulls smaller than this are discarded. */
	float MinHullVolume = 1.0f;
};

/**
 * Simple convex decomposition for converting meshes into convex pieces.
 *
 * Used for:
 * 1. Physics collision mesh export (UE complex collision -> multiple convex hulls for SMD)
 * 2. Static mesh -> brush solid conversion (each hull becomes a Source brush)
 *
 * Uses a plane-splitting approach: iteratively split the mesh along
 * optimal cutting planes until each piece is sufficiently convex.
 */
class SOURCEBRIDGE_API FConvexDecomposition
{
public:
	/**
	 * Decompose a triangle mesh into convex hulls.
	 * @param Vertices Mesh vertex positions
	 * @param Indices Triangle indices (groups of 3)
	 * @param Settings Decomposition parameters
	 * @return Array of convex hulls
	 */
	static TArray<FConvexHull> Decompose(
		const TArray<FVector>& Vertices,
		const TArray<int32>& Indices,
		const FConvexDecompositionSettings& Settings = FConvexDecompositionSettings());

	/**
	 * Compute the convex hull of a point set.
	 * Uses iterative convex hull (gift wrapping) approach.
	 * @param Points Input point cloud
	 * @return Convex hull with vertices and triangulated faces
	 */
	static FConvexHull ComputeConvexHull(const TArray<FVector>& Points);

	/**
	 * Measure the concavity of a mesh relative to its convex hull.
	 * Returns a value 0-1 where 0 is perfectly convex.
	 */
	static float MeasureConcavity(
		const TArray<FVector>& Vertices,
		const TArray<int32>& Indices);

private:
	/**
	 * Find the best cutting plane that minimizes the total concavity of both halves.
	 */
	static FPlane FindBestCuttingPlane(
		const TArray<FVector>& Vertices,
		const TArray<int32>& Indices);

	/**
	 * Split a mesh along a plane into two halves.
	 */
	static void SplitMeshByPlane(
		const TArray<FVector>& Vertices,
		const TArray<int32>& Indices,
		const FPlane& Plane,
		TArray<FVector>& OutVerticesA,
		TArray<int32>& OutIndicesA,
		TArray<FVector>& OutVerticesB,
		TArray<int32>& OutIndicesB);
};
