#include "VMF/SkyboxExporter.h"
#include "VMF/VMFExporter.h"
#include "Utilities/SourceCoord.h"
#include "Engine/World.h"
#include "EngineUtils.h"

TArray<FVMFKeyValues> FSkyboxExporter::GenerateSkyboxShell(
	int32& SolidIdCounter,
	int32& SideIdCounter,
	float RoomSize,
	float WallThickness)
{
	TArray<FVMFKeyValues> Brushes;

	float Half = RoomSize / 2.0f;
	float T = WallThickness;
	FString SkyMat = TEXT("TOOLS/TOOLSSKYBOX");

	// Top
	SolidIdCounter++;
	Brushes.Add(FVMFExporter::BuildAABBSolid(SolidIdCounter, SideIdCounter,
		FVector(-Half, -Half, Half), FVector(Half, Half, Half + T), SkyMat));

	// Bottom
	SolidIdCounter++;
	Brushes.Add(FVMFExporter::BuildAABBSolid(SolidIdCounter, SideIdCounter,
		FVector(-Half, -Half, -Half - T), FVector(Half, Half, -Half), SkyMat));

	// North (+X)
	SolidIdCounter++;
	Brushes.Add(FVMFExporter::BuildAABBSolid(SolidIdCounter, SideIdCounter,
		FVector(Half, -Half, -Half), FVector(Half + T, Half, Half), SkyMat));

	// South (-X)
	SolidIdCounter++;
	Brushes.Add(FVMFExporter::BuildAABBSolid(SolidIdCounter, SideIdCounter,
		FVector(-Half - T, -Half, -Half), FVector(-Half, Half, Half), SkyMat));

	// East (+Y)
	SolidIdCounter++;
	Brushes.Add(FVMFExporter::BuildAABBSolid(SolidIdCounter, SideIdCounter,
		FVector(-Half, Half, -Half), FVector(Half, Half + T, Half), SkyMat));

	// West (-Y)
	SolidIdCounter++;
	Brushes.Add(FVMFExporter::BuildAABBSolid(SolidIdCounter, SideIdCounter,
		FVector(-Half, -Half - T, -Half), FVector(Half, -Half, Half), SkyMat));

	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Generated skybox shell (%g x %g x %g Source units)"),
		RoomSize, RoomSize, RoomSize);

	return Brushes;
}

FVMFKeyValues FSkyboxExporter::GenerateSkyCamera(
	int32 EntityId,
	const FVector& Position,
	float Scale)
{
	FVMFKeyValues Entity;
	Entity.ClassName = TEXT("entity");
	Entity.Properties.Add(TPair<FString, FString>(TEXT("id"), FString::FromInt(EntityId)));
	Entity.Properties.Add(TPair<FString, FString>(TEXT("classname"), TEXT("sky_camera")));
	Entity.Properties.Add(TPair<FString, FString>(TEXT("origin"),
		FString::Printf(TEXT("%g %g %g"), Position.X, Position.Y, Position.Z)));
	Entity.Properties.Add(TPair<FString, FString>(TEXT("scale"), FString::Printf(TEXT("%g"), Scale)));

	return Entity;
}

FSkyboxData FSkyboxExporter::ExportSkybox(
	UWorld* World,
	int32& EntityIdCounter,
	int32& SolidIdCounter,
	int32& SideIdCounter,
	const FSkyboxSettings& Settings)
{
	FSkyboxData Data;
	Data.SkyName = Settings.SkyName;

	if (!World) return Data;

	// Look for actors tagged with "sky_camera" to place the sky camera entity
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		for (const FName& Tag : Actor->Tags)
		{
			if (Tag.ToString().Equals(TEXT("sky_camera"), ESearchCase::IgnoreCase))
			{
				FVector SrcPos = FSourceCoord::UEToSource(Actor->GetActorLocation());
				EntityIdCounter++;
				Data.SkyCameraEntity = GenerateSkyCamera(
					EntityIdCounter, SrcPos, 1.0f / Settings.SkyboxScale);
				Data.bHasSkyCamera = true;

				UE_LOG(LogTemp, Log, TEXT("SourceBridge: Found sky_camera actor at %s"),
					*Actor->GetActorLocation().ToString());
				break;
			}

			// Also check for "skyname:" tag to override skyname
			FString TagStr = Tag.ToString();
			if (TagStr.StartsWith(TEXT("skyname:"), ESearchCase::IgnoreCase))
			{
				Data.SkyName = TagStr.Mid(8).TrimStartAndEnd();
			}
		}
	}

	// Generate skybox shell brushes if requested
	if (Settings.bGenerate3DSkybox || Data.bHasSkyCamera)
	{
		Data.SkyboxBrushes = GenerateSkyboxShell(
			SolidIdCounter, SideIdCounter,
			Settings.SkyboxRoomSize,
			16.0f);
	}

	return Data;
}
