#include "UI/SourceBridgeToolbar.h"
#include "UI/SourceBridgeSettings.h"
#include "VMF/VMFExporter.h"
#include "VMF/BrushConverter.h"
#include "Pipeline/FullExportPipeline.h"
#include "Validation/ExportValidator.h"
#include "Import/VMFImporter.h"
#include "Import/BSPImporter.h"
#include "Import/MaterialImporter.h"
#include "Actors/SourceEntityActor.h"
#include "Entities/FGDParser.h"
#include "SourceBridgeModule.h"
#include "Utilities/SourceCoord.h"
#include "Materials/MaterialMapper.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Components/BrushComponent.h"
#include "Engine/Selection.h"
#include "Model.h"
#include "BSPOps.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Input/SSearchBox.h"
#include "DesktopPlatformModule.h"
#include "ProceduralMeshComponent.h"
#include "EngineUtils.h"

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
					LOCTEXT("ImportVMF", "Import VMF..."),
					LOCTEXT("ImportVMFTooltip", "Import a Source VMF file into the current level"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FSourceBridgeToolbar::OnImportVMF))
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("ImportBSP", "Import BSP..."),
					LOCTEXT("ImportBSPTooltip", "Decompile a BSP file via BSPSource and import into the current level"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FSourceBridgeToolbar::OnImportBSP))
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
					LOCTEXT("IOGraph", "I/O Graph"),
					LOCTEXT("IOGraphTooltip", "Visual node graph for Source entity I/O connections"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([]()
					{
						FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("SourceIOGraph")));
					}))
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("MaterialBrowser", "Source Materials"),
					LOCTEXT("MaterialBrowserTooltip", "Browse and apply Source engine materials (stock, imported, custom)"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([]()
					{
						FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("SourceMaterialBrowser")));
					}))
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("Settings", "Export Settings..."),
					LOCTEXT("SettingsTooltip", "Configure export settings (target game, output path, compile options)"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FSourceBridgeToolbar::OnOpenSettings))
				);

				MenuBuilder.AddSeparator();

				MenuBuilder.AddMenuEntry(
					LOCTEXT("CreateBrush", "Create Source Brush"),
					LOCTEXT("CreateBrushTooltip", "Create a BSP brush with Source-friendly defaults (64x64x64 grid)"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FSourceBridgeToolbar::OnCreateSourceBrush))
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("TieToEntity", "Tie to Entity (Ctrl+T)"),
					LOCTEXT("TieToEntityTooltip", "Convert selected BSP brushes to a Source brush entity"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FSourceBridgeToolbar::OnTieToEntity))
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("MoveToWorldspawn", "Move to Worldspawn (Ctrl+W)"),
					LOCTEXT("MoveToWorldspawnTooltip", "Convert selected brush entity back to worldspawn BSP brushes"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FSourceBridgeToolbar::OnMoveToWorldspawn))
				);
			}),
			LOCTEXT("SourceBridgeLabel", "SourceBridge"),
			LOCTEXT("SourceBridgeTooltip", "Source Engine Export Tools"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.GameSettings")
		));
	}));

	RegisterActorContextMenu();
}

void FSourceBridgeToolbar::Unregister()
{
	UToolMenus::UnregisterOwner(TEXT("SourceBridge"));
}

