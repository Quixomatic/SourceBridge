#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"
#include "Styling/SlateBrush.h"
#include "Materials/SourceMaterialManifest.h"

/** Type of material source for filtering. */
enum class EMaterialBrowserSource : uint8
{
	All,
	Stock,
	Imported,
	Custom
};

/** An entry in the material browser list. */
struct FMaterialBrowserEntry
{
	/** Source engine path (e.g. "concrete/concretefloor001a") */
	FString SourcePath;

	/** Display name (filename portion) */
	FString DisplayName;

	/** Category/folder (e.g. "concrete") */
	FString Category;

	/** Material type */
	ESourceMaterialType Type = ESourceMaterialType::Stock;

	/** VMT shader name (if known) */
	FString Shader;

	/** UE material interface (if loaded/imported) */
	TWeakObjectPtr<UMaterialInterface> UEMaterial;

	/** UE texture (if loaded/imported) */
	TWeakObjectPtr<UTexture2D> UETexture;

	/** Whether this is in the manifest */
	bool bInManifest = false;

	/** Thumbnail slate brush (created lazily from UETexture or VTF decode) */
	TSharedPtr<FSlateBrush> ThumbnailBrush;

	/** Whether we've attempted to load the thumbnail */
	bool bThumbnailLoaded = false;
};

/** A node in the category tree. */
struct FMaterialCategoryNode
{
	FString Name;
	FString FullPath;
	TArray<TSharedPtr<FMaterialCategoryNode>> Children;
	int32 MaterialCount = 0;
};

/**
 * Source Material Browser widget.
 * Unified panel for browsing and applying Source materials from all sources:
 * Stock (VPK), Imported (manifest), and Custom (UE Content Browser).
 */
class SSourceMaterialBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSourceMaterialBrowser) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// ---- Data ----

	TArray<TSharedPtr<FMaterialBrowserEntry>> AllMaterials;
	TArray<TSharedPtr<FMaterialBrowserEntry>> FilteredMaterials;
	TArray<TSharedPtr<FMaterialCategoryNode>> RootCategories;

	EMaterialBrowserSource CurrentSource = EMaterialBrowserSource::All;
	FString SearchFilter;
	FString SelectedCategory;
	TSharedPtr<FMaterialBrowserEntry> SelectedMaterial;

	// Recently used materials (kept as Source paths)
	TArray<FString> RecentlyUsed;
	static constexpr int32 MaxRecentlyUsed = 20;

	// ---- Widgets ----

	TSharedPtr<SListView<TSharedPtr<FMaterialBrowserEntry>>> MaterialListView;
	TSharedPtr<STreeView<TSharedPtr<FMaterialCategoryNode>>> CategoryTreeView;

	// ---- Data Population ----

	void RefreshAllMaterials();
	void LoadStockMaterials();
	void LoadImportedMaterials();
	void LoadCustomMaterials();
	void BuildCategoryTree();
	void ApplyFilter();

	// ---- Event Handlers ----

	void OnSearchTextChanged(const FText& NewText);
	void OnSourceChanged(EMaterialBrowserSource NewSource);

	// Category tree
	TSharedRef<ITableRow> OnGenerateCategoryRow(TSharedPtr<FMaterialCategoryNode> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetCategoryChildren(TSharedPtr<FMaterialCategoryNode> Item, TArray<TSharedPtr<FMaterialCategoryNode>>& OutChildren);
	void OnCategorySelectionChanged(TSharedPtr<FMaterialCategoryNode> Item, ESelectInfo::Type SelectInfo);

	// Material list
	TSharedRef<ITableRow> OnGenerateMaterialRow(TSharedPtr<FMaterialBrowserEntry> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnMaterialSelectionChanged(TSharedPtr<FMaterialBrowserEntry> Item, ESelectInfo::Type SelectInfo);
	void OnMaterialDoubleClicked(TSharedPtr<FMaterialBrowserEntry> Item);

	// Apply
	FReply OnApplyToSelected();
	FReply OnApplyToAllFaces();
	void ApplyMaterialToActor(AActor* Actor, const FString& SourceMaterialPath);
	void AddToRecentlyUsed(const FString& SourcePath);

	// ---- Helpers ----

	FText GetStatusText() const;
	FSlateColor GetSourceButtonColor(EMaterialBrowserSource Source) const;
	void EnsureThumbnail(TSharedPtr<FMaterialBrowserEntry> Entry);
	static TSharedPtr<FSlateBrush> CreateBrushFromTexture(UTexture2D* Texture);
};

/**
 * Registers the Source Material Browser as a nomad tab in the editor.
 */
class FSourceMaterialBrowserTab
{
public:
	static void Register();
	static void Unregister();

private:
	static TSharedRef<class SDockTab> SpawnTab(const class FSpawnTabArgs& Args);
	static const FName TabId;
};
