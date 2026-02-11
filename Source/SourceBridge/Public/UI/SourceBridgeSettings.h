#pragma once

#include "CoreMinimal.h"
#include "SourceBridgeSettings.generated.h"

/**
 * How materials should be exported.
 */
UENUM()
enum class EMaterialExportMode : uint8
{
	/** Only use the manual material mapping table. */
	ManualMapping UMETA(DisplayName = "Manual Mapping"),

	/** Auto-export UE textures to VTF and generate VMT files. */
	AutoExport UMETA(DisplayName = "Auto Export"),

	/** Auto-export with manual overrides taking priority. */
	AutoWithOverrides UMETA(DisplayName = "Auto + Overrides")
};

/**
 * Plugin settings stored per-project.
 * Configurable via the Export Settings panel.
 */
UCLASS(config=EditorPerProjectUserSettings)
class SOURCEBRIDGE_API USourceBridgeSettings : public UObject
{
	GENERATED_BODY()

public:
	USourceBridgeSettings();

	/** Target game (cstrike, tf, garrysmod, hl2, hl2mp) */
	UPROPERTY(Config, EditAnywhere, Category = "Export")
	FString TargetGame = TEXT("cstrike");

	/** Output directory for exported files */
	UPROPERTY(Config, EditAnywhere, Category = "Export")
	FDirectoryPath OutputDirectory;

	/** Override map name (empty = use level name) */
	UPROPERTY(Config, EditAnywhere, Category = "Export")
	FString MapName;

	/** Path to Source SDK bin directory (auto-detected if empty) */
	UPROPERTY(Config, EditAnywhere, Category = "Tools")
	FDirectoryPath ToolsDirectory;

	/** Path to vtfcmd.exe (auto-detected if empty) */
	UPROPERTY(Config, EditAnywhere, Category = "Tools")
	FFilePath VTFCmdPath;

	/** Run compile after export */
	UPROPERTY(Config, EditAnywhere, Category = "Compile")
	bool bCompileAfterExport = true;

	/** Use fast compile (low quality, quick iteration) */
	UPROPERTY(Config, EditAnywhere, Category = "Compile")
	bool bFastCompile = true;

	/** Copy compiled BSP to game maps folder */
	UPROPERTY(Config, EditAnywhere, Category = "Compile")
	bool bCopyToGame = true;

	/** Material export mode */
	UPROPERTY(Config, EditAnywhere, Category = "Materials")
	EMaterialExportMode MaterialExportMode = EMaterialExportMode::AutoWithOverrides;

	/** Run validation before export */
	UPROPERTY(Config, EditAnywhere, Category = "Validation")
	bool bValidateBeforeExport = true;

	/** Coordinate scale override (default 0.525) */
	UPROPERTY(Config, EditAnywhere, Category = "Advanced", meta = (ClampMin = "0.01", ClampMax = "10.0"))
	float ScaleOverride = 0.525f;

	/** 2D skybox texture name */
	UPROPERTY(Config, EditAnywhere, Category = "Skybox")
	FString SkyName = TEXT("sky_day01_01");

	/** Get the singleton settings instance. */
	static USourceBridgeSettings* Get();
};
