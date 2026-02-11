#include "UI/SourceBridgeSettings.h"
#include "Misc/Paths.h"

USourceBridgeSettings::USourceBridgeSettings()
{
	OutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("SourceBridge");
}

USourceBridgeSettings* USourceBridgeSettings::Get()
{
	static USourceBridgeSettings* Settings = nullptr;
	if (!Settings)
	{
		Settings = NewObject<USourceBridgeSettings>(GetTransientPackage(), TEXT("SourceBridgeSettings"));
		Settings->AddToRoot(); // Prevent GC
		Settings->LoadConfig();
	}
	return Settings;
}
