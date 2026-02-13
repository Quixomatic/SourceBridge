#include "UI/SSourceMaterialBrowser.h"
#include "Import/MaterialImporter.h"
#include "Materials/SourceMaterialManifest.h"
#include "Actors/SourceEntityActor.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Selection.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Components/BrushComponent.h"
#include "ProceduralMeshComponent.h"
#include "Model.h"
#include "BSPOps.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Framework/Docking/TabManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SourceMaterialBrowser"

const FName FSourceMaterialBrowserTab::TabId(TEXT("SourceMaterialBrowser"));

// ============================================================================
// Widget Construction
// ============================================================================

void SSourceMaterialBrowser::Construct(const FArguments& InArgs)
{
	RefreshAllMaterials();

	ChildSlot
	[
		SNew(SVerticalBox)

		// Source filter buttons
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
				.Text(LOCTEXT("All", "All"))
				.ButtonColorAndOpacity(this, &SSourceMaterialBrowser::GetSourceButtonColor, EMaterialBrowserSource::All)
				.OnClicked_Lambda([this]() -> FReply { OnSourceChanged(EMaterialBrowserSource::All); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Stock", "Stock"))
				.ButtonColorAndOpacity(this, &SSourceMaterialBrowser::GetSourceButtonColor, EMaterialBrowserSource::Stock)
				.OnClicked_Lambda([this]() -> FReply { OnSourceChanged(EMaterialBrowserSource::Stock); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Imported", "Imported"))
				.ButtonColorAndOpacity(this, &SSourceMaterialBrowser::GetSourceButtonColor, EMaterialBrowserSource::Imported)
				.OnClicked_Lambda([this]() -> FReply { OnSourceChanged(EMaterialBrowserSource::Imported); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Custom", "Custom"))
				.ButtonColorAndOpacity(this, &SSourceMaterialBrowser::GetSourceButtonColor, EMaterialBrowserSource::Custom)
				.OnClicked_Lambda([this]() -> FReply { OnSourceChanged(EMaterialBrowserSource::Custom); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "Refresh"))
				.ToolTipText(LOCTEXT("RefreshTooltip", "Reload materials from VPK archives, manifest, and content browser"))
				.OnClicked_Lambda([this]() -> FReply { RefreshAllMaterials(); return FReply::Handled(); })
			]
		]

		// Search bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Filter materials by name or path..."))
			.OnTextChanged(this, &SSourceMaterialBrowser::OnSearchTextChanged)
		]

		// Main content: category tree | material list
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			// Left: Category tree
			+ SSplitter::Slot()
			.Value(0.3f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(2)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4, 2)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Categories", "Categories"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SAssignNew(CategoryTreeView, STreeView<TSharedPtr<FMaterialCategoryNode>>)
						.TreeItemsSource(&RootCategories)
						.OnGenerateRow(this, &SSourceMaterialBrowser::OnGenerateCategoryRow)
						.OnGetChildren(this, &SSourceMaterialBrowser::OnGetCategoryChildren)
						.OnSelectionChanged(this, &SSourceMaterialBrowser::OnCategorySelectionChanged)
						.SelectionMode(ESelectionMode::Single)
					]
				]
			]

			// Right: Material list
			+ SSplitter::Slot()
			.Value(0.7f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(2)
				[
					SAssignNew(MaterialListView, SListView<TSharedPtr<FMaterialBrowserEntry>>)
					.ListItemsSource(&FilteredMaterials)
					.OnGenerateRow(this, &SSourceMaterialBrowser::OnGenerateMaterialRow)
					.OnSelectionChanged(this, &SSourceMaterialBrowser::OnMaterialSelectionChanged)
					.OnMouseButtonDoubleClick(this, &SSourceMaterialBrowser::OnMaterialDoubleClicked)
					.SelectionMode(ESelectionMode::Single)
				]
			]
		]

		// Bottom: Apply buttons and status
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
				.Text(LOCTEXT("ApplySelected", "Apply to Selected"))
				.ToolTipText(LOCTEXT("ApplySelectedTooltip", "Apply the selected material to the currently selected brush/entity faces"))
				.OnClicked(this, &SSourceMaterialBrowser::OnApplyToSelected)
				.IsEnabled_Lambda([this]() { return SelectedMaterial.IsValid(); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("ApplyAllFaces", "Apply to All Faces"))
				.ToolTipText(LOCTEXT("ApplyAllFacesTooltip", "Apply the selected material to all faces of the selected brushes/entities"))
				.OnClicked(this, &SSourceMaterialBrowser::OnApplyToAllFaces)
				.IsEnabled_Lambda([this]() { return SelectedMaterial.IsValid(); })
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8, 0)
			[
				SNew(STextBlock)
				.Text(this, &SSourceMaterialBrowser::GetStatusText)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
			]
		]
	];
}

