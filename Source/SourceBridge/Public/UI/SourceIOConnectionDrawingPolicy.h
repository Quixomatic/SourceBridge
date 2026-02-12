#pragma once

#include "CoreMinimal.h"
#include "ConnectionDrawingPolicy.h"

/**
 * Custom connection drawing policy for the Source I/O Graph.
 * Colors wires by source entity type and shows connection tooltips.
 */
class FSourceIOConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
public:
	FSourceIOConnectionDrawingPolicy(
		int32 InBackLayerID,
		int32 InFrontLayerID,
		float InZoomFactor,
		const FSlateRect& InClippingRect,
		FSlateWindowElementList& InDrawElements,
		UEdGraph* InGraph);

	virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& Params) override;

private:
	UEdGraph* Graph;
};
