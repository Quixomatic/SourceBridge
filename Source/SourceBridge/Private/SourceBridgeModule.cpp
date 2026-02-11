#include "SourceBridgeModule.h"
#include "VMF/VMFExporter.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"

#define LOCTEXT_NAMESPACE "FSourceBridgeModule"

void FSourceBridgeModule::StartupModule()
{
	ExportTestBoxRoomCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.ExportTestBoxRoom"),
		TEXT("Export a test box room to VMF. Usage: SourceBridge.ExportTestBoxRoom <filepath>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			FString OutputPath;
			if (Args.Num() > 0)
			{
				OutputPath = Args[0];
			}
			else
			{
				OutputPath = FPaths::ProjectSavedDir() / TEXT("SourceBridge") / TEXT("test_boxroom.vmf");
			}

			FString VMFContent = FVMFExporter::GenerateBoxRoom();

			FString Directory = FPaths::GetPath(OutputPath);
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			PlatformFile.CreateDirectoryTree(*Directory);

			if (FFileHelper::SaveStringToFile(VMFContent, *OutputPath))
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Box room VMF exported to: %s"), *OutputPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Failed to write VMF to: %s"), *OutputPath);
			}
		})
	);

	UE_LOG(LogTemp, Log, TEXT("SourceBridge plugin loaded. Use 'SourceBridge.ExportTestBoxRoom <path>' to test VMF export."));
}

void FSourceBridgeModule::ShutdownModule()
{
	ExportTestBoxRoomCommand.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSourceBridgeModule, SourceBridge)
