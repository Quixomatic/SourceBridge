#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class USoundWave;
class SWidgetSwitcher;

/** Display entry for model list */
struct FModelDisplayEntry
{
	FString SourcePath;
	FString TypeBadge; // "S", "I", "C"
	int32 FilterType = 0; // 1=Stock, 2=Imported, 3=Custom
	FString SurfaceProp;
	bool bIsStaticProp = true;
	float Mass = 0.0f;
	FSoftObjectPath MeshAsset;
};

/** Display entry for sound list */
struct FSoundDisplayEntry
{
	FString SourcePath;
	FString TypeBadge;
	int32 FilterType = 0;
	float Duration = 0.0f;
	int32 SampleRate = 0;
	int32 NumChannels = 0;
	FSoftObjectPath SoundAsset;
};

/** Display entry for resource list */
struct FResourceDisplayEntry
{
	FString SourcePath;
	FString TypeBadge;
	int32 FilterType = 0;
	FString ResourceTypeStr;
	FString DiskPath;
};

enum class EAssetManagerTab : uint8
{
	Materials = 0,
	Models = 1,
	Sounds = 2,
	Resources = 3
};

/**
 * Unified Source Asset Manager panel.
 * Tabbed browser for all Source assets: Materials, Models, Sounds, Resources.
 */
class SSourceAssetManager : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSourceAssetManager) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// Tab management
	void SetActiveTab(EAssetManagerTab Tab);
	EAssetManagerTab ActiveTab = EAssetManagerTab::Materials;
	TSharedPtr<SWidgetSwitcher> ContentSwitcher;

	// Filter
	FString SearchText;
	int32 TypeFilter = 0; // 0=All, 1=Stock, 2=Imported, 3=Custom

	// UI helpers
	TSharedRef<SWidget> MakeTabButton(EAssetManagerTab Tab, const FText& Label);
	TSharedRef<SWidget> MakeFilterButton(int32 FilterIndex, const FText& Label);

	// Models
	TArray<TSharedPtr<FModelDisplayEntry>> AllModels;
	TArray<TSharedPtr<FModelDisplayEntry>> FilteredModels;
	TSharedPtr<SListView<TSharedPtr<FModelDisplayEntry>>> ModelListView;
	void RefreshModels();
	void ApplyModelFilter();
	TSharedRef<ITableRow> OnGenerateModelRow(TSharedPtr<FModelDisplayEntry> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedPtr<SWidget> OnModelContextMenu();

	// Sounds
	TArray<TSharedPtr<FSoundDisplayEntry>> AllSounds;
	TArray<TSharedPtr<FSoundDisplayEntry>> FilteredSounds;
	TSharedPtr<SListView<TSharedPtr<FSoundDisplayEntry>>> SoundListView;
	void RefreshSounds();
	void ApplySoundFilter();
	TSharedRef<ITableRow> OnGenerateSoundRow(TSharedPtr<FSoundDisplayEntry> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedPtr<SWidget> OnSoundContextMenu();
	void PlaySound(TSharedPtr<FSoundDisplayEntry> Entry);
	void StopSound();
	FString CurrentlyPlayingPath;

	// Resources
	TArray<TSharedPtr<FResourceDisplayEntry>> AllResources;
	TArray<TSharedPtr<FResourceDisplayEntry>> FilteredResources;
	TSharedPtr<SListView<TSharedPtr<FResourceDisplayEntry>>> ResourceListView;
	void RefreshResources();
	void ApplyResourceFilter();
	TSharedRef<ITableRow> OnGenerateResourceRow(TSharedPtr<FResourceDisplayEntry> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedPtr<SWidget> OnResourceContextMenu();

	// Common
	FText GetStatsText() const;
	void CopySourcePath(const FString& Path);
	void BrowseToAsset(const FSoftObjectPath& AssetPath);
};

/** Tab registration (nomad tab) */
class FSourceAssetManagerTab
{
public:
	static void Register();
	static void Unregister();

private:
	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
	static const FName TabId;
};
