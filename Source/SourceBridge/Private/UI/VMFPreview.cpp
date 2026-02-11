#include "UI/VMFPreview.h"
#include "VMF/VMFExporter.h"
#include "Compile/CompileEstimator.h"
#include "UI/SourceBridgeSettings.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformApplicationMisc.h"

#define LOCTEXT_NAMESPACE "VMFPreview"

const FName FVMFPreviewTab::TabId(TEXT("VMFPreview"));

void SVMFPreview::Construct(const FArguments& InArgs)
{
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
			.Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "Generate Preview"))
				.ToolTipText(LOCTEXT("RefreshTooltip", "Generate VMF from current scene"))
				.OnClicked(this, &SVMFPreview::OnRefreshPreview)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("Copy", "Copy to Clipboard"))
				.ToolTipText(LOCTEXT("CopyTooltip", "Copy VMF text to clipboard"))
				.OnClicked(this, &SVMFPreview::OnCopyToClipboard)
			]
		]

		// Stats line
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4)
		[
			SAssignNew(StatsText, STextBlock)
			.Text(LOCTEXT("NoPreview", "Click 'Generate Preview' to see VMF output."))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.AutoWrapText(true)
		]

		// VMF text content
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4)
		[
			SAssignNew(VMFTextBox, SMultiLineEditableTextBox)
			.IsReadOnly(true)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
			.AutoWrapText(false)
		]
	];
}

FReply SVMFPreview::OnRefreshPreview()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		StatsText->SetText(LOCTEXT("NoWorld", "No editor world available."));
		return FReply::Handled();
	}

	// Generate VMF
	CachedVMFContent = FVMFExporter::ExportScene(World);

	if (CachedVMFContent.IsEmpty())
	{
		StatsText->SetText(LOCTEXT("EmptyVMF", "Export produced empty VMF. Add brushes to the scene."));
		VMFTextBox->SetText(FText::GetEmpty());
		return FReply::Handled();
	}

	// Set preview text
	VMFTextBox->SetText(FText::FromString(CachedVMFContent));

	// Calculate stats
	int32 LineCount = 1;
	for (const TCHAR& Ch : CachedVMFContent)
	{
		if (Ch == TEXT('\n')) LineCount++;
	}

	int32 SizeBytes = CachedVMFContent.Len() * sizeof(TCHAR);
	float SizeKB = SizeBytes / 1024.0f;

	// Count sections in the VMF
	int32 SolidCount = 0;
	int32 EntityCount = 0;
	int32 SideCount = 0;

	int32 SearchPos = 0;
	while ((SearchPos = CachedVMFContent.Find(TEXT("solid"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos)) != INDEX_NONE)
	{
		SolidCount++;
		SearchPos += 5;
	}
	SearchPos = 0;
	while ((SearchPos = CachedVMFContent.Find(TEXT("\"classname\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos)) != INDEX_NONE)
	{
		EntityCount++;
		SearchPos += 11;
	}
	SearchPos = 0;
	while ((SearchPos = CachedVMFContent.Find(TEXT("side\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos)) != INDEX_NONE)
	{
		SideCount++;
		SearchPos += 5;
	}

	// Compile time estimate
	USourceBridgeSettings* Settings = USourceBridgeSettings::Get();
	FCompileTimeEstimate TimeEst = FCompileEstimator::EstimateCompileTime(
		World,
		Settings->bFastCompile,
		false);

	FString Stats = FString::Printf(
		TEXT("VMF: %d lines, %.1f KB | %d solids, %d sides, %d entities\n%s"),
		LineCount, SizeKB, SolidCount, SideCount, EntityCount,
		*TimeEst.GetSummary());

	StatsText->SetText(FText::FromString(Stats));

	return FReply::Handled();
}

FReply SVMFPreview::OnCopyToClipboard()
{
	if (!CachedVMFContent.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*CachedVMFContent);
	}
	return FReply::Handled();
}

// --- Tab Registration ---

void FVMFPreviewTab::Register()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FVMFPreviewTab::SpawnTab))
		.SetDisplayName(LOCTEXT("PreviewTabTitle", "VMF Preview"))
		.SetTooltipText(LOCTEXT("PreviewTabTooltip", "Preview the VMF output for the current scene"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.GameSettings"));
}

void FVMFPreviewTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedRef<SDockTab> FVMFPreviewTab::SpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("PreviewTabLabel", "VMF Preview"))
		[
			SNew(SVMFPreview)
		];
}

#undef LOCTEXT_NAMESPACE
