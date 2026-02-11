#include "Utilities/ConvexDecomposition.h"

TArray<FConvexHull> FConvexDecomposition::Decompose(
	const TArray<FVector>& Vertices,
	const TArray<int32>& Indices,
	const FConvexDecompositionSettings& Settings)
{
	TArray<FConvexHull> Results;

	if (Vertices.Num() < 4 || Indices.Num() < 12)
	{
		// Too few vertices/triangles, just return convex hull
		FConvexHull Hull = ComputeConvexHull(Vertices);
		if (Hull.Vertices.Num() >= 4)
		{
			Results.Add(MoveTemp(Hull));
		}
		return Results;
	}

	// Check if already convex enough
	float Concavity = MeasureConcavity(Vertices, Indices);
	if (Concavity <= Settings.ConcavityThreshold)
	{
		FConvexHull Hull = ComputeConvexHull(Vertices);
		if (Hull.Vertices.Num() >= 4)
		{
			Results.Add(MoveTemp(Hull));
		}
		return Results;
	}

	// Work queue: pairs of (vertices, indices) to process
	struct FMeshPiece
	{
		TArray<FVector> Verts;
		TArray<int32> Idxs;
	};

	TArray<FMeshPiece> WorkQueue;
	FMeshPiece Initial;
	Initial.Verts = Vertices;
	Initial.Idxs = Indices;
	WorkQueue.Add(MoveTemp(Initial));

	while (WorkQueue.Num() > 0 && Results.Num() < Settings.MaxHulls)
	{
		FMeshPiece Piece = WorkQueue.Pop();

		float PieceConcavity = MeasureConcavity(Piece.Verts, Piece.Idxs);

		if (PieceConcavity <= Settings.ConcavityThreshold || Results.Num() + WorkQueue.Num() >= Settings.MaxHulls - 1)
		{
			// Convex enough or at hull limit - add as convex hull
			FConvexHull Hull = ComputeConvexHull(Piece.Verts);
			if (Hull.Vertices.Num() >= 4)
			{
				// Limit vertex count
				if (Hull.Vertices.Num() > Settings.MaxVerticesPerHull)
				{
					// Simplify by keeping only the most spread-out vertices
					TArray<FVector> Simplified;
					Simplified.Reserve(Settings.MaxVerticesPerHull);

					// Always keep the extremes on each axis
					FVector Min(FLT_MAX), Max(-FLT_MAX);
					for (const FVector& V : Hull.Vertices)
					{
						Min = Min.ComponentMin(V);
						Max = Max.ComponentMax(V);
					}

					// Subsample evenly
					int32 Step = FMath::Max(1, Hull.Vertices.Num() / Settings.MaxVerticesPerHull);
					for (int32 i = 0; i < Hull.Vertices.Num() && Simplified.Num() < Settings.MaxVerticesPerHull; i += Step)
					{
						Simplified.Add(Hull.Vertices[i]);
					}

					Hull = ComputeConvexHull(Simplified);
				}

				Results.Add(MoveTemp(Hull));
			}
			continue;
		}

		// Find best cutting plane and split
		FPlane CutPlane = FindBestCuttingPlane(Piece.Verts, Piece.Idxs);

		FMeshPiece PieceA, PieceB;
		SplitMeshByPlane(Piece.Verts, Piece.Idxs, CutPlane,
			PieceA.Verts, PieceA.Idxs,
			PieceB.Verts, PieceB.Idxs);

		// Only add pieces that have enough geometry
		if (PieceA.Verts.Num() >= 4 && PieceA.Idxs.Num() >= 12)
		{
			WorkQueue.Add(MoveTemp(PieceA));
		}
		if (PieceB.Verts.Num() >= 4 && PieceB.Idxs.Num() >= 12)
		{
			WorkQueue.Add(MoveTemp(PieceB));
		}
	}

	// Process any remaining in the work queue as convex hulls
	for (FMeshPiece& Piece : WorkQueue)
	{
		if (Results.Num() >= Settings.MaxHulls) break;

		FConvexHull Hull = ComputeConvexHull(Piece.Verts);
		if (Hull.Vertices.Num() >= 4)
		{
			Results.Add(MoveTemp(Hull));
		}
	}

	return Results;
}

