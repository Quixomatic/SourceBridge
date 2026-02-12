#include "Runtime/SourceBridgeGameMode.h"
#include "Actors/SourceEntityActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/InputSettings.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Brush.h"
#include "EngineUtils.h"

// ---- GameMode ----

ASourceBridgeGameMode::ASourceBridgeGameMode()
{
	DefaultPawnClass = ASourceBridgePawn::StaticClass();

	// No HUD or spectator needed for basic testing
	HUDClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
}

void ASourceBridgeGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World) return;

	// Ensure WASD/mouse input mappings exist (blank projects may not have them)
	UInputSettings* InputSettings = UInputSettings::GetInputSettings();
	if (InputSettings)
	{
		auto EnsureAxis = [&](const FName& AxisName, const FKey& Key, float Scale)
		{
			for (const FInputAxisKeyMapping& M : InputSettings->GetAxisMappings())
			{
				if (M.AxisName == AxisName && M.Key == Key) return;
			}
			InputSettings->AddAxisMapping(FInputAxisKeyMapping(AxisName, Key, Scale), false);
		};
		auto EnsureAction = [&](const FName& ActionName, const FKey& Key)
		{
			for (const FInputActionKeyMapping& M : InputSettings->GetActionMappings())
			{
				if (M.ActionName == ActionName && M.Key == Key) return;
			}
			InputSettings->AddActionMapping(FInputActionKeyMapping(ActionName, Key), false);
		};

		EnsureAxis(TEXT("MoveForward"), EKeys::W, 1.0f);
		EnsureAxis(TEXT("MoveForward"), EKeys::S, -1.0f);
		EnsureAxis(TEXT("MoveRight"), EKeys::D, 1.0f);
		EnsureAxis(TEXT("MoveRight"), EKeys::A, -1.0f);
		EnsureAxis(TEXT("Turn"), EKeys::MouseX, 1.0f);
		EnsureAxis(TEXT("LookUp"), EKeys::MouseY, -1.0f);
		EnsureAxis(TEXT("MoveUp"), EKeys::SpaceBar, 1.0f);
		EnsureAxis(TEXT("MoveUp"), EKeys::LeftControl, -1.0f);
		EnsureAction(TEXT("Jump"), EKeys::SpaceBar);
		EnsureAction(TEXT("Noclip"), EKeys::V);

		InputSettings->ForceRebuildKeymaps();
	}

	// Ensure worldspawn ABrush actors with ProceduralMeshComponents are visible in PIE
	// and hide TOOLS texture sections (ABrush doesn't have our BeginPlay, so handle it here)
	for (TActorIterator<ABrush> It(World); It; ++It)
	{
		ABrush* Brush = *It;
		if (!Brush) continue;

		TArray<UProceduralMeshComponent*> ProcMeshes;
		Brush->GetComponents<UProceduralMeshComponent>(ProcMeshes);
		if (ProcMeshes.Num() == 0) continue;

		// Unhide the actor if it's hidden in game
		if (Brush->IsHidden())
		{
			Brush->SetActorHiddenInGame(false);
		}

		// Ensure ProceduralMeshComponents are visible and hide TOOLS sections
		for (UProceduralMeshComponent* ProcMesh : ProcMeshes)
		{
			if (!ProcMesh) continue;

			if (!ProcMesh->IsVisible())
			{
				ProcMesh->SetVisibility(true);
			}

			int32 NumSections = ProcMesh->GetNumSections();
			for (int32 i = 0; i < NumSections; i++)
			{
				UMaterialInterface* Mat = ProcMesh->GetMaterial(i);
				if (Mat && Mat->GetName().Contains(TEXT("TOOLS"), ESearchCase::IgnoreCase))
				{
					ProcMesh->SetMeshSectionVisible(i, false);
				}
			}
		}
	}
}

AActor* ASourceBridgeGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	TArray<AActor*> CandidateSpawns;

	// Collect Source spawn entities based on team preference
	for (TActorIterator<ASourceEntityActor> It(World); It; ++It)
	{
		ASourceEntityActor* Entity = *It;
		if (!Entity) continue;

		bool bIsT = Entity->IsA<ASourceTSpawn>() ||
			Entity->SourceClassname.Equals(TEXT("info_player_terrorist"), ESearchCase::IgnoreCase);
		bool bIsCT = Entity->IsA<ASourceCTSpawn>() ||
			Entity->SourceClassname.Equals(TEXT("info_player_counterterrorist"), ESearchCase::IgnoreCase);

		if (!bIsT && !bIsCT) continue;

		// Filter by team preference
		if (PreferredTeam == ESourceSpawnTeam::Terrorist && !bIsT) continue;
		if (PreferredTeam == ESourceSpawnTeam::CounterTerrorist && !bIsCT) continue;

		// Filter by specific targetname if set
		if (!SpecificSpawnName.IsEmpty() && !Entity->TargetName.Equals(SpecificSpawnName, ESearchCase::IgnoreCase))
		{
			continue;
		}

		CandidateSpawns.Add(Entity);
	}

	if (CandidateSpawns.Num() > 0)
	{
		// Pick a random spawn from candidates
		int32 Index = FMath::RandRange(0, CandidateSpawns.Num() - 1);
		AActor* Chosen = CandidateSpawns[Index];
		UE_LOG(LogTemp, Log, TEXT("SourceBridge PIE: Spawning at %s (%s)"),
			*Cast<ASourceEntityActor>(Chosen)->TargetName,
			*Cast<ASourceEntityActor>(Chosen)->SourceClassname);
		return Chosen;
	}

	UE_LOG(LogTemp, Warning, TEXT("SourceBridge PIE: No Source spawn points found, falling back to default"));
	return Super::ChoosePlayerStart_Implementation(Player);
}

