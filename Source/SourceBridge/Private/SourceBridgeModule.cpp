#include "SourceBridgeModule.h"
#include "VMF/VMFExporter.h"
#include "VMF/VisOptimizer.h"
#include "Compile/CompilePipeline.h"
#include "Models/SMDExporter.h"
#include "Models/QCWriter.h"
#include "Pipeline/FullExportPipeline.h"
#include "Validation/ExportValidator.h"
#include "Entities/FGDParser.h"
#include "Import/VMFImporter.h"
#include "Import/BSPImporter.h"
#include "UI/SourceBridgeToolbar.h"
#include "UI/SourceEntityDetailCustomization.h"
#include "UI/SourceEntityPalette.h"
#include "UI/VMFPreview.h"
#include "UI/SourceIOGraphEditor.h"
#include "UI/SSourceMaterialBrowser.h"
#include "Runtime/SourceBridgeGameMode.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Selection.h"
#include "Editor.h"

FFGDDatabase FSourceBridgeModule::FGDDatabase;

const FFGDDatabase& FSourceBridgeModule::GetFGDDatabase()
{
	return FGDDatabase;
}

void FSourceBridgeModule::LoadFGD(const FString& FilePath)
{
	FGDDatabase = FFGDParser::ParseFile(FilePath);
}

#define LOCTEXT_NAMESPACE "FSourceBridgeModule"

