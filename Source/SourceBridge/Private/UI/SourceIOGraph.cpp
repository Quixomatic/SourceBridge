#include "UI/SourceIOGraph.h"
#include "UI/SSourceIOGraphNode.h"
#include "UI/SourceIOConnectionDrawingPolicy.h"
#include "Actors/SourceEntityActor.h"
#include "Entities/EntityIOConnection.h"
#include "SourceBridgeModule.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// ============================================================================
// USourceIOGraphNode
// ============================================================================

void USourceIOGraphNode::InitFromActor(ASourceEntityActor* Actor)
{
	if (!Actor) return;

	SourceActor = Actor;
	CachedClassname = Actor->SourceClassname;
	CachedTargetName = Actor->TargetName;

	// Resolve FGD class for pin creation
	const FFGDDatabase& FGD = FSourceBridgeModule::GetFGDDatabase();
	if (FGD.Classes.Num() > 0)
	{
		const FFGDEntityClass* FGDClass = FGD.FindClass(CachedClassname);
		if (FGDClass)
		{
			ResolvedFGDClass = FGD.GetResolved(CachedClassname);
			bHasFGDData = true;
		}
	}
}

void USourceIOGraphNode::SyncFromActor()
{
	if (SourceActor.IsValid())
	{
		CachedClassname = SourceActor->SourceClassname;
		CachedTargetName = SourceActor->TargetName;
	}
}

void USourceIOGraphNode::AllocateDefaultPins()
{
	if (bHasFGDData)
	{
		// Create output pins from FGD outputs
		for (const FFGDIODef& Output : ResolvedFGDClass.Outputs)
		{
			FEdGraphPinType PinType;
			PinType.PinCategory = TEXT("SourceIO");
			PinType.PinSubCategory = TEXT("Output");
			UEdGraphPin* Pin = CreatePin(EGPD_Output, PinType, FName(*Output.Name));
			if (Pin)
			{
				Pin->PinToolTip = Output.Description.IsEmpty()
					? FString::Printf(TEXT("Output: %s (%s)"), *Output.Name, *Output.ParamType)
					: FString::Printf(TEXT("%s (%s)"), *Output.Description, *Output.ParamType);
			}
		}

		// Create input pins from FGD inputs
		for (const FFGDIODef& Input : ResolvedFGDClass.Inputs)
		{
			FEdGraphPinType PinType;
			PinType.PinCategory = TEXT("SourceIO");
			PinType.PinSubCategory = TEXT("Input");
			UEdGraphPin* Pin = CreatePin(EGPD_Input, PinType, FName(*Input.Name));
			if (Pin)
			{
				Pin->PinToolTip = Input.Description.IsEmpty()
					? FString::Printf(TEXT("Input: %s (%s)"), *Input.Name, *Input.ParamType)
					: FString::Printf(TEXT("%s (%s)"), *Input.Description, *Input.ParamType);
			}
		}
	}
	else
	{
		// No FGD data - create generic pins
		{
			FEdGraphPinType OutPinType;
			OutPinType.PinCategory = TEXT("SourceIO");
			OutPinType.PinSubCategory = TEXT("Output");
			CreatePin(EGPD_Output, OutPinType, TEXT("Output"));

			FEdGraphPinType InPinType;
			InPinType.PinCategory = TEXT("SourceIO");
			InPinType.PinSubCategory = TEXT("Input");
			CreatePin(EGPD_Input, InPinType, TEXT("Input"));
		}
	}

	// Also discover output names from existing io: tags that aren't already pins
	if (SourceActor.IsValid())
	{
		for (const FName& Tag : SourceActor->Tags)
		{
			FString TagStr = Tag.ToString();
			if (!TagStr.StartsWith(TEXT("io:"), ESearchCase::IgnoreCase)) continue;

			FEntityIOConnection Conn;
			if (FEntityIOConnection::ParseFromTag(TagStr, Conn))
			{
				if (!FindOutputPin(Conn.OutputName))
				{
					FEdGraphPinType TagPinType;
					TagPinType.PinCategory = TEXT("SourceIO");
					TagPinType.PinSubCategory = TEXT("Output");
					UEdGraphPin* Pin = CreatePin(EGPD_Output, TagPinType, FName(*Conn.OutputName));
					if (Pin)
					{
						Pin->PinToolTip = FString::Printf(TEXT("Output: %s (discovered from tag)"), *Conn.OutputName);
					}
				}
			}
		}
	}
}

