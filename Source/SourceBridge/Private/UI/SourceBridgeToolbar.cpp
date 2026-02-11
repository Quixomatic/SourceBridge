#include "UI/SourceBridgeToolbar.h"
#include "UI/SourceBridgeSettings.h"
#include "VMF/VMFExporter.h"
#include "Pipeline/FullExportPipeline.h"
#include "Validation/ExportValidator.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "SourceBridgeToolbar"

void FSourceBridgeToolbar::Register()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.PlayToolBar"));
		if (!Menu) return;

		FToolMenuSection& Section = Menu->AddSection(TEXT("SourceBridge"), LOCTEXT("SourceBridge", "SourceBridge"));

		Section.AddEntry(FToolMenuEntry::InitComboButton(
			TEXT("SourceBridgeMenu"),
			FUIAction(),
			FOnGetContent::CreateLambda([](FMenuBuilder& MenuBuilder)
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("ExportScene", "Export Scene to VMF"),
					LOCTEXT("ExportSceneTooltip", "Export all brushes and entities to a VMF file"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FSourceBridgeToolbar::OnExportScene))
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("FullExport", "Export && Compile"),
					LOCTEXT("FullExportTooltip", "Export, validate, compile (vbsp/vvis/vrad), and copy to game"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FSourceBridgeToolbar::OnFullExport))
				);

				MenuBuilder.AddSeparator();

				MenuBuilder.AddMenuEntry(
					LOCTEXT("Validate", "Validate Scene"),
					LOCTEXT("ValidateTooltip", "Check for Source engine limit violations and common issues"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FSourceBridgeToolbar::OnValidate))
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("TestBoxRoom", "Export Test Box Room"),
					LOCTEXT("TestBoxRoomTooltip", "Export a hardcoded box room for testing"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FSourceBridgeToolbar::OnExportTestBoxRoom))
				);

				MenuBuilder.AddSeparator();

				MenuBuilder.AddMenuEntry(
					LOCTEXT("Settings", "Export Settings..."),
					LOCTEXT("SettingsTooltip", "Configure export settings (target game, output path, compile options)"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FSourceBridgeToolbar::OnOpenSettings))
				);
			}),
			LOCTEXT("SourceBridgeLabel", "SourceBridge"),
			LOCTEXT("SourceBridgeTooltip", "Source Engine Export Tools"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.GameSettings")
		));
	}));
}

void FSourceBridgeToolbar::Unregister()
{
	UToolMenus::UnregisterOwner(TEXT("SourceBridge"));
}

void FSourceBridgeToolbar::OnExportScene()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoWorld", "No editor world available."));
		return;
	}

	USourceBridgeSettings* Settings = USourceBridgeSettings::Get();
	FString OutputDir = Settings->OutputDirectory.Path;
	if (OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ProjectSavedDir() / TEXT("SourceBridge");
	}

	FString MapName = Settings->MapName;
	if (MapName.IsEmpty())
	{
		MapName = World->GetMapName();
		MapName = MapName.Replace(TEXT("UEDPIE_0_"), TEXT(""));
		MapName = MapName.Replace(TEXT("UEDPIE_"), TEXT(""));
		if (MapName.IsEmpty()) MapName = TEXT("export");
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutputDir);

	FString OutputPath = OutputDir / MapName + TEXT(".vmf");
	FString VMFContent = FVMFExporter::ExportScene(World);

	if (VMFContent.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("EmptyExport", "Export produced empty VMF. Add brushes to the scene."));
		return;
	}

	if (FFileHelper::SaveStringToFile(VMFContent, *OutputPath))
	{
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Scene exported to: %s"), *OutputPath);
		FMessageDialog::Open(EAppMsgType::Ok,
			FText::Format(LOCTEXT("ExportSuccess", "VMF exported to:\n{0}"),
				FText::FromString(OutputPath)));
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			FText::Format(LOCTEXT("ExportFail", "Failed to write VMF to:\n{0}"),
				FText::FromString(OutputPath)));
	}
}

