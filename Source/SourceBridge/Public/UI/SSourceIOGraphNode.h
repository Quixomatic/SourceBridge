#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"

class USourceIOGraphNode;

/**
 * Custom Slate widget for Source I/O graph nodes.
 * Color-coded title bar based on entity type, shows targetname + classname.
 */
class SSourceIOGraphNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SSourceIOGraphNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, USourceIOGraphNode* InNode);

	// SGraphNode overrides
	virtual void UpdateGraphNode() override;
	virtual void CreatePinWidgets() override;

private:
	USourceIOGraphNode* IONode = nullptr;
};
