#include "Actors/SourceEntityActor.h"
#include "Utilities/ToolTextureClassifier.h"
#include "UI/SourceIOVisualizer.h"
#include "Components/BillboardComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"

// ---- Base ----

ASourceEntityActor::ASourceEntityActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

#if WITH_EDITORONLY_DATA
	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (SpriteComponent)
	{
		SpriteComponent->SetupAttachment(RootComponent);
		SpriteComponent->bIsScreenSizeScaled = true;
	}

	// I/O wire visualization (automatically draws connection lines in editor)
	USourceIOVisualizer* IOVis = CreateEditorOnlyDefaultSubobject<USourceIOVisualizer>(TEXT("IOVisualizer"));
	if (IOVis)
	{
		IOVis->SetIsVisualizationComponent(true);
	}
#endif
}

#if WITH_EDITORONLY_DATA
void ASourceEntityActor::UpdateEditorSprite()
{
	if (!SpriteComponent || IsRunningCommandlet())
	{
		return;
	}

	const FString& CN = SourceClassname;
	FString SpritePath;

	// Sound entities
	if (CN.StartsWith(TEXT("ambient_")) || CN == TEXT("env_soundscape"))
	{
		SpritePath = TEXT("/Engine/EditorResources/AudioIcons/S_AudioComponent.S_AudioComponent");
	}
	// Trigger entities
	else if (CN.StartsWith(TEXT("trigger_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_Trigger.S_Trigger");
	}
	// Spot lights
	else if (CN == TEXT("light_spot"))
	{
		SpritePath = TEXT("/Engine/EditorResources/LightIcons/S_LightSpot.S_LightSpot");
	}
	// All other lights
	else if (CN.StartsWith(TEXT("light")))
	{
		SpritePath = TEXT("/Engine/EditorResources/LightIcons/S_LightPoint.S_LightPoint");
	}
	// Props
	else if (CN.StartsWith(TEXT("prop_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_Actor.S_Actor");
	}
	// Player spawns
	else if (CN.StartsWith(TEXT("info_player")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_Player.S_Player");
	}
	// Cameras
	else if (CN == TEXT("point_viewcontrol") || CN.StartsWith(TEXT("point_camera")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_Camera.S_Camera");
	}
	// Environment effects (but not soundscapes, handled above)
	else if (CN.StartsWith(TEXT("env_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_Emitter.S_Emitter");
	}
	// Logic entities
	else if (CN.StartsWith(TEXT("logic_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_TargetPoint.S_TargetPoint");
	}
	// Brush entities (func_detail, func_door, func_wall, etc.)
	else if (CN.StartsWith(TEXT("func_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_Trigger.S_Trigger");
	}
	// Info entities (info_target, info_landmark, etc.)
	else if (CN.StartsWith(TEXT("info_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_TargetPoint.S_TargetPoint");
	}
	// Game text entity
	else if (CN == TEXT("game_text"))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_TextRenderActorIcon.S_TextRenderActorIcon");
	}
	// Other game entities (game_end, etc.)
	else if (CN.StartsWith(TEXT("game_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_TargetPoint.S_TargetPoint");
	}
	// Point entities (point_template, point_spotlight, etc.)
	else if (CN.StartsWith(TEXT("point_")))
	{
		SpritePath = TEXT("/Engine/EditorResources/S_TargetPoint.S_TargetPoint");
	}
	else
	{
		return; // Keep default UE sprite
	}

	UTexture2D* Sprite = LoadObject<UTexture2D>(nullptr, *SpritePath);
	if (Sprite)
	{
		SpriteComponent->SetSprite(Sprite);
	}
}
#endif

// ---- BeginPlay (PIE Runtime) ----

void ASourceEntityActor::BeginPlay()
{
	Super::BeginPlay();

#if WITH_EDITORONLY_DATA
	// Hide editor-only visualization in PIE
	if (SpriteComponent)
	{
		SpriteComponent->SetVisibility(false);
	}

	// Disable I/O visualizer wire drawing
	if (USourceIOVisualizer* IOVis = FindComponentByClass<USourceIOVisualizer>())
	{
		IOVis->bDrawWires = false;
	}

	// Hide arrow components (spawn direction indicators, etc.)
	TArray<UArrowComponent*> Arrows;
	GetComponents<UArrowComponent>(Arrows);
	for (UArrowComponent* Arrow : Arrows)
	{
		Arrow->SetVisibility(false);
	}

	// Hide capsule visualization (spawn point bounds)
	TArray<UCapsuleComponent*> Capsules;
	GetComponents<UCapsuleComponent>(Capsules);
	for (UCapsuleComponent* Capsule : Capsules)
	{
		Capsule->SetVisibility(false);
	}
#endif
}

// ---- T Spawn ----

ASourceTSpawn::ASourceTSpawn()
{
	SourceClassname = TEXT("info_player_terrorist");

#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
	// Red capsule showing player bounds (72 units tall, 16 radius in Source = ~137cm tall, ~30cm radius in UE)
	UCapsuleComponent* Capsule = CreateEditorOnlyDefaultSubobject<UCapsuleComponent>(TEXT("PlayerBounds"));
	if (Capsule)
	{
		Capsule->SetupAttachment(RootComponent);
		Capsule->SetCapsuleHalfHeight(68.5f); // ~137cm / 2
		Capsule->SetCapsuleRadius(30.0f);
		Capsule->SetRelativeLocation(FVector(0, 0, 68.5f));
		Capsule->ShapeColor = FColor::Red;
		Capsule->SetHiddenInGame(true);
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Red arrow showing facing direction
	UArrowComponent* Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	if (Arrow)
	{
		Arrow->SetupAttachment(RootComponent);
		Arrow->ArrowColor = FColor::Red;
		Arrow->ArrowSize = 1.5f;
		Arrow->ArrowLength = 80.0f;
		Arrow->SetRelativeLocation(FVector(0, 0, 68.5f));
		Arrow->SetHiddenInGame(true);
	}
#endif
}

// ---- CT Spawn ----

ASourceCTSpawn::ASourceCTSpawn()
{
	SourceClassname = TEXT("info_player_counterterrorist");

#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
	// Blue capsule showing player bounds
	UCapsuleComponent* Capsule = CreateEditorOnlyDefaultSubobject<UCapsuleComponent>(TEXT("PlayerBounds"));
	if (Capsule)
	{
		Capsule->SetupAttachment(RootComponent);
		Capsule->SetCapsuleHalfHeight(68.5f);
		Capsule->SetCapsuleRadius(30.0f);
		Capsule->SetRelativeLocation(FVector(0, 0, 68.5f));
		Capsule->ShapeColor = FColor::Blue;
		Capsule->SetHiddenInGame(true);
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Blue arrow showing facing direction
	UArrowComponent* Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	if (Arrow)
	{
		Arrow->SetupAttachment(RootComponent);
		Arrow->ArrowColor = FColor::Blue;
		Arrow->ArrowSize = 1.5f;
		Arrow->ArrowLength = 80.0f;
		Arrow->SetRelativeLocation(FVector(0, 0, 68.5f));
		Arrow->SetHiddenInGame(true);
	}
#endif
}

// ---- Trigger ----

ASourceTrigger::ASourceTrigger()
{
	SourceClassname = TEXT("trigger_multiple");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif
}

// ---- Light ----

ASourceLight::ASourceLight()
{
	SourceClassname = TEXT("light");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif
}

// ---- Prop ----

ASourceProp::ASourceProp()
{
	SourceClassname = TEXT("prop_static");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ModelMesh"));
	if (MeshComponent)
	{
		MeshComponent->SetupAttachment(RootComponent);
		MeshComponent->SetMobility(EComponentMobility::Movable);
	}
}

void ASourceProp::SetStaticMesh(UStaticMesh* Mesh)
{
	if (MeshComponent && Mesh)
	{
		MeshComponent->SetStaticMesh(Mesh);
	}
}

// ---- Brush Entity ----

ASourceBrushEntity::ASourceBrushEntity()
{
	SourceClassname = TEXT("func_detail");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif
}

UProceduralMeshComponent* ASourceBrushEntity::AddBrushMesh(const FString& MeshName)
{
	UProceduralMeshComponent* ProcMesh = NewObject<UProceduralMeshComponent>(this, *MeshName);
	if (ProcMesh)
	{
		ProcMesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		ProcMesh->SetRelativeTransform(FTransform::Identity);
		ProcMesh->RegisterComponent();
		BrushMeshes.Add(ProcMesh);
	}
	return ProcMesh;
}

void ASourceBrushEntity::BeginPlay()
{
	Super::BeginPlay();

	const FString& CN = SourceClassname;

	// Trigger entities: invisible, overlap-only collision (walk through, fires events)
	if (CN.StartsWith(TEXT("trigger_")))
	{
		for (UProceduralMeshComponent* Mesh : BrushMeshes)
		{
			if (Mesh)
			{
				Mesh->SetVisibility(false);
				Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Mesh->SetCollisionResponseToAllChannels(ECR_Overlap);
			}
		}
	}
	// Clip entities: invisible, blocks player movement
	else if (CN.Equals(TEXT("func_clip_vphysics"), ESearchCase::IgnoreCase) ||
			 CN.Equals(TEXT("func_clip"), ESearchCase::IgnoreCase))
	{
		for (UProceduralMeshComponent* Mesh : BrushMeshes)
		{
			if (Mesh)
			{
				Mesh->SetVisibility(false);
				Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Mesh->SetCollisionResponseToAllChannels(ECR_Block);
			}
		}
	}
	// Illusionary: visible but no collision (walk through)
	else if (CN.Equals(TEXT("func_illusionary"), ESearchCase::IgnoreCase))
	{
		for (UProceduralMeshComponent* Mesh : BrushMeshes)
		{
			if (Mesh)
			{
				Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}
	// func_wall: check rendermode keyvalue — if non-zero, it's invisible at runtime
	else if (CN.Equals(TEXT("func_wall"), ESearchCase::IgnoreCase) ||
			 CN.Equals(TEXT("func_wall_toggle"), ESearchCase::IgnoreCase))
	{
		const FString* RenderMode = KeyValues.Find(TEXT("rendermode"));
		if (RenderMode && FCString::Atoi(**RenderMode) != 0)
		{
			// Non-default rendermode — entity may be invisible or translucent
			int32 Mode = FCString::Atoi(**RenderMode);
			if (Mode == 10) // Don't render
			{
				for (UProceduralMeshComponent* Mesh : BrushMeshes)
				{
					if (Mesh) Mesh->SetVisibility(false);
				}
			}
		}
	}
	// func_detail, func_breakable, func_brush, etc.: normal (visible + solid)
	// No changes needed — default behavior is correct
}

// ---- Env Sprite ----

ASourceEnvSprite::ASourceEnvSprite()
{
	SourceClassname = TEXT("env_sprite");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif
}

// ---- Soundscape ----

ASourceSoundscape::ASourceSoundscape()
{
	SourceClassname = TEXT("env_soundscape");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif
}

// ---- Spectator Spawn ----

ASourceSpectatorSpawn::ASourceSpectatorSpawn()
{
	SourceClassname = TEXT("info_player_spectator");

#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
	UArrowComponent* Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	if (Arrow)
	{
		Arrow->SetupAttachment(RootComponent);
		Arrow->ArrowColor = FColor::Yellow;
		Arrow->ArrowSize = 1.5f;
		Arrow->ArrowLength = 80.0f;
		Arrow->SetHiddenInGame(true);
	}
#endif
}

// ---- Goal Trigger (Soccer) ----

ASourceGoalTrigger::ASourceGoalTrigger()
{
	SourceClassname = TEXT("trigger_multiple");
#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
#endif
}

// ---- Ball Spawn (Soccer) ----

ASourceBallSpawn::ASourceBallSpawn()
{
	SourceClassname = TEXT("info_target");

#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
	UArrowComponent* Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("SpawnArrow"));
	if (Arrow)
	{
		Arrow->SetupAttachment(RootComponent);
		Arrow->ArrowColor = FColor::Green;
		Arrow->ArrowSize = 1.0f;
		Arrow->ArrowLength = 60.0f;
		Arrow->SetRelativeRotation(FRotator(-90, 0, 0)); // Point up
		Arrow->SetHiddenInGame(true);
	}
#endif
}

// ---- Spectator Camera ----

ASourceSpectatorCamera::ASourceSpectatorCamera()
{
	SourceClassname = TEXT("point_viewcontrol");

#if WITH_EDITORONLY_DATA
	UpdateEditorSprite();
	UArrowComponent* Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("CameraDir"));
	if (Arrow)
	{
		Arrow->SetupAttachment(RootComponent);
		Arrow->ArrowColor = FColor::Cyan;
		Arrow->ArrowSize = 2.0f;
		Arrow->ArrowLength = 100.0f;
		Arrow->SetHiddenInGame(true);
	}
#endif
}