// ============================================================================
// Data Population
// ============================================================================

void SSourceMaterialBrowser::RefreshAllMaterials()
{
	AllMaterials.Empty();

	LoadStockMaterials();
	LoadImportedMaterials();
	LoadCustomMaterials();

	// Sort: imported and custom first (most likely to be used), then stock
	AllMaterials.Sort([](const TSharedPtr<FMaterialBrowserEntry>& A, const TSharedPtr<FMaterialBrowserEntry>& B)
	{
		// Type priority: Custom < Imported < Stock (lower = earlier in list)
		int32 PriorityA = (A->Type == ESourceMaterialType::Custom) ? 0 : (A->Type == ESourceMaterialType::Imported) ? 1 : 2;
		int32 PriorityB = (B->Type == ESourceMaterialType::Custom) ? 0 : (B->Type == ESourceMaterialType::Imported) ? 1 : 2;
		if (PriorityA != PriorityB) return PriorityA < PriorityB;
		return A->SourcePath < B->SourcePath;
	});

	BuildCategoryTree();
	ApplyFilter();
}

void SSourceMaterialBrowser::LoadStockMaterials()
{
	TArray<FString> StockPaths = FMaterialImporter::GetStockMaterialPaths();

	// Get manifest for cross-referencing
	USourceMaterialManifest* Manifest = USourceMaterialManifest::Get();

	for (const FString& Path : StockPaths)
	{
		// Skip if already added as imported (manifest takes priority)
		if (Manifest)
		{
			const FSourceMaterialEntry* ManifestEntry = Manifest->FindBySourcePath(Path);
			if (ManifestEntry && ManifestEntry->Type != ESourceMaterialType::Stock)
			{
				continue; // Will be added by LoadImportedMaterials
			}
		}

		TSharedPtr<FMaterialBrowserEntry> Entry = MakeShared<FMaterialBrowserEntry>();
		Entry->SourcePath = Path;
		Entry->Type = ESourceMaterialType::Stock;

		// Extract display name and category from path
		int32 LastSlash;
		if (Path.FindLastChar('/', LastSlash))
		{
			Entry->Category = Path.Left(LastSlash);
			Entry->DisplayName = Path.Mid(LastSlash + 1);
		}
		else
		{
			Entry->Category = TEXT("other");
			Entry->DisplayName = Path;
		}

		// Check manifest for additional info
		if (Manifest)
		{
			const FSourceMaterialEntry* ManifestEntry = Manifest->FindBySourcePath(Path);
			if (ManifestEntry)
			{
				Entry->bInManifest = true;
				Entry->Shader = ManifestEntry->VMTShader;

				UMaterialInterface* Mat = Cast<UMaterialInterface>(ManifestEntry->MaterialAsset.TryLoad());
				if (Mat) Entry->UEMaterial = Mat;

				UTexture2D* Tex = Cast<UTexture2D>(ManifestEntry->TextureAsset.TryLoad());
				if (Tex) Entry->UETexture = Tex;
			}
		}

		AllMaterials.Add(Entry);
	}

	UE_LOG(LogTemp, Log, TEXT("SourceMaterialBrowser: Loaded %d stock materials from VPK"), StockPaths.Num());
}