FText USourceIOGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::FullTitle)
	{
		if (!CachedTargetName.IsEmpty())
		{
			return FText::FromString(FString::Printf(TEXT("%s\n%s"), *CachedTargetName, *CachedClassname));
		}
		return FText::FromString(CachedClassname);
	}

	// ListView / MenuTitle / EditableTitle
	if (!CachedTargetName.IsEmpty())
	{
		return FText::FromString(FString::Printf(TEXT("%s (%s)"), *CachedClassname, *CachedTargetName));
	}
	return FText::FromString(CachedClassname);
}

FLinearColor USourceIOGraphNode::GetNodeTitleColor() const
{
	return GetColorForClassname(CachedClassname);
}

FLinearColor USourceIOGraphNode::GetNodeBodyTintColor() const
{
	return FLinearColor(0.08f, 0.08f, 0.08f);
}

TSharedPtr<SGraphNode> USourceIOGraphNode::CreateVisualWidget()
{
	return SNew(SSourceIOGraphNode, this);
}

void USourceIOGraphNode::DestroyNode()
{
	// Only destroy the world actor if this is an explicit user deletion, not a graph rebuild
	USourceIOGraph* IOGraph = Cast<USourceIOGraph>(GetGraph());
	if (IOGraph && !IOGraph->bIsRebuilding)
	{
		if (SourceActor.IsValid())
		{
			SourceActor->Modify();
			if (SourceActor->GetWorld())
			{
				SourceActor->GetWorld()->DestroyActor(SourceActor.Get());
			}
		}
	}
	Super::DestroyNode();
}

UEdGraphPin* USourceIOGraphNode::FindOutputPin(const FString& Name) const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && Pin->PinName.ToString() == Name)
		{
			return Pin;
		}
	}
	return nullptr;
}

UEdGraphPin* USourceIOGraphNode::FindInputPin(const FString& Name) const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Pin->PinName.ToString() == Name)
		{
			return Pin;
		}
	}
	return nullptr;
}

FLinearColor USourceIOGraphNode::GetColorForClassname(const FString& Classname)
{
	if (Classname.StartsWith(TEXT("trigger_")))	return FLinearColor(0.9f, 0.5f, 0.1f);
	if (Classname.StartsWith(TEXT("logic_")))	return FLinearColor(0.2f, 0.4f, 0.9f);
	if (Classname.StartsWith(TEXT("light")))	return FLinearColor(0.9f, 0.9f, 0.2f);
	if (Classname.StartsWith(TEXT("func_")))	return FLinearColor(0.2f, 0.7f, 0.7f);
	if (Classname.StartsWith(TEXT("info_player")))	return FLinearColor(0.2f, 0.8f, 0.2f);
	if (Classname.StartsWith(TEXT("prop_")))	return FLinearColor(0.6f, 0.3f, 0.8f);
	if (Classname.StartsWith(TEXT("env_")) || Classname.StartsWith(TEXT("ambient_")))	return FLinearColor(0.3f, 0.7f, 0.5f);
	if (Classname.StartsWith(TEXT("game_")) || Classname.StartsWith(TEXT("point_")))	return FLinearColor(0.5f, 0.5f, 0.6f);
	return FLinearColor(0.4f, 0.4f, 0.4f);
}

// ============================================================================
// USourceIOGraphSchema
// ============================================================================

const FPinConnectionResponse USourceIOGraphSchema::CanCreateConnection(
	const UEdGraphPin* A, const UEdGraphPin* B) const
{
	if (!A || !B)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Invalid pin"));
	}

	// Can't connect to self
	if (A->GetOwningNode() == B->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Cannot connect to self"));
	}

	// Must be output to input
	if (A->Direction == B->Direction)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Must connect an output to an input"));
	}

	// Source I/O is free-form: any output can connect to any input
	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT(""));
}

