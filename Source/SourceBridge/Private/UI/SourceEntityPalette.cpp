#include "UI/SourceEntityPalette.h"
#include "SourceBridgeModule.h"
#include "Entities/FGDParser.h"
#include "Actors/SourceEntityActor.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Framework/Docking/TabManager.h"

#define LOCTEXT_NAMESPACE "SourceEntityPalette"

const FName FSourceEntityPaletteTab::TabId(TEXT("SourceEntityPalette"));

void SSourceEntityPalette::Construct(const FArguments& InArgs)
{
	RefreshEntities();

	ChildSlot
	[
		SNew(SVerticalBox)

		// Search bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Filter entities..."))
			.OnTextChanged(this, &SSourceEntityPalette::OnSearchTextChanged)
		]

		// Refresh button
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4)
		[
			SNew(SButton)
			.Text(LOCTEXT("Refresh", "Refresh from FGD"))
			.OnClicked_Lambda([this]() -> FReply
			{
				RefreshEntities();
				return FReply::Handled();
			})
		]

		// Entity list
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(EntityListView, SListView<TSharedPtr<FEntityPaletteEntry>>)
			.ListItemsSource(&FilteredEntities)
			.OnGenerateRow(this, &SSourceEntityPalette::OnGenerateRow)
			.SelectionMode(ESelectionMode::Single)
		]
	];
}

void SSourceEntityPalette::RefreshEntities()
{
	AllEntities.Empty();

	const FFGDDatabase& FGD = FSourceBridgeModule::GetFGDDatabase();

	if (FGD.Classes.Num() == 0)
	{
		// Add some built-in common entities even without FGD
		auto AddBuiltin = [this](const FString& Class, const FString& Name, const FString& Desc, const FString& Cat, bool bSolid)
		{
			TSharedPtr<FEntityPaletteEntry> Entry = MakeShared<FEntityPaletteEntry>();
			Entry->ClassName = Class;
			Entry->DisplayName = Name;
			Entry->Description = Desc;
			Entry->Category = Cat;
			Entry->bIsSolid = bSolid;
			AllEntities.Add(Entry);
		};

		AddBuiltin(TEXT("info_player_terrorist"), TEXT("T Spawn"), TEXT("Terrorist spawn point"), TEXT("Spawns"), false);
		AddBuiltin(TEXT("info_player_counterterrorist"), TEXT("CT Spawn"), TEXT("Counter-Terrorist spawn point"), TEXT("Spawns"), false);
		AddBuiltin(TEXT("info_player_start"), TEXT("Player Start"), TEXT("Generic deathmatch spawn"), TEXT("Spawns"), false);
		AddBuiltin(TEXT("info_player_spectator"), TEXT("Spectator"), TEXT("Spectator camera position"), TEXT("Spawns"), false);
		AddBuiltin(TEXT("light"), TEXT("Point Light"), TEXT("Omnidirectional light"), TEXT("Lights"), false);
		AddBuiltin(TEXT("light_spot"), TEXT("Spot Light"), TEXT("Directional spotlight"), TEXT("Lights"), false);
		AddBuiltin(TEXT("light_environment"), TEXT("Environment Light"), TEXT("Sun/sky directional light"), TEXT("Lights"), false);
		AddBuiltin(TEXT("prop_static"), TEXT("Static Prop"), TEXT("Non-moving model"), TEXT("Props"), false);
		AddBuiltin(TEXT("prop_dynamic"), TEXT("Dynamic Prop"), TEXT("Animated/moving model"), TEXT("Props"), false);
		AddBuiltin(TEXT("prop_physics"), TEXT("Physics Prop"), TEXT("Physics-enabled model"), TEXT("Props"), false);
		AddBuiltin(TEXT("trigger_multiple"), TEXT("Trigger (Multiple)"), TEXT("Reusable trigger volume"), TEXT("Triggers"), true);
		AddBuiltin(TEXT("trigger_once"), TEXT("Trigger (Once)"), TEXT("Single-fire trigger volume"), TEXT("Triggers"), true);
		AddBuiltin(TEXT("func_detail"), TEXT("Detail Brush"), TEXT("Non-structural detail geometry"), TEXT("Brushes"), true);
		AddBuiltin(TEXT("func_wall"), TEXT("Wall"), TEXT("Toggleable wall brush"), TEXT("Brushes"), true);
		AddBuiltin(TEXT("func_door"), TEXT("Door"), TEXT("Moving door brush"), TEXT("Brushes"), true);
		AddBuiltin(TEXT("func_breakable"), TEXT("Breakable"), TEXT("Destroyable brush"), TEXT("Brushes"), true);
		AddBuiltin(TEXT("logic_relay"), TEXT("Logic Relay"), TEXT("Fans out I/O signals"), TEXT("Logic"), false);
		AddBuiltin(TEXT("game_text"), TEXT("Game Text"), TEXT("Display text on screen"), TEXT("Logic"), false);
		AddBuiltin(TEXT("ambient_generic"), TEXT("Sound"), TEXT("Play a sound"), TEXT("Effects"), false);
		AddBuiltin(TEXT("env_sprite"), TEXT("Sprite"), TEXT("Visual sprite effect"), TEXT("Effects"), false);
		AddBuiltin(TEXT("env_soundscape"), TEXT("Soundscape"), TEXT("Ambient sound area"), TEXT("Effects"), false);
	}
	else
	{
		TArray<FString> Names = FGD.GetPlaceableClassNames();
		for (const FString& Name : Names)
		{
			const FFGDEntityClass* FGDClass = FGD.FindClass(Name);
			if (!FGDClass) continue;

			TSharedPtr<FEntityPaletteEntry> Entry = MakeShared<FEntityPaletteEntry>();
			Entry->ClassName = FGDClass->ClassName;
			Entry->DisplayName = FGDClass->ClassName;
			Entry->Description = FGDClass->Description;
			Entry->bIsSolid = FGDClass->bIsSolid;

			// Categorize by prefix
			if (Name.StartsWith(TEXT("info_player"))) Entry->Category = TEXT("Spawns");
			else if (Name.StartsWith(TEXT("light"))) Entry->Category = TEXT("Lights");
			else if (Name.StartsWith(TEXT("prop_"))) Entry->Category = TEXT("Props");
			else if (Name.StartsWith(TEXT("trigger_"))) Entry->Category = TEXT("Triggers");
			else if (Name.StartsWith(TEXT("func_"))) Entry->Category = TEXT("Brushes");
			else if (Name.StartsWith(TEXT("logic_"))) Entry->Category = TEXT("Logic");
			else if (Name.StartsWith(TEXT("env_"))) Entry->Category = TEXT("Effects");
			else if (Name.StartsWith(TEXT("ambient_"))) Entry->Category = TEXT("Effects");
			else if (Name.StartsWith(TEXT("game_"))) Entry->Category = TEXT("Game");
			else if (Name.StartsWith(TEXT("point_"))) Entry->Category = TEXT("Point");
			else Entry->Category = TEXT("Other");

			AllEntities.Add(Entry);
		}
	}

	// Sort by category then name
	AllEntities.Sort([](const TSharedPtr<FEntityPaletteEntry>& A, const TSharedPtr<FEntityPaletteEntry>& B)
	{
		if (A->Category != B->Category) return A->Category < B->Category;
		return A->ClassName < B->ClassName;
	});

	FilteredEntities = AllEntities;

	if (EntityListView.IsValid())
	{
		EntityListView->RequestListRefresh();
	}
}

