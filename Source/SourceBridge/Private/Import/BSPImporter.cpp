#include "Import/BSPImporter.h"
#include "Import/VMFImporter.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"

FVMFImportResult FBSPImporter::ImportFile(const FString& BSPPath, UWorld* World,
	const FVMFImportSettings& Settings)
{
	FVMFImportResult Result;

	if (!FPaths::FileExists(BSPPath))
	{
		Result.Warnings.Add(FString::Printf(TEXT("BSP file not found: %s"), *BSPPath));
		return Result;
	}

	// Decompile BSP → VMF
	FString DecompileError;
	FString VMFPath = DecompileBSP(BSPPath, DecompileError);

	if (VMFPath.IsEmpty())
	{
		Result.Warnings.Add(FString::Printf(TEXT("BSPSource decompile failed: %s"), *DecompileError));
		return Result;
	}

	UE_LOG(LogTemp, Log, TEXT("BSPImporter: Decompiled '%s' → '%s'"), *BSPPath, *VMFPath);

	// Import the decompiled VMF
	Result = FVMFImporter::ImportFile(VMFPath, World, Settings);

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

FString FBSPImporter::DecompileBSP(const FString& BSPPath, FString& OutError)
{
	FString JavaPath = FindBSPSourceJavaPath();
	if (JavaPath.IsEmpty())
	{
		OutError = TEXT("BSPSource not found. Place it in Resources/tools/bspsrc/");
		return FString();
	}

	// Output VMF goes next to the BSP with _decompiled suffix
	FString VMFPath = FPaths::GetPath(BSPPath) / FPaths::GetBaseFilename(BSPPath) + TEXT("_decompiled.vmf");

	// Build command: java.exe -m info.ata4.bspsrc.app/info.ata4.bspsrc.app.src.BspSourceLauncher -o <vmf> <bsp>
	FString Args = FString::Printf(
		TEXT("-m info.ata4.bspsrc.app/info.ata4.bspsrc.app.src.BspSourceLauncher -o \"%s\" \"%s\""),
		*VMFPath, *BSPPath);

	UE_LOG(LogTemp, Log, TEXT("BSPImporter: Running '%s' %s"), *JavaPath, *Args);

	// Run BSPSource
	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;

	FPlatformProcess::ExecProcess(*JavaPath, *Args, &ReturnCode, &StdOut, &StdErr);

	if (ReturnCode != 0)
	{
		OutError = FString::Printf(TEXT("BSPSource exited with code %d. Output: %s %s"),
			ReturnCode, *StdOut, *StdErr);
		return FString();
	}

	if (!FPaths::FileExists(VMFPath))
	{
		OutError = FString::Printf(TEXT("BSPSource ran but output VMF not found at: %s"), *VMFPath);
		return FString();
	}

	return VMFPath;
}
