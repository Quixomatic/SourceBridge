#include "Pipeline/FullExportPipeline.h"
#include "VMF/VMFExporter.h"
#include "Validation/ExportValidator.h"
#include "Compile/CompilePipeline.h"
#include "Models/SMDExporter.h"
#include "Models/QCWriter.h"
#include "Import/ModelImporter.h"
#include "Materials/SourceMaterialManifest.h"
#include "Materials/TextureExporter.h"
#include "Materials/VMTWriter.h"
#include "Materials/MaterialAnalyzer.h"
#include "Actors/SourceEntityActor.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"

FFullExportResult FFullExportPipeline::Run(UWorld* World, const FFullExportSettings& Settings)
{
	return RunWithProgress(World, Settings, FOnPipelineProgress());
}

FFullExportResult FFullExportPipeline::RunWithProgress(
	UWorld* World,
	const FFullExportSettings& Settings,
	FOnPipelineProgress ProgressCallback)
{
	FFullExportResult Result;
	double StartTime = FPlatformTime::Seconds();

	auto ReportProgress = [&ProgressCallback](const FString& Step, float Progress)
	{
		if (ProgressCallback.IsBound())
		{
			ProgressCallback.Execute(Step, Progress);
		}
	};

	if (!World)
	{
		Result.ErrorMessage = TEXT("No world provided.");
		return Result;
	}

	// ---- Step 1: Validate ----
	if (Settings.bValidate)
	{
		ReportProgress(TEXT("Validating scene..."), 0.0f);
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

	// Convert to absolute path so compile tools can find the files regardless of working directory
	OutputDir = FPaths::ConvertRelativePathToFull(OutputDir);

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
		ReportProgress(TEXT("Compiling models..."), 0.2f);
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

	// ---- Step 3b: Collect custom imported models (ASourceProp) ----
	// These already have compiled .mdl/.vvd/.vtx/.phy files from import.
	// We need to stage them for bspzip packing (stock models are skipped).
	TMap<FString, FString> CustomContentFiles; // internal path → disk path
	{
		// Initialize model importer search paths for stock detection
		FModelImporter::SetupGameSearchPaths(Settings.GameName);

		TSet<FString> ProcessedModels; // avoid duplicates
		for (TActorIterator<ASourceProp> It(World); It; ++It)
		{
			ASourceProp* Prop = *It;
			if (!Prop || Prop->ModelPath.IsEmpty()) continue;

			FString NormPath = Prop->ModelPath.ToLower();
			NormPath.ReplaceInline(TEXT("\\"), TEXT("/"));
			if (ProcessedModels.Contains(NormPath)) continue;
			ProcessedModels.Add(NormPath);

			// Skip stock models that exist in game VPKs
			if (FModelImporter::IsStockModel(NormPath))
			{
				UE_LOG(LogTemp, Verbose, TEXT("SourceBridge: Stock model (skipped): %s"), *NormPath);
				continue;
			}

			// Find the model files on disk
			TMap<FString, FString> DiskPaths;
			if (FModelImporter::FindModelDiskPaths(NormPath, DiskPaths))
			{
				FString BasePath = FPaths::ChangeExtension(NormPath, TEXT(""));
				for (const auto& FilePair : DiskPaths)
				{
					// Internal path: "models/foo/bar.mdl" etc.
					FString InternalPath = BasePath + FilePair.Key;
					InternalPath.ReplaceInline(TEXT("\\"), TEXT("/"));
					CustomContentFiles.Add(InternalPath, FilePair.Value);
				}
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Custom model staged: %s (%d files)"),
					*NormPath, DiskPaths.Num());
			}
			else
			{
				Result.Warnings.Add(FString::Printf(
					TEXT("[Models] Custom model files not found on disk: %s"), *NormPath));
			}
		}

		if (CustomContentFiles.Num() > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("SourceBridge: %d custom content files staged for BSP packing"),
				CustomContentFiles.Num());
		}
	}

	// ---- Step 4: Export VMF ----
	ReportProgress(TEXT("Exporting VMF..."), 0.4f);
	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Exporting scene to VMF..."));
	TSet<FString> UsedMaterialPaths;
	FString VMFContent = FVMFExporter::ExportScene(World, MapName, &UsedMaterialPaths);

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

	// ---- Step 4b: Export custom material files (VTF + VMT) ----
	{
		USourceMaterialManifest* Manifest = USourceMaterialManifest::Get();
		if (Manifest && UsedMaterialPaths.Num() > 0)
		{
			ReportProgress(TEXT("Exporting custom materials..."), 0.5f);
			FString MaterialsDir = OutputDir / TEXT("materials");
			int32 MaterialExportCount = 0;

			for (const FString& UsedPath : UsedMaterialPaths)
			{
				FSourceMaterialEntry* Entry = Manifest->FindBySourcePath(UsedPath);
				if (!Entry) continue;

				// Only export materials that need files
				bool bNeedsExport = false;
				if (Entry->Type == ESourceMaterialType::Custom)
				{
					bNeedsExport = true;
				}
				else if (Entry->Type == ESourceMaterialType::Imported && !Entry->bIsInVPK)
				{
					bNeedsExport = true;
				}

				if (!bNeedsExport) continue;

				// Create output directory for this material
				FString MatDir = MaterialsDir / FPaths::GetPath(Entry->SourcePath);
				PlatformFile.CreateDirectoryTree(*MatDir);
				FString BaseName = FPaths::GetBaseFilename(Entry->SourcePath);

				// Export base texture -> TGA -> VTF
				UTexture2D* BaseTexture = Cast<UTexture2D>(Entry->TextureAsset.TryLoad());
				FString VTFPath;
				if (BaseTexture)
				{
					FString TGAPath = MatDir / BaseName + TEXT(".tga");
					if (FTextureExporter::ExportTextureToTGA(BaseTexture, TGAPath))
					{
						// Determine VTF format based on transparency
						FVTFConvertOptions VTFOptions;
						if (Entry->VMTParams.Contains(TEXT("$alphatest")) ||
							Entry->VMTParams.Contains(TEXT("$translucent")))
						{
							VTFOptions.Format = TEXT("DXT5");
						}
						else
						{
							VTFOptions.Format = TEXT("DXT1");
						}
						VTFOptions.bGenerateMipmaps = true;

						VTFPath = FTextureExporter::ConvertTGAToVTF(TGAPath, MatDir, VTFOptions);
						if (!VTFPath.IsEmpty())
						{
							FString InternalPath = FString(TEXT("materials/")) + Entry->SourcePath + TEXT(".vtf");
							InternalPath.ReplaceInline(TEXT("\\"), TEXT("/"));
							CustomContentFiles.Add(InternalPath, VTFPath);
						}

						// Clean up TGA (VTF is what we need for the game)
						PlatformFile.DeleteFile(*TGAPath);
					}
				}

				// Export normal map -> TGA -> VTF (if present)
				UTexture2D* NormalMap = Cast<UTexture2D>(Entry->NormalMapAsset.TryLoad());
				FString NormalVTFPath;
				FString NormalSourcePath;
				if (NormalMap)
				{
					NormalSourcePath = Entry->SourcePath + TEXT("_normal");
					FString NormalTGAPath = MatDir / BaseName + TEXT("_normal.tga");
					if (FTextureExporter::ExportTextureToTGA(NormalMap, NormalTGAPath))
					{
						FVTFConvertOptions NormalOptions;
						NormalOptions.Format = TEXT("DXT5");
						NormalOptions.bGenerateMipmaps = true;
						NormalOptions.bNormalMap = true;

						NormalVTFPath = FTextureExporter::ConvertTGAToVTF(NormalTGAPath, MatDir, NormalOptions);
						if (!NormalVTFPath.IsEmpty())
						{
							FString InternalPath = FString(TEXT("materials/")) + NormalSourcePath + TEXT(".vtf");
							InternalPath.ReplaceInline(TEXT("\\"), TEXT("/"));
							CustomContentFiles.Add(InternalPath, NormalVTFPath);
						}

						PlatformFile.DeleteFile(*NormalTGAPath);
					}
				}

				// Generate VMT
				FString VMTContent;
				if (Entry->Type == ESourceMaterialType::Imported && Entry->VMTParams.Num() > 0)
				{
					// Lossless re-export: use stored VMT params from import
					VMTContent = FVMTWriter::GenerateFromStoredParams(Entry->VMTShader, Entry->VMTParams);
				}
				else
				{
					// Generate VMT for custom materials
					FVMTWriter Writer;
					Writer.SetShader(TEXT("LightmappedGeneric"));
					Writer.SetBaseTexture(Entry->SourcePath);

					if (!NormalSourcePath.IsEmpty())
					{
						Writer.SetBumpMap(NormalSourcePath);
					}

					// Apply stored VMT params (transparency, two-sided, etc.)
					for (const auto& Param : Entry->VMTParams)
					{
						Writer.SetParameter(Param.Key, Param.Value);
					}

					// Auto-detect surface property if not already set
					if (!Entry->VMTParams.Contains(TEXT("$surfaceprop")))
					{
						Writer.SetSurfaceProp(TEXT("default"));
					}

					VMTContent = Writer.Serialize();
				}

				FString VMTPath = MatDir / BaseName + TEXT(".vmt");
				if (FFileHelper::SaveStringToFile(VMTContent, *VMTPath))
				{
					FString InternalPath = FString(TEXT("materials/")) + Entry->SourcePath + TEXT(".vmt");
					InternalPath.ReplaceInline(TEXT("\\"), TEXT("/"));
					CustomContentFiles.Add(InternalPath, VMTPath);
				}

				MaterialExportCount++;
			}

			if (MaterialExportCount > 0)
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Exported %d custom material(s) to %s"),
					MaterialExportCount, *MaterialsDir);
			}
		}
	}

	// ---- Step 5: Compile map (dependency: after materials and models) ----
	if (Settings.bCompile)
	{
		ReportProgress(TEXT("Compiling map (vbsp/vvis/vrad)..."), 0.6f);
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

		// ---- Step 5b: Pack custom content into BSP via bspzip ----
		if (CustomContentFiles.Num() > 0 && FPaths::FileExists(Result.BSPPath))
		{
			ReportProgress(TEXT("Packing custom content into BSP..."), 0.8f);
			FCompileResult PackResult = FCompilePipeline::PackCustomContent(
				Result.BSPPath, ToolsDir, CustomContentFiles);

			if (PackResult.bSuccess)
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Packed %d custom files into BSP"),
					CustomContentFiles.Num());
			}
			else
			{
				Result.Warnings.Add(TEXT("[Pack] bspzip failed: ") + PackResult.ErrorMessage);
				UE_LOG(LogTemp, Warning, TEXT("SourceBridge: bspzip failed: %s"),
					*PackResult.ErrorMessage);
			}
		}
	}

	// ---- Step 6: Package distributable ----
	if (Settings.bPackage)
	{
		ReportProgress(TEXT("Packaging distributable..."), 0.9f);
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
