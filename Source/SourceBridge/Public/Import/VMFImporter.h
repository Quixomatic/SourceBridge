#pragma once

#include "CoreMinimal.h"
#include "VMF/VMFKeyValues.h"

/** Per-face data parsed from a VMF side definition. */
struct FVMFSideData
{
	/** Source material path (e.g., "TOOLS/TOOLSNODRAW") */
	FString Material;

	/** Texture U axis direction (Source world space) */
	FVector UAxis = FVector(1, 0, 0);
	float UOffset = 0.0f;
	float UScale = 0.25f;

	/** Texture V axis direction (Source world space) */
	FVector VAxis = FVector(0, -1, 0);
	float VOffset = 0.0f;
	float VScale = 0.25f;

	/** Lightmap scale */
	int32 LightmapScale = 16;
};

struct FVMFImportSettings
{
	/** Scale multiplier (default: 1/0.525 = ~1.905 to reverse Source→UE) */
	float ScaleMultiplier = 1.0f / 0.525f;

	/** Whether to import entities */
	bool bImportEntities = true;

	/** Whether to import brush geometry */
	bool bImportBrushes = true;

	/** Whether to apply material names to brush faces */
	bool bImportMaterials = true;

	/** Directory containing extracted assets (VMT/VTF files from BSP pakfile) */
	FString AssetSearchPath;
};

struct FVMFImportResult
{
	int32 BrushesImported = 0;
	int32 EntitiesImported = 0;
	TArray<FString> Warnings;
};

/**
 * Imports a parsed VMF into the current UE level.
 * Creates ABrush actors for solids and spawns entities.
 */
class SOURCEBRIDGE_API FVMFImporter
{
public:
	/** Import a VMF file into the given world. */
	static FVMFImportResult ImportFile(const FString& FilePath, UWorld* World,
		const FVMFImportSettings& Settings = FVMFImportSettings());

	/** Import parsed VMF blocks into the given world. */
	static FVMFImportResult ImportBlocks(const TArray<FVMFKeyValues>& Blocks, UWorld* World,
		const FVMFImportSettings& Settings = FVMFImportSettings());

private:
	/** Convert Source coordinates back to UE coordinates. */
	static FVector SourceToUE(const FVector& SourcePos, float Scale);

	/** Convert a Source-space direction to UE-space (negate Y, no scaling). */
	static FVector SourceDirToUE(const FVector& SourceDir);

	/** Parse a plane string "(x1 y1 z1) (x2 y2 z2) (x3 y3 z3)" into 3 points. */
	static bool ParsePlanePoints(const FString& PlaneStr, FVector& P1, FVector& P2, FVector& P3);

	/** Parse a UV axis string "[x y z offset] scale" */
	static bool ParseUVAxis(const FString& AxisStr, FVector& Axis, float& Offset, float& Scale);

	/** Parse an origin string "x y z" into a vector. */
	static FVector ParseOrigin(const FString& OriginStr);

	/** Parse an angles string "pitch yaw roll" into a rotator. */
	static FRotator ParseAngles(const FString& AnglesStr);

	/**
	 * Reconstruct face polygons from plane definitions using CSG clipping.
	 * Returns arrays of vertex lists (one per face).
	 * OutFaceToPlaneIdx maps each output face to its original plane/side index.
	 */
	static TArray<TArray<FVector>> ReconstructFacesFromPlanes(
		const TArray<FPlane>& Planes, const TArray<FVector>& PlanePoints,
		TArray<int32>& OutFaceToPlaneIdx);

	/** Clip a polygon against a plane, keeping the part on the positive side of the plane normal. */
	static TArray<FVector> ClipPolygonByPlane(const TArray<FVector>& Polygon, const FPlane& Plane);

	/** Create a large initial polygon on the given plane. */
	static TArray<FVector> CreateLargePolygonOnPlane(const FPlane& Plane, const FVector& PointOnPlane);

	/** Create an ABrush actor in the world from face polygons with per-face data. */
	static class ABrush* CreateBrushFromFaces(
		UWorld* World,
		const TArray<TArray<FVector>>& Faces,
		const TArray<FVector>& FaceNormals,
		const TArray<FVMFSideData>& SideData,
		const TArray<int32>& FaceToSideMapping,
		const FVMFImportSettings& Settings);

	/** Import a worldspawn or entity solid block. Returns the created brush (or null). */
	static class ABrush* ImportSolid(const FVMFKeyValues& SolidBlock, UWorld* World,
		const FVMFImportSettings& Settings, FVMFImportResult& Result);

	/** Import a point entity. */
	static bool ImportPointEntity(const FVMFKeyValues& EntityBlock, UWorld* World,
		const FVMFImportSettings& Settings, FVMFImportResult& Result);
};