// ---- Pawn ----

ASourceBridgePawn::ASourceBridgePawn()
{
	// Source player dimensions: 72 units tall, 32 units wide
	// Conversion: Source units * 0.525 is wrong direction - we're IN UE so use UE scale
	// Source 72 tall = ~137cm in UE, Source 32 wide = ~61cm diameter = ~30.5cm radius
	// Capsule half-height = 68.5cm (with radius of 30.5cm subtracted for hemisphere = ~38cm half-height)
	// Actually: UE capsule half-height includes the hemisphere caps
	// Total height = 137cm, radius = 30.5cm, so half-height = 68.5cm
	GetCapsuleComponent()->SetCapsuleHalfHeight(68.5f);
	GetCapsuleComponent()->SetCapsuleRadius(30.5f);

	// First-person camera at eye level (~64 Source units up = ~121.9cm in UE)
	// Relative to capsule center (which is at half-height = 68.5cm up from feet)
	// Eye offset from center = 121.9 - 68.5 = 53.4cm
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 53.4f));
	FirstPersonCamera->bUsePawnControlRotation = true;

	// Walking movement
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (MoveComp)
	{
		MoveComp->MaxWalkSpeed = 500.0f;	// ~262 Source units/s (normal run speed)
		MoveComp->JumpZVelocity = 350.0f;
		MoveComp->AirControl = 0.3f;
		MoveComp->GravityScale = 1.0f;
		MoveComp->bOrientRotationToMovement = false;
		MoveComp->bUseControllerDesiredRotation = false;
	}

	// Don't rotate character mesh with controller (first-person: only camera rotates pitch)
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// Hide the skeletal mesh (no visible player model in first-person)
	GetMesh()->SetVisibility(false);
}

void ASourceBridgePawn::BeginPlay()
{
	Super::BeginPlay();

	// Ensure mouse look is active
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void ASourceBridgePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// WASD movement
	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ASourceBridgePawn::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &ASourceBridgePawn::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("MoveUp"), this, &ASourceBridgePawn::MoveUp);

	// Mouse look
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &ASourceBridgePawn::Turn);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &ASourceBridgePawn::LookUp);

	// Jump
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &ACharacter::StopJumping);

	// Noclip toggle (V key)
	PlayerInputComponent->BindAction(TEXT("Noclip"), IE_Pressed, this, &ASourceBridgePawn::ToggleNoclip);
}

void ASourceBridgePawn::ToggleNoclip()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp) return;

	bNoclip = !bNoclip;

	if (bNoclip)
	{
		MoveComp->SetMovementMode(MOVE_Flying);
		MoveComp->MaxFlySpeed = NoclipSpeed;
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: noclip ON"));
	}
	else
	{
		MoveComp->SetMovementMode(MOVE_Walking);
		MoveComp->MaxWalkSpeed = WalkSpeed;
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: noclip OFF"));
	}
}

void ASourceBridgePawn::MoveForward(float Value)
{
	if (FMath::Abs(Value) > KINDA_SMALL_NUMBER)
	{
		if (bNoclip)
		{
			// In noclip, move in the direction the camera is looking
			const FVector Direction = GetControlRotation().Vector();
			AddMovementInput(Direction, Value);
		}
		else
		{
			const FRotator YawRotation(0, GetControlRotation().Yaw, 0);
			const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
			AddMovementInput(Direction, Value);
		}
	}
}

void ASourceBridgePawn::MoveRight(float Value)
{
	if (FMath::Abs(Value) > KINDA_SMALL_NUMBER)
	{
		const FRotator YawRotation(0, GetControlRotation().Yaw, 0);
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		AddMovementInput(Direction, Value);
	}
}

void ASourceBridgePawn::MoveUp(float Value)
{
	if (bNoclip && FMath::Abs(Value) > KINDA_SMALL_NUMBER)
	{
		AddMovementInput(FVector::UpVector, Value);
	}
}

void ASourceBridgePawn::Turn(float Value)
{
	AddControllerYawInput(Value);
}

void ASourceBridgePawn::LookUp(float Value)
{
	AddControllerPitchInput(Value);
}
