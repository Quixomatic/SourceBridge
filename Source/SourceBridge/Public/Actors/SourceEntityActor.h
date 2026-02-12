#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "SourceEntityActor.generated.h"

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

	/** Source engine classname (e.g., "light", "info_player_terrorist"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Entity")
	FString SourceClassname;

	/** Source engine targetname for I/O connections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Entity")
	FString TargetName;

	/** Additional key-values exported to VMF. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Entity")
	TMap<FString, FString> KeyValues;

	/** Spawnflags bitmask. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Entity")
	int32 SpawnFlags = 0;
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

	/** Static mesh component for displaying imported model geometry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source Prop")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** Set the static mesh on this prop (called by ModelImporter). */
	void SetStaticMesh(UStaticMesh* Mesh);
};

/**
 * Source engine func_detail/func_wall/func_door brush entity.
 */
UCLASS(Blueprintable, ClassGroup = "SourceBridge", meta = (DisplayName = "Source Func Brush"))
class SOURCEBRIDGE_API ASourceFuncBrush : public ASourceEntityActor
{
	GENERATED_BODY()

public:
	ASourceFuncBrush();
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
