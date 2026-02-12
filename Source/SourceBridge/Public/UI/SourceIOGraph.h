#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "Entities/FGDParser.h"
#include "SourceIOGraph.generated.h"

class ASourceEntityActor;

/**
 * Graph node representing a single ASourceEntityActor in the I/O graph.
 * Pins are created from FGD input/output definitions.
 */
UCLASS()
class USourceIOGraphNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	/** Weak reference to the world actor this node represents. */
	TWeakObjectPtr<ASourceEntityActor> SourceActor;

	/** Cached Source classname (for display even if actor gets deleted). */
	FString CachedClassname;

	/** Cached targetname. */
	FString CachedTargetName;

	/** Resolved FGD class data (inputs/outputs for pin creation). */
	FFGDEntityClass ResolvedFGDClass;

	/** Whether FGD data was found for this entity. */
	bool bHasFGDData = false;

	/** Whether the properties section is expanded. */
	bool bShowProperties = false;

	/** Whether the connections section is expanded. */
	bool bShowConnections = false;

	// --- UEdGraphNode overrides ---
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FLinearColor GetNodeBodyTintColor() const override;
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	virtual void DestroyNode() override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return false; }

	// --- Custom methods ---

	/** Initialize this node from a world actor. Call before FGraphNodeCreator::Finalize(). */
	void InitFromActor(ASourceEntityActor* Actor);

	/** Refresh cached data from the actor. */
	void SyncFromActor();

	/** Find an output pin by name. */
	UEdGraphPin* FindOutputPin(const FString& Name) const;

	/** Find an input pin by name. */
	UEdGraphPin* FindInputPin(const FString& Name) const;

	/** Get node color for a classname prefix. */
	static FLinearColor GetColorForClassname(const FString& Classname);
};

/**
 * Schema defining connection rules and context menu for the I/O graph.
 */
UCLASS()
class USourceIOGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
	virtual bool TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const override;
	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const override;
	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override;
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
	virtual class FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) const override;

	static void RemoveIOTagForConnection(UEdGraphPin* OutputPin, UEdGraphPin* InputPin);
};

/**
 * The I/O graph containing all entity nodes and their connections.
 */
UCLASS()
class USourceIOGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	/** Build (or rebuild) the graph from all ASourceEntityActor instances in the world. */
	void RebuildFromWorld(UWorld* World);

	/** Find the graph node for a given actor. */
	USourceIOGraphNode* FindNodeForActor(ASourceEntityActor* Actor) const;

	/** Create connections (wires) based on io: tags on all actors. Call after all nodes exist. */
	void RebuildConnections();

	/** Apply a simple auto-layout (left-to-right columns by connection flow). */
	void AutoLayout();

	/** Flag to prevent DestroyNode from deleting actors during graph rebuild. */
	bool bIsRebuilding = false;
};

/**
 * Schema action for spawning a new Source entity from the graph context menu.
 */
struct FSourceIOSchemaAction_NewEntity : public FEdGraphSchemaAction
{
	FString EntityClassName;
	bool bIsSolid = false;

	FSourceIOSchemaAction_NewEntity() {}
	FSourceIOSchemaAction_NewEntity(FText InNodeCategory, FText InMenuDesc, FText InToolTip, int32 InGrouping)
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping)
	{}

	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode = true) override;
};
