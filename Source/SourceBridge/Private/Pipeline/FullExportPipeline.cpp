#include "Pipeline/FullExportPipeline.h"
#include "VMF/VMFExporter.h"
#include "Validation/ExportValidator.h"
#include "Compile/CompilePipeline.h"
#include "Models/SMDExporter.h"
#include "Models/QCWriter.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"

FFullExportResult FFullExportPipeline::Run(UWorld* World, const FFullExportSettings& Settings)
{
	FFullExportResult Result;
	double StartTime = FPlatformTime::Seconds();

	if (!World)
	{
		Result.ErrorMessage = TEXT("No world provided.");
		return Result;
	}

	// ---- Step 1: Validate ----
	if (Settings.bValidate)
	{
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Running pre-export validation..."));
		FValidationResult Validation = FExportValidator::ValidateWorld(World);
		Validation.LogAll();

		if (Validation.HasErrors())
		{
			Result.ErrorMessage = FString::Printf(
				TEXT("Validation failed with %d errors. Fix issues before exporting."),
				Validation.ErrorCount);

			for (const FValidationMessage& Msg : Validation.Messages)
			{
				if (Msg.Severity == EValidationSeverity::Warning ||
					Msg.Severity == EValidationSeverity::Error)
				{
					Result.Warnings.Add(FString::Printf(TEXT("[%s] %s"), *Msg.Category, *Msg.Message));
				}
			}
			return Result;
		}

		// Collect warnings even on success
		for (const FValidationMessage& Msg : Validation.Messages)
		{
			if (Msg.Severity == EValidationSeverity::Warning)
			{
				Result.Warnings.Add(FString::Printf(TEXT("[%s] %s"), *Msg.Category, *Msg.Message));
			}
		}
	}

	// ---- Step 2: Set up output paths ----
	FString OutputDir = Settings.OutputDir;
	if (OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ProjectSavedDir() / TEXT("SourceBridge");
	}

	FString MapName = Settings.MapName;
	if (MapName.IsEmpty())
	{
		MapName = World->GetMapName();
		// Clean up UE map name prefixes
		MapName = MapName.Replace(TEXT("UEDPIE_0_"), TEXT(""));
		MapName = MapName.Replace(TEXT("UEDPIE_"), TEXT(""));
		if (MapName.IsEmpty()) MapName = TEXT("export");
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutputDir);

	Result.VMFPath = OutputDir / MapName + TEXT(".vmf");

	// Detect compile tools early (needed for model compile and map compile)
	FString ToolsDir;
	FString GameDir;
	if (Settings.bCompile)
	{
		ToolsDir = FCompilePipeline::FindToolsDirectory();
		GameDir = FCompilePipeline::FindGameDirectory(Settings.GameName);
	}

	// ---- Step 3: Export and compile models (dependency: before map compile) ----
	// Models must be compiled before the map so prop_static references resolve
	if (Settings.bCompile && !ToolsDir.IsEmpty() && !GameDir.IsEmpty())
	{
		FString ModelsDir = OutputDir / TEXT("models");
		PlatformFile.CreateDirectoryTree(*ModelsDir);

		int32 ModelCount = 0;
		int32 ModelErrors = 0;

		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			AStaticMeshActor* Actor = *It;
			if (!Actor) continue;

			UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
			if (!MeshComp) continue;

			UStaticMesh* Mesh = MeshComp->GetStaticMesh();
			if (!Mesh) continue;

			// Skip meshes that are just BSP geometry references
			FString MeshName = Mesh->GetName();
			if (MeshName.StartsWith(TEXT("Default")) || MeshName.IsEmpty())
			{
				continue;
			}

			// Export SMD
			FSMDExportResult SMDResult = FSMDExporter::ExportStaticMesh(Mesh);
			if (!SMDResult.bSuccess)
			{
				Result.Warnings.Add(FString::Printf(TEXT("[Models] Failed to export %s: %s"),
					*MeshName, *SMDResult.ErrorMessage));
				ModelErrors++;
				continue;
			}

			FString BaseName = MeshName.ToLower();
			if (BaseName.StartsWith(TEXT("SM_"))) BaseName = BaseName.Mid(3);
			else if (BaseName.StartsWith(TEXT("S_"))) BaseName = BaseName.Mid(2);

			FString RefPath = ModelsDir / BaseName + TEXT("_ref.smd");
			FString PhysPath = ModelsDir / BaseName + TEXT("_phys.smd");
			FString IdlePath = ModelsDir / BaseName + TEXT("_idle.smd");
			FString QCPath = ModelsDir / BaseName + TEXT(".qc");

			FFileHelper::SaveStringToFile(SMDResult.ReferenceSMD, *RefPath);
			FFileHelper::SaveStringToFile(SMDResult.PhysicsSMD, *PhysPath);
			FFileHelper::SaveStringToFile(SMDResult.IdleSMD, *IdlePath);

			FQCSettings QCSettings = FQCWriter::MakeDefaultSettings(MeshName);
			FString QCContent = FQCWriter::GenerateQC(QCSettings);
			FFileHelper::SaveStringToFile(QCContent, *QCPath);

			// Compile with studiomdl
			FModelCompileSettings ModelSettings;
			ModelSettings.ToolsDir = ToolsDir;
			ModelSettings.GameDir = GameDir;
			ModelSettings.QCPath = QCPath;

			FCompileResult ModelResult = FCompilePipeline::CompileModel(ModelSettings);
			if (ModelResult.bSuccess)
			{
				ModelCount++;
			}
			else
			{
				ModelErrors++;
				Result.Warnings.Add(FString::Printf(TEXT("[Models] studiomdl failed for %s: %s"),
					*BaseName, *ModelResult.ErrorMessage));
			}
		}

		if (ModelCount > 0 || ModelErrors > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("SourceBridge: Model compile: %d succeeded, %d failed"),
				ModelCount, ModelErrors);
		}
	}

	// ---- Step 4: Export VMF ----
	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Exporting scene to VMF..."));
	FString VMFContent = FVMFExporter::ExportScene(World);

	if (VMFContent.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Export produced empty VMF.");
		return Result;
	}

	if (!FFileHelper::SaveStringToFile(VMFContent, *Result.VMFPath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Failed to write VMF to: %s"), *Result.VMFPath);
		return Result;
	}

	Result.ExportSeconds = FPlatformTime::Seconds() - StartTime;
	UE_LOG(LogTemp, Log, TEXT("SourceBridge: VMF exported to %s (%.1f seconds)"),
		*Result.VMFPath, Result.ExportSeconds);

	// ---- Step 5: Compile map (dependency: after materials and models) ----
	if (Settings.bCompile)
	{
		FCompileSettings CompileSettings;
		CompileSettings.VMFPath = Result.VMFPath;
		CompileSettings.bFastCompile = Settings.bFastCompile;
		CompileSettings.bFinalCompile = Settings.bFinalCompile;
		CompileSettings.bCopyToGame = Settings.bCopyToGame;
		CompileSettings.ToolsDir = ToolsDir;
		CompileSettings.GameDir = GameDir;

		if (CompileSettings.ToolsDir.IsEmpty())
		{
			Result.ErrorMessage = TEXT("Could not find Source compile tools. Install Source SDK via Steam.");
			// VMF was still exported successfully
			Result.bSuccess = true;
			UE_LOG(LogTemp, Warning, TEXT("SourceBridge: VMF exported but compile skipped - no tools found."));
			return Result;
		}

		if (CompileSettings.GameDir.IsEmpty())
		{
			Result.ErrorMessage = FString::Printf(
				TEXT("Could not find game directory for '%s'. Install the game via Steam."),
				*Settings.GameName);
			Result.bSuccess = true;
			UE_LOG(LogTemp, Warning, TEXT("SourceBridge: VMF exported but compile skipped - game not found."));
			return Result;
		}

		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Compiling map..."));
		double CompileStart = FPlatformTime::Seconds();
		FCompileResult CompileResult = FCompilePipeline::CompileMap(CompileSettings);
		Result.CompileSeconds = FPlatformTime::Seconds() - CompileStart;

		if (!CompileResult.bSuccess)
		{
			Result.ErrorMessage = TEXT("Compile failed: ") + CompileResult.ErrorMessage;
			// VMF export was still successful
			Result.bSuccess = true;
			UE_LOG(LogTemp, Error, TEXT("SourceBridge: Compile failed: %s"), *CompileResult.ErrorMessage);
			return Result;
		}

		Result.BSPPath = FPaths::ChangeExtension(Result.VMFPath, TEXT(".bsp"));
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Compile completed in %.1f seconds."), Result.CompileSeconds);
	}

	// ---- Step 6: Package distributable ----
	if (Settings.bPackage)
	{
		FString PackageDir = OutputDir / TEXT("package") / Settings.GameName;
		FString PackageMaps = PackageDir / TEXT("maps");
		FString PackageMaterials = PackageDir / TEXT("materials");
		FString PackageModels = PackageDir / TEXT("models");

		PlatformFile.CreateDirectoryTree(*PackageMaps);
		PlatformFile.CreateDirectoryTree(*PackageMaterials);
		PlatformFile.CreateDirectoryTree(*PackageModels);

		// Copy BSP to package
		if (!Result.BSPPath.IsEmpty() && PlatformFile.FileExists(*Result.BSPPath))
		{
			FString DestBSP = PackageMaps / FPaths::GetCleanFilename(Result.BSPPath);
			PlatformFile.CopyFile(*DestBSP, *Result.BSPPath);
		}

		// Copy VMF source to package (for editing)
		if (PlatformFile.FileExists(*Result.VMFPath))
		{
			FString DestVMF = PackageMaps / FPaths::GetCleanFilename(Result.VMFPath);
			PlatformFile.CopyFile(*DestVMF, *Result.VMFPath);
		}

		// Copy materials from output to package
		FString MatSourceDir = OutputDir / TEXT("materials");
		if (PlatformFile.DirectoryExists(*MatSourceDir))
		{
			TArray<FString> MatFiles;
			PlatformFile.FindFilesRecursively(MatFiles, *MatSourceDir, TEXT(""));
			for (const FString& MatFile : MatFiles)
			{
				FString RelPath = MatFile;
				FPaths::MakePathRelativeTo(RelPath, *(MatSourceDir + TEXT("/")));
				FString DestPath = PackageMaterials / RelPath;
				PlatformFile.CreateDirectoryTree(*FPaths::GetPath(DestPath));
				PlatformFile.CopyFile(*DestPath, *MatFile);
			}
		}

		// Copy models from output to package
		FString ModelSourceDir = OutputDir / TEXT("models");
		if (PlatformFile.DirectoryExists(*ModelSourceDir))
		{
			TArray<FString> ModelFiles;
			PlatformFile.FindFilesRecursively(ModelFiles, *ModelSourceDir, TEXT(""));
			for (const FString& ModelFile : ModelFiles)
			{
				// Only copy compiled model files (.mdl, .vtx, .vvd, .phy)
				FString Ext = FPaths::GetExtension(ModelFile).ToLower();
				if (Ext == TEXT("mdl") || Ext == TEXT("vtx") || Ext == TEXT("vvd") || Ext == TEXT("phy"))
				{
					FString RelPath = ModelFile;
					FPaths::MakePathRelativeTo(RelPath, *(ModelSourceDir + TEXT("/")));
					FString DestPath = PackageModels / RelPath;
					PlatformFile.CreateDirectoryTree(*FPaths::GetPath(DestPath));
					PlatformFile.CopyFile(*DestPath, *ModelFile);
				}
			}
		}

		Result.PackagePath = PackageDir;
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Package created at %s"), *PackageDir);
	}

	Result.bSuccess = true;

	double TotalTime = FPlatformTime::Seconds() - StartTime;
	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Full pipeline completed in %.1f seconds."), TotalTime);

	return Result;
}
