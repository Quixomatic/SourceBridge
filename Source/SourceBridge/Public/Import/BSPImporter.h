#pragma once

#include "CoreMinimal.h"
#include "Import/VMFImporter.h"

/**
 * Imports Source BSP files by decompiling them with BSPSource, then
 * importing the resulting VMF via FVMFImporter.
 *
 * Output goes to Saved/SourceBridge/Import/<mapname>/ including:
 * - Decompiled VMF file
 * - Extracted materials (.vmt) and textures (.vtf) from BSP pakfile
 */
class SOURCEBRIDGE_API FBSPImporter
{
public:
	/** Import a BSP file into the given world. Decompiles via BSPSource first. */
	static FVMFImportResult ImportFile(const FString& BSPPath, UWorld* World,
		const FVMFImportSettings& Settings = FVMFImportSettings());

	/** Find the path to the bundled BSPSource java executable. */
	static FString FindBSPSourceJavaPath();

	/**
	 * Decompile a BSP to VMF using BSPSource with asset extraction.
	 * @param BSPPath Path to the input BSP file
	 * @param OutputDir Directory for decompiled VMF and extracted assets
	 * @param OutError Error message if decompilation fails
	 * @return Path to the decompiled VMF file, or empty on failure
	 */
	static FString DecompileBSP(const FString& BSPPath, const FString& OutputDir, FString& OutError);
};
