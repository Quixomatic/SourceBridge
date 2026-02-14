#include "UI/SourceAssetManager.h"
#include "UI/SSourceMaterialBrowser.h"
#include "Models/SourceModelManifest.h"
#include "Import/SourceSoundManifest.h"
#include "Import/SourceResourceManifest.h"
#include "Materials/SourceMaterialManifest.h"
#include "Sound/SoundWave.h"
#include "Editor.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "SourceAssetManager"

// ---- Tab Registration ----

const FName FSourceAssetManagerTab::TabId(TEXT("SourceAssetManager"));

void FSourceAssetManagerTab::Register()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FSourceAssetManagerTab::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Source Asset Manager"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Browse and manage all Source engine assets"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.ContentBrowser"));
}

void FSourceAssetManagerTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedRef<SDockTab> FSourceAssetManagerTab::SpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("TabLabel", "Source Assets"))
		[
			SNew(SSourceAssetManager)
		];
}

// ---- Widget Construction ----

void SSourceAssetManager::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// Tab bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 4, 4, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[ MakeTabButton(EAssetManagerTab::Materials, LOCTEXT("Tab_Materials", "Materials")) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[ MakeTabButton(EAssetManagerTab::Models, LOCTEXT("Tab_Models", "Models")) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[ MakeTabButton(EAssetManagerTab::Sounds, LOCTEXT("Tab_Sounds", "Sounds")) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[ MakeTabButton(EAssetManagerTab::Resources, LOCTEXT("Tab_Resources", "Resources")) ]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SSeparator)
		]

		// Filter bar (hidden for Materials tab which has its own filters)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 2)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this]() {
				return ActiveTab != EAssetManagerTab::Materials
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[ MakeFilterButton(0, LOCTEXT("Filter_All", "All")) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[ MakeFilterButton(1, LOCTEXT("Filter_Stock", "Stock")) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[ MakeFilterButton(2, LOCTEXT("Filter_Imported", "Imported")) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[ MakeFilterButton(3, LOCTEXT("Filter_Custom", "Custom")) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
			[
				SNew(SButton)
				.OnClicked_Lambda([this]() {
					switch (ActiveTab) {
					case EAssetManagerTab::Models: RefreshModels(); break;
					case EAssetManagerTab::Sounds: RefreshSounds(); break;
					case EAssetManagerTab::Resources: RefreshResources(); break;
					default: break;
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Refresh", "Refresh"))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(8, 0)
			[
				SNew(SSearchBox)
				.OnTextChanged_Lambda([this](const FText& Text) {
					SearchText = Text.ToString();
					switch (ActiveTab) {
					case EAssetManagerTab::Models: ApplyModelFilter(); break;
					case EAssetManagerTab::Sounds: ApplySoundFilter(); break;
					case EAssetManagerTab::Resources: ApplyResourceFilter(); break;
					default: break;
					}
				})
			]
		]

		// Content area
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4)
		[
			SAssignNew(ContentSwitcher, SWidgetSwitcher)

			// Materials tab (index 0) - embed existing browser
			+ SWidgetSwitcher::Slot()
			[
				SNew(SSourceMaterialBrowser)
			]

			// Models tab (index 1)
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(ModelListView, SListView<TSharedPtr<FModelDisplayEntry>>)
				.ListItemsSource(&FilteredModels)
				.OnGenerateRow(this, &SSourceAssetManager::OnGenerateModelRow)
				.OnContextMenuOpening(this, &SSourceAssetManager::OnModelContextMenu)
				.SelectionMode(ESelectionMode::Single)
			]

			// Sounds tab (index 2)
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(SoundListView, SListView<TSharedPtr<FSoundDisplayEntry>>)
				.ListItemsSource(&FilteredSounds)
				.OnGenerateRow(this, &SSourceAssetManager::OnGenerateSoundRow)
				.OnContextMenuOpening(this, &SSourceAssetManager::OnSoundContextMenu)
				.SelectionMode(ESelectionMode::Single)
			]

			// Resources tab (index 3)
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(ResourceListView, SListView<TSharedPtr<FResourceDisplayEntry>>)
				.ListItemsSource(&FilteredResources)
				.OnGenerateRow(this, &SSourceAssetManager::OnGenerateResourceRow)
				.OnContextMenuOpening(this, &SSourceAssetManager::OnResourceContextMenu)
				.SelectionMode(ESelectionMode::Single)
			]
		]

		// Stats bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 4)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return GetStatsText(); })
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		]
	];
}

// ---- Tab Buttons ----

