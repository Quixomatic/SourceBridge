#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SourceIOVisualizer.generated.h"

/**
 * Editor-only component that draws I/O connection wires between Source entities.
 *
 * Parses I/O tags on the owning actor (format: io:Output:target,input,param,delay,refire)
 * and draws debug lines to target actors in the editor viewport.
 *
 * Wire colors indicate connection type:
 * - Green: OnStartTouch/OnEndTouch (trigger outputs)
 * - Yellow: OnTrigger/OnPressed (relay/button outputs)
 * - Cyan: General I/O connections
 * - Red: Broken connections (target not found)
 */
UCLASS(ClassGroup = "SourceBridge", meta = (BlueprintSpawnableComponent, DisplayName = "Source I/O Visualizer"))
class SOURCEBRIDGE_API USourceIOVisualizer : public UActorComponent
{
	GENERATED_BODY()

public:
	USourceIOVisualizer();

	/** Whether to draw I/O wires in the editor viewport. */
	UPROPERTY(EditAnywhere, Category = "Source I/O")
	bool bDrawWires = true;

	/** Wire thickness for drawing. */
	UPROPERTY(EditAnywhere, Category = "Source I/O", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float WireThickness = 2.0f;

	/** Parse I/O connections from actor tags. */
	void RefreshConnections();

	/** Cached parsed connections. */
	struct FIOWire
	{
		FString OutputName;
		FString TargetName;
		FString InputName;
		FString Parameter;
		float Delay = 0.0f;
		int32 RefireCount = -1;
		TWeakObjectPtr<AActor> ResolvedTarget;
		bool bBroken = false;
	};

	TArray<FIOWire> CachedWires;

protected:
#if WITH_EDITORONLY_DATA
	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
#endif
};
