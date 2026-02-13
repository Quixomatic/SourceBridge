#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BillboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "SourceEntityActor.generated.h"

/**
 * Stores per-side (face) data from an imported VMF solid for lossless re-export.
 */
USTRUCT()
struct FImportedSideData
{
	GENERATED_BODY()

	/** Source plane definition points (3 points in Source coordinates). */
	UPROPERTY()
	FVector PlaneP1 = FVector::ZeroVector;

	UPROPERTY()
	FVector PlaneP2 = FVector::ZeroVector;

	UPROPERTY()
	FVector PlaneP3 = FVector::ZeroVector;

	/** Source material path (e.g., "TOOLS/TOOLSNODRAW"). */
	UPROPERTY()
	FString Material;

	/** Texture U axis string (e.g., "[1 0 0 0] 0.25"). */
	UPROPERTY()
	FString UAxisStr;

	/** Texture V axis string (e.g., "[0 -1 0 0] 0.25"). */
	UPROPERTY()
	FString VAxisStr;

	/** Lightmap scale. */
	UPROPERTY()
	int32 LightmapScale = 16;
};

/**
 * Stores per-solid data from an imported VMF brush entity for lossless re-export.
 */
USTRUCT()
struct FImportedBrushData
{
	GENERATED_BODY()

	/** Per-face side data (plane points, material, UV axes). */
	UPROPERTY()
	TArray<FImportedSideData> Sides;

	/** Original VMF solid ID (for reference). */
	UPROPERTY()
	int32 SolidId = 0;
};

/**
 * Base actor class for Source engine entities placed in UE.
 * Exposes Source entity classname, targetname, and key-values as UE properties.
 * All subclasses export to VMF entities with correct coordinate conversion.
 */
UCLASS(Abstract, Blueprintable, ClassGroup = "SourceBridge")
class SOURCEBRIDGE_API ASourceEntityActor : public AActor
{
	GENERATED_BODY()

public:
	ASourceEntityActor();
	virtual void BeginPlay() override;

	/** Source engine classname (e.g., "light", "info_player_terrorist"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Entity")
	FString SourceClassname;

	/** Source engine targetname for I/O connections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Entity")
	FString TargetName;

	/** Source engine parentname for entity parent-child attachment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Entity")
	FString ParentName;

	/** Additional key-values exported to VMF. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Entity")
	TMap<FString, FString> KeyValues;

	/** Spawnflags bitmask. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Entity")
	int32 SpawnFlags = 0;

#if WITH_EDITORONLY_DATA
	/** Update the editor billboard sprite based on SourceClassname prefix. */
	void UpdateEditorSprite();

protected:
	/** Billboard sprite component for editor visualization. */
	UPROPERTY()
	TObjectPtr<UBillboardComponent> SpriteComponent;
#endif
};

/**
 * Generic (non-abstract) Source entity for import.
 * Used when no specific subclass matches the imported classname.
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source Generic Entity"))
class SOURCEBRIDGE_API ASourceGenericEntity : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceGenericEntity() {}
};

/**
 * Terrorist spawn point. Exports as info_player_terrorist.
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source T Spawn"))
class SOURCEBRIDGE_API ASourceTSpawn : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceTSpawn();
};

/**
 * Counter-Terrorist spawn point. Exports as info_player_counterterrorist.
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source CT Spawn"))
class SOURCEBRIDGE_API ASourceCTSpawn : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceCTSpawn();
};

/**
 * Generic Source trigger volume. Exports as trigger_multiple by default.
 * Classname can be overridden to trigger_once, trigger_push, etc.
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source Trigger"))
class SOURCEBRIDGE_API ASourceTrigger : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceTrigger();

	/** Wait time between triggers (seconds). Only for trigger_multiple. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Trigger")
	float WaitTime = 1.0f;
};

/**
 * Source engine point light. Exports as light entity.
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source Light"))
class SOURCEBRIDGE_API ASourceLight : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceLight();

	/** Light color (RGB). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Light")
	FColor LightColor = FColor(255, 255, 255);

	/** Light brightness (1-10000). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Light", meta = (ClampMin = 1, ClampMax = 10000))
	int32 Brightness = 300;

	/** Light style (0 = normal, 1-12 = animated patterns). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Light", meta = (ClampMin = 0, ClampMax = 12))
	int32 Style = 0;
};

/**
 * Source engine prop (prop_static, prop_dynamic, prop_physics).
 * Exports with a model path reference.
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source Prop"))
class SOURCEBRIDGE_API ASourceProp : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceProp();

	/** Source model path (e.g., "models/props/barrel.mdl"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Prop")
	FString ModelPath;

	/** Model skin index. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Prop")
	int32 Skin = 0;

	/** Collision type (0 = not solid, 2 = BSP, 6 = VPhysics). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Prop")
	int32 Solid = 6;

	/** Model scale (from "modelscale" keyvalue). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Prop")
	float ModelScale = 1.0f;

	/** Minimum fade distance (Source units, -1 = use engine default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Prop")
	float FadeMinDist = -1.0f;

	/** Maximum fade distance (Source units, 0 = no fade). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Prop")
	float FadeMaxDist = 0.0f;

	/** Disable shadows for this prop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Prop")
	bool bDisableShadows = false;

	/** Render color tint (from "rendercolor" keyvalue). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Prop")
	FColor RenderColor = FColor(255, 255, 255);

	/** Render alpha (from "renderamt" keyvalue). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Prop")
	int32 RenderAmt = 255;

	/** Surface property from MDL header (for re-export). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source Prop|Model Info")
	FString SurfaceProp;

	/** Whether the original MDL had $staticprop flag (for re-export QC). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source Prop|Model Info")
	bool bIsStaticProp = true;

	/** Mass from MDL header in kg (for re-export QC). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source Prop|Model Info")
	float ModelMass = 0.0f;

	/** Material search dirs from MDL ($cdmaterials, for re-export QC). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source Prop|Model Info")
	TArray<FString> CDMaterials;

	/** Static mesh component for displaying imported model geometry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source Prop")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** Set the static mesh on this prop (called by ModelImporter). */
	void SetStaticMesh(UStaticMesh* Mesh);
};

