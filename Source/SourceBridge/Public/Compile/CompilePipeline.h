#pragma once

#include "CoreMinimal.h"

/**
 * Settings for a Source engine map compile.
 */
struct SOURCEBRIDGE_API FCompileSettings
{
	/** Path to the game directory (e.g., C:\...\cstrike) */
	FString GameDir;

	/** Path to the Source SDK bin directory containing compile tools */
	FString ToolsDir;

	/** Path to the VMF file to compile */
	FString VMFPath;

	/** Use -fast flag for quick test compiles (low quality vis/rad) */
	bool bFastCompile = true;

	/** Use -final flag for high quality lighting (slow) */
	bool bFinalCompile = false;

	/** Copy resulting BSP to game's maps/ folder */
	bool bCopyToGame = true;
};

/**
 * Settings for a Source engine model compile.
 */
struct SOURCEBRIDGE_API FModelCompileSettings
{
	/** Path to the game directory */
	FString GameDir;

	/** Path to the Source SDK bin directory */
	FString ToolsDir;

	/** Path to the QC file to compile */
	FString QCPath;

	/** Copy resulting MDL files to game's models/ folder */
	bool bCopyToGame = true;
};

/**
 * Result of a compile step or full pipeline.
 */
struct SOURCEBRIDGE_API FCompileResult
{
	bool bSuccess = false;
	FString Output;
	FString ErrorMessage;
	double ElapsedSeconds = 0.0;
};

/**
 * Headless Source engine compile pipeline.
 * Runs vbsp -> vvis -> vrad via CLI to produce a playable .bsp from a .vmf.
 *
 * All tools follow the pattern: tool.exe -game "gamedir" input_file
 */
class SOURCEBRIDGE_API FCompilePipeline
{
public:
	/**
	 * Run the full compile pipeline: vbsp -> vvis -> vrad.
	 * Optionally copies BSP to game's maps/ folder.
	 */
	static FCompileResult CompileMap(const FCompileSettings& Settings);

	/**
	 * Run studiomdl to compile a model from QC file.
	 * Optionally copies output files to game's models/ folder.
	 */
	static FCompileResult CompileModel(const FModelCompileSettings& Settings);

	/**
	 * Pack custom content (models, materials) into a compiled BSP using bspzip.
	 *
	 * @param BSPPath Path to the compiled .bsp file
	 * @param ToolsDir Path to Source SDK bin directory containing bspzip.exe
	 * @param FileList Map of internal paths (e.g., "models/foo/bar.mdl") to absolute disk paths
	 * @return Compile result
	 */
	static FCompileResult PackCustomContent(
		const FString& BSPPath,
		const FString& ToolsDir,
		const TMap<FString, FString>& FileList);

	/**
	 * Try to auto-detect Source SDK tools in common Steam install paths.
	 * Returns the path to the bin/ directory, or empty string if not found.
	 */
	static FString FindToolsDirectory();

	/**
	 * Try to auto-detect a game directory (e.g., cstrike for CS:S).
	 * Returns the path to the game directory, or empty string.
	 */
	static FString FindGameDirectory(const FString& GameName = TEXT("cstrike"));

private:
	/** Run a single compile tool and capture output. */
	static FCompileResult RunTool(
		const FString& ToolPath,
		const FString& Arguments,
		const FString& ToolName);

	/** Common Steam library paths to search. */
	static TArray<FString> GetSteamLibraryPaths();
};
