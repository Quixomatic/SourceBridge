#include "Actors/SourceEntityActor.h"
#include "Components/BillboardComponent.h"

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
#endif
}

// ---- T Spawn ----

ASourceTSpawn::ASourceTSpawn()
{
	SourceClassname = TEXT("info_player_terrorist");
}

// ---- CT Spawn ----

ASourceCTSpawn::ASourceCTSpawn()
{
	SourceClassname = TEXT("info_player_counterterrorist");
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
}

// ---- Spectator Camera ----

ASourceSpectatorCamera::ASourceSpectatorCamera()
{
	SourceClassname = TEXT("point_viewcontrol");
}
