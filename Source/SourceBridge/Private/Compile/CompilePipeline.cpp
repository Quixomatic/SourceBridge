#include "Compile/CompilePipeline.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FCompileResult FCompilePipeline::CompileMap(const FCompileSettings& Settings)
{
	FCompileResult FinalResult;
	double StartTime = FPlatformTime::Seconds();

	if (Settings.ToolsDir.IsEmpty())
	{
		FinalResult.ErrorMessage = TEXT("Tools directory not set. Use FindToolsDirectory() or set manually.");
		return FinalResult;
	}

	if (Settings.GameDir.IsEmpty())
	{
		FinalResult.ErrorMessage = TEXT("Game directory not set. Use FindGameDirectory() or set manually.");
		return FinalResult;
	}

	if (Settings.VMFPath.IsEmpty() || !FPaths::FileExists(Settings.VMFPath))
	{
		FinalResult.ErrorMessage = FString::Printf(TEXT("VMF file not found: %s"), *Settings.VMFPath);
		return FinalResult;
	}

	FString MapName = FPaths::GetBaseFilename(Settings.VMFPath);
	FString MapDir = FPaths::GetPath(Settings.VMFPath);
	FString BSPPath = MapDir / MapName + TEXT(".bsp");

	// ---- VBSP (geometry) ----
	{
		FString VBSPPath = Settings.ToolsDir / TEXT("vbsp.exe");
		FString Args = FString::Printf(TEXT("-game \"%s\" \"%s\""),
			*Settings.GameDir, *Settings.VMFPath);

		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Running VBSP..."));
		FCompileResult VBSPResult = RunTool(VBSPPath, Args, TEXT("VBSP"));
		FinalResult.Output += VBSPResult.Output + TEXT("\n");

		if (!VBSPResult.bSuccess)
		{
			FinalResult.ErrorMessage = TEXT("VBSP failed: ") + VBSPResult.ErrorMessage;
			FinalResult.ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
			return FinalResult;
		}
	}

	// ---- VVIS (visibility) ----
	{
		FString VVISPath = Settings.ToolsDir / TEXT("vvis.exe");
		FString FastFlag = Settings.bFastCompile ? TEXT("-fast ") : TEXT("");
		FString Args = FString::Printf(TEXT("%s-game \"%s\" \"%s\""),
			*FastFlag, *Settings.GameDir, *BSPPath);

		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Running VVIS%s..."),
			Settings.bFastCompile ? TEXT(" (fast)") : TEXT(""));
		FCompileResult VVISResult = RunTool(VVISPath, Args, TEXT("VVIS"));
		FinalResult.Output += VVISResult.Output + TEXT("\n");

		if (!VVISResult.bSuccess)
		{
			FinalResult.ErrorMessage = TEXT("VVIS failed: ") + VVISResult.ErrorMessage;
			FinalResult.ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
			return FinalResult;
		}
	}

	// ---- VRAD (lighting) ----
	{
		FString VRADPath = Settings.ToolsDir / TEXT("vrad.exe");
		FString QualityFlag;
		if (Settings.bFinalCompile)
		{
			QualityFlag = TEXT("-final ");
		}
		else if (Settings.bFastCompile)
		{
			QualityFlag = TEXT("-fast ");
		}
		FString Args = FString::Printf(TEXT("%s-game \"%s\" \"%s\""),
			*QualityFlag, *Settings.GameDir, *BSPPath);

		FString QualityLabel = Settings.bFinalCompile ? TEXT(" (final)") :
			(Settings.bFastCompile ? TEXT(" (fast)") : TEXT(""));
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Running VRAD%s..."), *QualityLabel);
		FCompileResult VRADResult = RunTool(VRADPath, Args, TEXT("VRAD"));
		FinalResult.Output += VRADResult.Output + TEXT("\n");

		if (!VRADResult.bSuccess)
		{
			FinalResult.ErrorMessage = TEXT("VRAD failed: ") + VRADResult.ErrorMessage;
			FinalResult.ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
			return FinalResult;
		}
	}

	// ---- Copy BSP to game maps folder ----
	if (Settings.bCopyToGame && FPaths::FileExists(BSPPath))
	{
		FString GameMapsDir = Settings.GameDir / TEXT("maps");
		FString DestPath = GameMapsDir / MapName + TEXT(".bsp");

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*GameMapsDir);

		if (PlatformFile.CopyFile(*DestPath, *BSPPath))
		{
			UE_LOG(LogTemp, Log, TEXT("SourceBridge: BSP copied to %s"), *DestPath);
			FinalResult.Output += FString::Printf(TEXT("BSP copied to: %s\n"), *DestPath);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SourceBridge: Failed to copy BSP to %s"), *DestPath);
		}
	}

	FinalResult.bSuccess = true;
	FinalResult.ElapsedSeconds = FPlatformTime::Seconds() - StartTime;

	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Compile completed in %.1f seconds."),
		FinalResult.ElapsedSeconds);

	return FinalResult;
}