void FSourceBridgeToolbar::OnFullExport()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoWorldFull", "No editor world available."));
		return;
	}

	USourceBridgeSettings* Settings = USourceBridgeSettings::Get();

	FFullExportSettings ExportSettings;
	ExportSettings.GameName = Settings->TargetGame;
	ExportSettings.OutputDir = Settings->OutputDirectory.Path;
	ExportSettings.MapName = Settings->MapName;
	ExportSettings.bCompile = Settings->bCompileAfterExport;
	ExportSettings.bFastCompile = Settings->bFastCompile;
	ExportSettings.bCopyToGame = Settings->bCopyToGame;
	ExportSettings.bValidate = Settings->bValidateBeforeExport;

	FFullExportResult Result = FFullExportPipeline::Run(World, ExportSettings);

	if (Result.bSuccess)
	{
		FString Msg = FString::Printf(TEXT("Export succeeded!\n\nVMF: %s"), *Result.VMFPath);
		if (!Result.BSPPath.IsEmpty())
		{
			Msg += FString::Printf(TEXT("\nBSP: %s"), *Result.BSPPath);
		}
		Msg += FString::Printf(TEXT("\nExport: %.1fs, Compile: %.1fs"),
			Result.ExportSeconds, Result.CompileSeconds);

		if (Result.Warnings.Num() > 0)
		{
			Msg += TEXT("\n\nWarnings:");
			for (const FString& W : Result.Warnings)
			{
				Msg += TEXT("\n- ") + W;
			}
		}

		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Msg));
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			FText::Format(LOCTEXT("FullExportFail", "Export failed:\n{0}"),
				FText::FromString(Result.ErrorMessage)));
	}
}

void FSourceBridgeToolbar::OnValidate()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoWorldVal", "No editor world available."));
		return;
	}

	FValidationResult Result = FExportValidator::ValidateWorld(World);
	Result.LogAll();

	FString Msg;
	if (Result.HasErrors())
	{
		Msg = FString::Printf(TEXT("Validation FAILED\n%d errors, %d warnings\n\n"),
			Result.ErrorCount, Result.WarningCount);
	}
	else
	{
		Msg = FString::Printf(TEXT("Validation PASSED\n%d warnings\n\n"),
			Result.WarningCount);
	}

	for (const FValidationMessage& M : Result.Messages)
	{
		FString Prefix;
		switch (M.Severity)
		{
		case EValidationSeverity::Error: Prefix = TEXT("[ERROR] "); break;
		case EValidationSeverity::Warning: Prefix = TEXT("[WARN]  "); break;
		case EValidationSeverity::Info: Prefix = TEXT("[INFO]  "); break;
		}
		Msg += Prefix + M.Message + TEXT("\n");
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Msg));
}

void FSourceBridgeToolbar::OnExportTestBoxRoom()
{
	USourceBridgeSettings* Settings = USourceBridgeSettings::Get();
	FString OutputDir = Settings->OutputDirectory.Path;
	if (OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ProjectSavedDir() / TEXT("SourceBridge");
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutputDir);

	FString OutputPath = OutputDir / TEXT("test_boxroom.vmf");
	FString VMFContent = FVMFExporter::GenerateBoxRoom();

	if (FFileHelper::SaveStringToFile(VMFContent, *OutputPath))
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			FText::Format(LOCTEXT("BoxRoomSuccess", "Test box room exported to:\n{0}"),
				FText::FromString(OutputPath)));
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("BoxRoomFail", "Failed to write test box room VMF."));
	}
}

void FSourceBridgeToolbar::OnOpenSettings()
{
	// Open the settings in a property editor window
	USourceBridgeSettings* Settings = USourceBridgeSettings::Get();

	// Use the details panel approach - create an FPropertyEditorModule window
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	TArray<UObject*> Objects;
	Objects.Add(Settings);

	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bShowPropertyMatrixButton = false;

	TSharedRef<IDetailsView> DetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	DetailsView->SetObjects(Objects);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("SettingsTitle", "SourceBridge Export Settings"))
		.ClientSize(FVector2D(450, 500))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4)
			[
				DetailsView
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			.HAlign(HAlign_Right)
			[
				SNew(SButton)
				.Text(LOCTEXT("SaveSettings", "Save Settings"))
				.OnClicked_Lambda([Settings]() -> FReply
				{
					Settings->SaveConfig();
					UE_LOG(LogTemp, Log, TEXT("SourceBridge: Settings saved."));
					return FReply::Handled();
				})
			]
		];

	FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