TSharedRef<SWidget> SSourceAssetManager::MakeTabButton(EAssetManagerTab Tab, const FText& Label)
{
	return SNew(SButton)
		.OnClicked_Lambda([this, Tab]() {
			SetActiveTab(Tab);
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(Label)
			.Font_Lambda([this, Tab]() {
				return ActiveTab == Tab
					? FCoreStyle::GetDefaultFontStyle("Bold", 11)
					: FCoreStyle::GetDefaultFontStyle("Regular", 10);
			})
			.ColorAndOpacity_Lambda([this, Tab]() {
				return ActiveTab == Tab
					? FSlateColor(FLinearColor::White)
					: FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f));
			})
		];
}

TSharedRef<SWidget> SSourceAssetManager::MakeFilterButton(int32 FilterIndex, const FText& Label)
{
	return SNew(SButton)
		.OnClicked_Lambda([this, FilterIndex]() {
			TypeFilter = FilterIndex;
			switch (ActiveTab) {
			case EAssetManagerTab::Models: ApplyModelFilter(); break;
			case EAssetManagerTab::Sounds: ApplySoundFilter(); break;
			case EAssetManagerTab::Resources: ApplyResourceFilter(); break;
			default: break;
			}
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(Label)
			.ColorAndOpacity_Lambda([this, FilterIndex]() {
				return TypeFilter == FilterIndex
					? FSlateColor(FLinearColor::White)
					: FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f));
			})
		];
}

// ---- Tab Switching ----

void SSourceAssetManager::SetActiveTab(EAssetManagerTab Tab)
{
	ActiveTab = Tab;
	if (ContentSwitcher.IsValid())
	{
		ContentSwitcher->SetActiveWidgetIndex((int32)Tab);
	}

	switch (Tab)
	{
	case EAssetManagerTab::Models: RefreshModels(); break;
	case EAssetManagerTab::Sounds: RefreshSounds(); break;
	case EAssetManagerTab::Resources: RefreshResources(); break;
	default: break;
	}
}

// ---- Models ----

void SSourceAssetManager::RefreshModels()
{
	AllModels.Empty();

	USourceModelManifest* Manifest = USourceModelManifest::Get();
	if (Manifest)
	{
		for (const FSourceModelEntry& Entry : Manifest->Entries)
		{
			TSharedPtr<FModelDisplayEntry> Display = MakeShared<FModelDisplayEntry>();
			Display->SourcePath = Entry.SourcePath;
			Display->SurfaceProp = Entry.SurfaceProp;
			Display->bIsStaticProp = Entry.bIsStaticProp;
			Display->Mass = Entry.ModelMass;
			Display->MeshAsset = Entry.MeshAsset;
			Display->bForcePack = Entry.bForcePack;

			switch (Entry.Type)
			{
			case ESourceModelType::Stock:
				Display->TypeBadge = TEXT("S");
				Display->FilterType = 1;
				break;
			case ESourceModelType::Imported:
				Display->TypeBadge = TEXT("I");
				Display->FilterType = 2;
				break;
			case ESourceModelType::Custom:
				Display->TypeBadge = TEXT("C");
				Display->FilterType = 3;
				break;
			}

			AllModels.Add(Display);
		}
	}

	ApplyModelFilter();
}

void SSourceAssetManager::ApplyModelFilter()
{
	FilteredModels.Empty();

	for (const TSharedPtr<FModelDisplayEntry>& Entry : AllModels)
	{
		if (TypeFilter != 0 && Entry->FilterType != TypeFilter) continue;
		if (!SearchText.IsEmpty() && !Entry->SourcePath.Contains(SearchText)) continue;
		FilteredModels.Add(Entry);
	}

	if (ModelListView.IsValid())
	{
		ModelListView->RequestListRefresh();
	}
}

static FSlateColor GetTypeBadgeColor(const FString& Badge)
{
	if (Badge == TEXT("S")) return FSlateColor(FLinearColor(0.3f, 0.7f, 0.3f));
	if (Badge == TEXT("I")) return FSlateColor(FLinearColor(0.3f, 0.5f, 1.0f));
	return FSlateColor(FLinearColor(1.0f, 0.7f, 0.3f)); // Custom
}