void SSourceMaterialBrowser::LoadImportedMaterials()
{
	USourceMaterialManifest* Manifest = USourceMaterialManifest::Get();
	if (!Manifest) return;

	TArray<FSourceMaterialEntry*> ImportedEntries = Manifest->GetAllOfType(ESourceMaterialType::Imported);

	for (FSourceMaterialEntry* ManifestEntry : ImportedEntries)
	{
		TSharedPtr<FMaterialBrowserEntry> Entry = MakeShared<FMaterialBrowserEntry>();
		Entry->SourcePath = ManifestEntry->SourcePath;
		Entry->Type = ESourceMaterialType::Imported;
		Entry->bInManifest = true;
		Entry->Shader = ManifestEntry->VMTShader;

		int32 LastSlash;
		if (ManifestEntry->SourcePath.FindLastChar('/', LastSlash))
		{
			Entry->Category = ManifestEntry->SourcePath.Left(LastSlash);
			Entry->DisplayName = ManifestEntry->SourcePath.Mid(LastSlash + 1);
		}
		else
		{
			Entry->Category = TEXT("imported");
			Entry->DisplayName = ManifestEntry->SourcePath;
		}

		UMaterialInterface* Mat = Cast<UMaterialInterface>(ManifestEntry->MaterialAsset.TryLoad());
		if (Mat) Entry->UEMaterial = Mat;

		UTexture2D* Tex = Cast<UTexture2D>(ManifestEntry->TextureAsset.TryLoad());
		if (Tex) Entry->UETexture = Tex;

		AllMaterials.Add(Entry);
	}

	UE_LOG(LogTemp, Log, TEXT("SourceMaterialBrowser: Loaded %d imported materials from manifest"), ImportedEntries.Num());
}

void SSourceMaterialBrowser::LoadCustomMaterials()
{
	USourceMaterialManifest* Manifest = USourceMaterialManifest::Get();
	if (!Manifest) return;

	TArray<FSourceMaterialEntry*> CustomEntries = Manifest->GetAllOfType(ESourceMaterialType::Custom);

	for (FSourceMaterialEntry* ManifestEntry : CustomEntries)
	{
		TSharedPtr<FMaterialBrowserEntry> Entry = MakeShared<FMaterialBrowserEntry>();
		Entry->SourcePath = ManifestEntry->SourcePath;
		Entry->Type = ESourceMaterialType::Custom;
		Entry->bInManifest = true;
		Entry->Shader = ManifestEntry->VMTShader;

		int32 LastSlash;
		if (ManifestEntry->SourcePath.FindLastChar('/', LastSlash))
		{
			Entry->Category = ManifestEntry->SourcePath.Left(LastSlash);
			Entry->DisplayName = ManifestEntry->SourcePath.Mid(LastSlash + 1);
		}
		else
		{
			Entry->Category = TEXT("custom");
			Entry->DisplayName = ManifestEntry->SourcePath;
		}

		UMaterialInterface* Mat = Cast<UMaterialInterface>(ManifestEntry->MaterialAsset.TryLoad());
		if (Mat) Entry->UEMaterial = Mat;

		UTexture2D* Tex = Cast<UTexture2D>(ManifestEntry->TextureAsset.TryLoad());
		if (Tex) Entry->UETexture = Tex;

		AllMaterials.Add(Entry);
	}

	UE_LOG(LogTemp, Log, TEXT("SourceMaterialBrowser: Loaded %d custom materials from manifest"), CustomEntries.Num());
}