void FSourceBridgeModule::StartupModule()
{
	FSourceBridgeToolbar::Register();
	FSourceEntityDetailCustomization::Register();
	FSourceEntityPaletteTab::Register();
	FVMFPreviewTab::Register();
	FSourceIOGraphTab::Register();
	FSourceMaterialBrowserTab::Register();

	ExportTestBoxRoomCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.ExportTestBoxRoom"),
		TEXT("Export a test box room to VMF. Usage: SourceBridge.ExportTestBoxRoom <filepath>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			FString OutputPath;
			if (Args.Num() > 0)
			{
				OutputPath = Args[0];
			}
			else
			{
				OutputPath = FPaths::ProjectSavedDir() / TEXT("SourceBridge") / TEXT("test_boxroom.vmf");
			}

			FString VMFContent = FVMFExporter::GenerateBoxRoom();

			FString Directory = FPaths::GetPath(OutputPath);
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			PlatformFile.CreateDirectoryTree(*Directory);

			if (FFileHelper::SaveStringToFile(VMFContent, *OutputPath))
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Box room VMF exported to: %s"), *OutputPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Failed to write VMF to: %s"), *OutputPath);
			}
		})
	);

	ExportSceneCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.ExportScene"),
		TEXT("Export current level brushes to VMF. Usage: SourceBridge.ExportScene <filepath>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: No editor world available."));
				return;
			}

			FString OutputPath;
			if (Args.Num() > 0)
			{
				OutputPath = Args[0];
			}
			else
			{
				OutputPath = FPaths::ProjectSavedDir() / TEXT("SourceBridge") / TEXT("export.vmf");
			}

			FString VMFContent = FVMFExporter::ExportScene(World);

			if (VMFContent.IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Export produced empty VMF."));
				return;
			}

			FString Directory = FPaths::GetPath(OutputPath);
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			PlatformFile.CreateDirectoryTree(*Directory);

			if (FFileHelper::SaveStringToFile(VMFContent, *OutputPath))
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Scene exported to: %s"), *OutputPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Failed to write VMF to: %s"), *OutputPath);
			}
		})
	);

	CompileMapCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.CompileMap"),
		TEXT("Compile a VMF to BSP. Usage: SourceBridge.CompileMap <vmf_path> [game_name]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Usage: SourceBridge.CompileMap <vmf_path> [game_name]"));
				return;
			}

			FCompileSettings Settings;
			Settings.VMFPath = Args[0];
			Settings.bFastCompile = true;

			FString GameName = Args.Num() > 1 ? Args[1] : TEXT("cstrike");

			Settings.ToolsDir = FCompilePipeline::FindToolsDirectory();
			Settings.GameDir = FCompilePipeline::FindGameDirectory(GameName);

			if (Settings.ToolsDir.IsEmpty() || Settings.GameDir.IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Could not auto-detect SDK paths. Install CS:S via Steam."));
				return;
			}

			FCompileResult Result = FCompilePipeline::CompileMap(Settings);

			if (Result.bSuccess)
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Compile succeeded in %.1f seconds."), Result.ElapsedSeconds);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Compile failed: %s"), *Result.ErrorMessage);
			}
		})
	);

	ExportModelCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.ExportModel"),
		TEXT("Export a static mesh to SMD+QC. Usage: SourceBridge.ExportModel <mesh_path> [output_dir]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Usage: SourceBridge.ExportModel <mesh_asset_path> [output_dir]"));
				return;
			}

			// Load the static mesh asset
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Args[0]);
			if (!Mesh)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Could not load mesh: %s"), *Args[0]);
				return;
			}

			FString OutputDir;
			if (Args.Num() > 1)
			{
				OutputDir = Args[1];
			}
			else
			{
				OutputDir = FPaths::ProjectSavedDir() / TEXT("SourceBridge") / TEXT("models");
			}

			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			PlatformFile.CreateDirectoryTree(*OutputDir);

			// Export to SMD
			FSMDExportResult Result = FSMDExporter::ExportStaticMesh(Mesh);
			if (!Result.bSuccess)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: SMD export failed: %s"), *Result.ErrorMessage);
				return;
			}

			// Generate QC settings
			FQCSettings QCSettings = FQCWriter::MakeDefaultSettings(Mesh->GetName());

			// Write files
			FString BaseName = Mesh->GetName().ToLower();
			if (BaseName.StartsWith(TEXT("SM_"))) BaseName = BaseName.Mid(3);
			else if (BaseName.StartsWith(TEXT("S_"))) BaseName = BaseName.Mid(2);

			FString RefPath = OutputDir / BaseName + TEXT("_ref.smd");
			FString PhysPath = OutputDir / BaseName + TEXT("_phys.smd");
			FString IdlePath = OutputDir / BaseName + TEXT("_idle.smd");
			FString QCPath = OutputDir / BaseName + TEXT(".qc");

			bool bAllWritten = true;
			bAllWritten &= FFileHelper::SaveStringToFile(Result.ReferenceSMD, *RefPath);
			bAllWritten &= FFileHelper::SaveStringToFile(Result.PhysicsSMD, *PhysPath);
			bAllWritten &= FFileHelper::SaveStringToFile(Result.IdleSMD, *IdlePath);

			FString QCContent = FQCWriter::GenerateQC(QCSettings);
			bAllWritten &= FFileHelper::SaveStringToFile(QCContent, *QCPath);

			if (bAllWritten)
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Model exported to: %s"), *OutputDir);
				UE_LOG(LogTemp, Log, TEXT("  Reference: %s"), *RefPath);
				UE_LOG(LogTemp, Log, TEXT("  Physics:   %s"), *PhysPath);
				UE_LOG(LogTemp, Log, TEXT("  Idle:      %s"), *IdlePath);
				UE_LOG(LogTemp, Log, TEXT("  QC:        %s"), *QCPath);
				UE_LOG(LogTemp, Log, TEXT("  Materials: %s"), *FString::Join(Result.MaterialNames, TEXT(", ")));
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Failed to write some model files to: %s"), *OutputDir);
			}
		})
	);

	FullExportCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.FullExport"),
		TEXT("Full pipeline: validate, export VMF, compile, copy to game. Usage: SourceBridge.FullExport [map_name] [game_name]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: No editor world available."));
				return;
			}

			FFullExportSettings Settings;
			if (Args.Num() > 0) Settings.MapName = Args[0];
			if (Args.Num() > 1) Settings.GameName = Args[1];

			FFullExportResult Result = FFullExportPipeline::Run(World, Settings);

			if (Result.bSuccess)
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Full export succeeded!"));
				UE_LOG(LogTemp, Log, TEXT("  VMF: %s"), *Result.VMFPath);
				if (!Result.BSPPath.IsEmpty())
				{
					UE_LOG(LogTemp, Log, TEXT("  BSP: %s"), *Result.BSPPath);
				}
				UE_LOG(LogTemp, Log, TEXT("  Export: %.1fs, Compile: %.1fs"),
					Result.ExportSeconds, Result.CompileSeconds);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Full export failed: %s"), *Result.ErrorMessage);
			}

			for (const FString& Warning : Result.Warnings)
			{
				UE_LOG(LogTemp, Warning, TEXT("SourceBridge: %s"), *Warning);
			}
		})
	);

	ValidateCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.Validate"),
		TEXT("Run pre-export validation on the current scene."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: No editor world available."));
				return;
			}

			FValidationResult Result = FExportValidator::ValidateWorld(World);
			Result.LogAll();

			if (Result.HasErrors())
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Validation FAILED - %d errors found."), Result.ErrorCount);
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Validation PASSED (%d warnings)."), Result.WarningCount);
			}
		})
	);

	LoadFGDCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.LoadFGD"),
		TEXT("Load an FGD file for entity validation. Usage: SourceBridge.LoadFGD <fgd_path>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Usage: SourceBridge.LoadFGD <fgd_path>"));
				return;
			}

			FSourceBridgeModule::LoadFGD(Args[0]);

			const FFGDDatabase& DB = FSourceBridgeModule::GetFGDDatabase();
			UE_LOG(LogTemp, Log, TEXT("SourceBridge: FGD loaded. %d entity classes (%d warnings)."),
				DB.Classes.Num(), DB.Warnings.Num());

			for (const FString& Warning : DB.Warnings)
			{
				UE_LOG(LogTemp, Warning, TEXT("SourceBridge FGD: %s"), *Warning);
			}
		})
	);

	ListEntitiesCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.ListEntities"),
		TEXT("List all entity classes from loaded FGD. Usage: SourceBridge.ListEntities [filter]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const FFGDDatabase& DB = FSourceBridgeModule::GetFGDDatabase();

			if (DB.Classes.Num() == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("SourceBridge: No FGD loaded. Use SourceBridge.LoadFGD first."));
				return;
			}

			FString Filter = Args.Num() > 0 ? Args[0] : TEXT("");

			TArray<FString> Names = DB.GetPlaceableClassNames();
			int32 Shown = 0;

			for (const FString& Name : Names)
			{
				if (!Filter.IsEmpty() && !Name.Contains(Filter))
				{
					continue;
				}

				const FFGDEntityClass* Class = DB.FindClass(Name);
				if (Class)
				{
					UE_LOG(LogTemp, Log, TEXT("  [%s] %s - %s"),
						Class->bIsSolid ? TEXT("SOLID") : TEXT("POINT"),
						*Class->ClassName,
						Class->Description.IsEmpty() ? TEXT("") : *Class->Description.Left(80));
					Shown++;
				}
			}

			UE_LOG(LogTemp, Log, TEXT("SourceBridge: %d entities listed (of %d total, %d base classes)."),
				Shown, Names.Num(), DB.Classes.Num() - Names.Num());
		})
	);

	AnalyzeVisCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.AnalyzeVis"),
		TEXT("Analyze the scene for vis optimization suggestions."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: No editor world available."));
				return;
			}

			TArray<FVisOptSuggestion> Suggestions = FVisOptimizer::AnalyzeWorld(World);

			if (Suggestions.Num() == 0)
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: No vis optimization suggestions for this scene."));
				return;
			}

			UE_LOG(LogTemp, Log, TEXT("SourceBridge: %d vis optimization suggestions:"), Suggestions.Num());
			for (int32 i = 0; i < Suggestions.Num(); i++)
			{
				const FVisOptSuggestion& S = Suggestions[i];
				const TCHAR* TypeStr = TEXT("Unknown");
				switch (S.Type)
				{
				case FVisOptSuggestion::EType::HintBrush: TypeStr = TEXT("HINT"); break;
				case FVisOptSuggestion::EType::AreaPortal: TypeStr = TEXT("AREAPORTAL"); break;
				case FVisOptSuggestion::EType::VisCluster: TypeStr = TEXT("VISCLUSTER"); break;
				}

				UE_LOG(LogTemp, Log, TEXT("  [%s] %s"), TypeStr, *S.Description);
			}
		})
	);

	ImportVMFCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.ImportVMF"),
		TEXT("Import a VMF file into the current level. Usage: SourceBridge.ImportVMF <vmf_path>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Usage: SourceBridge.ImportVMF <vmf_path>"));
				return;
			}

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: No editor world available."));
				return;
			}

			FVMFImportSettings Settings;
			FVMFImportResult Result = FVMFImporter::ImportFile(Args[0], World, Settings);

			UE_LOG(LogTemp, Log, TEXT("SourceBridge: VMF import complete. %d brushes, %d entities."),
				Result.BrushesImported, Result.EntitiesImported);

			for (const FString& Warning : Result.Warnings)
			{
				UE_LOG(LogTemp, Warning, TEXT("SourceBridge Import: %s"), *Warning);
			}
		})
	);

	ImportBSPCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.ImportBSP"),
		TEXT("Import a BSP file (decompiles via BSPSource, then imports VMF). Usage: SourceBridge.ImportBSP <bsp_path>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Usage: SourceBridge.ImportBSP <bsp_path>"));
				return;
			}

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: No editor world available."));
				return;
			}

			FVMFImportSettings Settings;
			FVMFImportResult Result = FBSPImporter::ImportFile(Args[0], World, Settings);

			UE_LOG(LogTemp, Log, TEXT("SourceBridge: BSP import complete. %d brushes, %d entities."),
				Result.BrushesImported, Result.EntitiesImported);

			for (const FString& Warning : Result.Warnings)
			{
				UE_LOG(LogTemp, Warning, TEXT("SourceBridge Import: %s"), *Warning);
			}
		})
	);

	PlayTestCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.PlayTest"),
		TEXT("Start PIE using Source spawn points. Usage: SourceBridge.PlayTest [T|CT]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: No editor world available"));
				return;
			}

			// Set the game mode override on the current world
			World->GetWorldSettings()->DefaultGameMode = ASourceBridgeGameMode::StaticClass();

			UE_LOG(LogTemp, Log, TEXT("SourceBridge: Game mode set to SourceBridgeGameMode. Use Play (Alt+P) to test."));
			UE_LOG(LogTemp, Log, TEXT("SourceBridge: Spawns at Source T/CT spawn points with proper tool texture visibility."));

			if (Args.Num() > 0)
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Team preference '%s' — set in World Settings > Game Mode > Preferred Team"), *Args[0]);
			}
		})
	);

	// Auto-load FGD from Resources directory if present
	FString PluginFGDPath = FPaths::ProjectPluginsDir() / TEXT("SourceBridge") / TEXT("Resources") / TEXT("cstrike.fgd");
	if (!FPaths::FileExists(PluginFGDPath))
	{
		PluginFGDPath = FPaths::ProjectDir() / TEXT("Resources") / TEXT("cstrike.fgd");
	}
	if (FPaths::FileExists(PluginFGDPath))
	{
		LoadFGD(PluginFGDPath);
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Auto-loaded FGD. %d entity classes."), FGDDatabase.Classes.Num());
	}
}

void FSourceBridgeModule::ShutdownModule()
{
	FSourceMaterialBrowserTab::Unregister();
	FSourceIOGraphTab::Unregister();
	FVMFPreviewTab::Unregister();
	FSourceEntityPaletteTab::Unregister();
	FSourceEntityDetailCustomization::Unregister();
	FSourceBridgeToolbar::Unregister();

	ExportTestBoxRoomCommand.Reset();
	ExportSceneCommand.Reset();
	CompileMapCommand.Reset();
	ExportModelCommand.Reset();
	FullExportCommand.Reset();
	ValidateCommand.Reset();
	LoadFGDCommand.Reset();
	ListEntitiesCommand.Reset();
	AnalyzeVisCommand.Reset();
	ImportVMFCommand.Reset();
	ImportBSPCommand.Reset();
	PlayTestCommand.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSourceBridgeModule, SourceBridge)