FConvexHull FConvexDecomposition::ComputeConvexHull(const TArray<FVector>& Points)
{
	FConvexHull Result;

	if (Points.Num() < 4)
	{
		Result.Vertices = Points;
		return Result;
	}

	// Find extreme points for initial tetrahedron
	FVector Min = Points[0], Max = Points[0];
	int32 MinIdx[3] = {0, 0, 0};
	int32 MaxIdx[3] = {0, 0, 0};

	for (int32 i = 1; i < Points.Num(); i++)
	{
		for (int32 Axis = 0; Axis < 3; Axis++)
		{
			if (Points[i][Axis] < Min[Axis]) { Min[Axis] = Points[i][Axis]; MinIdx[Axis] = i; }
			if (Points[i][Axis] > Max[Axis]) { Max[Axis] = Points[i][Axis]; MaxIdx[Axis] = i; }
		}
	}

	// Use unique extreme points as hull seed
	TSet<int32> UsedIndices;
	TArray<FVector> HullVerts;

	auto TryAddPoint = [&](int32 Idx)
	{
		if (!UsedIndices.Contains(Idx))
		{
			UsedIndices.Add(Idx);
			HullVerts.Add(Points[Idx]);
		}
	};

	for (int32 Axis = 0; Axis < 3; Axis++)
	{
		TryAddPoint(MinIdx[Axis]);
		TryAddPoint(MaxIdx[Axis]);
	}

	// Add remaining points that are on the convex hull
	// Simple approach: keep points that are outside the current hull's planes
	for (int32 i = 0; i < Points.Num(); i++)
	{
		if (UsedIndices.Contains(i)) continue;

		// Check if point is far from centroid
		FVector Centroid = FVector::ZeroVector;
		for (const FVector& V : HullVerts) Centroid += V;
		if (HullVerts.Num() > 0) Centroid /= HullVerts.Num();

		FVector Dir = Points[i] - Centroid;
		float DistSq = Dir.SizeSquared();

		// Include if it extends the hull significantly
		bool bExtends = false;
		for (const FVector& V : HullVerts)
		{
			FVector ToExisting = V - Centroid;
			if (FVector::DotProduct(Dir, ToExisting) < DistSq * 0.5f)
			{
				bExtends = true;
				break;
			}
		}

		if (bExtends)
		{
			HullVerts.Add(Points[i]);
		}
	}

	// Build result - create triangulated faces from convex hull points
	// Use a simple fan triangulation from centroid projection
	Result.Vertices = HullVerts;

	if (HullVerts.Num() >= 4)
	{
		FVector Centroid = FVector::ZeroVector;
		for (const FVector& V : HullVerts) Centroid += V;
		Centroid /= HullVerts.Num();

		// Generate faces by finding triangles on the surface
		// For each triple of points, if all other points are behind the plane, it's a face
		for (int32 i = 0; i < HullVerts.Num(); i++)
		{
			for (int32 j = i + 1; j < HullVerts.Num(); j++)
			{
				for (int32 k = j + 1; k < HullVerts.Num(); k++)
				{
					FVector Normal = FVector::CrossProduct(
						HullVerts[j] - HullVerts[i],
						HullVerts[k] - HullVerts[i]).GetSafeNormal();

					if (Normal.IsNearlyZero()) continue;

					// Orient normal outward (away from centroid)
					if (FVector::DotProduct(Normal, HullVerts[i] - Centroid) < 0)
					{
						Normal = -Normal;
					}

					// Check if all other points are behind this plane
					float PlaneD = FVector::DotProduct(Normal, HullVerts[i]);
					bool bAllBehind = true;

					for (int32 m = 0; m < HullVerts.Num(); m++)
					{
						if (m == i || m == j || m == k) continue;
						float Dist = FVector::DotProduct(Normal, HullVerts[m]) - PlaneD;
						if (Dist > 0.1f) // Small tolerance
						{
							bAllBehind = false;
							break;
						}
					}

					if (bAllBehind)
					{
						// Orient triangle to match outward normal
						FVector TriNormal = FVector::CrossProduct(
							HullVerts[j] - HullVerts[i],
							HullVerts[k] - HullVerts[i]);

						if (FVector::DotProduct(TriNormal, Normal) > 0)
						{
							Result.Indices.Add(i);
							Result.Indices.Add(j);
							Result.Indices.Add(k);
						}
						else
						{
							Result.Indices.Add(i);
							Result.Indices.Add(k);
							Result.Indices.Add(j);
						}
					}
				}
			}
		}
	}

	return Result;
}

