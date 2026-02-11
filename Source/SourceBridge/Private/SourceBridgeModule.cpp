#include "SourceBridgeModule.h"
#include "VMF/VMFExporter.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Engine/World.h"
#include "Editor.h"

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

	ExportSceneCommand = MakeShared<FAutoConsoleCommand>(
		TEXT("SourceBridge.ExportScene"),
		TEXT("Export current level brushes to VMF. Usage: SourceBridge.ExportScene <filepath>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World)
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: No editor world available."));
				return;
			}

			FString OutputPath;
			if (Args.Num() > 0)
			{
				OutputPath = Args[0];
			}
			else
			{
				OutputPath = FPaths::ProjectSavedDir() / TEXT("SourceBridge") / TEXT("export.vmf");
			}

			FString VMFContent = FVMFExporter::ExportScene(World);

			if (VMFContent.IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Export produced empty VMF."));
				return;
			}

			FString Directory = FPaths::GetPath(OutputPath);
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			PlatformFile.CreateDirectoryTree(*Directory);

			if (FFileHelper::SaveStringToFile(VMFContent, *OutputPath))
			{
				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Scene exported to: %s"), *OutputPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("SourceBridge: Failed to write VMF to: %s"), *OutputPath);
			}
		})
	);

	UE_LOG(LogTemp, Log, TEXT("SourceBridge plugin loaded. Commands: SourceBridge.ExportScene, SourceBridge.ExportTestBoxRoom"));
}

void FSourceBridgeModule::ShutdownModule()
{
	ExportTestBoxRoomCommand.Reset();
	ExportSceneCommand.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSourceBridgeModule, SourceBridge)
