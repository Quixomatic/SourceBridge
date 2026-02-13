#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/**
 * Options for VTF conversion via vtfcmd.exe.
 */
struct FVTFConvertOptions
{
	/** Pixel format: "DXT1" (opaque, smaller), "DXT5" (has alpha), "BGRA8888" (uncompressed) */
	FString Format = TEXT("DXT5");

	/** Generate mipmaps (recommended for quality at distance) */
	bool bGenerateMipmaps = true;

	/** Input is a normal map (enables normal map processing in vtfcmd) */
	bool bNormalMap = false;
};

/**
 * Result of a texture export operation.
 */
struct FTextureExportResult
{
	bool bSuccess = false;
	FString TGAPath;    // Path to exported TGA file
	FString VTFPath;    // Path to compiled VTF file (if vtfcmd was run)
	FString VMTPath;    // Path to generated VMT file
	FString ErrorMessage;
};

/**
 * Exports UE textures to Source engine VTF format.
 *
 * Pipeline: UE Texture2D -> TGA file -> vtfcmd.exe -> .vtf
 * Also generates accompanying .vmt material files.
 */
class SOURCEBRIDGE_API FTextureExporter
{
public:
	/**
	 * Export a UTexture2D to TGA file.
	 * Returns the path to the written TGA.
	 */
	static bool ExportTextureToTGA(UTexture2D* Texture, const FString& OutputPath);

	/**
	 * Run vtfcmd.exe to convert a TGA file to VTF.
	 * @param TGAPath Path to the input TGA file
	 * @param OutputDir Directory for the output VTF file
	 * @param VTFCmdPath Path to vtfcmd.exe (auto-detected if empty)
	 * @return Path to the output VTF file, or empty on failure
	 */
	static FString ConvertTGAToVTF(
		const FString& TGAPath,
		const FString& OutputDir,
		const FString& VTFCmdPath = FString());

	/**
	 * Run vtfcmd.exe with explicit format/mipmap/normal options.
	 */
	static FString ConvertTGAToVTF(
		const FString& TGAPath,
		const FString& OutputDir,
		const FVTFConvertOptions& Options,
		const FString& VTFCmdPath = FString());

	/**
	 * Full pipeline: export texture, convert to VTF, generate VMT.
	 * @param Texture UE texture to export
	 * @param OutputDir Base output directory (materials will be placed in subdirs)
	 * @param MaterialPath Source material path (e.g., "custom/mymap/floor")
	 * @param SurfaceProp Surface property for VMT (e.g., "concrete")
	 * @param VTFCmdPath Path to vtfcmd.exe
	 */
	static FTextureExportResult ExportFullPipeline(
		UTexture2D* Texture,
		const FString& OutputDir,
		const FString& MaterialPath,
		const FString& SurfaceProp = TEXT("default"),
		const FString& VTFCmdPath = FString());

	/**
	 * Try to find vtfcmd.exe in common locations.
	 */
	static FString FindVTFCmd();

	/**
	 * Copy exported VTF+VMT to the game's materials folder.
	 */
	static bool CopyToGameMaterials(
		const FString& VTFPath,
		const FString& VMTPath,
		const FString& GameDir,
		const FString& MaterialPath);
};