FCompileResult FCompilePipeline::CompileModel(const FModelCompileSettings& Settings)
{
	FCompileResult Result;
	double StartTime = FPlatformTime::Seconds();

	if (Settings.ToolsDir.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Tools directory not set.");
		return Result;
	}

	if (Settings.GameDir.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Game directory not set.");
		return Result;
	}

	if (Settings.QCPath.IsEmpty() || !FPaths::FileExists(Settings.QCPath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("QC file not found: %s"), *Settings.QCPath);
		return Result;
	}

	FString StudioMDLPath = Settings.ToolsDir / TEXT("studiomdl.exe");
	FString Args = FString::Printf(TEXT("-nop4 -game \"%s\" \"%s\""),
		*Settings.GameDir, *Settings.QCPath);

	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Running studiomdl..."));
	FCompileResult MDLResult = RunTool(StudioMDLPath, Args, TEXT("studiomdl"));
	Result.Output = MDLResult.Output;

	if (!MDLResult.bSuccess)
	{
		Result.ErrorMessage = TEXT("studiomdl failed: ") + MDLResult.ErrorMessage;
		Result.ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		return Result;
	}

	// studiomdl outputs files to the game's models/ folder automatically
	// based on $modelname in the QC file

	Result.bSuccess = true;
	Result.ElapsedSeconds = FPlatformTime::Seconds() - StartTime;

	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Model compile completed in %.1f seconds."),
		Result.ElapsedSeconds);

	return Result;
}

FCompileResult FCompilePipeline::RunTool(
	const FString& ToolPath,
	const FString& Arguments,
	const FString& ToolName)
{
	FCompileResult Result;

	if (!FPaths::FileExists(ToolPath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("%s not found at: %s"), *ToolName, *ToolPath);
		return Result;
	}

	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;

	// Run the tool and capture output
	bool bLaunched = FPlatformProcess::ExecProcess(
		*ToolPath,
		*Arguments,
		&ReturnCode,
		&StdOut,
		&StdErr);

	Result.Output = StdOut;

	if (!bLaunched)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Failed to launch %s"), *ToolName);
		return Result;
	}

	if (ReturnCode != 0)
	{
		Result.ErrorMessage = FString::Printf(
			TEXT("%s returned error code %d.\n%s"),
			*ToolName, ReturnCode, *StdErr);
		return Result;
	}

	Result.bSuccess = true;
	return Result;
}

