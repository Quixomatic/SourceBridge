#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class USourceIOGraph;
class SGraphEditor;
class ASourceEntityActor;

/**
 * Main editor widget for the Source I/O node graph.
 * Contains an SGraphEditor plus a toolbar with Refresh/AutoLayout buttons.
 * Handles bidirectional selection sync between graph and viewport.
 */
class SSourceIOGraphEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSourceIOGraphEditor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SSourceIOGraphEditor();

private:
	/** The graph data model. */
	USourceIOGraph* Graph = nullptr;

	/** The graph editor widget. */
	TSharedPtr<SGraphEditor> GraphEditorWidget;

	/** Build/rebuild the graph from the current world. */
	void BuildGraph();

	/** Toolbar actions. */
	FReply OnRefreshClicked();
	FReply OnAutoLayoutClicked();

	/** Selection sync: graph -> viewport. */
	void OnGraphSelectionChanged(const TSet<class UObject*>& NewSelection);

	/** Selection sync: viewport -> graph. */
	void OnEditorSelectionChanged(UObject* Object);

	/** World change detection. */
	void OnActorAdded(AActor* Actor);
	void OnActorDeleted(AActor* Actor);

	/** Delegate handles for cleanup. */
	FDelegateHandle EditorSelectionChangedHandle;
	FDelegateHandle ActorAddedHandle;
	FDelegateHandle ActorDeletedHandle;

	/** Prevents recursive selection sync loops. */
	bool bSyncingSelection = false;
};

/**
 * Tab registration for the Source I/O Graph editor.
 */
class FSourceIOGraphTab
{
public:
	static void Register();
	static void Unregister();

private:
	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
	static const FName TabId;
};
