#include "Import/BSPImporter.h"
#include "Import/VMFImporter.h"
#include "Import/MaterialImporter.h"
#include "Import/ModelImporter.h"
#include "Import/SoundImporter.h"
#include "Import/SourceResourceManifest.h"
#include "Models/SourceModelManifest.h"
#include "Import/VTFReader.h"
#include "UI/SourceBridgeSettings.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"

FVMFImportResult FBSPImporter::ImportFile(const FString& BSPPath, UWorld* World,
	const FVMFImportSettings& Settings)
{
	FVMFImportResult Result;

	if (!FPaths::FileExists(BSPPath))
	{
		Result.Warnings.Add(FString::Printf(TEXT("BSP file not found: %s"), *BSPPath));
		return Result;
	}

	FString MapName = FPaths::GetBaseFilename(BSPPath);

	FScopedSlowTask SlowTask(5.0f, FText::FromString(FString::Printf(TEXT("Importing %s..."), *MapName)));
	SlowTask.MakeDialog(true);

	// Output to Saved/SourceBridge/Import/<mapname>/ (absolute path for external tools)
	FString OutputDir = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("SourceBridge") / TEXT("Import") / MapName);

	// Create the output directory
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutputDir);

	UE_LOG(LogTemp, Log, TEXT("BSPImporter: Import directory: %s"), *OutputDir);

	// Step 1: Decompile BSP → VMF + extract embedded assets
	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Decompiling BSP...")));

	FString DecompileError;
	FString VMFPath = DecompileBSP(BSPPath, OutputDir, DecompileError);

	if (VMFPath.IsEmpty())
	{
		Result.Warnings.Add(FString::Printf(TEXT("BSPSource decompile failed: %s"), *DecompileError));
		return Result;
	}

	UE_LOG(LogTemp, Log, TEXT("BSPImporter: Decompiled '%s' → '%s'"), *BSPPath, *VMFPath);

	// Step 2: Set up search paths
	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Setting up search paths...")));

	// BSPSource --unpack_embedded extracts files into a nested subdirectory:
	//   <OutputDir>/<mapname>/materials/  (not <OutputDir>/materials/)
	// Detect this and use the nested directory as the asset search path
	FString AssetSearchDir = OutputDir;
	FString NestedDir = OutputDir / MapName;
	if (FPaths::DirectoryExists(NestedDir / TEXT("materials")))
	{
		AssetSearchDir = NestedDir;
		UE_LOG(LogTemp, Log, TEXT("BSPImporter: Using nested asset directory: %s"), *AssetSearchDir);
	}
	else if (!FPaths::DirectoryExists(OutputDir / TEXT("materials")))
	{
		// Search for any materials/ directory under OutputDir
		TArray<FString> SubDirs;
		IFileManager::Get().FindFiles(SubDirs, *(OutputDir / TEXT("*")), false, true);
		for (const FString& SubDir : SubDirs)
		{
			if (FPaths::DirectoryExists(OutputDir / SubDir / TEXT("materials")))
			{
				AssetSearchDir = OutputDir / SubDir;
				UE_LOG(LogTemp, Log, TEXT("BSPImporter: Found assets in subdirectory: %s"), *AssetSearchDir);
				break;
			}
		}
	}

	FMaterialImporter::SetAssetSearchPath(AssetSearchDir);

	// Set up additional search paths from the game installation directory
	// This finds materials in cstrike/materials/, cstrike/custom/*/materials/, etc.
	FString TargetGame = USourceBridgeSettings::Get()->TargetGame;
	FMaterialImporter::SetupGameSearchPaths(TargetGame);

	// Initialize model importer with same search paths
	FModelImporter::SetAssetSearchPath(AssetSearchDir);
	FModelImporter::SetupGameSearchPaths(TargetGame);

	// Debug texture dump is OFF by default — enable via: FVTFReader::bDebugDumpTextures = true
	// (Set it in the console or code before import if you need to inspect textures)
	FVTFReader::DebugDumpPath = OutputDir / TEXT("Debug_Textures");

	// Step 3: Import sounds from extracted BSP content
	SlowTask.EnterProgressFrame(0.5f, FText::FromString(TEXT("Importing sounds...")));
	{
		int32 SoundCount = FSoundImporter::ImportSoundsFromDirectory(AssetSearchDir);
		if (SoundCount > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("BSPImporter: Imported %d sounds"), SoundCount);
		}
	}

	// Step 4: Import resource files (overviews, configs)
	SlowTask.EnterProgressFrame(0.5f, FText::FromString(TEXT("Importing resources...")));
	{
		USourceResourceManifest* ResourceManifest = USourceResourceManifest::Get();
		if (ResourceManifest)
		{
			int32 ResourceCount = 0;

			// Import overview/radar config files
			FString ResourceDir = AssetSearchDir / TEXT("resource");
			if (FPaths::DirectoryExists(ResourceDir))
			{
				TArray<FString> ResourceFiles;
				IFileManager::Get().FindFilesRecursive(ResourceFiles, *ResourceDir, TEXT("*.*"), true, false);
				for (const FString& ResFile : ResourceFiles)
				{
					FString RelPath = ResFile;
					FPaths::MakePathRelativeTo(RelPath, *(AssetSearchDir + TEXT("/")));
					RelPath.ReplaceInline(TEXT("\\"), TEXT("/"));

					FSourceResourceEntry Entry;
					Entry.SourcePath = RelPath;
					Entry.Origin = ESourceResourceOrigin::Imported;
					Entry.DiskPath = ResFile;
					Entry.LastImported = FDateTime::Now();

					FString Ext = FPaths::GetExtension(ResFile).ToLower();
					if (RelPath.Contains(TEXT("overviews")))
					{
						Entry.ResourceType = (Ext == TEXT("txt"))
							? ESourceResourceType::OverviewConfig
							: ESourceResourceType::Overview;

						// Store text content for config files
						if (Ext == TEXT("txt"))
						{
							FFileHelper::LoadFileToString(Entry.TextContent, *ResFile);
						}
					}

					ResourceManifest->Register(Entry);
					ResourceCount++;
				}
			}

			if (ResourceCount > 0)
			{
				ResourceManifest->SaveManifest();
				UE_LOG(LogTemp, Log, TEXT("BSPImporter: Imported %d resource files"), ResourceCount);
			}
		}
	}

	// Step 5: Import the decompiled VMF (geometry, entities, materials, models)
	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Importing VMF...")));

	FVMFImportSettings ImportSettings = Settings;
	ImportSettings.AssetSearchPath = AssetSearchDir;
	Result = FVMFImporter::ImportFile(VMFPath, World, ImportSettings);

	// Save model manifest after VMF import (models are registered during ResolveModel calls)
	{
		USourceModelManifest* ModelManifest = USourceModelManifest::Get();
		if (ModelManifest && ModelManifest->Num() > 0)
		{
			ModelManifest->SaveManifest();
		}
	}

	return Result;
}