TSharedRef<ITableRow> SSourceAssetManager::OnGenerateModelRow(
	TSharedPtr<FModelDisplayEntry> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FModelDisplayEntry>>, OwnerTable)
	[
		SNew(SHorizontalBox)

		// Type badge
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 2)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.Padding(FMargin(4, 1))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->TypeBadge))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(GetTypeBadgeColor(Item->TypeBadge))
			]
		]

		// Force-pack indicator
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 2)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ForcePack", "P"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.4f, 0.1f)))
			.Visibility(Item->bForcePack ? EVisibility::Visible : EVisibility::Collapsed)
		]

		// Source path
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(4, 2)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->SourcePath))
		]

		// Surface prop
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 2)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->SurfaceProp))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		]

		// Static/dynamic
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 2)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Item->bIsStaticProp
				? LOCTEXT("Static", "static")
				: LOCTEXT("Dynamic", "dynamic"))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
		]
	];
}

TSharedPtr<SWidget> SSourceAssetManager::OnModelContextMenu()
{
	TArray<TSharedPtr<FModelDisplayEntry>> Selected = ModelListView->GetSelectedItems();
	if (Selected.Num() == 0) return nullptr;

	TSharedPtr<FModelDisplayEntry> Item = Selected[0];

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CopyModelPath", "Copy Source Path"),
		LOCTEXT("CopyModelPathTip", "Copy the Source engine path to clipboard"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, Path = Item->SourcePath]() {
			CopySourcePath(Path);
		}))
	);

	if (!Item->MeshAsset.GetAssetPathString().IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenModelInCB", "Open in Content Browser"),
			LOCTEXT("OpenModelInCBTip", "Navigate to this asset in the Content Browser"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Asset = Item->MeshAsset]() {
				BrowseToAsset(Asset);
			}))
		);
	}

	MenuBuilder.AddSeparator();

	MenuBuilder.AddMenuEntry(
		Item->bForcePack
			? LOCTEXT("UnforcePackModel", "Remove Force Pack")
			: LOCTEXT("ForcePackModel", "Force Pack"),
		LOCTEXT("ForcePackModelTip", "Always include this model in exports regardless of entity references"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, Path = Item->SourcePath, bCurrent = Item->bForcePack]() {
			USourceModelManifest* Manifest = USourceModelManifest::Get();
			if (Manifest)
			{
				FSourceModelEntry* Entry = Manifest->FindBySourcePath(Path);
				if (Entry)
				{
					Entry->bForcePack = !bCurrent;
					Manifest->MarkDirty();
					Manifest->SaveManifest();
					RefreshModels();
				}
			}
		}))
	);

	return MenuBuilder.MakeWidget();
}

// ---- Sounds ----

void SSourceAssetManager::RefreshSounds()
{
	AllSounds.Empty();

	USourceSoundManifest* Manifest = USourceSoundManifest::Get();
	if (Manifest)
	{
		for (const FSourceSoundEntry& Entry : Manifest->Entries)
		{
			TSharedPtr<FSoundDisplayEntry> Display = MakeShared<FSoundDisplayEntry>();
			Display->SourcePath = Entry.SourcePath;
			Display->Duration = Entry.Duration;
			Display->SampleRate = Entry.SampleRate;
			Display->NumChannels = Entry.NumChannels;
			Display->SoundAsset = Entry.SoundAsset;
			Display->bForcePack = Entry.bForcePack;

			switch (Entry.Type)
			{
			case ESourceSoundType::Stock:
				Display->TypeBadge = TEXT("S");
				Display->FilterType = 1;
				break;
			case ESourceSoundType::Imported:
				Display->TypeBadge = TEXT("I");
				Display->FilterType = 2;
				break;
			case ESourceSoundType::Custom:
				Display->TypeBadge = TEXT("C");
				Display->FilterType = 3;
				break;
			}

			AllSounds.Add(Display);
		}
	}

	ApplySoundFilter();
}

void SSourceAssetManager::ApplySoundFilter()
{
	FilteredSounds.Empty();

	for (const TSharedPtr<FSoundDisplayEntry>& Entry : AllSounds)
	{
		if (TypeFilter != 0 && Entry->FilterType != TypeFilter) continue;
		if (!SearchText.IsEmpty() && !Entry->SourcePath.Contains(SearchText)) continue;
		FilteredSounds.Add(Entry);
	}

	if (SoundListView.IsValid())
	{
		SoundListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SSourceAssetManager::OnGenerateSoundRow(
	TSharedPtr<FSoundDisplayEntry> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FSoundDisplayEntry>>, OwnerTable)
	[
		SNew(SHorizontalBox)

		// Play/Stop button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 2)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.OnClicked_Lambda([this, Item]() {
				if (CurrentlyPlayingPath == Item->SourcePath)
				{
					StopSound();
				}
				else
				{
					PlaySound(Item);
				}
				return FReply::Handled();
			})
			.ToolTipText(LOCTEXT("PlayStopTip", "Play / Stop"))
			[
				SNew(STextBlock)
				.Text_Lambda([this, Item]() {
					return CurrentlyPlayingPath == Item->SourcePath
						? LOCTEXT("StopIcon", "||")
						: LOCTEXT("PlayIcon", ">");
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
		]

		// Type badge
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 2)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.Padding(FMargin(4, 1))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->TypeBadge))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(GetTypeBadgeColor(Item->TypeBadge))
			]
		]

		// Force-pack indicator
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 2)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ForcePackSound", "P"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.4f, 0.1f)))
			.Visibility(Item->bForcePack ? EVisibility::Visible : EVisibility::Collapsed)
		]

		// Source path
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(4, 2)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->SourcePath))
		]

		// Duration
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 2)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("%.1fs"), Item->Duration)))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		]

		// Sample rate / channels
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 2)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("%dHz %dch"),
				Item->SampleRate, Item->NumChannels)))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		]
	];
}

