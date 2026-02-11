#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Settings for a full project export (map + materials + models).
 */
struct SOURCEBRIDGE_API FFullExportSettings
{
	/** Target game name (e.g., "cstrike", "tf", "hl2") */
	FString GameName = TEXT("cstrike");

	/** Output directory for exported files */
	FString OutputDir;

	/** Map name for the VMF file */
	FString MapName;

	/** Whether to compile after export */
	bool bCompile = true;

	/** Use fast compile flags */
	bool bFastCompile = true;

	/** Copy results to game directory */
	bool bCopyToGame = true;

	/** Run validation before export */
	bool bValidate = true;
};

/**
 * Result of a full export operation.
 */
struct SOURCEBRIDGE_API FFullExportResult
{
	bool bSuccess = false;
	FString VMFPath;
	FString BSPPath;
	int32 BrushCount = 0;
	int32 EntityCount = 0;
	double ExportSeconds = 0.0;
	double CompileSeconds = 0.0;
	FString ErrorMessage;
	TArray<FString> Warnings;
};

/**
 * One-click full export pipeline.
 *
 * Orchestrates: Validate -> Export VMF -> Compile (vbsp/vvis/vrad) -> Copy to game.
 * Single console command to go from UE scene to playable map.
 */
class SOURCEBRIDGE_API FFullExportPipeline
{
public:
	/**
	 * Run the full export pipeline on the current editor world.
	 */
	static FFullExportResult Run(UWorld* World, const FFullExportSettings& Settings);
};
