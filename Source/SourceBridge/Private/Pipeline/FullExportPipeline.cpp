#include "Pipeline/FullExportPipeline.h"
#include "VMF/VMFExporter.h"
#include "Validation/ExportValidator.h"
#include "Compile/CompilePipeline.h"
#include "Models/SMDExporter.h"
#include "Models/QCWriter.h"
#include "Models/SourceModelManifest.h"
#include "Import/ModelImporter.h"
#include "Import/SourceSoundManifest.h"
#include "Import/SourceResourceManifest.h"
#include "Materials/SourceMaterialManifest.h"
#include "Materials/TextureExporter.h"
#include "Materials/VMTWriter.h"
#include "Materials/MaterialAnalyzer.h"
#include "Actors/SourceEntityActor.h"
#include "Entities/FGDParser.h"
#include "SourceBridgeModule.h"
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
	FString MapName = Settings.MapName;
	if (MapName.IsEmpty())
	{
		MapName = World->GetMapName();
		// Clean up UE map name prefixes
		MapName = MapName.Replace(TEXT("UEDPIE_0_"), TEXT(""));
		MapName = MapName.Replace(TEXT("UEDPIE_"), TEXT(""));
		if (MapName.IsEmpty()) MapName = TEXT("export");
	}

	FString OutputDir = Settings.OutputDir;
	if (OutputDir.IsEmpty())
	{
		// Default: Export/<MapName>/ with all assets alongside the VMF
		OutputDir = FPaths::ProjectSavedDir() / TEXT("SourceBridge") / TEXT("Export") / MapName;
	}

	// Convert to absolute path so compile tools can find the files regardless of working directory
	OutputDir = FPaths::ConvertRelativePathToFull(OutputDir);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutputDir);

	// Create standard Source content subdirectories
	PlatformFile.CreateDirectoryTree(*(OutputDir / TEXT("materials")));
	PlatformFile.CreateDirectoryTree(*(OutputDir / TEXT("models")));
	PlatformFile.CreateDirectoryTree(*(OutputDir / TEXT("resource")));
	PlatformFile.CreateDirectoryTree(*(OutputDir / TEXT("sound")));

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

	// ---- Step 3b: Collect custom content for packing ----
	// Uses FGD-aware entity scanning (auto-detect), force-pack overrides, and pack-all toggle.
	// Collected files are staged to the output folder AND tracked for bspzip packing.
	TMap<FString, FString> CustomContentFiles; // internal path → staged disk path
	{
		ReportProgress(TEXT("Collecting custom content..."), 0.3f);
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: === Content Collection ==="));

		// Get manifests
		USourceModelManifest* ModelManifest = USourceModelManifest::Get();
		USourceSoundManifest* SoundManifest = USourceSoundManifest::Get();
		USourceResourceManifest* ResourceManifest = USourceResourceManifest::Get();
		FModelImporter::SetupGameSearchPaths(Settings.GameName);

		// Sets of Source paths we want to pack (normalized lowercase, forward slashes)
		TSet<FString> ReferencedModelPaths;
		TSet<FString> ReferencedSoundPaths;

		// Counters for logging
		int32 EntityCount = 0;
		int32 ModelRefsFound = 0;
		int32 SoundRefsFound = 0;

		if (Settings.bPackAllManifestAssets)
		{
			UE_LOG(LogTemp, Log, TEXT("SourceBridge: Pack-all mode: including all non-stock manifest assets"));
		}
		else
		{
			// ---- FGD-aware auto-detect: scan entities for asset references ----
			const FFGDDatabase& FGD = FSourceBridgeModule::GetFGDDatabase();
			bool bHasFGD = FGD.Classes.Num() > 0;

			if (bHasFGD)
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Using FGD-aware entity scanning (%d classes loaded)"), FGD.Classes.Num());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("SourceBridge: No FGD loaded — using fallback key name scanning"));
			}

			for (TActorIterator<ASourceEntityActor> It(World); It; ++It)
			{
				ASourceEntityActor* Entity = *It;
				if (!Entity) continue;
				EntityCount++;

				// Collect model from ASourceProp::ModelPath directly
				if (ASourceProp* Prop = Cast<ASourceProp>(Entity))
				{
					if (!Prop->ModelPath.IsEmpty())
					{
						FString NormPath = Prop->ModelPath.ToLower();
						NormPath.ReplaceInline(TEXT("\\"), TEXT("/"));
						ReferencedModelPaths.Add(NormPath);
						ModelRefsFound++;
					}
				}

				// FGD-aware property scanning
				if (bHasFGD && !Entity->SourceClassname.IsEmpty())
				{
					FFGDEntityClass Resolved = FGD.GetResolved(Entity->SourceClassname);
					for (const FFGDProperty& Prop : Resolved.Properties)
					{
						const FString* Val = Entity->KeyValues.Find(Prop.Name);
						if (!Val || Val->IsEmpty()) continue;

						FString NormVal = Val->ToLower();
						NormVal.ReplaceInline(TEXT("\\"), TEXT("/"));

						if (Prop.Type == EFGDPropertyType::Studio || Prop.Type == EFGDPropertyType::Sprite)
						{
							ReferencedModelPaths.Add(NormVal);
							ModelRefsFound++;
						}
						else if (Prop.Type == EFGDPropertyType::Sound)
						{
							ReferencedSoundPaths.Add(NormVal);
							if (!NormVal.StartsWith(TEXT("sound/")))
								ReferencedSoundPaths.Add(TEXT("sound/") + NormVal);
							SoundRefsFound++;
						}
						// Materials are handled separately via VMF export UsedMaterialPaths
					}
				}
				else
				{
					// Fallback: hardcoded key name scanning when FGD not loaded
					static const TArray<FString> SoundKeyNames = {
						TEXT("message"), TEXT("startsound"), TEXT("stopsound"), TEXT("movesound"),
						TEXT("StartSound"), TEXT("StopSound"), TEXT("MoveSound"),
						TEXT("ClosedSound"), TEXT("LockedSound"), TEXT("UnlockedSound"),
						TEXT("SoundStart"), TEXT("SoundStop"),
					};

					for (const FString& SoundKey : SoundKeyNames)
					{
						const FString* Val = Entity->KeyValues.Find(SoundKey);
						if (Val && !Val->IsEmpty())
						{
							FString NormPath = Val->ToLower();
							NormPath.ReplaceInline(TEXT("\\"), TEXT("/"));
							ReferencedSoundPaths.Add(NormPath);
							if (!NormPath.StartsWith(TEXT("sound/")))
								ReferencedSoundPaths.Add(TEXT("sound/") + NormPath);
							SoundRefsFound++;
						}
					}

					// Fallback model scanning from keyvalues named "model"
					const FString* ModelVal = Entity->KeyValues.Find(TEXT("model"));
					if (ModelVal && !ModelVal->IsEmpty() && ModelVal->EndsWith(TEXT(".mdl")))
					{
						FString NormPath = ModelVal->ToLower();
						NormPath.ReplaceInline(TEXT("\\"), TEXT("/"));
						ReferencedModelPaths.Add(NormPath);
						ModelRefsFound++;
					}
				}
			}

			UE_LOG(LogTemp, Log, TEXT("SourceBridge: Scanned %d entities: %d model refs, %d sound refs"),
				EntityCount, ModelRefsFound, SoundRefsFound);
		}

		// ---- Collect models ----
		int32 ModelsStaged = 0;
		int32 ModelsStock = 0;
		int32 ModelFiles = 0;

		auto StageModelEntry = [&](const FSourceModelEntry& Entry)
		{
			if (Entry.bIsStock) { ModelsStock++; return; }
			if (Entry.DiskPaths.Num() == 0) return;

			FString BasePath = FPaths::ChangeExtension(Entry.SourcePath.ToLower(), TEXT(""));
			BasePath.ReplaceInline(TEXT("\\"), TEXT("/"));

			for (const auto& FilePair : Entry.DiskPaths)
			{
				if (!FPaths::FileExists(FilePair.Value)) continue;
				FString InternalPath = BasePath + FilePair.Key;
				InternalPath.ReplaceInline(TEXT("\\"), TEXT("/"));
				if (!CustomContentFiles.Contains(InternalPath))
				{
					// Stage: copy to output dir
					FString StagedPath = OutputDir / InternalPath;
					PlatformFile.CreateDirectoryTree(*FPaths::GetPath(StagedPath));
					PlatformFile.CopyFile(*StagedPath, *FilePair.Value);
					CustomContentFiles.Add(InternalPath, StagedPath);
					ModelFiles++;
				}
			}
			ModelsStaged++;
		};

		if (ModelManifest)
		{
			TSet<FString> ProcessedModels;
			for (const FSourceModelEntry& Entry : ModelManifest->Entries)
			{
				FString NormPath = Entry.SourcePath.ToLower();
				NormPath.ReplaceInline(TEXT("\\"), TEXT("/"));
				if (ProcessedModels.Contains(NormPath)) continue;
				ProcessedModels.Add(NormPath);

				bool bShouldPack = Entry.bForcePack;
				if (!bShouldPack && Settings.bPackAllManifestAssets)
					bShouldPack = !Entry.bIsStock;
				if (!bShouldPack)
					bShouldPack = ReferencedModelPaths.Contains(NormPath);

				if (bShouldPack)
				{
					StageModelEntry(Entry);
				}
				else if (Entry.bIsStock)
				{
					ModelsStock++;
				}
			}

			// Fallback for models not in manifest (imported before manifest existed)
			for (const FString& RefPath : ReferencedModelPaths)
			{
				if (ProcessedModels.Contains(RefPath)) continue;
				ProcessedModels.Add(RefPath);

				if (FModelImporter::IsStockModel(RefPath))
				{
					ModelsStock++;
					continue;
				}

				TMap<FString, FString> DiskPaths;
				if (FModelImporter::FindModelDiskPaths(RefPath, DiskPaths))
				{
					FString BasePath = FPaths::ChangeExtension(RefPath, TEXT(""));
					for (const auto& FilePair : DiskPaths)
					{
						if (!FPaths::FileExists(FilePair.Value)) continue;
						FString InternalPath = BasePath + FilePair.Key;
						InternalPath.ReplaceInline(TEXT("\\"), TEXT("/"));
						if (!CustomContentFiles.Contains(InternalPath))
						{
							FString StagedPath = OutputDir / InternalPath;
							PlatformFile.CreateDirectoryTree(*FPaths::GetPath(StagedPath));
							PlatformFile.CopyFile(*StagedPath, *FilePair.Value);
							CustomContentFiles.Add(InternalPath, StagedPath);
							ModelFiles++;
						}
					}
					ModelsStaged++;
				}
				else
				{
					Result.Warnings.Add(FString::Printf(
						TEXT("[Models] Custom model files not found on disk: %s"), *RefPath));
				}
			}
		}

		// ---- Collect sounds ----
		int32 SoundsStaged = 0;
		int32 SoundsStock = 0;

		if (SoundManifest)
		{
			for (const FSourceSoundEntry& Entry : SoundManifest->Entries)
			{
				if (Entry.DiskPath.IsEmpty() || !FPaths::FileExists(Entry.DiskPath))
				{
					if (!Entry.bIsStock && !Entry.DiskPath.IsEmpty())
						Result.Warnings.Add(FString::Printf(TEXT("[Sounds] Disk file missing: %s"), *Entry.DiskPath));
					continue;
				}

				if (Entry.bIsStock) { SoundsStock++; continue; }

				FString NormPath = Entry.SourcePath.ToLower();
				NormPath.ReplaceInline(TEXT("\\"), TEXT("/"));

				bool bShouldPack = Entry.bForcePack;
				if (!bShouldPack && Settings.bPackAllManifestAssets)
					bShouldPack = true;
				if (!bShouldPack)
					bShouldPack = ReferencedSoundPaths.Contains(NormPath);

				if (bShouldPack)
				{
					FString InternalPath = Entry.SourcePath;
					InternalPath.ReplaceInline(TEXT("\\"), TEXT("/"));
					if (!CustomContentFiles.Contains(InternalPath))
					{
						// Stage: copy to output dir
						FString StagedPath = OutputDir / InternalPath;
						PlatformFile.CreateDirectoryTree(*FPaths::GetPath(StagedPath));
						PlatformFile.CopyFile(*StagedPath, *Entry.DiskPath);
						CustomContentFiles.Add(InternalPath, StagedPath);
						SoundsStaged++;
					}
				}
			}
		}

		// ---- Collect resources ----
		int32 ResourcesStaged = 0;

		if (ResourceManifest)
		{
			for (const FSourceResourceEntry& Entry : ResourceManifest->Entries)
			{
				if (Entry.Origin == ESourceResourceOrigin::Stock) continue;
				if (Entry.DiskPath.IsEmpty() || !FPaths::FileExists(Entry.DiskPath))
				{
					if (!Entry.DiskPath.IsEmpty())
						Result.Warnings.Add(FString::Printf(TEXT("[Resources] Disk file missing: %s"), *Entry.DiskPath));
					continue;
				}

				bool bShouldPack = Entry.bForcePack;
				if (!bShouldPack)
					bShouldPack = true; // Resources always packed (overviews, configs)

				if (bShouldPack)
				{
					FString InternalPath = Entry.SourcePath;
					InternalPath.ReplaceInline(TEXT("\\"), TEXT("/"));
					if (!CustomContentFiles.Contains(InternalPath))
					{
						FString StagedPath = OutputDir / InternalPath;
						PlatformFile.CreateDirectoryTree(*FPaths::GetPath(StagedPath));
						PlatformFile.CopyFile(*StagedPath, *Entry.DiskPath);
						CustomContentFiles.Add(InternalPath, StagedPath);
						ResourcesStaged++;
					}
				}
			}
		}

		// Log collection summary
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: === Content Staging ==="));
		UE_LOG(LogTemp, Log, TEXT("SourceBridge:   Models: %d custom (%d files), %d stock skipped"),
			ModelsStaged, ModelFiles, ModelsStock);
		UE_LOG(LogTemp, Log, TEXT("SourceBridge:   Sounds: %d custom, %d stock skipped"),
			SoundsStaged, SoundsStock);
		UE_LOG(LogTemp, Log, TEXT("SourceBridge:   Resources: %d files"), ResourcesStaged);
		// Material count logged in Step 4b below
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
	// Also handles force-pack materials not in the VMF UsedMaterialPaths set
	{
		USourceMaterialManifest* Manifest = USourceMaterialManifest::Get();
		if (Manifest)
		{
			// Add force-packed materials to the used set
			for (const FSourceMaterialEntry& Entry : Manifest->Entries)
			{
				if (Entry.bForcePack)
				{
					UsedMaterialPaths.Add(Entry.SourcePath);
				}
				else if (Settings.bPackAllManifestAssets && !Entry.bIsInVPK &&
					Entry.Type != ESourceMaterialType::Stock)
				{
					UsedMaterialPaths.Add(Entry.SourcePath);
				}
			}

			if (UsedMaterialPaths.Num() > 0)
			{
				ReportProgress(TEXT("Exporting custom materials..."), 0.5f);
				FString MaterialsDir = OutputDir / TEXT("materials");
				int32 MaterialExportCount = 0;
				int32 MaterialStockCount = 0;

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

					if (!bNeedsExport) { MaterialStockCount++; continue; }

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

				UE_LOG(LogTemp, Log, TEXT("SourceBridge:   Materials: %d custom exported (VTF+VMT), %d stock skipped"),
					MaterialExportCount, MaterialStockCount);
			}
		}
	}

	// ---- Export Summary ----
	UE_LOG(LogTemp, Log, TEXT("SourceBridge: === Export Summary ==="));
	UE_LOG(LogTemp, Log, TEXT("SourceBridge:   Output: %s"), *OutputDir);
	UE_LOG(LogTemp, Log, TEXT("SourceBridge:   VMF: %s"), *FPaths::GetCleanFilename(Result.VMFPath));
	UE_LOG(LogTemp, Log, TEXT("SourceBridge:   Total content files: %d"), CustomContentFiles.Num());
	if (Result.Warnings.Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("SourceBridge:   Warnings: %d"), Result.Warnings.Num());
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
			UE_LOG(LogTemp, Log, TEXT("SourceBridge: Packing %d content files into BSP via bspzip..."),
				CustomContentFiles.Num());

			FCompileResult PackResult = FCompilePipeline::PackCustomContent(
				Result.BSPPath, ToolsDir, CustomContentFiles);

			if (PackResult.bSuccess)
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Successfully packed %d files into BSP"),
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

		PlatformFile.CreateDirectoryTree(*PackageMaps);

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

		// Copy all content subdirectories from output to package
		static const TArray<FString> ContentDirs = {
			TEXT("materials"), TEXT("models"), TEXT("sound"), TEXT("resource")
		};

		for (const FString& DirName : ContentDirs)
		{
			FString SrcDir = OutputDir / DirName;
			FString DstDir = PackageDir / DirName;
			if (PlatformFile.DirectoryExists(*SrcDir))
			{
				PlatformFile.CreateDirectoryTree(*DstDir);
				TArray<FString> Files;
				PlatformFile.FindFilesRecursively(Files, *SrcDir, TEXT(""));
				for (const FString& SrcFile : Files)
				{
					FString RelPath = SrcFile;
					FPaths::MakePathRelativeTo(RelPath, *(SrcDir + TEXT("/")));
					FString DstFile = DstDir / RelPath;
					PlatformFile.CreateDirectoryTree(*FPaths::GetPath(DstFile));
					PlatformFile.CopyFile(*DstFile, *SrcFile);
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