void SSourceEntityPalette::OnSearchTextChanged(const FText& NewText)
{
	SearchFilter = NewText.ToString();

	FilteredEntities.Empty();
	for (const TSharedPtr<FEntityPaletteEntry>& Entry : AllEntities)
	{
		if (SearchFilter.IsEmpty() ||
			Entry->ClassName.Contains(SearchFilter) ||
			Entry->DisplayName.Contains(SearchFilter) ||
			Entry->Description.Contains(SearchFilter) ||
			Entry->Category.Contains(SearchFilter))
		{
			FilteredEntities.Add(Entry);
		}
	}

	if (EntityListView.IsValid())
	{
		EntityListView->RequestListRefresh();
	}
}

FReply SSourceEntityPalette::OnSpawnEntity(TSharedPtr<FEntityPaletteEntry> Entry)
{
	if (!Entry.IsValid()) return FReply::Handled();

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return FReply::Handled();

	// Spawn an ASourceEntityActor at the current viewport camera location
	FVector SpawnLocation = FVector::ZeroVector;
	FRotator SpawnRotation = FRotator::ZeroRotator;

	// Try to get viewport camera position for spawn location
	if (GEditor->GetActiveViewport())
	{
		FEditorViewportClient* ViewClient = static_cast<FEditorViewportClient*>(
			GEditor->GetActiveViewport()->GetClient());
		if (ViewClient)
		{
			SpawnLocation = ViewClient->GetViewLocation() + ViewClient->GetViewRotation().Vector() * 200.0f;
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Spawn the appropriate actor class based on whether this is a brush entity
	ASourceEntityActor* NewActor = nullptr;
	if (Entry->bIsSolid)
	{
		NewActor = World->SpawnActor<ASourceBrushEntity>(
			ASourceBrushEntity::StaticClass(), SpawnLocation, SpawnRotation, SpawnParams);
	}
	else
	{
		NewActor = World->SpawnActor<ASourceGenericEntity>(
			ASourceGenericEntity::StaticClass(), SpawnLocation, SpawnRotation, SpawnParams);
	}

	if (NewActor)
	{
		NewActor->SourceClassname = Entry->ClassName;
		NewActor->SetActorLabel(Entry->ClassName);

		// Select the newly spawned actor
		GEditor->SelectNone(true, true, false);
		GEditor->SelectActor(NewActor, true, true, true);

		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Spawned %s entity at (%s)"),
			*Entry->ClassName, *SpawnLocation.ToString());
	}

	return FReply::Handled();
}

TSharedRef<ITableRow> SSourceEntityPalette::OnGenerateRow(
	TSharedPtr<FEntityPaletteEntry> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	FString TypeLabel = Item->bIsSolid ? TEXT("[Brush]") : TEXT("[Point]");
	FString RowText = FString::Printf(TEXT("[%s] %s"), *Item->Category, *Item->ClassName);

	return SNew(STableRow<TSharedPtr<FEntityPaletteEntry>>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(4, 2)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(RowText))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item->Description.Left(80)))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Spawn", "Spawn"))
				.ToolTipText(FText::Format(
					LOCTEXT("SpawnTooltip", "Spawn a {0} entity in the viewport"),
					FText::FromString(Item->ClassName)))
				.OnClicked(this, &SSourceEntityPalette::OnSpawnEntity, Item)
			]
		];
}

// --- Tab Registration ---

void FSourceEntityPaletteTab::Register()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FSourceEntityPaletteTab::SpawnTab))
		.SetDisplayName(LOCTEXT("PaletteTabTitle", "Source Entity Palette"))
		.SetTooltipText(LOCTEXT("PaletteTabTooltip", "Browse and spawn Source engine entities"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.GameSettings"));
}

void FSourceEntityPaletteTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedRef<SDockTab> FSourceEntityPaletteTab::SpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("PaletteTabLabel", "Source Entities"))
		[
			SNew(SSourceEntityPalette)
		];
}

#undef LOCTEXT_NAMESPACE
