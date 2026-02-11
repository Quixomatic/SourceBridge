#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class USkeletalMesh;

/**
 * A bone weight pair for skeletal mesh vertices.
 */
struct FSMDBoneWeight
{
	int32 BoneIndex = 0;
	float Weight = 1.0f;
};

/**
 * SMD vertex data for export.
 */
struct FSMDVertex
{
	int32 BoneIndex = 0;
	FVector Position;
	FVector Normal;
	FVector2D UV;

	/** Additional bone weights for skeletal meshes (SMD extended format). */
	TArray<FSMDBoneWeight> BoneWeights;
};

/**
 * SMD triangle (3 vertices + material name).
 */
struct FSMDTriangle
{
	FString MaterialName;
	FSMDVertex Vertices[3];
};

/**
 * SMD bone node for skeleton hierarchy.
 */
struct FSMDBone
{
	int32 Index;
	FString Name;
	int32 ParentIndex;
	FVector Position;
	FVector Rotation; // Euler angles in radians
};

/**
 * Bone transform for a single frame of animation.
 */
struct FSMDBoneFrame
{
	FVector Position;
	FVector Rotation; // Euler angles in radians
};

/**
 * A single animation sequence ready for SMD export.
 */
struct FSMDAnimation
{
	FString Name;
	float FrameRate = 30.0f;
	int32 NumFrames = 1;
	/** Per-frame per-bone transforms. Outer = frame, Inner = bone index. */
	TArray<TArray<FSMDBoneFrame>> Frames;
};

/**
 * Result of an SMD export operation.
 */
struct FSMDExportResult
{
	bool bSuccess = false;
	FString ReferenceSMD;  // Reference mesh content
	FString PhysicsSMD;    // Physics collision mesh content (simplified)
	FString IdleSMD;       // Idle animation (static pose)
	TArray<FString> MaterialNames; // All materials used
	TArray<FSMDAnimation> Animations; // Exported animation sequences
	FString ErrorMessage;
};

/**
 * Exports UE meshes to Valve SMD format.
 *
 * SMD format:
 *   version 1
 *   nodes
 *     0 "root" -1
 *   end
 *   skeleton
 *     time 0
 *       0 0.0 0.0 0.0 0.0 0.0 0.0
 *   end
 *   triangles
 *   material_name
 *     0  posX posY posZ  normX normY normZ  u v
 *     0  posX posY posZ  normX normY normZ  u v
 *     0  posX posY posZ  normX normY normZ  u v
 *   end
 */
class SOURCEBRIDGE_API FSMDExporter
{
public:
	/**
	 * Export a UStaticMesh to SMD format.
	 * Generates reference mesh, physics mesh, and idle animation SMDs.
	 * LOD 0 is used for the reference mesh.
	 */
	static FSMDExportResult ExportStaticMesh(UStaticMesh* Mesh, float Scale = 0.525f);

	/**
	 * Export a USkeletalMesh to SMD format with bone hierarchy and weights.
	 */
	static FSMDExportResult ExportSkeletalMesh(USkeletalMesh* Mesh, float Scale = 0.525f);

	/**
	 * Build SMD text content from triangle data with a single root bone.
	 */
	static FString BuildSMD(const TArray<FSMDTriangle>& Triangles, const TArray<FSMDBone>& Bones);

	/**
	 * Build a minimal idle animation SMD (single frame, static pose).
	 */
	static FString BuildIdleSMD(const TArray<FSMDBone>& Bones);

	/**
	 * Build an animation SMD from bone hierarchy and per-frame transforms.
	 * Animation SMDs have the skeleton section with multiple time frames but no triangles.
	 */
	static FString BuildAnimationSMD(const TArray<FSMDBone>& Bones, const FSMDAnimation& Animation);

	/**
	 * Export animations from a USkeletalMesh's associated AnimSequences.
	 * Extracts bone transforms per frame for each animation.
	 */
	static TArray<FSMDAnimation> ExportAnimations(USkeletalMesh* Mesh, float Scale = 0.525f);

private:
	/** Convert a UE position to Source engine coordinates for models. */
	static FVector ConvertPosition(const FVector& UEPos, float Scale);

	/** Convert a UE normal to Source engine coordinates. */
	static FVector ConvertNormal(const FVector& UENormal);

	/** Get a clean material name from a UE material reference. */
	static FString CleanMaterialName(const FString& MaterialPath);

	/** Extract collision mesh triangles from a UStaticMesh's body setup. */
	static TArray<FSMDTriangle> ExtractCollisionMesh(UStaticMesh* Mesh, float Scale);
};