/**
 * Generic brush entity actor for Source engine brush entities.
 * Owns one or more ProceduralMeshComponent children (one per solid in the entity).
 * Stores original VMF plane/UV/material data for lossless round-trip export.
 * Works with ANY brush entity classname (func_detail, func_clip_vphysics, func_door, etc.).
 * The FGD-driven detail panel auto-generates property UI based on SourceClassname.
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source Brush Entity"))
class SOURCEBRIDGE_API ASourceBrushEntity : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceBrushEntity();
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Brush dimensions in Source units (default 64x64x64). Only used for newly created brush entities. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush Geometry", meta = (DisplayName = "Dimensions (Source Units)"))
	FVector BrushDimensions = FVector(64.0, 64.0, 64.0);

	/** Procedural mesh components for each solid in this brush entity. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source Brush Entity")
	TArray<TObjectPtr<UProceduralMeshComponent>> BrushMeshes;

	/** Original VMF solid data for lossless re-export. One entry per solid. */
	UPROPERTY()
	TArray<FImportedBrushData> StoredBrushData;

	/** Add a ProceduralMeshComponent to this entity (called by VMFImporter). */
	UProceduralMeshComponent* AddBrushMesh(const FString& MeshName);

	/**
	 * Generate default box geometry from BrushDimensions and SourceClassname.
	 * Creates StoredBrushData with 6 faces and builds a ProceduralMeshComponent.
	 * Material is chosen by classname (TOOLSTRIGGER for triggers, TOOLSPLAYERCLIP for clips, etc.)
	 */
	void GenerateDefaultGeometry();

	/**
	 * Rebuild the ProceduralMeshComponent from current BrushDimensions.
	 * Preserves per-face materials if they exist, otherwise uses defaults.
	 */
	void RebuildGeometryFromDimensions();

private:
	/** Get the default Source material path for this entity's classname. */
	FString GetDefaultMaterialForClassname() const;

	/** Whether this entity's geometry was generated (vs imported). Generated geometry rebuilds on dimension change. */
	bool bIsGeneratedGeometry = false;
};

/**
 * Source engine env_sprite (glow effect).
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source Env Sprite"))
class SOURCEBRIDGE_API ASourceEnvSprite : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceEnvSprite();

	/** Sprite model path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Sprite")
	FString SpriteModel = TEXT("sprites/glow01.spr");

	/** Render mode (5 = additive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Sprite")
	int32 RenderMode = 5;

	/** Render color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Sprite")
	FColor RenderColor = FColor(255, 255, 255);

	/** Sprite scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Sprite")
	float SourceSpriteScale = 0.25f;
};

/**
 * Source engine env_soundscape.
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source Soundscape"))
class SOURCEBRIDGE_API ASourceSoundscape : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceSoundscape();

	/** Soundscape name (must match a soundscapes_*.txt entry). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Soundscape")
	FString SoundscapeName;

	/** Radius for soundscape (Source units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Soundscape")
	float Radius = 128.0f;
};

/**
 * Spectator spawn point. Exports as info_player_spectator.
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source Spectator Spawn"))
class SOURCEBRIDGE_API ASourceSpectatorSpawn : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceSpectatorSpawn();
};

/**
 * Soccer goal trigger volume. Exports as trigger_multiple with team-specific I/O.
 * Used for goal detection in soccer game modes.
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source Goal Trigger"))
class SOURCEBRIDGE_API ASourceGoalTrigger : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceGoalTrigger();

	/** Which team's goal this is (the ball entering scores for the other team). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Soccer")
	int32 TeamNumber = 0;

	/** Wait time between trigger activations (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Soccer")
	float WaitTime = 3.0f;
};

/**
 * Ball spawn point. Exports as a point entity for ball spawn location.
 * Used in soccer game modes for initial/respawn ball placement.
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source Ball Spawn"))
class SOURCEBRIDGE_API ASourceBallSpawn : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceBallSpawn();
};

/**
 * Spectator camera (point_viewcontrol). Exports as a camera entity.
 * Used for spectator camera positions (e.g., overhead view of field).
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source Spectator Camera"))
class SOURCEBRIDGE_API ASourceSpectatorCamera : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceSpectatorCamera();

	/** Field of view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Camera", meta = (ClampMin = 10, ClampMax = 170))
	float FOV = 90.0f;
};
