#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Character.h"
#include "SourceBridgeGameMode.generated.h"

class UCameraComponent;

/**
 * Team preference for PIE spawn point selection.
 */
UENUM(BlueprintType)
enum class ESourceSpawnTeam : uint8
{
	Random		UMETA(DisplayName = "Random"),
	Terrorist	UMETA(DisplayName = "Terrorist (T)"),
	CounterTerrorist UMETA(DisplayName = "Counter-Terrorist (CT)")
};

/**
 * GameMode for testing Source maps in PIE.
 * Spawns the player at Source engine spawn points (ASourceTSpawn / ASourceCTSpawn).
 * Use this as the GameMode Override in World Settings to test your map layout.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Source Bridge Game Mode"))
class SOURCEBRIDGE_API ASourceBridgeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASourceBridgeGameMode();

	/** Which team's spawn points to use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Spawns")
	ESourceSpawnTeam PreferredTeam = ESourceSpawnTeam::Random;

	/** If set, only spawn at the point with this targetname. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Spawns")
	FString SpecificSpawnName;

protected:
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
};

/**
 * First-person walking pawn for testing Source maps in PIE.
 * Collision capsule matches Source engine player dimensions.
 * WASD movement + mouse look, walking with gravity.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Source Bridge Pawn"))
class SOURCEBRIDGE_API ASourceBridgePawn : public ACharacter
{
	GENERATED_BODY()

public:
	ASourceBridgePawn();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void BeginPlay() override;

	/** First-person camera. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

private:
	void MoveForward(float Value);
	void MoveRight(float Value);
	void Turn(float Value);
	void LookUp(float Value);
};
