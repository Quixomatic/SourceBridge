#pragma once

#include "CoreMinimal.h"

// ============================================================================
// Source Engine MDL/VVD/VTX Binary Format Structures and Reader
// References: Valve SDK public/studio.h, public/optimize.h
// ============================================================================

#define SOURCE_MDL_ID		0x54534449	// "IDST"
#define SOURCE_VVD_ID		0x56534449	// "IDSV"
#define SOURCE_VTX_VERSION	7
#define SOURCE_MAX_NUM_LODS	8
#define SOURCE_MAX_NUM_BONES_PER_VERT 3

// ---- Parsed Result Structures ----

/** A single vertex from a Source model. */
struct FSourceModelVertex
{
	FVector Position;
	FVector Normal;
	FVector2D UV;
	FVector4 Tangent;

	// Bone weights (for skeletal models)
	float BoneWeights[SOURCE_MAX_NUM_BONES_PER_VERT] = { 0, 0, 0 };
	int32 BoneIndices[SOURCE_MAX_NUM_BONES_PER_VERT] = { 0, 0, 0 };
	int32 NumBones = 0;
};

/** A single triangle (3 vertex indices + material index). */
struct FSourceModelTriangle
{
	int32 VertexIndices[3];
	int32 MaterialIndex;
};

/** A mesh section within a body part model. */
struct FSourceModelMesh
{
	int32 MaterialIndex;
	TArray<FSourceModelTriangle> Triangles;
};

/** Bone in the model hierarchy. */
struct FSourceModelBone
{
	FString Name;
	int32 ParentIndex;
	FVector Position;
	FQuat Rotation;
};

/** Complete parsed model data ready for UStaticMesh creation. */
struct FSourceModelData
{
	// Identity
	FString Name;
	int32 Version = 0;
	int32 Checksum = 0;
	int32 Flags = 0;

	// Geometry
	TArray<FSourceModelVertex> Vertices;
	TArray<FSourceModelMesh> Meshes;

	// Materials
	TArray<FString> MaterialNames;
	TArray<FString> MaterialSearchDirs;

	// Skin replacement table: SkinTable[familyIndex][textureSlot] = textureIndex
	TArray<TArray<int16>> SkinFamilies;
	int32 NumSkinReferences = 0;

	// Skeleton
	TArray<FSourceModelBone> Bones;

	// Bounds
	FVector HullMin = FVector::ZeroVector;
	FVector HullMax = FVector::ZeroVector;
	FVector ViewBBMin = FVector::ZeroVector;
	FVector ViewBBMax = FVector::ZeroVector;
	FVector EyePosition = FVector::ZeroVector;
	FVector IllumPosition = FVector::ZeroVector;

	// Physics
	float Mass = 0.0f;
	int32 Contents = 0;
	FString SurfaceProp;

	// LOD info
	int32 NumLODs = 0;
	int32 RootLOD = 0;

	bool bIsStaticProp = false;
	bool bSuccess = false;
	FString ErrorMessage;
};

/**
 * Reads Source engine MDL/VVD/VTX binary model files.
 *
 * Usage:
 *   FSourceModelData Data = FMDLReader::ReadModel(MDLData, VVDData, VTXData);
 *   if (Data.bSuccess) { ... use Data.Vertices, Data.Meshes, etc. }
 */
class SOURCEBRIDGE_API FMDLReader
{
public:
	/**
	 * Parse a complete Source model from its component file data.
	 * @param MDLData Raw bytes of the .mdl file
	 * @param VVDData Raw bytes of the .vvd file
	 * @param VTXData Raw bytes of the .vtx file (any variant: .dx90.vtx, .dx80.vtx, .sw.vtx)
	 * @param RequestedLOD Which LOD to extract geometry for (0 = highest detail)
	 */
	static FSourceModelData ReadModel(
		const TArray<uint8>& MDLData,
		const TArray<uint8>& VVDData,
		const TArray<uint8>& VTXData,
		int32 RequestedLOD = 0);

private:
	// ---- MDL Parsing ----
	static bool ParseMDLHeader(const uint8* Data, int32 DataSize, FSourceModelData& OutModel);
	static bool ParseMDLMaterials(const uint8* Data, int32 DataSize, FSourceModelData& OutModel);
	static bool ParseMDLTextureDirs(const uint8* Data, int32 DataSize, FSourceModelData& OutModel);
	static bool ParseMDLSkinTable(const uint8* Data, int32 DataSize, FSourceModelData& OutModel);
	static bool ParseMDLBones(const uint8* Data, int32 DataSize, FSourceModelData& OutModel);
	static bool ParseMDLSurfaceProp(const uint8* Data, int32 DataSize, FSourceModelData& OutModel);

	// ---- VVD Parsing ----
	static bool ParseVVD(const uint8* Data, int32 DataSize, int32 ExpectedChecksum,
		int32 RequestedLOD, FSourceModelData& OutModel);

	// ---- VTX Parsing ----
	static bool ParseVTX(const uint8* Data, int32 DataSize, int32 MDLVersion, int32 ExpectedChecksum,
		const uint8* MDLData, int32 MDLDataSize,
		int32 RequestedLOD, FSourceModelData& OutModel);

	// ---- Helpers ----
	static FString ReadNullTermString(const uint8* Data, int32 DataSize, int32 Offset);

	template<typename T>
	static T ReadValue(const uint8* Data, int32 Offset)
	{
		T Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(T));
		return Value;
	}
};