void FSourceBridgeToolbar::RegisterActorContextMenu()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.ActorContextMenu"));
		if (!Menu) return;

		FToolMenuSection& Section = Menu->AddSection(TEXT("SourceBridgeBrushTools"), LOCTEXT("SourceBridgeBrush", "SourceBridge"));

		Section.AddDynamicEntry(TEXT("SourceBridgeTieToEntity"), FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InSection)
		{
			// Check if any ABrush actors are selected
			bool bHasBrush = false;
			bool bHasBrushEntity = false;

			if (GEditor)
			{
				USelection* Selection = GEditor->GetSelectedActors();
				for (int32 i = 0; i < Selection->Num(); i++)
				{
					AActor* Actor = Cast<AActor>(Selection->GetSelectedObject(i));
					if (!Actor) continue;

					if (Cast<ASourceBrushEntity>(Actor))
					{
						bHasBrushEntity = true;
					}
					else if (Cast<ABrush>(Actor))
					{
						bHasBrush = true;
					}
				}
			}

			if (bHasBrush)
			{
				InSection.AddMenuEntry(
					TEXT("TieToEntity"),
					LOCTEXT("CtxTieToEntity", "Tie to Entity"),
					LOCTEXT("CtxTieToEntityTooltip", "Convert selected BSP brushes to a Source brush entity"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FSourceBridgeToolbar::OnTieToEntity))
				);
			}

			if (bHasBrushEntity)
			{
				InSection.AddMenuEntry(
					TEXT("MoveToWorldspawn"),
					LOCTEXT("CtxMoveToWorldspawn", "Move to Worldspawn"),
					LOCTEXT("CtxMoveToWorldspawnTooltip", "Convert selected brush entity back to worldspawn BSP brushes"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FSourceBridgeToolbar::OnMoveToWorldspawn))
				);
			}
		}));
	}));
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

void FSourceBridgeToolbar::OnImportVMF()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoWorldImportVMF", "No editor world available."));
		return;
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	TArray<FString> OutFiles;
	bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Import VMF File"),
		FPaths::ProjectSavedDir(),
		TEXT(""),
		TEXT("VMF Files (*.vmf)|*.vmf"),
		0,
		OutFiles);

	if (!bOpened || OutFiles.Num() == 0) return;

	FScopedSlowTask SlowTask(1.0f, LOCTEXT("ImportingVMF", "Importing VMF..."));
	SlowTask.MakeDialog();

	USourceBridgeSettings* PluginSettings = USourceBridgeSettings::Get();
	FVMFImportSettings Settings;
	Settings.ScaleMultiplier = 1.0f / PluginSettings->ScaleOverride;
	Settings.bImportBrushes = PluginSettings->bImportBrushes;
	Settings.bImportEntities = PluginSettings->bImportEntities;
	Settings.bImportMaterials = PluginSettings->bImportMaterials;
	FVMFImportResult Result = FVMFImporter::ImportFile(OutFiles[0], World, Settings);

	FString Msg = FString::Printf(TEXT("VMF Import Complete\n\nBrushes: %d\nEntities: %d"),
		Result.BrushesImported, Result.EntitiesImported);
	if (Result.Warnings.Num() > 0)
	{
		Msg += FString::Printf(TEXT("\nWarnings: %d"), Result.Warnings.Num());
		for (const FString& W : Result.Warnings)
		{
			Msg += TEXT("\n- ") + W;
		}
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Msg));
}

void FSourceBridgeToolbar::OnImportBSP()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoWorldImportBSP", "No editor world available."));
		return;
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	TArray<FString> OutFiles;
	bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Import BSP File"),
		FPaths::ProjectSavedDir(),
		TEXT(""),
		TEXT("BSP Files (*.bsp)|*.bsp"),
		0,
		OutFiles);

	if (!bOpened || OutFiles.Num() == 0) return;

	FScopedSlowTask SlowTask(1.0f, LOCTEXT("ImportingBSP", "Decompiling and importing BSP..."));
	SlowTask.MakeDialog();

	USourceBridgeSettings* PluginSettings = USourceBridgeSettings::Get();
	FVMFImportSettings Settings;
	Settings.ScaleMultiplier = 1.0f / PluginSettings->ScaleOverride;
	Settings.bImportBrushes = PluginSettings->bImportBrushes;
	Settings.bImportEntities = PluginSettings->bImportEntities;
	Settings.bImportMaterials = PluginSettings->bImportMaterials;
	FVMFImportResult Result = FBSPImporter::ImportFile(OutFiles[0], World, Settings);

	FString Msg = FString::Printf(TEXT("BSP Import Complete\n\nBrushes: %d\nEntities: %d"),
		Result.BrushesImported, Result.EntitiesImported);
	if (Result.Warnings.Num() > 0)
	{
		Msg += FString::Printf(TEXT("\nWarnings: %d"), Result.Warnings.Num());
		for (const FString& W : Result.Warnings)
		{
			Msg += TEXT("\n- ") + W;
		}
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Msg));
}

