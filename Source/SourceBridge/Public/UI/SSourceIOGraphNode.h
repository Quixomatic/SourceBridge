#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"
#include "Entities/FGDParser.h"

class USourceIOGraphNode;
class ASourceEntityActor;

/**
 * Custom Slate widget for Source I/O graph nodes.
 * Delegates to base SGraphNode for core rendering (title, pins, wires),
 * then adds collapsible FGD property editing and I/O connection sections below pins.
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
	virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) override;

private:
	USourceIOGraphNode* IONode = nullptr;

	/** Custom content sections added below pins. */
	TSharedPtr<SVerticalBox> PropertiesBox;
	TSharedPtr<SVerticalBox> ConnectionsBox;

	/** Build the FGD property widgets inside PropertiesBox. */
	void BuildPropertyWidgets();

	/** Build the I/O connection rows inside ConnectionsBox. */
	void BuildConnectionWidgets();

	/** Create a property widget for a single FGD property. */
	TSharedRef<SWidget> CreatePropertyWidget(const FFGDProperty& Prop, TWeakObjectPtr<ASourceEntityActor> WeakActor);

	/** Create a single spawnflag checkbox row. */
	TSharedRef<SWidget> CreateFlagWidget(const FFGDFlag& Flag, TWeakObjectPtr<ASourceEntityActor> WeakActor);

	/** Create a single connection row widget. */
	TSharedRef<SWidget> CreateConnectionRowWidget(int32 TagIndex, const FString& TagStr, TWeakObjectPtr<ASourceEntityActor> WeakActor);

	/** Compact font for node body. */
	static FSlateFontInfo GetNodeBodyFont();
	static FSlateFontInfo GetNodeBodyBoldFont();
};
