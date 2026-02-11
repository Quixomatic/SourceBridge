#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FSourceBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedPtr<class FAutoConsoleCommand> ExportTestBoxRoomCommand;
	TSharedPtr<class FAutoConsoleCommand> ExportSceneCommand;
	TSharedPtr<class FAutoConsoleCommand> CompileMapCommand;
	TSharedPtr<class FAutoConsoleCommand> ExportModelCommand;
};