bool USourceIOGraphSchema::TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const
{
	bool bResult = Super::TryCreateConnection(A, B);
	if (!bResult) return false;

	// Determine which is output and which is input
	UEdGraphPin* OutputPin = (A->Direction == EGPD_Output) ? A : B;
	UEdGraphPin* InputPin = (A->Direction == EGPD_Input) ? A : B;

	USourceIOGraphNode* SourceNode = Cast<USourceIOGraphNode>(OutputPin->GetOwningNode());
	USourceIOGraphNode* TargetNode = Cast<USourceIOGraphNode>(InputPin->GetOwningNode());

	if (!SourceNode || !TargetNode) return bResult;
	if (!SourceNode->SourceActor.IsValid() || !TargetNode->SourceActor.IsValid()) return bResult;

	// Build the io: tag
	FString TargetName = TargetNode->SourceActor->TargetName;
	if (TargetName.IsEmpty())
	{
		// Use a placeholder if no targetname
		TargetName = TargetNode->SourceActor->GetActorLabel();
	}

	FString TagStr = FString::Printf(TEXT("io:%s:%s,%s,,%g,%d"),
		*OutputPin->PinName.ToString(),
		*TargetName,
		*InputPin->PinName.ToString(),
		0.0f,
		-1);

	SourceNode->SourceActor->Modify();
	SourceNode->SourceActor->Tags.AddUnique(FName(*TagStr));

	UE_LOG(LogTemp, Log, TEXT("SourceIOGraph: Created connection %s -> %s.%s (tag: %s)"),
		*OutputPin->PinName.ToString(), *TargetName, *InputPin->PinName.ToString(), *TagStr);

	return true;
}

void USourceIOGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const
{
	// Remove io: tags for all connections on this pin before breaking
	TArray<UEdGraphPin*> LinkedPinsCopy = TargetPin.LinkedTo;
	for (UEdGraphPin* LinkedPin : LinkedPinsCopy)
	{
		if (TargetPin.Direction == EGPD_Output)
		{
			RemoveIOTagForConnection(&TargetPin, LinkedPin);
		}
		else
		{
			RemoveIOTagForConnection(LinkedPin, &TargetPin);
		}
	}

	Super::BreakPinLinks(TargetPin, bSendsNodeNotification);
}

void USourceIOGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	// Determine output and input
	UEdGraphPin* OutputPin = (SourcePin->Direction == EGPD_Output) ? SourcePin : TargetPin;
	UEdGraphPin* InputPin = (SourcePin->Direction == EGPD_Input) ? SourcePin : TargetPin;

	RemoveIOTagForConnection(OutputPin, InputPin);
	Super::BreakSinglePinLink(SourcePin, TargetPin);
}

void USourceIOGraphSchema::RemoveIOTagForConnection(UEdGraphPin* OutputPin, UEdGraphPin* InputPin)
{
	if (!OutputPin || !InputPin) return;

	USourceIOGraphNode* SourceNode = Cast<USourceIOGraphNode>(OutputPin->GetOwningNode());
	USourceIOGraphNode* TargetNode = Cast<USourceIOGraphNode>(InputPin->GetOwningNode());

	if (!SourceNode || !TargetNode) return;
	if (!SourceNode->SourceActor.IsValid()) return;

	FString OutputName = OutputPin->PinName.ToString();
	FString InputName = InputPin->PinName.ToString();
	FString TargetName = TargetNode->CachedTargetName;

	// Find and remove the matching io: tag
	ASourceEntityActor* Actor = SourceNode->SourceActor.Get();
	Actor->Modify();

	for (int32 i = Actor->Tags.Num() - 1; i >= 0; --i)
	{
		FString TagStr = Actor->Tags[i].ToString();
		if (!TagStr.StartsWith(TEXT("io:"), ESearchCase::IgnoreCase)) continue;

		FEntityIOConnection Conn;
		if (FEntityIOConnection::ParseFromTag(TagStr, Conn))
		{
			if (Conn.OutputName == OutputName &&
				Conn.TargetEntity == TargetName &&
				Conn.InputName == InputName)
			{
				Actor->Tags.RemoveAt(i);
				UE_LOG(LogTemp, Log, TEXT("SourceIOGraph: Removed connection %s -> %s.%s"),
					*OutputName, *TargetName, *InputName);
				break;
			}
		}
	}
}

void USourceIOGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	// Add common Source entities to the right-click menu
	struct FEntityEntry { const TCHAR* Class; const TCHAR* Category; const TCHAR* Desc; bool bSolid; };
	static const FEntityEntry Entries[] = {
		{ TEXT("logic_relay"),			TEXT("Logic"),		TEXT("Fans out I/O signals"),			false },
		{ TEXT("logic_auto"),			TEXT("Logic"),		TEXT("Fires on map start"),				false },
		{ TEXT("logic_timer"),			TEXT("Logic"),		TEXT("Fires at timed intervals"),		false },
		{ TEXT("trigger_multiple"),		TEXT("Triggers"),	TEXT("Reusable trigger volume"),		true },
		{ TEXT("trigger_once"),			TEXT("Triggers"),	TEXT("Single-fire trigger volume"),		true },
		{ TEXT("func_button"),			TEXT("Brushes"),	TEXT("Pressable button"),				true },
		{ TEXT("func_door"),			TEXT("Brushes"),	TEXT("Moving door brush"),				true },
		{ TEXT("game_text"),			TEXT("Logic"),		TEXT("Display text on screen"),			false },
		{ TEXT("ambient_generic"),		TEXT("Effects"),	TEXT("Play a sound"),					false },
		{ TEXT("env_sprite"),			TEXT("Effects"),	TEXT("Visual sprite effect"),			false },
		{ TEXT("math_counter"),			TEXT("Logic"),		TEXT("Counts and compares values"),		false },
		{ TEXT("point_template"),		TEXT("Logic"),		TEXT("Spawns entity templates"),		false },
	};

	for (const FEntityEntry& E : Entries)
	{
		TSharedPtr<FSourceIOSchemaAction_NewEntity> Action = MakeShared<FSourceIOSchemaAction_NewEntity>(
			FText::FromString(E.Category),
			FText::FromString(E.Class),
			FText::FromString(E.Desc),
			0);
		Action->EntityClassName = E.Class;
		Action->bIsSolid = E.bSolid;

		ContextMenuBuilder.AddAction(Action);
	}

	// Also add entities from FGD if loaded
	const FFGDDatabase& FGD = FSourceBridgeModule::GetFGDDatabase();
	if (FGD.Classes.Num() > 0)
	{
		TArray<FString> Names = FGD.GetPlaceableClassNames();
		for (const FString& Name : Names)
		{
			// Skip ones we already added as hardcoded entries
			bool bAlreadyAdded = false;
			for (const FEntityEntry& E : Entries)
			{
				if (Name.Equals(E.Class, ESearchCase::IgnoreCase))
				{
					bAlreadyAdded = true;
					break;
				}
			}
			if (bAlreadyAdded) continue;

			const FFGDEntityClass* FGDClass = FGD.FindClass(Name);
			if (!FGDClass) continue;

			// Only show entities that have I/O (check unresolved first for speed, then resolve if needed)
			if (FGDClass->Inputs.Num() == 0 && FGDClass->Outputs.Num() == 0)
			{
				// Check resolved (inherits from base with I/O)
				FFGDEntityClass Resolved = FGD.GetResolved(Name);
				if (Resolved.Inputs.Num() == 0 && Resolved.Outputs.Num() == 0) continue;
			}

			FString Category;
			if (Name.StartsWith(TEXT("trigger_"))) Category = TEXT("Triggers");
			else if (Name.StartsWith(TEXT("logic_"))) Category = TEXT("Logic");
			else if (Name.StartsWith(TEXT("func_"))) Category = TEXT("Brushes");
			else if (Name.StartsWith(TEXT("env_")) || Name.StartsWith(TEXT("ambient_"))) Category = TEXT("Effects");
			else if (Name.StartsWith(TEXT("prop_"))) Category = TEXT("Props");
			else if (Name.StartsWith(TEXT("info_"))) Category = TEXT("Info");
			else Category = TEXT("Other");

			TSharedPtr<FSourceIOSchemaAction_NewEntity> Action = MakeShared<FSourceIOSchemaAction_NewEntity>(
				FText::FromString(Category),
				FText::FromString(Name),
				FText::FromString(FGDClass->Description),
				1); // Lower priority than hardcoded
			Action->EntityClassName = Name;
			Action->bIsSolid = FGDClass->bIsSolid;

			ContextMenuBuilder.AddAction(Action);
		}
	}
}

