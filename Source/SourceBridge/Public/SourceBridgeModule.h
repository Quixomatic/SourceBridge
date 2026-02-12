#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Entities/FGDParser.h"

class FSourceBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the loaded FGD database (empty if not loaded). */
	static const FFGDDatabase& GetFGDDatabase();

	/** Load FGD from a file path. */
	static void LoadFGD(const FString& FilePath);

private:
	static FFGDDatabase FGDDatabase;

	TSharedPtr<class FAutoConsoleCommand> ExportTestBoxRoomCommand;
	TSharedPtr<class FAutoConsoleCommand> ExportSceneCommand;
	TSharedPtr<class FAutoConsoleCommand> CompileMapCommand;
	TSharedPtr<class FAutoConsoleCommand> ExportModelCommand;
	TSharedPtr<class FAutoConsoleCommand> FullExportCommand;
	TSharedPtr<class FAutoConsoleCommand> ValidateCommand;
	TSharedPtr<class FAutoConsoleCommand> LoadFGDCommand;
	TSharedPtr<class FAutoConsoleCommand> ListEntitiesCommand;
	TSharedPtr<class FAutoConsoleCommand> AnalyzeVisCommand;
	TSharedPtr<class FAutoConsoleCommand> ImportVMFCommand;
	TSharedPtr<class FAutoConsoleCommand> ImportBSPCommand;
	TSharedPtr<class FAutoConsoleCommand> PlayTestCommand;
};
