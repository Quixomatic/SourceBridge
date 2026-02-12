#include "UI/SourceIOConnectionDrawingPolicy.h"
#include "UI/SourceIOGraph.h"

FSourceIOConnectionDrawingPolicy::FSourceIOConnectionDrawingPolicy(
	int32 InBackLayerID,
	int32 InFrontLayerID,
	float InZoomFactor,
	const FSlateRect& InClippingRect,
	FSlateWindowElementList& InDrawElements,
	UEdGraph* InGraph)
	: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
	, Graph(InGraph)
{
}

void FSourceIOConnectionDrawingPolicy::DetermineWiringStyle(
	UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& Params)
{
	// Start with defaults
	Params.AssociatedPin1 = OutputPin;
	Params.AssociatedPin2 = InputPin;
	Params.WireThickness = 2.0f;

	// Color wire based on source entity type
	if (OutputPin)
	{
		USourceIOGraphNode* SourceNode = Cast<USourceIOGraphNode>(OutputPin->GetOwningNode());
		if (SourceNode)
		{
			Params.WireColor = USourceIOGraphNode::GetColorForClassname(SourceNode->CachedClassname);
			// Slightly brighter/more saturated than title bar
			Params.WireColor = FLinearColor::LerpUsingHSV(Params.WireColor, FLinearColor::White, 0.15f);
		}
	}

	// Hovering makes wires thicker
	if (HoveredPins.Contains(OutputPin) || HoveredPins.Contains(InputPin))
	{
		Params.WireThickness = 4.0f;
		Params.WireColor = FLinearColor::LerpUsingHSV(Params.WireColor, FLinearColor::White, 0.3f);
	}
}

