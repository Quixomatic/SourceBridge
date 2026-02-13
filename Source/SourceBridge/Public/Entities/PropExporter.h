#pragma once

#include "CoreMinimal.h"
#include "VMF/VMFKeyValues.h"

class UWorld;
class AStaticMeshActor;

/**
 * How to export a UE static mesh actor.
 */
enum class EPropExportMode : uint8
{
	/** Export as prop_static (reference to .mdl model file) */
	PropStatic,

	/** Export as prop_dynamic (physics-enabled model) */
	PropDynamic,

	/** Export as prop_physics (moveable physics prop) */
	PropPhysics,

	/** Export as func_detail (non-vis brush entity - only for very simple meshes) */
	FuncDetail
};

/**
 * Settings for prop export.
 */
struct FPropExportSettings
{
	/** Default export mode for static mesh actors */
	EPropExportMode DefaultMode = EPropExportMode::PropStatic;

	/** Model path prefix in Source (e.g., "models/props/") */
	FString ModelPathPrefix = TEXT("models/props/");
};

/**
 * Exports UE static mesh actors as Source prop entities.
 *
 * Static mesh actors in UE become one of:
 * - prop_static: Static model placed in the world (most common)
 * - prop_dynamic: Animated/moveable model
 * - prop_physics: Physics-simulated model
 * - func_detail: Brush entity (only for simple convex meshes)
 *
 * Actor tags control export behavior:
 * - "prop_static", "prop_dynamic", "prop_physics" - override export mode
 * - "mdl:path/to/model" - override model path
 * - "skin:N" - model skin index
 * - "solid:N" - collision type (0=not solid, 2=BSP, 6=VPhysics)
 */
/**
 * Result of attempting to convert a static mesh to brush geometry.
 */
struct FMeshToBrushResult
{
	/** True if the mesh was successfully converted to brush geometry. */
	bool bSuccess = false;

	/** VMF solid nodes for this mesh. */
	TArray<FVMFKeyValues> Solids;

	/** Entity classname (empty = worldspawn, "func_detail" etc.) */
	FString EntityClass;

	/** Warnings generated during conversion. */
	TArray<FString> Warnings;
};

class SOURCEBRIDGE_API FPropExporter
{
public:
	/**
	 * Export all static mesh actors from a world as prop entities.
	 * Actors tagged source:worldspawn or source:func_detail are skipped here
	 * (handled by CollectMeshBrushes instead).
	 */
	static TArray<FVMFKeyValues> ExportProps(
		UWorld* World,
		int32& EntityIdCounter,
		const FPropExportSettings& Settings = FPropExportSettings());

	/**
	 * Export a single static mesh actor as a prop entity.
	 */
	static FVMFKeyValues ExportProp(
		AStaticMeshActor* Actor,
		int32 EntityId,
		const FPropExportSettings& Settings);

	/**
	 * Collect all static mesh actors that should be exported as brush geometry.
	 * Handles source:worldspawn, source:func_detail tags and auto-detection.
	 */
	static TArray<FMeshToBrushResult> CollectMeshBrushes(
		UWorld* World,
		int32& SolidIdCounter,
		int32& SideIdCounter);

	/**
	 * Try to convert a static mesh actor to VMF brush solids.
	 * Returns success if the mesh is convex with reasonable face count.
	 */
	static FMeshToBrushResult ConvertMeshToBrush(
		AStaticMeshActor* Actor,
		int32& SolidIdCounter,
		int32& SideIdCounter,
		const FString& ForcedEntityClass = FString());

	/**
	 * Check if a static mesh actor should be exported as brush geometry.
	 * Returns the entity class ("" for worldspawn, "func_detail" etc.)
	 * or empty optional if it should remain a prop.
	 */
	static TOptional<FString> ShouldConvertToBrush(AStaticMeshActor* Actor);

private:
	/** Determine the export mode from actor tags. */
	static EPropExportMode GetExportMode(AStaticMeshActor* Actor, EPropExportMode Default);

	/** Get the Source model path from actor tags or mesh name. */
	static FString GetModelPath(AStaticMeshActor* Actor, const FString& Prefix);

	/** Get a classname string from export mode. */
	static FString GetClassname(EPropExportMode Mode);

	/**
	 * Extract face planes from a triangle mesh by merging coplanar triangles.
	 * Returns an array of face data: normal + polygon vertices.
	 */
	struct FMeshFace
	{
		FVector Normal;
		TArray<FVector> Vertices; // Polygon vertices in winding order
		FString Material;
	};

	static TArray<FMeshFace> ExtractFaces(
		const TArray<FVector>& Vertices,
		const TArray<uint32>& Indices,
		const TArray<int32>& MaterialIds,
		const TArray<FString>& MaterialNames);

	/** Check if a mesh is convex (all faces' normals point outward consistently). */
	static bool IsMeshConvex(const TArray<FMeshFace>& Faces);
};
