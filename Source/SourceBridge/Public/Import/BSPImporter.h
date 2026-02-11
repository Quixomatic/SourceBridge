#pragma once

#include "CoreMinimal.h"
#include "Import/VMFImporter.h"

/**
 * Imports Source BSP files by decompiling them with BSPSource, then
 * importing the resulting VMF via FVMFImporter.
 */
class SOURCEBRIDGE_API FBSPImporter
{
public:
	/** Import a BSP file into the given world. Decompiles via BSPSource first. */
	static FVMFImportResult ImportFile(const FString& BSPPath, UWorld* World,
		const FVMFImportSettings& Settings = FVMFImportSettings());

	/** Find the path to the bundled BSPSource java executable. */
	static FString FindBSPSourceJavaPath();

	/** Decompile a BSP to VMF using BSPSource. Returns the output VMF path. */
	static FString DecompileBSP(const FString& BSPPath, FString& OutError);
};