void FSourceBridgeToolbar::OnCreateSourceBrush()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return;

	// Spawn location: in front of the viewport camera
	FVector SpawnLocation = FVector::ZeroVector;
	if (GEditor->GetActiveViewport())
	{
		FEditorViewportClient* ViewClient = static_cast<FEditorViewportClient*>(
			GEditor->GetActiveViewport()->GetClient());
		if (ViewClient)
		{
			SpawnLocation = ViewClient->GetViewLocation() + ViewClient->GetViewRotation().Vector() * 200.0f;
			// Snap to 16-unit grid (in Source units → ~30.48 UE units)
			float GridSize = 16.0f / FSourceCoord::ScaleFactor; // ~30.48 UE cm
			SpawnLocation.X = FMath::RoundToFloat(SpawnLocation.X / GridSize) * GridSize;
			SpawnLocation.Y = FMath::RoundToFloat(SpawnLocation.Y / GridSize) * GridSize;
			SpawnLocation.Z = FMath::RoundToFloat(SpawnLocation.Z / GridSize) * GridSize;
		}
	}

	// Box dimensions: 64x64x64 Source units = ~121.9 UE units
	float HalfSize = 64.0f / (2.0f * FSourceCoord::ScaleFactor); // ~60.95 UE cm

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABrush* Brush = World->SpawnActor<ABrush>(ABrush::StaticClass(), FTransform(SpawnLocation), SpawnParams);
	if (!Brush) return;

	Brush->SetActorLocation(SpawnLocation);
	Brush->BrushType = Brush_Add;
	Brush->SetActorLabel(TEXT("SourceBrush"));

	// Create box model
	UModel* Model = NewObject<UModel>(Brush, NAME_None, RF_Transactional);
	Model->Initialize(nullptr, true);
	Model->Polys = NewObject<UPolys>(Model, NAME_None, RF_Transactional);
	Brush->Brush = Model;

	// 6 faces of a box centered at origin (in local space)
	struct FBoxFace
	{
		FVector3f Verts[4];
	};

	// Define box faces with vertices wound CCW from outside (UE convention)
	float S = HalfSize;
	FBoxFace BoxFaces[6] = {
		// Top (+Z)
		{{{-S, -S, S}, {-S, S, S}, {S, S, S}, {S, -S, S}}},
		// Bottom (-Z)
		{{{-S, S, -S}, {-S, -S, -S}, {S, -S, -S}, {S, S, -S}}},
		// Front (+X)
		{{{S, -S, -S}, {S, -S, S}, {S, S, S}, {S, S, -S}}},
		// Back (-X)
		{{{-S, S, -S}, {-S, S, S}, {-S, -S, S}, {-S, -S, -S}}},
		// Right (+Y)
		{{{-S, S, -S}, {S, S, -S}, {S, S, S}, {-S, S, S}}},
		// Left (-Y)
		{{{S, -S, -S}, {-S, -S, -S}, {-S, -S, S}, {S, -S, S}}},
	};

	for (int32 i = 0; i < 6; i++)
	{
		FPoly Poly;
		Poly.Init();
		Poly.iLink = i;

		for (int32 j = 0; j < 4; j++)
		{
			Poly.Vertices.Add(BoxFaces[i].Verts[j]);
		}

		Poly.Base = Poly.Vertices[0];

		if (Poly.Finalize(Brush, 1) == 0)
		{
			Model->Polys->Element.Add(MoveTemp(Poly));
		}
	}

	Model->BuildBound();
	Brush->GetBrushComponent()->Brush = Model;
	FBSPOps::csgPrepMovingBrush(Brush);

	// Rebuild BSP so the new brush renders in Lit mode
	GEditor->csgRebuild(World);
	ULevel* Level = World->GetCurrentLevel();
	if (Level)
	{
		World->InvalidateModelGeometry(Level);
		Level->UpdateModelComponents();
	}

	// Select the new brush
	GEditor->SelectNone(true, true, false);
	GEditor->SelectActor(Brush, true, true, true);

	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Created Source brush at (%s)"), *SpawnLocation.ToString());
}