void SSourceMaterialBrowser::BuildCategoryTree()
{
	RootCategories.Empty();

	// Collect unique categories from filtered by current source
	TMap<FString, int32> CategoryCounts;
	for (const TSharedPtr<FMaterialBrowserEntry>& Mat : AllMaterials)
	{
		if (CurrentSource != EMaterialBrowserSource::All)
		{
			ESourceMaterialType FilterType;
			switch (CurrentSource)
			{
			case EMaterialBrowserSource::Stock: FilterType = ESourceMaterialType::Stock; break;
			case EMaterialBrowserSource::Imported: FilterType = ESourceMaterialType::Imported; break;
			case EMaterialBrowserSource::Custom: FilterType = ESourceMaterialType::Custom; break;
			default: FilterType = ESourceMaterialType::Stock; break;
			}
			if (Mat->Type != FilterType) continue;
		}

		FString Cat = Mat->Category;
		// Get top-level category
		int32 SlashIdx;
		if (Cat.FindChar('/', SlashIdx))
		{
			Cat = Cat.Left(SlashIdx);
		}

		int32& Count = CategoryCounts.FindOrAdd(Cat);
		Count++;
	}

	// Build flat list of top-level categories (no deep nesting for now — keeps it fast)
	for (const auto& Pair : CategoryCounts)
	{
		TSharedPtr<FMaterialCategoryNode> Node = MakeShared<FMaterialCategoryNode>();
		Node->Name = Pair.Key;
		Node->FullPath = Pair.Key;
		Node->MaterialCount = Pair.Value;

		// Build subcategories
		TMap<FString, int32> SubCounts;
		for (const TSharedPtr<FMaterialBrowserEntry>& Mat : AllMaterials)
		{
			if (!Mat->Category.StartsWith(Pair.Key + TEXT("/"))) continue;
			if (CurrentSource != EMaterialBrowserSource::All)
			{
				ESourceMaterialType FilterType;
				switch (CurrentSource)
				{
				case EMaterialBrowserSource::Stock: FilterType = ESourceMaterialType::Stock; break;
				case EMaterialBrowserSource::Imported: FilterType = ESourceMaterialType::Imported; break;
				case EMaterialBrowserSource::Custom: FilterType = ESourceMaterialType::Custom; break;
				default: FilterType = ESourceMaterialType::Stock; break;
				}
				if (Mat->Type != FilterType) continue;
			}

			FString SubCat = Mat->Category.Mid(Pair.Key.Len() + 1);
			int32 SubSlash;
			if (SubCat.FindChar('/', SubSlash))
			{
				SubCat = SubCat.Left(SubSlash);
			}
			int32& Count = SubCounts.FindOrAdd(SubCat);
			Count++;
		}

		for (const auto& SubPair : SubCounts)
		{
			TSharedPtr<FMaterialCategoryNode> SubNode = MakeShared<FMaterialCategoryNode>();
			SubNode->Name = SubPair.Key;
			SubNode->FullPath = Pair.Key + TEXT("/") + SubPair.Key;
			SubNode->MaterialCount = SubPair.Value;
			Node->Children.Add(SubNode);
		}

		Node->Children.Sort([](const TSharedPtr<FMaterialCategoryNode>& A, const TSharedPtr<FMaterialCategoryNode>& B)
		{
			return A->Name < B->Name;
		});

		RootCategories.Add(Node);
	}

	RootCategories.Sort([](const TSharedPtr<FMaterialCategoryNode>& A, const TSharedPtr<FMaterialCategoryNode>& B)
	{
		return A->Name < B->Name;
	});

	if (CategoryTreeView.IsValid())
	{
		CategoryTreeView->RequestTreeRefresh();
	}
}

void SSourceMaterialBrowser::ApplyFilter()
{
	FilteredMaterials.Empty();

	FString SearchLower = SearchFilter.ToLower();

	for (const TSharedPtr<FMaterialBrowserEntry>& Mat : AllMaterials)
	{
		// Filter by source type
		if (CurrentSource != EMaterialBrowserSource::All)
		{
			ESourceMaterialType FilterType;
			switch (CurrentSource)
			{
			case EMaterialBrowserSource::Stock: FilterType = ESourceMaterialType::Stock; break;
			case EMaterialBrowserSource::Imported: FilterType = ESourceMaterialType::Imported; break;
			case EMaterialBrowserSource::Custom: FilterType = ESourceMaterialType::Custom; break;
			default: FilterType = ESourceMaterialType::Stock; break;
			}
			if (Mat->Type != FilterType) continue;
		}

		// Filter by category
		if (!SelectedCategory.IsEmpty())
		{
			if (!Mat->Category.StartsWith(SelectedCategory))
			{
				continue;
			}
		}

		// Filter by search text
		if (!SearchLower.IsEmpty())
		{
			FString PathLower = Mat->SourcePath.ToLower();
			FString NameLower = Mat->DisplayName.ToLower();
			FString ShaderLower = Mat->Shader.ToLower();

			if (!PathLower.Contains(SearchLower) &&
				!NameLower.Contains(SearchLower) &&
				!ShaderLower.Contains(SearchLower))
			{
				continue;
			}
		}

		FilteredMaterials.Add(Mat);
	}

	if (MaterialListView.IsValid())
	{
		MaterialListView->RequestListRefresh();
	}
}

