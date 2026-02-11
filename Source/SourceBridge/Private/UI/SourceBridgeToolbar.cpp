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
#include "Misc/ScopedSlowTask.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

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
			FNewMenuDelegate::CreateLambda([](FMenuBuilder& MenuBuilder)
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
					LOCTEXT("EntityPalette", "Entity Palette"),
					LOCTEXT("EntityPaletteTooltip", "Open the Source entity palette for browsing and spawning entities"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([]()
					{
						FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("SourceEntityPalette")));
					}))
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("VMFPreview", "VMF Preview"),
					LOCTEXT("VMFPreviewTooltip", "Preview the VMF output with compile time estimates"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([]()
					{
						FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("VMFPreview")));
					}))
				);

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

	// Progress bar with steps
	FScopedSlowTask SlowTask(100.0f, LOCTEXT("ExportProgress", "SourceBridge: Exporting..."));
	SlowTask.MakeDialog(true);

	// Run pipeline with progress callback
	FOnPipelineProgress ProgressCallback;
	ProgressCallback.BindLambda([&SlowTask](const FString& StepName, float Progress)
	{
		float StepAmount = Progress * 100.0f - SlowTask.CompletedWork;
		if (StepAmount > 0.0f)
		{
			SlowTask.EnterProgressFrame(StepAmount, FText::FromString(StepName));
		}
	});

	FFullExportResult Result = FFullExportPipeline::RunWithProgress(World, ExportSettings, ProgressCallback);

	// Complete remaining progress
	float Remaining = 100.0f - SlowTask.CompletedWork;
	if (Remaining > 0.0f)
	{
		SlowTask.EnterProgressFrame(Remaining, LOCTEXT("StepDone", "Complete"));
	}

	// Show result notification
	FNotificationInfo Info(FText::GetEmpty());
	Info.bFireAndForget = true;
	Info.ExpireDuration = 8.0f;
	Info.bUseSuccessFailIcons = true;

	if (Result.bSuccess)
	{
		FString Summary = FString::Printf(TEXT("Export succeeded (%.1fs + %.1fs compile)"),
			Result.ExportSeconds, Result.CompileSeconds);

		if (Result.Warnings.Num() > 0)
		{
			Summary += FString::Printf(TEXT(" - %d warnings"), Result.Warnings.Num());
		}

		Info.Text = FText::FromString(Summary);
		Info.Image = FCoreStyle::Get().GetBrush(TEXT("Icons.SuccessWithColor"));

		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(SNotificationItem::CS_Success);
		}

		// Also show full details dialog
		FString Msg = FString::Printf(TEXT("Export succeeded!\n\nVMF: %s"), *Result.VMFPath);
		if (!Result.BSPPath.IsEmpty())
		{
			Msg += FString::Printf(TEXT("\nBSP: %s"), *Result.BSPPath);
		}
		if (!Result.PackagePath.IsEmpty())
		{
			Msg += FString::Printf(TEXT("\nPackage: %s"), *Result.PackagePath);
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
		Info.Text = FText::Format(LOCTEXT("ExportFailNotif", "Export failed: {0}"),
			FText::FromString(Result.ErrorMessage));
		Info.Image = FCoreStyle::Get().GetBrush(TEXT("Icons.ErrorWithColor"));

		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(SNotificationItem::CS_Fail);
		}

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