void FSourceBridgeToolbar::OnTieToEntity()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return;

	// Collect selected ABrush actors (excluding default brush and volumes)
	TArray<ABrush*> SelectedBrushes;
	USelection* Selection = GEditor->GetSelectedActors();
	for (int32 i = 0; i < Selection->Num(); i++)
	{
		ABrush* Brush = Cast<ABrush>(Selection->GetSelectedObject(i));
		if (!Brush) continue;
		if (Brush == World->GetDefaultBrush()) continue;
		if (Brush->IsA<AVolume>()) continue;
		if (!Brush->Brush || !Brush->Brush->Polys) continue;

		SelectedBrushes.Add(Brush);
	}

	if (SelectedBrushes.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoSelectedBrushes", "Select one or more BSP brushes first."));
		return;
	}

	// Get solid class entity names from FGD for the picker
	TArray<FString> SolidClasses;
	const FFGDDatabase& FGD = FSourceBridgeModule::GetFGDDatabase();
	if (FGD.Classes.Num() > 0)
	{
		SolidClasses = FGD.GetSolidClassNames();
	}
	else
	{
		// Fallback common classes
		SolidClasses = {
			TEXT("func_detail"), TEXT("func_wall"), TEXT("func_brush"),
			TEXT("func_illusionary"), TEXT("func_breakable"), TEXT("func_door"),
			TEXT("func_door_rotating"), TEXT("func_rotating"), TEXT("func_physbox"),
			TEXT("func_areaportal"), TEXT("func_clip_vphysics"),
			TEXT("trigger_multiple"), TEXT("trigger_once"), TEXT("trigger_push"), TEXT("trigger_hurt")
		};
	}
	SolidClasses.Sort();

	// Show entity class picker dialog
	TSharedPtr<FString> SelectedClass;
	TSharedPtr<SWindow> PickerWindow;
	TSharedPtr<SListView<TSharedPtr<FString>>> ListView;
	TArray<TSharedPtr<FString>> ClassItems;
	TArray<TSharedPtr<FString>> FilteredItems;
	for (const FString& C : SolidClasses)
	{
		ClassItems.Add(MakeShared<FString>(C));
	}
	FilteredItems = ClassItems;

	PickerWindow = SNew(SWindow)
		.Title(LOCTEXT("TieToEntityTitle", "Tie to Entity - Select Class"))
		.ClientSize(FVector2D(350, 400))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.IsTopmostWindow(true);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchEntity", "Filter entity classes..."))
			.OnTextChanged_Lambda([&FilteredItems, &ClassItems, &ListView](const FText& NewText)
			{
				FString Filter = NewText.ToString();
				FilteredItems.Empty();
				for (const TSharedPtr<FString>& Item : ClassItems)
				{
					if (Filter.IsEmpty() || Item->Contains(Filter))
					{
						FilteredItems.Add(Item);
					}
				}
				if (ListView.IsValid())
				{
					ListView->RequestListRefresh();
				}
			})
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4)
		[
			SAssignNew(ListView, SListView<TSharedPtr<FString>>)
			.ListItemsSource(&FilteredItems)
			.OnGenerateRow_Lambda([](TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable) -> TSharedRef<ITableRow>
			{
				return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
					[
						SNew(STextBlock)
						.Text(FText::FromString(*Item))
						.Margin(FMargin(4, 2))
					];
			})
			.OnMouseButtonDoubleClick_Lambda([&SelectedClass, &PickerWindow](TSharedPtr<FString> Item)
			{
				SelectedClass = Item;
				if (PickerWindow.IsValid())
				{
					PickerWindow->RequestDestroyWindow();
				}
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4)
		.HAlign(HAlign_Right)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("OK", "OK"))
				.OnClicked_Lambda([&SelectedClass, &ListView, &PickerWindow]() -> FReply
				{
					TArray<TSharedPtr<FString>> Selected = ListView->GetSelectedItems();
					if (Selected.Num() > 0)
					{
						SelectedClass = Selected[0];
					}
					if (PickerWindow.IsValid())
					{
						PickerWindow->RequestDestroyWindow();
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Cancel", "Cancel"))
				.OnClicked_Lambda([&PickerWindow]() -> FReply
				{
					if (PickerWindow.IsValid())
					{
						PickerWindow->RequestDestroyWindow();
					}
					return FReply::Handled();
				})
			]
		];

	PickerWindow->SetContent(Content);
	GEditor->EditorAddModalWindow(PickerWindow.ToSharedRef());

	if (!SelectedClass.IsValid() || SelectedClass->IsEmpty())
	{
		return; // User cancelled
	}

	FString EntityClass = *SelectedClass;

	// Compute center of all selected brushes
	FVector Center = FVector::ZeroVector;
	int32 TotalVerts = 0;
	for (ABrush* Brush : SelectedBrushes)
	{
		FTransform BrushTransform = Brush->GetActorTransform();
		for (const FPoly& Poly : Brush->Brush->Polys->Element)
		{
			for (const FVector3f& V : Poly.Vertices)
			{
				Center += BrushTransform.TransformPosition(FVector(V));
				TotalVerts++;
			}
		}
	}
	if (TotalVerts > 0) Center /= TotalVerts;

	// Spawn ASourceBrushEntity at center
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASourceBrushEntity* BrushEntity = World->SpawnActor<ASourceBrushEntity>(
		ASourceBrushEntity::StaticClass(), Center, FRotator::ZeroRotator, SpawnParams);
	if (!BrushEntity) return;

	BrushEntity->SourceClassname = EntityClass;
	BrushEntity->SetActorLabel(EntityClass);

	FMaterialMapper MatMapper;

	// Convert each brush to a StoredBrushData entry
	for (ABrush* Brush : SelectedBrushes)
	{
		FTransform BrushTransform = Brush->GetActorTransform();
		const TArray<FPoly>& Polys = Brush->Brush->Polys->Element;

		FImportedBrushData BrushData;

		for (const FPoly& Poly : Polys)
		{
			if (Poly.Vertices.Num() < 3) continue;

			// Convert vertices to Source space
			TArray<FVector> SourceVerts;
			for (const FVector3f& LocalVert : Poly.Vertices)
			{
				FVector WorldVert = BrushTransform.TransformPosition(FVector(LocalVert));
				SourceVerts.Add(FSourceCoord::UEToSource(WorldVert));
			}

			// Pick 3 plane points
			FVector P1, P2, P3;
			if (!FBrushConverter::Pick3PlanePoints(SourceVerts, P1, P2, P3))
			{
				continue;
			}

			// Get material path
			FString MaterialPath = TEXT("DEV/DEV_MEASUREWALL01A");
			if (Poly.Material)
			{
				MaterialPath = MatMapper.MapMaterial(Poly.Material);
			}
			else if (Poly.ItemName != NAME_None)
			{
				MaterialPath = Poly.ItemName.ToString();
			}

			// Get UV axes
			FVector WorldNormal = BrushTransform.TransformVectorNoScale(FVector(Poly.Normal));
			FVector SourceNormal(WorldNormal.X, -WorldNormal.Y, WorldNormal.Z);
			SourceNormal.Normalize();

			FString UAxis, VAxis;
			FVector TexU(Poly.TextureU);
			FVector TexV(Poly.TextureV);
			if (!TexU.IsNearlyZero() && !TexV.IsNearlyZero())
			{
				FBrushConverter::ComputeUVAxesFromPoly(
					FVector(Poly.TextureU), FVector(Poly.TextureV), FVector(Poly.Base),
					SourceNormal, BrushTransform, UAxis, VAxis);
			}
			else
			{
				FBrushConverter::GetDefaultUVAxes(SourceNormal, UAxis, VAxis);
			}

			FImportedSideData Side;
			Side.PlaneP1 = P1;
			Side.PlaneP2 = P2;
			Side.PlaneP3 = P3;
			Side.Material = MaterialPath;
			Side.UAxisStr = UAxis;
			Side.VAxisStr = VAxis;
			Side.LightmapScale = 16;

			BrushData.Sides.Add(MoveTemp(Side));
		}

		if (BrushData.Sides.Num() >= 4)
		{
			BrushEntity->StoredBrushData.Add(MoveTemp(BrushData));
		}
	}

	// Build proc mesh from the stored data
	// Reuse each brush's face vertices for the proc mesh sections
	int32 SolidIdx = 0;
	for (ABrush* Brush : SelectedBrushes)
	{
		if (SolidIdx >= BrushEntity->StoredBrushData.Num()) break;

		FTransform BrushTransform = Brush->GetActorTransform();
		const TArray<FPoly>& Polys = Brush->Brush->Polys->Element;

		UProceduralMeshComponent* ProcMesh = BrushEntity->AddBrushMesh(
			FString::Printf(TEXT("Solid_%d"), SolidIdx));

		int32 SectionIdx = 0;
		for (const FPoly& Poly : Polys)
		{
			if (Poly.Vertices.Num() < 3) continue;

			TArray<FVector> Vertices;
			TArray<int32> Triangles;
			TArray<FVector> Normals;
			TArray<FVector2D> UVs;

			FVector WorldNormal = BrushTransform.TransformVectorNoScale(FVector(Poly.Normal));

			for (const FVector3f& V : Poly.Vertices)
			{
				FVector WorldPos = BrushTransform.TransformPosition(FVector(V));
				Vertices.Add(WorldPos - Center); // Local to entity
				Normals.Add(WorldNormal);
				UVs.Add(FVector2D(0, 0));
			}

			// Fan triangulation
			for (int32 i = 1; i + 1 < Vertices.Num(); i++)
			{
				Triangles.Add(0);
				Triangles.Add(i);
				Triangles.Add(i + 1);
			}

			ProcMesh->CreateMeshSection_LinearColor(SectionIdx, Vertices, Triangles,
				Normals, UVs, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), true);

			// Apply material
			if (SolidIdx < BrushEntity->StoredBrushData.Num() &&
				SectionIdx < BrushEntity->StoredBrushData[SolidIdx].Sides.Num())
			{
				FString MatPath = BrushEntity->StoredBrushData[SolidIdx].Sides[SectionIdx].Material;
				UMaterialInterface* Mat = FMaterialImporter::ResolveSourceMaterial(MatPath);
				if (Mat)
				{
					ProcMesh->SetMaterial(SectionIdx, Mat);
				}
			}

			SectionIdx++;
		}

		SolidIdx++;
	}

	// Delete original brushes
	GEditor->SelectNone(true, true, false);
	for (ABrush* Brush : SelectedBrushes)
	{
		World->DestroyActor(Brush);
	}

	// Rebuild BSP
	GEditor->csgRebuild(World);
	ULevel* Level = World->GetCurrentLevel();
	if (Level)
	{
		World->InvalidateModelGeometry(Level);
		Level->UpdateModelComponents();
	}

	// Select the new entity
	GEditor->SelectActor(BrushEntity, true, true, true);

	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Tied %d brushes to %s entity"), SelectedBrushes.Num(), *EntityClass);
}