// ============================================================================
// Event Handlers
// ============================================================================

void SSourceMaterialBrowser::OnSearchTextChanged(const FText& NewText)
{
	SearchFilter = NewText.ToString();
	ApplyFilter();
}

void SSourceMaterialBrowser::OnSourceChanged(EMaterialBrowserSource NewSource)
{
	CurrentSource = NewSource;
	SelectedCategory.Empty();
	BuildCategoryTree();
	ApplyFilter();
}

// ---- Category Tree ----

TSharedRef<ITableRow> SSourceMaterialBrowser::OnGenerateCategoryRow(
	TSharedPtr<FMaterialCategoryNode> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	FString Label = FString::Printf(TEXT("%s (%d)"), *Item->Name, Item->MaterialCount);

	return SNew(STableRow<TSharedPtr<FMaterialCategoryNode>>, OwnerTable)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Label))
			.Margin(FMargin(4, 2))
		];
}

void SSourceMaterialBrowser::OnGetCategoryChildren(
	TSharedPtr<FMaterialCategoryNode> Item,
	TArray<TSharedPtr<FMaterialCategoryNode>>& OutChildren)
{
	if (Item.IsValid())
	{
		OutChildren = Item->Children;
	}
}

void SSourceMaterialBrowser::OnCategorySelectionChanged(
	TSharedPtr<FMaterialCategoryNode> Item,
	ESelectInfo::Type SelectInfo)
{
	if (Item.IsValid())
	{
		SelectedCategory = Item->FullPath;
	}
	else
	{
		SelectedCategory.Empty();
	}
	ApplyFilter();
}

// ---- Material List ----

TSharedRef<ITableRow> SSourceMaterialBrowser::OnGenerateMaterialRow(
	TSharedPtr<FMaterialBrowserEntry> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	// Type label
	FString TypeLabel;
	FLinearColor TypeColor;
	switch (Item->Type)
	{
	case ESourceMaterialType::Stock:
		TypeLabel = TEXT("S");
		TypeColor = FLinearColor(0.4f, 0.6f, 0.9f);
		break;
	case ESourceMaterialType::Imported:
		TypeLabel = TEXT("I");
		TypeColor = FLinearColor(0.4f, 0.9f, 0.4f);
		break;
	case ESourceMaterialType::Custom:
		TypeLabel = TEXT("C");
		TypeColor = FLinearColor(0.9f, 0.7f, 0.3f);
		break;
	}

	// Lazily load thumbnail for this entry
	EnsureThumbnail(Item);

	// Build the thumbnail widget
	TSharedRef<SWidget> ThumbnailWidget =
		Item->ThumbnailBrush.IsValid()
		? StaticCastSharedRef<SWidget>(
			SNew(SBox)
			.WidthOverride(48)
			.HeightOverride(48)
			[
				SNew(SImage)
				.Image(Item->ThumbnailBrush.Get())
			])
		: StaticCastSharedRef<SWidget>(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor_Lambda([SourcePath = Item->SourcePath]()
			{
				uint32 Hash = GetTypeHash(SourcePath);
				float H = (float)(Hash % 360) / 360.0f;
				float S = 0.3f + (float)((Hash >> 8) % 30) / 100.0f;
				float V = 0.4f + (float)((Hash >> 16) % 20) / 100.0f;
				return FSlateColor(FLinearColor::MakeFromHSV8(
					(uint8)(H * 255), (uint8)(S * 255), (uint8)(V * 255)));
			})
			.Padding(0)
			[
				SNew(SBox)
				.WidthOverride(48)
				.HeightOverride(48)
			]);

	return SNew(STableRow<TSharedPtr<FMaterialBrowserEntry>>, OwnerTable)
		[
			SNew(SHorizontalBox)

			// Texture thumbnail or color swatch
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 2)
			[
				ThumbnailWidget
			]

			// Type badge
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(TypeColor)
				.Padding(FMargin(4, 1))
				[
					SNew(STextBlock)
					.Text(FText::FromString(TypeLabel))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor::Black))
				]
			]

			// Material info
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(4, 2)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item->DisplayName))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item->SourcePath))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
				]
			]
		];
}