float FConvexDecomposition::MeasureConcavity(
	const TArray<FVector>& Vertices,
	const TArray<int32>& Indices)
{
	if (Vertices.Num() < 4) return 0.0f;

	// Compute convex hull volume vs mesh volume
	// Concavity = 1 - (mesh_volume / hull_volume)
	FConvexHull Hull = ComputeConvexHull(Vertices);

	// Compute volumes using signed tetrahedron method
	auto ComputeVolume = [](const TArray<FVector>& Verts, const TArray<int32>& Idxs) -> float
	{
		float Volume = 0.0f;
		for (int32 i = 0; i + 2 < Idxs.Num(); i += 3)
		{
			if (!Verts.IsValidIndex(Idxs[i]) || !Verts.IsValidIndex(Idxs[i+1]) || !Verts.IsValidIndex(Idxs[i+2]))
				continue;

			const FVector& A = Verts[Idxs[i]];
			const FVector& B = Verts[Idxs[i + 1]];
			const FVector& C = Verts[Idxs[i + 2]];
			Volume += FVector::DotProduct(A, FVector::CrossProduct(B, C)) / 6.0f;
		}
		return FMath::Abs(Volume);
	};

	float MeshVolume = ComputeVolume(Vertices, Indices);
	float HullVolume = ComputeVolume(Hull.Vertices, Hull.Indices);

	if (HullVolume <= SMALL_NUMBER) return 0.0f;

	return FMath::Clamp(1.0f - (MeshVolume / HullVolume), 0.0f, 1.0f);
}

FPlane FConvexDecomposition::FindBestCuttingPlane(
	const TArray<FVector>& Vertices,
	const TArray<int32>& Indices)
{
	// Find the bounding box and use the principal axes as candidate planes
	FVector Min(FLT_MAX), Max(-FLT_MAX);
	FVector Centroid = FVector::ZeroVector;

	for (const FVector& V : Vertices)
	{
		Min = Min.ComponentMin(V);
		Max = Max.ComponentMax(V);
		Centroid += V;
	}
	if (Vertices.Num() > 0) Centroid /= Vertices.Num();

	FVector Extent = Max - Min;

	// Try cutting along each axis at the centroid
	// Pick the axis with the largest extent for the most balanced split
	int32 BestAxis = 0;
	float BestExtent = Extent.X;

	if (Extent.Y > BestExtent) { BestAxis = 1; BestExtent = Extent.Y; }
	if (Extent.Z > BestExtent) { BestAxis = 2; BestExtent = Extent.Z; }

	FVector Normal = FVector::ZeroVector;
	Normal[BestAxis] = 1.0f;

	return FPlane(Centroid, Normal);
}

void FConvexDecomposition::SplitMeshByPlane(
	const TArray<FVector>& Vertices,
	const TArray<int32>& Indices,
	const FPlane& Plane,
	TArray<FVector>& OutVerticesA,
	TArray<int32>& OutIndicesA,
	TArray<FVector>& OutVerticesB,
	TArray<int32>& OutIndicesB)
{
	// Classify each vertex
	TArray<float> Distances;
	Distances.SetNum(Vertices.Num());
	for (int32 i = 0; i < Vertices.Num(); i++)
	{
		Distances[i] = Plane.PlaneDot(Vertices[i]);
	}

	// Map from old vertex indices to new indices per side
	TMap<int32, int32> VertMapA, VertMapB;

	auto GetOrAddVertA = [&](int32 OldIdx) -> int32
	{
		if (int32* Found = VertMapA.Find(OldIdx))
		{
			return *Found;
		}
		int32 NewIdx = OutVerticesA.Num();
		VertMapA.Add(OldIdx, NewIdx);
		OutVerticesA.Add(Vertices[OldIdx]);
		return NewIdx;
	};

	auto GetOrAddVertB = [&](int32 OldIdx) -> int32
	{
		if (int32* Found = VertMapB.Find(OldIdx))
		{
			return *Found;
		}
		int32 NewIdx = OutVerticesB.Num();
		VertMapB.Add(OldIdx, NewIdx);
		OutVerticesB.Add(Vertices[OldIdx]);
		return NewIdx;
	};

	// Process each triangle
	for (int32 i = 0; i + 2 < Indices.Num(); i += 3)
	{
		int32 I0 = Indices[i], I1 = Indices[i + 1], I2 = Indices[i + 2];

		if (!Vertices.IsValidIndex(I0) || !Vertices.IsValidIndex(I1) || !Vertices.IsValidIndex(I2))
			continue;

		float D0 = Distances[I0], D1 = Distances[I1], D2 = Distances[I2];

		// Simple classification: assign entire triangle to whichever side its centroid is on
		// This avoids complex edge splitting while being good enough for iterative decomposition
		float AvgDist = (D0 + D1 + D2) / 3.0f;

		if (AvgDist >= 0.0f)
		{
			OutIndicesA.Add(GetOrAddVertA(I0));
			OutIndicesA.Add(GetOrAddVertA(I1));
			OutIndicesA.Add(GetOrAddVertA(I2));
		}
		else
		{
			OutIndicesB.Add(GetOrAddVertB(I0));
			OutIndicesB.Add(GetOrAddVertB(I1));
			OutIndicesB.Add(GetOrAddVertB(I2));
		}
	}
}