void FSourceBridgeToolbar::OnMoveToWorldspawn()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return;

	// Collect selected ASourceBrushEntity actors
	TArray<ASourceBrushEntity*> SelectedEntities;
	USelection* Selection = GEditor->GetSelectedActors();
	for (int32 i = 0; i < Selection->Num(); i++)
	{
		ASourceBrushEntity* BrushEnt = Cast<ASourceBrushEntity>(Selection->GetSelectedObject(i));
		if (BrushEnt && BrushEnt->BrushMeshes.Num() > 0)
		{
			SelectedEntities.Add(BrushEnt);
		}
	}

	if (SelectedEntities.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoSelectedBrushEntities", "Select one or more Source brush entities first."));
		return;
	}

	TArray<ABrush*> CreatedBrushes;

	for (ASourceBrushEntity* BrushEnt : SelectedEntities)
	{
		FVector EntityLocation = BrushEnt->GetActorLocation();

		// For each proc mesh component, create an ABrush
		for (int32 MeshIdx = 0; MeshIdx < BrushEnt->BrushMeshes.Num(); MeshIdx++)
		{
			UProceduralMeshComponent* ProcMesh = BrushEnt->BrushMeshes[MeshIdx];
			if (!ProcMesh) continue;

			int32 NumSections = ProcMesh->GetNumSections();
			if (NumSections < 4) continue; // Need at least 4 faces for a solid

			// Compute center from all proc mesh vertices
			FVector BrushCenter = FVector::ZeroVector;
			int32 TotalVerts = 0;
			for (int32 SecIdx = 0; SecIdx < NumSections; SecIdx++)
			{
				const FProcMeshSection* Section = ProcMesh->GetProcMeshSection(SecIdx);
				if (!Section) continue;
				for (const FProcMeshVertex& V : Section->ProcVertexBuffer)
				{
					BrushCenter += FVector(V.Position) + EntityLocation;
					TotalVerts++;
				}
			}
			if (TotalVerts > 0) BrushCenter /= TotalVerts;

			// Spawn ABrush
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ABrush* NewBrush = World->SpawnActor<ABrush>(ABrush::StaticClass(),
				FTransform(BrushCenter), SpawnParams);
			if (!NewBrush) continue;

			NewBrush->SetActorLocation(BrushCenter);
			NewBrush->BrushType = Brush_Add;
			NewBrush->SetActorLabel(TEXT("WorldspawnBrush"));

			UModel* Model = NewObject<UModel>(NewBrush, NAME_None, RF_Transactional);
			Model->Initialize(nullptr, true);
			Model->Polys = NewObject<UPolys>(Model, NAME_None, RF_Transactional);
			NewBrush->Brush = Model;

			// Create FPoly from each proc mesh section
			for (int32 SecIdx = 0; SecIdx < NumSections; SecIdx++)
			{
				const FProcMeshSection* Section = ProcMesh->GetProcMeshSection(SecIdx);
				if (!Section || Section->ProcVertexBuffer.Num() < 3) continue;

				FPoly Poly;
				Poly.Init();
				Poly.iLink = SecIdx;

				// Get unique vertices from the section (skip duplicates from triangle fans)
				TArray<FVector3f> UniqueVerts;
				for (const FProcMeshVertex& V : Section->ProcVertexBuffer)
				{
					FVector WorldPos = FVector(V.Position) + EntityLocation;
					FVector3f LocalPos = FVector3f(WorldPos - BrushCenter);

					bool bDup = false;
					for (const FVector3f& Existing : UniqueVerts)
					{
						if (FVector3f::DistSquared(LocalPos, Existing) < 0.01f)
						{
							bDup = true;
							break;
						}
					}
					if (!bDup) UniqueVerts.Add(LocalPos);
				}

				for (const FVector3f& V : UniqueVerts)
				{
					Poly.Vertices.Add(V);
				}

				if (Poly.Vertices.Num() < 3) continue;

				Poly.Base = Poly.Vertices[0];

				// Apply material from stored data if available
				if (MeshIdx < BrushEnt->StoredBrushData.Num() &&
					SecIdx < BrushEnt->StoredBrushData[MeshIdx].Sides.Num())
				{
					FString MatPath = BrushEnt->StoredBrushData[MeshIdx].Sides[SecIdx].Material;
					Poly.ItemName = FName(*MatPath);
					UMaterialInterface* Mat = FMaterialImporter::ResolveSourceMaterial(MatPath);
					if (Mat) Poly.Material = Mat;
				}

				if (Poly.Finalize(NewBrush, 1) == 0)
				{
					Model->Polys->Element.Add(MoveTemp(Poly));
				}
			}

			if (Model->Polys->Element.Num() < 4)
			{
				World->DestroyActor(NewBrush);
				continue;
			}

			Model->BuildBound();
			NewBrush->GetBrushComponent()->Brush = Model;
			FBSPOps::csgPrepMovingBrush(NewBrush);

			CreatedBrushes.Add(NewBrush);
		}
	}

	// Delete original entities
	GEditor->SelectNone(true, true, false);
	for (ASourceBrushEntity* BrushEnt : SelectedEntities)
	{
		World->DestroyActor(BrushEnt);
	}

	// Rebuild BSP
	GEditor->csgRebuild(World);
	ULevel* Level = World->GetCurrentLevel();
	if (Level)
	{
		World->InvalidateModelGeometry(Level);
		Level->UpdateModelComponents();
	}

	// Select the new brushes
	for (ABrush* B : CreatedBrushes)
	{
		GEditor->SelectActor(B, true, true, true);
	}

	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Moved %d brush entities to worldspawn (%d brushes created)"),
		SelectedEntities.Num(), CreatedBrushes.Num());
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