TSharedPtr<SWidget> SSourceAssetManager::OnSoundContextMenu()
{
	TArray<TSharedPtr<FSoundDisplayEntry>> Selected = SoundListView->GetSelectedItems();
	if (Selected.Num() == 0) return nullptr;

	TSharedPtr<FSoundDisplayEntry> Item = Selected[0];

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("PlaySound", "Play"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, Item]() {
			PlaySound(Item);
		}))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("StopSound", "Stop"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() {
			StopSound();
		}))
	);

	MenuBuilder.AddSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CopySoundPath", "Copy Source Path"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, Path = Item->SourcePath]() {
			CopySourcePath(Path);
		}))
	);

	if (!Item->SoundAsset.GetAssetPathString().IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenSoundInCB", "Open in Content Browser"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Asset = Item->SoundAsset]() {
				BrowseToAsset(Asset);
			}))
		);
	}

	MenuBuilder.AddSeparator();

	MenuBuilder.AddMenuEntry(
		Item->bForcePack
			? LOCTEXT("UnforcePackSound", "Remove Force Pack")
			: LOCTEXT("ForcePackSound2", "Force Pack"),
		LOCTEXT("ForcePackSoundTip", "Always include this sound in exports regardless of entity references"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, Path = Item->SourcePath, bCurrent = Item->bForcePack]() {
			USourceSoundManifest* Manifest = USourceSoundManifest::Get();
			if (Manifest)
			{
				FSourceSoundEntry* Entry = Manifest->FindBySourcePath(Path);
				if (Entry)
				{
					Entry->bForcePack = !bCurrent;
					Manifest->MarkDirty();
					Manifest->SaveManifest();
					RefreshSounds();
				}
			}
		}))
	);

	return MenuBuilder.MakeWidget();
}

void SSourceAssetManager::PlaySound(TSharedPtr<FSoundDisplayEntry> Entry)
{
	StopSound();

	USoundWave* Sound = Cast<USoundWave>(Entry->SoundAsset.TryLoad());
	if (Sound && GEditor)
	{
		GEditor->PlayPreviewSound(Sound);
		CurrentlyPlayingPath = Entry->SourcePath;
	}
}

void SSourceAssetManager::StopSound()
{
	if (GEditor)
	{
		GEditor->ResetPreviewAudioComponent();
	}
	CurrentlyPlayingPath.Empty();
}

// ---- Resources ----

void SSourceAssetManager::RefreshResources()
{
	AllResources.Empty();

	USourceResourceManifest* Manifest = USourceResourceManifest::Get();
	if (Manifest)
	{
		for (const FSourceResourceEntry& Entry : Manifest->Entries)
		{
			TSharedPtr<FResourceDisplayEntry> Display = MakeShared<FResourceDisplayEntry>();
			Display->SourcePath = Entry.SourcePath;
			Display->DiskPath = Entry.DiskPath;
			Display->bForcePack = Entry.bForcePack;

			switch (Entry.ResourceType)
			{
			case ESourceResourceType::Overview: Display->ResourceTypeStr = TEXT("Overview"); break;
			case ESourceResourceType::OverviewConfig: Display->ResourceTypeStr = TEXT("Config"); break;
			case ESourceResourceType::DetailSprites: Display->ResourceTypeStr = TEXT("Detail Sprites"); break;
			case ESourceResourceType::LoadingScreen: Display->ResourceTypeStr = TEXT("Loading Screen"); break;
			default: Display->ResourceTypeStr = TEXT("Other"); break;
			}

			switch (Entry.Origin)
			{
			case ESourceResourceOrigin::Stock:
				Display->TypeBadge = TEXT("S");
				Display->FilterType = 1;
				break;
			case ESourceResourceOrigin::Imported:
				Display->TypeBadge = TEXT("I");
				Display->FilterType = 2;
				break;
			case ESourceResourceOrigin::Custom:
				Display->TypeBadge = TEXT("C");
				Display->FilterType = 3;
				break;
			}

			AllResources.Add(Display);
		}
	}

	ApplyResourceFilter();
}