FLinearColor USourceIOGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	if (PinType.PinSubCategory == TEXT("Input"))
	{
		return FLinearColor(0.3f, 0.5f, 0.9f); // Light blue for inputs
	}
	return FLinearColor(0.4f, 0.8f, 0.3f); // Light green for outputs
}

FConnectionDrawingPolicy* USourceIOGraphSchema::CreateConnectionDrawingPolicy(
	int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor,
	const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
	return new FSourceIOConnectionDrawingPolicy(
		InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
}

// ============================================================================
// USourceIOGraph
// ============================================================================

void USourceIOGraph::RebuildFromWorld(UWorld* World)
{
	if (!World) return;

	bIsRebuilding = true;

	// Clear existing nodes
	TArray<UEdGraphNode*> OldNodes = Nodes;
	for (UEdGraphNode* Node : OldNodes)
	{
		if (Node)
		{
			RemoveNode(Node);
		}
	}
	Nodes.Empty();

	// Create a node for each ASourceEntityActor in the world
	for (TActorIterator<ASourceEntityActor> It(World); It; ++It)
	{
		ASourceEntityActor* Actor = *It;
		if (!Actor) continue;

		FGraphNodeCreator<USourceIOGraphNode> NodeCreator(*this);
		USourceIOGraphNode* Node = NodeCreator.CreateNode(false);
		Node->InitFromActor(Actor);
		NodeCreator.Finalize();
	}

	// Create connections from io: tags
	RebuildConnections();

	// Layout
	AutoLayout();

	bIsRebuilding = false;

	NotifyGraphChanged();
}

USourceIOGraphNode* USourceIOGraph::FindNodeForActor(ASourceEntityActor* Actor) const
{
	if (!Actor) return nullptr;

	for (UEdGraphNode* Node : Nodes)
	{
		USourceIOGraphNode* IONode = Cast<USourceIOGraphNode>(Node);
		if (IONode && IONode->SourceActor.Get() == Actor)
		{
			return IONode;
		}
	}
	return nullptr;
}

void USourceIOGraph::RebuildConnections()
{
	// First, break all existing pin links
	for (UEdGraphNode* Node : Nodes)
	{
		if (!Node) continue;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin)
			{
				Pin->BreakAllPinLinks();
			}
		}
	}

	// Build a map of targetname -> node for fast lookup
	TMap<FString, USourceIOGraphNode*> TargetNameToNode;
	for (UEdGraphNode* Node : Nodes)
	{
		USourceIOGraphNode* IONode = Cast<USourceIOGraphNode>(Node);
		if (IONode && !IONode->CachedTargetName.IsEmpty())
		{
			TargetNameToNode.Add(IONode->CachedTargetName, IONode);
		}
	}

	// For each node, parse io: tags and create connections
	for (UEdGraphNode* Node : Nodes)
	{
		USourceIOGraphNode* IONode = Cast<USourceIOGraphNode>(Node);
		if (!IONode || !IONode->SourceActor.IsValid()) continue;

		for (const FName& Tag : IONode->SourceActor->Tags)
		{
			FString TagStr = Tag.ToString();
			FEntityIOConnection Conn;
			if (!FEntityIOConnection::ParseFromTag(TagStr, Conn)) continue;

			// Find the output pin on this node
			UEdGraphPin* OutputPin = IONode->FindOutputPin(Conn.OutputName);
			if (!OutputPin)
			{
				// Create the pin dynamically if it doesn't exist
				FEdGraphPinType DynOutType;
				DynOutType.PinCategory = TEXT("SourceIO");
				DynOutType.PinSubCategory = TEXT("Output");
				OutputPin = IONode->CreatePin(EGPD_Output, DynOutType, FName(*Conn.OutputName));
				if (OutputPin)
				{
					OutputPin->PinToolTip = FString::Printf(TEXT("Output: %s (from tag)"), *Conn.OutputName);
				}
			}

			// Find the target node
			USourceIOGraphNode** TargetNodePtr = TargetNameToNode.Find(Conn.TargetEntity);
			if (!TargetNodePtr || !*TargetNodePtr) continue;
			USourceIOGraphNode* TargetNode = *TargetNodePtr;

			// Find the input pin on the target node
			UEdGraphPin* InputPin = TargetNode->FindInputPin(Conn.InputName);
			if (!InputPin)
			{
				// Create the pin dynamically if it doesn't exist
				FEdGraphPinType DynInType;
				DynInType.PinCategory = TEXT("SourceIO");
				DynInType.PinSubCategory = TEXT("Input");
				InputPin = TargetNode->CreatePin(EGPD_Input, DynInType, FName(*Conn.InputName));
				if (InputPin)
				{
					InputPin->PinToolTip = FString::Printf(TEXT("Input: %s (from tag)"), *Conn.InputName);
				}
			}

			// Make the connection
			if (OutputPin && InputPin)
			{
				OutputPin->MakeLinkTo(InputPin);
			}
		}
	}
}

