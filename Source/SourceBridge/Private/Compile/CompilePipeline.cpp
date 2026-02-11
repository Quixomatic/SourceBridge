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
		FString FastFlag = Settings.bFastCompile ? TEXT("-fast ") : TEXT("");
		FString Args = FString::Printf(TEXT("%s-game \"%s\" \"%s\""),
			*FastFlag, *Settings.GameDir, *BSPPath);

		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Running VRAD%s..."),
			Settings.bFastCompile ? TEXT(" (fast)") : TEXT(""));
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

	// TODO: Parse libraryfolders.vdf for additional Steam library locations

	return Paths;
}