void SSourceMaterialBrowser::OnMaterialSelectionChanged(
	TSharedPtr<FMaterialBrowserEntry> Item,
	ESelectInfo::Type SelectInfo)
{
	SelectedMaterial = Item;
}

void SSourceMaterialBrowser::OnMaterialDoubleClicked(TSharedPtr<FMaterialBrowserEntry> Item)
{
	if (!Item.IsValid()) return;

	// Double-click = apply to selected actors
	SelectedMaterial = Item;
	OnApplyToSelected();
}

// ============================================================================
// Apply Material
// ============================================================================

FReply SSourceMaterialBrowser::OnApplyToSelected()
{
	if (!SelectedMaterial.IsValid()) return FReply::Handled();
	if (!GEditor) return FReply::Handled();

	FString SourcePath = SelectedMaterial->SourcePath;
	AddToRecentlyUsed(SourcePath);

	// Resolve the material (import on demand if stock and not yet imported)
	UMaterialInterface* UEMat = FMaterialImporter::ResolveSourceMaterial(SourcePath);
	if (!UEMat)
	{
		UE_LOG(LogTemp, Warning, TEXT("SourceMaterialBrowser: Failed to resolve material '%s'"), *SourcePath);
		return FReply::Handled();
	}

	// Apply to all selected actors
	USelection* Selection = GEditor->GetSelectedActors();
	int32 Applied = 0;

	for (int32 i = 0; i < Selection->Num(); i++)
	{
		AActor* Actor = Cast<AActor>(Selection->GetSelectedObject(i));
		if (!Actor) continue;

		ApplyMaterialToActor(Actor, SourcePath);
		Applied++;
	}

	if (Applied > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("SourceMaterialBrowser: Applied '%s' to %d actors"), *SourcePath, Applied);
	}

	return FReply::Handled();
}

FReply SSourceMaterialBrowser::OnApplyToAllFaces()
{
	// Same as OnApplyToSelected but explicitly applies to ALL faces (not just selected face)
	return OnApplyToSelected();
}

void SSourceMaterialBrowser::ApplyMaterialToActor(AActor* Actor, const FString& SourceMaterialPath)
{
	if (!Actor) return;

	UMaterialInterface* UEMat = FMaterialImporter::ResolveSourceMaterial(SourceMaterialPath);
	if (!UEMat) return;

	// Handle ASourceBrushEntity
	if (ASourceBrushEntity* BrushEntity = Cast<ASourceBrushEntity>(Actor))
	{
		// Update all stored brush data sides
		for (FImportedBrushData& BrushData : BrushEntity->StoredBrushData)
		{
			for (FImportedSideData& Side : BrushData.Sides)
			{
				Side.Material = SourceMaterialPath;
			}
		}

		// Update all proc mesh section materials
		for (UProceduralMeshComponent* ProcMesh : BrushEntity->BrushMeshes)
		{
			if (!ProcMesh) continue;
			for (int32 SecIdx = 0; SecIdx < ProcMesh->GetNumSections(); SecIdx++)
			{
				ProcMesh->SetMaterial(SecIdx, UEMat);
			}
		}

		return;
	}

	// Handle ABrush
	if (ABrush* Brush = Cast<ABrush>(Actor))
	{
		if (!Brush->Brush || !Brush->Brush->Polys) return;

		for (FPoly& Poly : Brush->Brush->Polys->Element)
		{
			Poly.Material = UEMat;
			Poly.ItemName = FName(*SourceMaterialPath);
		}

		// Rebuild BSP to show updated materials
		UWorld* World = Actor->GetWorld();
		if (World)
		{
			Brush->GetBrushComponent()->Brush = Brush->Brush;
			FBSPOps::csgPrepMovingBrush(Brush);
			GEditor->csgRebuild(World);
			ULevel* Level = World->GetCurrentLevel();
			if (Level)
			{
				World->InvalidateModelGeometry(Level);
				Level->UpdateModelComponents();
			}
		}

		return;
	}
}

