#include "SourceBridgeModule.h"
#include "VMF/VMFExporter.h"
#include "Compile/CompilePipeline.h"
#include "Models/SMDExporter.h"
#include "Models/QCWriter.h"
#include "Pipeline/FullExportPipeline.h"
#include "Validation/ExportValidator.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Selection.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "FSourceBridgeModule"

void FSourceBridgeModule::StartupModule()
{
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

	UE_LOG(LogTemp, Log, TEXT("SourceBridge plugin loaded. Commands: SourceBridge.ExportScene, SourceBridge.ExportTestBoxRoom, SourceBridge.CompileMap, SourceBridge.ExportModel, SourceBridge.FullExport, SourceBridge.Validate"));
}

void FSourceBridgeModule::ShutdownModule()
{
	ExportTestBoxRoomCommand.Reset();
	ExportSceneCommand.Reset();
	CompileMapCommand.Reset();
	ExportModelCommand.Reset();
	FullExportCommand.Reset();
	ValidateCommand.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSourceBridgeModule, SourceBridge)
