#include "UI/SourceIOGraphEditor.h"
#include "UI/SourceIOGraph.h"
#include "Actors/SourceEntityActor.h"
#include "GraphEditor.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Framework/Docking/TabManager.h"

#define LOCTEXT_NAMESPACE "SourceIOGraphEditor"

// ============================================================================
// SSourceIOGraphEditor
// ============================================================================

void SSourceIOGraphEditor::Construct(const FArguments& InArgs)
{
	// Create the graph object
	Graph = NewObject<USourceIOGraph>(GetTransientPackage());
	Graph->Schema = USourceIOGraphSchema::StaticClass();
	Graph->AddToRoot(); // Prevent GC while tab is open

	// Build the graph from the current world
	BuildGraph();

	// Set up graph editor events
	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(
		this, &SSourceIOGraphEditor::OnGraphSelectionChanged);

	// Build the UI
	ChildSlot
	[
		SNew(SVerticalBox)

		// Toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "Refresh"))
				.ToolTipText(LOCTEXT("RefreshTip", "Rebuild the graph from current world state"))
				.OnClicked(this, &SSourceIOGraphEditor::OnRefreshClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("AutoLayout", "Auto Layout"))
				.ToolTipText(LOCTEXT("AutoLayoutTip", "Arrange nodes in left-to-right flow"))
				.OnClicked(this, &SSourceIOGraphEditor::OnAutoLayoutClicked)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() -> FText
				{
					int32 NodeCount = Graph ? Graph->Nodes.Num() : 0;
					return FText::Format(LOCTEXT("NodeCount", "{0} entities"), FText::AsNumber(NodeCount));
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
			]
		]

		// Graph editor
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(GraphEditorWidget, SGraphEditor)
			.GraphToEdit(Graph)
			.IsEditable(true)
			.GraphEvents(GraphEvents)
		]
	];

	// Register for viewport selection changes
	if (GEditor)
	{
		EditorSelectionChangedHandle = GEditor->GetSelectedActors()->SelectionChangedEvent.AddSP(
			this, &SSourceIOGraphEditor::OnEditorSelectionChanged);
	}

	// Register for actor add/delete
	if (GEngine)
	{
		ActorAddedHandle = GEngine->OnLevelActorAdded().AddSP(
			this, &SSourceIOGraphEditor::OnActorAdded);
		ActorDeletedHandle = GEngine->OnLevelActorDeleted().AddSP(
			this, &SSourceIOGraphEditor::OnActorDeleted);
	}
}

SSourceIOGraphEditor::~SSourceIOGraphEditor()
{
	// Unregister delegates
	if (GEditor && EditorSelectionChangedHandle.IsValid())
	{
		GEditor->GetSelectedActors()->SelectionChangedEvent.Remove(EditorSelectionChangedHandle);
	}
	if (GEngine)
	{
		if (ActorAddedHandle.IsValid())
		{
			GEngine->OnLevelActorAdded().Remove(ActorAddedHandle);
		}
		if (ActorDeletedHandle.IsValid())
		{
			GEngine->OnLevelActorDeleted().Remove(ActorDeletedHandle);
		}
	}

	// Release the graph
	if (Graph)
	{
		Graph->RemoveFromRoot();
		Graph = nullptr;
	}
}

void SSourceIOGraphEditor::BuildGraph()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World && Graph)
	{
		Graph->RebuildFromWorld(World);
	}
}

FReply SSourceIOGraphEditor::OnRefreshClicked()
{
	BuildGraph();
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->NotifyGraphChanged();
	}
	return FReply::Handled();
}

FReply SSourceIOGraphEditor::OnAutoLayoutClicked()
{
	if (Graph)
	{
		Graph->AutoLayout();
		if (GraphEditorWidget.IsValid())
		{
			GraphEditorWidget->NotifyGraphChanged();
		}
	}
	return FReply::Handled();
}

void SSourceIOGraphEditor::OnGraphSelectionChanged(const TSet<UObject*>& NewSelection)
{
	if (bSyncingSelection || !GEditor) return;
	bSyncingSelection = true;

	GEditor->SelectNone(false, true, false);

	for (UObject* Obj : NewSelection)
	{
		USourceIOGraphNode* IONode = Cast<USourceIOGraphNode>(Obj);
		if (IONode && IONode->SourceActor.IsValid())
		{
			GEditor->SelectActor(IONode->SourceActor.Get(), true, true, true);
		}
	}

	bSyncingSelection = false;
}

void SSourceIOGraphEditor::OnEditorSelectionChanged(UObject* Object)
{
	if (bSyncingSelection || !GEditor || !GraphEditorWidget.IsValid() || !Graph) return;
	bSyncingSelection = true;

	GraphEditorWidget->ClearSelectionSet();

	USelection* Selection = GEditor->GetSelectedActors();
	if (Selection)
	{
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			ASourceEntityActor* Actor = Cast<ASourceEntityActor>(*It);
			if (Actor)
			{
				USourceIOGraphNode* Node = Graph->FindNodeForActor(Actor);
				if (Node)
				{
					GraphEditorWidget->SetNodeSelection(Node, true);
				}
			}
		}
	}

	bSyncingSelection = false;
}

void SSourceIOGraphEditor::OnActorAdded(AActor* Actor)
{
	ASourceEntityActor* SourceActor = Cast<ASourceEntityActor>(Actor);
	if (!SourceActor || !Graph) return;

	// Check if we already have a node for this actor
	if (Graph->FindNodeForActor(SourceActor)) return;

	// Add a new node
	FGraphNodeCreator<USourceIOGraphNode> NodeCreator(*Graph);
	USourceIOGraphNode* Node = NodeCreator.CreateNode(false);
	Node->InitFromActor(SourceActor);
	NodeCreator.Finalize();

	// Place it at a reasonable position
	float MaxX = 0;
	for (UEdGraphNode* Existing : Graph->Nodes)
	{
		if (Existing)
		{
			MaxX = FMath::Max(MaxX, (float)Existing->NodePosX);
		}
	}
	Node->NodePosX = MaxX + 400;
	Node->NodePosY = 0;

	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->NotifyGraphChanged();
	}
}

void SSourceIOGraphEditor::OnActorDeleted(AActor* Actor)
{
	ASourceEntityActor* SourceActor = Cast<ASourceEntityActor>(Actor);
	if (!SourceActor || !Graph) return;

	USourceIOGraphNode* Node = Graph->FindNodeForActor(SourceActor);
	if (!Node) return;

	// Remove the node from the graph (don't destroy the actor - it's already being deleted)
	Graph->bIsRebuilding = true;
	Graph->RemoveNode(Node);
	Graph->bIsRebuilding = false;

	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->NotifyGraphChanged();
	}
}

// ============================================================================
// FSourceIOGraphTab
// ============================================================================

const FName FSourceIOGraphTab::TabId(TEXT("SourceIOGraph"));

void FSourceIOGraphTab::Register()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FSourceIOGraphTab::SpawnTab))
		.SetDisplayName(LOCTEXT("IOGraphTabTitle", "Source I/O Graph"))
		.SetTooltipText(LOCTEXT("IOGraphTabTooltip", "Visual node graph for Source entity I/O connections"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.GameSettings"));
}

void FSourceIOGraphTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedRef<SDockTab> FSourceIOGraphTab::SpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("IOGraphTabLabel", "Source I/O"))
		[
			SNew(SSourceIOGraphEditor)
		];
}

#undef LOCTEXT_NAMESPACE