void SSourceMaterialBrowser::AddToRecentlyUsed(const FString& SourcePath)
{
	RecentlyUsed.Remove(SourcePath);
	RecentlyUsed.Insert(SourcePath, 0);
	if (RecentlyUsed.Num() > MaxRecentlyUsed)
	{
		RecentlyUsed.SetNum(MaxRecentlyUsed);
	}
}

// ============================================================================
// Helpers
// ============================================================================

FText SSourceMaterialBrowser::GetStatusText() const
{
	FString Status = FString::Printf(TEXT("%d materials shown (of %d total)"),
		FilteredMaterials.Num(), AllMaterials.Num());

	if (SelectedMaterial.IsValid())
	{
		Status += FString::Printf(TEXT("  |  Selected: %s"), *SelectedMaterial->SourcePath);
	}

	return FText::FromString(Status);
}

FSlateColor SSourceMaterialBrowser::GetSourceButtonColor(EMaterialBrowserSource Source) const
{
	if (Source == CurrentSource)
	{
		return FSlateColor(FLinearColor(0.2f, 0.4f, 0.8f, 1.0f));
	}
	return FSlateColor(FLinearColor(0.15f, 0.15f, 0.15f, 1.0f));
}

void SSourceMaterialBrowser::EnsureThumbnail(TSharedPtr<FMaterialBrowserEntry> Entry)
{
	if (!Entry.IsValid() || Entry->bThumbnailLoaded) return;
	Entry->bThumbnailLoaded = true;

	// 1. Try the UE texture already stored on the entry (imported/custom materials)
	if (Entry->UETexture.IsValid())
	{
		Entry->ThumbnailBrush = CreateBrushFromTexture(Entry->UETexture.Get());
		return;
	}

	// 2. For stock materials, try to load VTF from VPK on demand
	if (Entry->Type == ESourceMaterialType::Stock)
	{
		UTexture2D* ThumbTex = FMaterialImporter::LoadThumbnailTexture(Entry->SourcePath);
		if (ThumbTex)
		{
			Entry->UETexture = ThumbTex;
			Entry->ThumbnailBrush = CreateBrushFromTexture(ThumbTex);
		}
	}
}

TSharedPtr<FSlateBrush> SSourceMaterialBrowser::CreateBrushFromTexture(UTexture2D* Texture)
{
	if (!Texture) return nullptr;

	TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	Brush->SetResourceObject(Texture);
	Brush->ImageSize = FVector2D(48, 48);
	Brush->DrawAs = ESlateBrushDrawType::Image;
	return Brush;
}

// ============================================================================
// Tab Registration
// ============================================================================

void FSourceMaterialBrowserTab::Register()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FSourceMaterialBrowserTab::SpawnTab))
		.SetDisplayName(LOCTEXT("MaterialTabTitle", "Source Materials"))
		.SetTooltipText(LOCTEXT("MaterialTabTooltip", "Browse and apply Source engine materials"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.GameSettings"));
}

void FSourceMaterialBrowserTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedRef<SDockTab> FSourceMaterialBrowserTab::SpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("MaterialTabLabel", "Source Materials"))
		[
			SNew(SSourceMaterialBrowser)
		];
}

#undef LOCTEXT_NAMESPACE