void USourceIOGraph::AutoLayout()
{
	if (Nodes.Num() == 0) return;

	const float ColumnSpacing = 450.0f;
	const float RowSpacing = 180.0f;
	const int32 MaxRowsPerColumn = 12;

	// Separate connected and disconnected nodes
	// Build adjacency for connected subgraphs
	TMap<USourceIOGraphNode*, TArray<USourceIOGraphNode*>> Outgoing;
	TSet<USourceIOGraphNode*> HasIncoming;
	TSet<USourceIOGraphNode*> HasAnyConnection;

	for (UEdGraphNode* Node : Nodes)
	{
		USourceIOGraphNode* IONode = Cast<USourceIOGraphNode>(Node);
		if (!IONode) continue;

		for (UEdGraphPin* Pin : IONode->Pins)
		{
			if (!Pin) continue;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin) continue;
				USourceIOGraphNode* OtherNode = Cast<USourceIOGraphNode>(LinkedPin->GetOwningNode());
				if (OtherNode && OtherNode != IONode)
				{
					HasAnyConnection.Add(IONode);
					HasAnyConnection.Add(OtherNode);

					if (Pin->Direction == EGPD_Output)
					{
						Outgoing.FindOrAdd(IONode).AddUnique(OtherNode);
						HasIncoming.Add(OtherNode);
					}
				}
			}
		}
	}

	// === Layout connected nodes using BFS columns ===
	TMap<USourceIOGraphNode*, int32> NodeColumn;
	TQueue<USourceIOGraphNode*> Queue;

	// Root nodes: connected but no incoming edges
	for (USourceIOGraphNode* IONode : HasAnyConnection)
	{
		if (!HasIncoming.Contains(IONode))
		{
			NodeColumn.Add(IONode, 0);
			Queue.Enqueue(IONode);
		}
	}

	// BFS to assign columns
	while (!Queue.IsEmpty())
	{
		USourceIOGraphNode* Current;
		Queue.Dequeue(Current);
		int32 CurrentCol = NodeColumn[Current];

		TArray<USourceIOGraphNode*>* Targets = Outgoing.Find(Current);
		if (Targets)
		{
			for (USourceIOGraphNode* Target : *Targets)
			{
				int32* ExistingCol = NodeColumn.Find(Target);
				if (!ExistingCol)
				{
					NodeColumn.Add(Target, CurrentCol + 1);
					Queue.Enqueue(Target);
				}
				else if (*ExistingCol < CurrentCol + 1)
				{
					*ExistingCol = CurrentCol + 1;
					Queue.Enqueue(Target);
				}
			}
		}
	}

	// Any connected nodes that weren't reached (cycles) get column 0
	for (USourceIOGraphNode* IONode : HasAnyConnection)
	{
		if (!NodeColumn.Contains(IONode))
		{
			NodeColumn.Add(IONode, 0);
		}
	}

	// Group connected nodes by column and position them
	TMap<int32, TArray<USourceIOGraphNode*>> ColumnNodes;
	for (const auto& Pair : NodeColumn)
	{
		ColumnNodes.FindOrAdd(Pair.Value).Add(Pair.Key);
	}

	// Sort columns
	TArray<int32> ColumnKeys;
	ColumnNodes.GetKeys(ColumnKeys);
	ColumnKeys.Sort();

	for (int32 Col : ColumnKeys)
	{
		TArray<USourceIOGraphNode*>& NodesInCol = ColumnNodes[Col];
		float StartY = -(NodesInCol.Num() - 1) * RowSpacing * 0.5f;

		for (int32 i = 0; i < NodesInCol.Num(); ++i)
		{
			NodesInCol[i]->NodePosX = Col * ColumnSpacing;
			NodesInCol[i]->NodePosY = StartY + i * RowSpacing;
		}
	}

	// === Layout disconnected nodes in a grid below the connected section ===
	TArray<USourceIOGraphNode*> DisconnectedNodes;
	for (UEdGraphNode* Node : Nodes)
	{
		USourceIOGraphNode* IONode = Cast<USourceIOGraphNode>(Node);
		if (IONode && !HasAnyConnection.Contains(IONode))
		{
			DisconnectedNodes.Add(IONode);
		}
	}

	if (DisconnectedNodes.Num() == 0) return;

	// Sort disconnected nodes by classname for visual grouping
	DisconnectedNodes.Sort([](const USourceIOGraphNode& A, const USourceIOGraphNode& B)
	{
		return A.CachedClassname < B.CachedClassname;
	});

	// Find the bottom of the connected layout
	float ConnectedMaxY = 0;
	for (const auto& Pair : NodeColumn)
	{
		ConnectedMaxY = FMath::Max(ConnectedMaxY, (float)Pair.Key->NodePosY);
	}
	float DisconnectedStartY = (NodeColumn.Num() > 0) ? ConnectedMaxY + RowSpacing * 3.0f : 0.0f;

	// Place in a grid
	for (int32 i = 0; i < DisconnectedNodes.Num(); ++i)
	{
		int32 Col = i / MaxRowsPerColumn;
		int32 Row = i % MaxRowsPerColumn;
		DisconnectedNodes[i]->NodePosX = Col * ColumnSpacing;
		DisconnectedNodes[i]->NodePosY = DisconnectedStartY + Row * RowSpacing;
	}
}

