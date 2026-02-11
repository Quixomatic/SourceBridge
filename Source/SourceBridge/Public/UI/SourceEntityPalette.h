#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

/**
 * An entry in the entity palette.
 */
struct FEntityPaletteEntry
{
	FString ClassName;
	FString DisplayName;
	FString Description;
	FString Category;
	bool bIsSolid = false;
};

/**
 * Source entity palette widget.
 * Shows a categorized, filterable list of Source entities from the loaded FGD.
 * Clicking an entry spawns the corresponding ASourceEntityActor in the viewport.
 */
class SSourceEntityPalette : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSourceEntityPalette) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** Refresh the entity list from the FGD database. */
	void RefreshEntities();

	/** Filter the entity list based on search text. */
	void OnSearchTextChanged(const FText& NewText);

	/** Spawn the selected entity in the viewport. */
	FReply OnSpawnEntity(TSharedPtr<FEntityPaletteEntry> Entry);

	/** Generate a row for the list view. */
	TSharedRef<ITableRow> OnGenerateRow(
		TSharedPtr<FEntityPaletteEntry> Item,
		const TSharedRef<STableViewBase>& OwnerTable);

	TArray<TSharedPtr<FEntityPaletteEntry>> AllEntities;
	TArray<TSharedPtr<FEntityPaletteEntry>> FilteredEntities;
	TSharedPtr<SListView<TSharedPtr<FEntityPaletteEntry>>> EntityListView;
	FString SearchFilter;
};

/**
 * Registers the Source Entity Palette as a nomad tab in the editor.
 */
class FSourceEntityPaletteTab
{
public:
	static void Register();
	static void Unregister();

private:
	static TSharedRef<class SDockTab> SpawnTab(const class FSpawnTabArgs& Args);
	static const FName TabId;
};