FString FCompilePipeline::FindToolsDirectory()
{
	TArray<FString> LibPaths = GetSteamLibraryPaths();

	// Source SDK tools are typically in:
	// <steam>/steamapps/common/Source SDK Base 2013 Singleplayer/bin/
	// or <steam>/steamapps/common/Counter-Strike Source/bin/
	TArray<FString> ToolSubPaths = {
		TEXT("steamapps/common/Source SDK Base 2013 Singleplayer/bin"),
		TEXT("steamapps/common/Source SDK Base 2013 Multiplayer/bin"),
		TEXT("steamapps/common/Counter-Strike Source/bin"),
		TEXT("steamapps/common/Half-Life 2/bin"),
		TEXT("steamapps/common/Source SDK/bin/orangebox/bin"),
	};

	for (const FString& LibPath : LibPaths)
	{
		for (const FString& SubPath : ToolSubPaths)
		{
			FString FullPath = LibPath / SubPath;
			FString VBSPPath = FullPath / TEXT("vbsp.exe");

			if (FPaths::FileExists(VBSPPath))
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Found compile tools at: %s"), *FullPath);
				return FullPath;
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("SourceBridge: Could not auto-detect Source compile tools."));
	return FString();
}

FString FCompilePipeline::FindGameDirectory(const FString& GameName)
{
	TArray<FString> LibPaths = GetSteamLibraryPaths();

	// Map game names to Steam app paths
	TMap<FString, FString> GamePaths;
	GamePaths.Add(TEXT("cstrike"), TEXT("steamapps/common/Counter-Strike Source/cstrike"));
	GamePaths.Add(TEXT("tf"), TEXT("steamapps/common/Team Fortress 2/tf"));
	GamePaths.Add(TEXT("garrysmod"), TEXT("steamapps/common/GarrysMod/garrysmod"));
	GamePaths.Add(TEXT("hl2"), TEXT("steamapps/common/Half-Life 2/hl2"));
	GamePaths.Add(TEXT("hl2mp"), TEXT("steamapps/common/Half-Life 2 Deathmatch/hl2mp"));

	const FString* SubPath = GamePaths.Find(GameName);
	if (!SubPath)
	{
		UE_LOG(LogTemp, Warning, TEXT("SourceBridge: Unknown game name '%s'"), *GameName);
		return FString();
	}

	for (const FString& LibPath : LibPaths)
	{
		FString FullPath = LibPath / *SubPath;
		if (FPaths::DirectoryExists(FullPath))
		{
			UE_LOG(LogTemp, Log, TEXT("SourceBridge: Found game directory at: %s"), *FullPath);
			return FullPath;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("SourceBridge: Could not find game directory for '%s'"), *GameName);
	return FString();
}

TArray<FString> FCompilePipeline::GetSteamLibraryPaths()
{
	TArray<FString> Paths;

	// Default Steam install locations on Windows
	Paths.Add(TEXT("C:/Program Files (x86)/Steam"));
	Paths.Add(TEXT("C:/Program Files/Steam"));
	Paths.Add(TEXT("D:/Steam"));
	Paths.Add(TEXT("D:/SteamLibrary"));
	Paths.Add(TEXT("E:/Steam"));
	Paths.Add(TEXT("E:/SteamLibrary"));

	// Parse libraryfolders.vdf for additional Steam library locations
	// The VDF is located at <steam>/steamapps/libraryfolders.vdf
	TArray<FString> VDFSearchPaths = {
		TEXT("C:/Program Files (x86)/Steam/steamapps/libraryfolders.vdf"),
		TEXT("C:/Program Files/Steam/steamapps/libraryfolders.vdf"),
		TEXT("D:/Steam/steamapps/libraryfolders.vdf"),
	};

	for (const FString& VDFPath : VDFSearchPaths)
	{
		if (!FPaths::FileExists(VDFPath))
		{
			continue;
		}

		FString VDFContent;
		if (!FFileHelper::LoadFileToString(VDFContent, *VDFPath))
		{
			continue;
		}

		// Parse "path" keys from the VDF file
		// Format: "path"		"C:\\SteamLibrary"
		int32 SearchStart = 0;
		while (SearchStart < VDFContent.Len())
		{
			int32 PathKeyIdx = VDFContent.Find(TEXT("\"path\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchStart);
			if (PathKeyIdx == INDEX_NONE)
			{
				break;
			}

			// Find the value after "path" - skip to the next quoted string
			int32 ValueStart = VDFContent.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, PathKeyIdx + 6);
			if (ValueStart == INDEX_NONE)
			{
				break;
			}
			ValueStart++; // skip opening quote

			int32 ValueEnd = VDFContent.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
			if (ValueEnd == INDEX_NONE)
			{
				break;
			}

			FString LibPath = VDFContent.Mid(ValueStart, ValueEnd - ValueStart);
			// Unescape backslashes from VDF format
			LibPath = LibPath.Replace(TEXT("\\\\"), TEXT("/"));
			LibPath = LibPath.Replace(TEXT("\\"), TEXT("/"));

			if (!LibPath.IsEmpty() && !Paths.Contains(LibPath))
			{
				Paths.Add(LibPath);
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Found Steam library path from VDF: %s"), *LibPath);
			}

			SearchStart = ValueEnd + 1;
		}

		// Only need to parse one VDF file
		break;
	}

	return Paths;
}
