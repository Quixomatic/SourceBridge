#include "Actors/SourceEntityActor.h"
#include "UI/SourceIOVisualizer.h"
#include "Components/BillboardComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"

// ---- Base ----

ASourceEntityActor::ASourceEntityActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

#if WITH_EDITORONLY_DATA
	UBillboardComponent* Billboard = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Billboard)
	{
		Billboard->SetupAttachment(RootComponent);
		Billboard->bIsScreenSizeScaled = true;
	}

	// I/O wire visualization (automatically draws connection lines in editor)
	USourceIOVisualizer* IOVis = CreateEditorOnlyDefaultSubobject<USourceIOVisualizer>(TEXT("IOVisualizer"));
	if (IOVis)
	{
		IOVis->SetIsVisualizationComponent(true);
	}
#endif
}

// ---- T Spawn ----

ASourceTSpawn::ASourceTSpawn()
{
	SourceClassname = TEXT("info_player_terrorist");

#if WITH_EDITORONLY_DATA
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
}

// ---- Light ----

ASourceLight::ASourceLight()
{
	SourceClassname = TEXT("light");
}

// ---- Prop ----

ASourceProp::ASourceProp()
{
	SourceClassname = TEXT("prop_static");
}

// ---- Func Brush ----

ASourceFuncBrush::ASourceFuncBrush()
{
	SourceClassname = TEXT("func_detail");
}

// ---- Env Sprite ----

ASourceEnvSprite::ASourceEnvSprite()
{
	SourceClassname = TEXT("env_sprite");
}

// ---- Soundscape ----

ASourceSoundscape::ASourceSoundscape()
{
	SourceClassname = TEXT("env_soundscape");
}

// ---- Spectator Spawn ----

ASourceSpectatorSpawn::ASourceSpectatorSpawn()
{
	SourceClassname = TEXT("info_player_spectator");

#if WITH_EDITORONLY_DATA
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
}

// ---- Ball Spawn (Soccer) ----

ASourceBallSpawn::ASourceBallSpawn()
{
	SourceClassname = TEXT("info_target");

#if WITH_EDITORONLY_DATA
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