FString FBSPImporter::FindBSPSourceJavaPath()
{
	// Look in plugin Resources/tools/bspsrc/bin/java.exe
	FString PluginToolsPath = FPaths::ProjectPluginsDir() / TEXT("SourceBridge") / TEXT("Resources") / TEXT("tools") / TEXT("bspsrc");
	FString JavaPath = PluginToolsPath / TEXT("bin") / TEXT("java.exe");

	if (FPaths::FileExists(JavaPath))
	{
		return JavaPath;
	}

	// Fallback: try project root Resources
	PluginToolsPath = FPaths::ProjectDir() / TEXT("Resources") / TEXT("tools") / TEXT("bspsrc");
	JavaPath = PluginToolsPath / TEXT("bin") / TEXT("java.exe");

	if (FPaths::FileExists(JavaPath))
	{
		return JavaPath;
	}

	return FString();
}

FString FBSPImporter::DecompileBSP(const FString& BSPPath, const FString& OutputDir, FString& OutError)
{
	FString JavaPath = FPaths::ConvertRelativePathToFull(FindBSPSourceJavaPath());
	if (JavaPath.IsEmpty())
	{
		OutError = TEXT("BSPSource not found. Place it in Resources/tools/bspsrc/");
		return FString();
	}

	// Convert BSP path to absolute for external Java process
	FString AbsBSPPath = FPaths::ConvertRelativePathToFull(BSPPath);

	// Output VMF goes in the import directory
	// BSPSource's -o flag takes a FILE path for single BSP input, not a directory
	FString MapName = FPaths::GetBaseFilename(BSPPath);
	FString VMFPath = OutputDir / MapName + TEXT(".vmf");

	// Build command with --unpack_embedded to extract materials/textures from BSP pakfile
	// -o points to the output VMF file path (BSPSource extracts embedded files relative to it)
	// All paths must be absolute since BSPSource runs as an external process
	FString Args = FString::Printf(
		TEXT("-m info.ata4.bspsrc.app/info.ata4.bspsrc.app.src.BspSourceLauncher --unpack_embedded -o \"%s\" \"%s\""),
		*VMFPath, *AbsBSPPath);

	UE_LOG(LogTemp, Log, TEXT("BSPImporter: Running '%s' %s"), *JavaPath, *Args);

	// Run BSPSource
	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;

	FPlatformProcess::ExecProcess(*JavaPath, *Args, &ReturnCode, &StdOut, &StdErr);

	UE_LOG(LogTemp, Log, TEXT("BSPImporter: BSPSource exit code: %d"), ReturnCode);
	if (!StdOut.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("BSPImporter: stdout: %s"), *StdOut.Left(2000));
	}
	if (!StdErr.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("BSPImporter: stderr: %s"), *StdErr.Left(2000));
	}

	if (ReturnCode != 0)
	{
		OutError = FString::Printf(TEXT("BSPSource exited with code %d. Output: %s %s"),
			ReturnCode, *StdOut, *StdErr);
		return FString();
	}

	// BSPSource sometimes returns exit code 0 even on failure - check stdout for errors
	if (StdOut.Contains(TEXT("ERROR")) && StdOut.Contains(TEXT("Failed")))
	{
		OutError = FString::Printf(TEXT("BSPSource reported error: %s"), *StdOut.Left(500));
		// Don't return yet - the embedded files may have been extracted even if decompile failed
		// We'll check for the VMF below
	}

	// BSPSource with -o <dir> puts the VMF as <dir>/<mapname>.vmf
	// But it might also use _d suffix or the original filename - check both
	if (!FPaths::FileExists(VMFPath))
	{
		// Try with _d suffix (BSPSource sometimes appends this)
		FString AltPath = OutputDir / MapName + TEXT("_d.vmf");
		if (FPaths::FileExists(AltPath))
		{
			VMFPath = AltPath;
		}
		else
		{
			// Search for any .vmf file in the output directory
			TArray<FString> FoundFiles;
			IFileManager::Get().FindFiles(FoundFiles, *(OutputDir / TEXT("*.vmf")), true, false);
			if (FoundFiles.Num() > 0)
			{
				VMFPath = OutputDir / FoundFiles[0];
				UE_LOG(LogTemp, Log, TEXT("BSPImporter: Found VMF at alternate path: %s"), *VMFPath);
			}
			else
			{
				OutError = FString::Printf(TEXT("BSPSource ran but no VMF found in: %s"), *OutputDir);
				return FString();
			}
		}
	}

	// Log what was extracted
	TArray<FString> ExtractedVMTs;
	IFileManager::Get().FindFilesRecursive(ExtractedVMTs, *OutputDir, TEXT("*.vmt"), true, false);
	TArray<FString> ExtractedVTFs;
	IFileManager::Get().FindFilesRecursive(ExtractedVTFs, *OutputDir, TEXT("*.vtf"), true, false);
	TArray<FString> ExtractedMDLs;
	IFileManager::Get().FindFilesRecursive(ExtractedMDLs, *OutputDir, TEXT("*.mdl"), true, false);

	UE_LOG(LogTemp, Log, TEXT("BSPImporter: Extracted %d VMT, %d VTF, %d MDL files to %s"),
		ExtractedVMTs.Num(), ExtractedVTFs.Num(), ExtractedMDLs.Num(), *OutputDir);

	return VMFPath;
}