// ============================================================================
// FSourceIOSchemaAction_NewEntity
// ============================================================================

UEdGraphNode* FSourceIOSchemaAction_NewEntity::PerformAction(
	UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World || !ParentGraph) return nullptr;

	// Spawn the entity actor in the world
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ASourceEntityActor* NewActor = nullptr;
	if (bIsSolid)
	{
		NewActor = World->SpawnActor<ASourceBrushEntity>(
			ASourceBrushEntity::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	}
	else
	{
		NewActor = World->SpawnActor<ASourceGenericEntity>(
			ASourceGenericEntity::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	}

	if (!NewActor) return nullptr;

	NewActor->SourceClassname = EntityClassName;
	NewActor->SetActorLabel(EntityClassName);

#if WITH_EDITORONLY_DATA
	NewActor->UpdateEditorSprite();
#endif

	// Create a graph node for this actor
	FGraphNodeCreator<USourceIOGraphNode> NodeCreator(*ParentGraph);
	USourceIOGraphNode* Node = NodeCreator.CreateNode(bSelectNewNode);
	Node->InitFromActor(NewActor);
	Node->NodePosX = Location.X;
	Node->NodePosY = Location.Y;
	NodeCreator.Finalize();

	// If dragged from a pin, auto-connect to the first compatible pin
	if (FromPin && Node)
	{
		UEdGraphPin* ConnectTo = nullptr;
		if (FromPin->Direction == EGPD_Output)
		{
			// Find first input pin on new node
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input)
				{
					ConnectTo = Pin;
					break;
				}
			}
		}
		else
		{
			// Find first output pin on new node
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output)
				{
					ConnectTo = Pin;
					break;
				}
			}
		}

		if (ConnectTo)
		{
			const UEdGraphSchema* Schema = ParentGraph->GetSchema();
			if (Schema)
			{
				Schema->TryCreateConnection(FromPin, ConnectTo);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("SourceIOGraph: Spawned %s entity from context menu"), *EntityClassName);

	return Node;
}