void SSourceAssetManager::ApplyResourceFilter()
{
	FilteredResources.Empty();

	for (const TSharedPtr<FResourceDisplayEntry>& Entry : AllResources)
	{
		if (TypeFilter != 0 && Entry->FilterType != TypeFilter) continue;
		if (!SearchText.IsEmpty() && !Entry->SourcePath.Contains(SearchText)) continue;
		FilteredResources.Add(Entry);
	}

	if (ResourceListView.IsValid())
	{
		ResourceListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SSourceAssetManager::OnGenerateResourceRow(
	TSharedPtr<FResourceDisplayEntry> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FResourceDisplayEntry>>, OwnerTable)
	[
		SNew(SHorizontalBox)

		// Type badge
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 2)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.Padding(FMargin(4, 1))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->TypeBadge))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(GetTypeBadgeColor(Item->TypeBadge))
			]
		]

		// Force-pack indicator
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 2)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ForcePackRes", "P"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.4f, 0.1f)))
			.Visibility(Item->bForcePack ? EVisibility::Visible : EVisibility::Collapsed)
		]

		// Resource type
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 2)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->ResourceTypeStr))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		// Source path
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(4, 2)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->SourcePath))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
		]
	];
}

TSharedPtr<SWidget> SSourceAssetManager::OnResourceContextMenu()
{
	TArray<TSharedPtr<FResourceDisplayEntry>> Selected = ResourceListView->GetSelectedItems();
	if (Selected.Num() == 0) return nullptr;

	TSharedPtr<FResourceDisplayEntry> Item = Selected[0];

	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CopyResPath", "Copy Source Path"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, Path = Item->SourcePath]() {
			CopySourcePath(Path);
		}))
	);

	if (!Item->DiskPath.IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CopyDiskPath", "Copy Disk Path"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Path = Item->DiskPath]() {
				CopySourcePath(Path);
			}))
		);
	}

	MenuBuilder.AddSeparator();

	MenuBuilder.AddMenuEntry(
		Item->bForcePack
			? LOCTEXT("UnforcePackRes", "Remove Force Pack")
			: LOCTEXT("ForcePackRes2", "Force Pack"),
		LOCTEXT("ForcePackResTip", "Always include this resource in exports"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, Path = Item->SourcePath, bCurrent = Item->bForcePack]() {
			USourceResourceManifest* Manifest = USourceResourceManifest::Get();
			if (Manifest)
			{
				FSourceResourceEntry* Entry = Manifest->FindBySourcePath(Path);
				if (Entry)
				{
					Entry->bForcePack = !bCurrent;
					Manifest->MarkDirty();
					Manifest->SaveManifest();
					RefreshResources();
				}
			}
		}))
	);

	return MenuBuilder.MakeWidget();
}

// ---- Common Helpers ----

FText SSourceAssetManager::GetStatsText() const
{
	USourceMaterialManifest* MatManifest = USourceMaterialManifest::Get();
	USourceModelManifest* ModelManifest = USourceModelManifest::Get();
	USourceSoundManifest* SoundManifest = USourceSoundManifest::Get();
	USourceResourceManifest* ResourceManifest = USourceResourceManifest::Get();

	int32 MatCount = MatManifest ? MatManifest->Num() : 0;
	int32 ModelCount = ModelManifest ? ModelManifest->Num() : 0;
	int32 SoundCount = SoundManifest ? SoundManifest->Num() : 0;
	int32 ResCount = ResourceManifest ? ResourceManifest->Num() : 0;

	return FText::FromString(FString::Printf(
		TEXT("%d Materials | %d Models | %d Sounds | %d Resources"),
		MatCount, ModelCount, SoundCount, ResCount));
}

void SSourceAssetManager::CopySourcePath(const FString& Path)
{
	FPlatformApplicationMisc::ClipboardCopy(*Path);
}

void SSourceAssetManager::BrowseToAsset(const FSoftObjectPath& AssetPath)
{
	UObject* Asset = AssetPath.TryLoad();
	if (Asset && GEditor)
	{
		TArray<UObject*> Objects;
		Objects.Add(Asset);
		GEditor->SyncBrowserToObjects(Objects);
	}
}

#undef LOCTEXT_NAMESPACE
