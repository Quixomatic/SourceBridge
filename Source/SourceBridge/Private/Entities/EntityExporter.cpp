#include "Entities/EntityExporter.h"
#include "Utilities/SourceCoord.h"
#include "Engine/World.h"
#include "Engine/TriggerBox.h"
#include "Engine/TriggerVolume.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"

// ---- FSourceEntity ----

void FSourceEntity::AddKeyValue(const FString& Key, const FString& Value)
{
	KeyValues.Emplace(Key, Value);
}

void FSourceEntity::AddKeyValue(const FString& Key, int32 Value)
{
	KeyValues.Emplace(Key, FString::FromInt(Value));
}

void FSourceEntity::AddKeyValue(const FString& Key, float Value)
{
	if (FMath::IsNearlyEqual(Value, FMath::RoundToFloat(Value)))
	{
		KeyValues.Emplace(Key, FString::FromInt(FMath::RoundToInt(Value)));
	}
	else
	{
		KeyValues.Emplace(Key, FString::SanitizeFloat(Value));
	}
}

// ---- FEntityExporter ----

FEntityExportResult FEntityExporter::ExportEntities(UWorld* World)
{
	FEntityExportResult Result;

	if (!World)
	{
		return Result;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;

		if (TryExportPlayerStart(Actor, Result))
		{
			continue;
		}

		if (TryExportLight(Actor, Result))
		{
			continue;
		}

		if (TryExportTriggerVolume(Actor, Result))
		{
			continue;
		}
	}

	return Result;
}

FVMFKeyValues FEntityExporter::EntityToVMF(const FSourceEntity& Entity, int32 EntityId)
{
	FVMFKeyValues Node(TEXT("entity"));
	Node.AddProperty(TEXT("id"), EntityId);
	Node.AddProperty(TEXT("classname"), Entity.ClassName);

	// Targetname (if set)
	if (!Entity.TargetName.IsEmpty())
	{
		Node.AddProperty(TEXT("targetname"), Entity.TargetName);
	}

	// Origin in Source coordinates
	FVector SourceOrigin = FSourceCoord::UEToSource(Entity.Origin);
	Node.AddProperty(TEXT("origin"), FSourceCoord::FormatVector(SourceOrigin));

	// Angles
	FString AnglesStr = FSourceCoord::UERotationToSourceAngles(Entity.Angles);
	Node.AddProperty(TEXT("angles"), AnglesStr);

	// Additional key-values
	for (const auto& KV : Entity.KeyValues)
	{
		Node.AddProperty(KV.Key, KV.Value);
	}

	// Entity I/O connections
	if (Entity.Connections.Num() > 0)
	{
		FVMFKeyValues& ConnBlock = Node.AddChild(TEXT("connections"));
		for (const FEntityIOConnection& Conn : Entity.Connections)
		{
			ConnBlock.AddProperty(Conn.OutputName, Conn.FormatValue());
		}
	}

	return Node;
}

void FEntityExporter::ParseActorTags(AActor* Actor, FSourceEntity& Entity)
{
	for (const FName& Tag : Actor->Tags)
	{
		FString TagStr = Tag.ToString();

		// I/O connections: "io:OutputName:target,input,param,delay,refire"
		FEntityIOConnection IOConn;
		if (FEntityIOConnection::ParseFromTag(TagStr, IOConn))
		{
			Entity.Connections.Add(MoveTemp(IOConn));
			continue;
		}

		// Targetname: "targetname:my_entity_name"
		if (TagStr.StartsWith(TEXT("targetname:"), ESearchCase::IgnoreCase))
		{
			Entity.TargetName = TagStr.Mid(11);
			continue;
		}

		// Source classname override: "classname:trigger_once"
		if (TagStr.StartsWith(TEXT("classname:"), ESearchCase::IgnoreCase))
		{
			Entity.ClassName = TagStr.Mid(10);
			continue;
		}

		// Arbitrary key-values: "kv:key:value"
		if (TagStr.StartsWith(TEXT("kv:"), ESearchCase::IgnoreCase))
		{
			FString Remainder = TagStr.Mid(3);
			int32 ColonIdx;
			if (Remainder.FindChar(TEXT(':'), ColonIdx))
			{
				FString Key = Remainder.Left(ColonIdx);
				FString Value = Remainder.Mid(ColonIdx + 1);
				Entity.AddKeyValue(Key, Value);
			}
			continue;
		}
	}
}

bool FEntityExporter::TryExportPlayerStart(AActor* Actor, FEntityExportResult& Result)
{
	APlayerStart* PlayerStart = Cast<APlayerStart>(Actor);
	if (!PlayerStart)
	{
		return false;
	}

	FString Tag = PlayerStart->PlayerStartTag.ToString();

	FSourceEntity Entity;
	Entity.Origin = PlayerStart->GetActorLocation();
	Entity.Angles = PlayerStart->GetActorRotation();

	if (Tag.Equals(TEXT("CT"), ESearchCase::IgnoreCase) ||
		Tag.Equals(TEXT("CounterTerrorist"), ESearchCase::IgnoreCase))
	{
		Entity.ClassName = TEXT("info_player_counterterrorist");
	}
	else if (Tag.Equals(TEXT("T"), ESearchCase::IgnoreCase) ||
		Tag.Equals(TEXT("Terrorist"), ESearchCase::IgnoreCase))
	{
		Entity.ClassName = TEXT("info_player_terrorist");
	}
	else
	{
		int32 TCount = 0, CTCount = 0;
		for (const FSourceEntity& E : Result.Entities)
		{
			if (E.ClassName == TEXT("info_player_terrorist")) TCount++;
			if (E.ClassName == TEXT("info_player_counterterrorist")) CTCount++;
		}

		Entity.ClassName = (TCount <= CTCount)
			? TEXT("info_player_terrorist")
			: TEXT("info_player_counterterrorist");
	}

	// Parse any additional tags (targetname, kv, io)
	ParseActorTags(PlayerStart, Entity);

	Result.Entities.Add(MoveTemp(Entity));
	return true;
}

bool FEntityExporter::TryExportLight(AActor* Actor, FEntityExportResult& Result)
{
	// Point Light
	APointLight* PointLight = Cast<APointLight>(Actor);
	if (PointLight)
	{
		UPointLightComponent* Comp = PointLight->PointLightComponent;
		if (!Comp)
		{
			return true;
		}

		FSourceEntity Entity;
		Entity.ClassName = TEXT("light");
		Entity.Origin = PointLight->GetActorLocation();
		Entity.Angles = PointLight->GetActorRotation();

		FLinearColor Color = Comp->GetLightColor();
		float Intensity = Comp->Intensity;
		float SourceBrightness = FMath::Clamp(Intensity * 0.5f, 1.0f, 10000.0f);

		Entity.AddKeyValue(TEXT("_light"), FString::Printf(TEXT("%d %d %d %d"),
			FMath::RoundToInt(Color.R * 255),
			FMath::RoundToInt(Color.G * 255),
			FMath::RoundToInt(Color.B * 255),
			FMath::RoundToInt(SourceBrightness)));

		Entity.AddKeyValue(TEXT("_constant_attn"), TEXT("0"));
		Entity.AddKeyValue(TEXT("_linear_attn"), TEXT("0"));
		Entity.AddKeyValue(TEXT("_quadratic_attn"), TEXT("1"));

		ParseActorTags(PointLight, Entity);
		Result.Entities.Add(MoveTemp(Entity));
		return true;
	}

	// Spot Light
	ASpotLight* SpotLight = Cast<ASpotLight>(Actor);
	if (SpotLight)
	{
		USpotLightComponent* Comp = SpotLight->SpotLightComponent;
		if (!Comp)
		{
			return true;
		}

		FSourceEntity Entity;
		Entity.ClassName = TEXT("light_spot");
		Entity.Origin = SpotLight->GetActorLocation();
		Entity.Angles = SpotLight->GetActorRotation();

		FLinearColor Color = Comp->GetLightColor();
		float Intensity = Comp->Intensity;
		float SourceBrightness = FMath::Clamp(Intensity * 0.5f, 1.0f, 10000.0f);

		Entity.AddKeyValue(TEXT("_light"), FString::Printf(TEXT("%d %d %d %d"),
			FMath::RoundToInt(Color.R * 255),
			FMath::RoundToInt(Color.G * 255),
			FMath::RoundToInt(Color.B * 255),
			FMath::RoundToInt(SourceBrightness)));

		Entity.AddKeyValue(TEXT("_inner_cone"),
			FString::FromInt(FMath::RoundToInt(Comp->InnerConeAngle)));
		Entity.AddKeyValue(TEXT("_cone"),
			FString::FromInt(FMath::RoundToInt(Comp->OuterConeAngle)));

		ParseActorTags(SpotLight, Entity);
		Result.Entities.Add(MoveTemp(Entity));
		return true;
	}

	// Directional Light -> light_environment
	ADirectionalLight* DirLight = Cast<ADirectionalLight>(Actor);
	if (DirLight)
	{
		UDirectionalLightComponent* Comp = DirLight->GetComponent();
		if (!Comp)
		{
			return true;
		}

		FSourceEntity Entity;
		Entity.ClassName = TEXT("light_environment");
		Entity.Origin = DirLight->GetActorLocation();
		Entity.Angles = DirLight->GetActorRotation();

		FLinearColor Color = Comp->GetLightColor();
		float Intensity = Comp->Intensity;
		float SourceBrightness = FMath::Clamp(Intensity * 100.0f, 1.0f, 10000.0f);

		Entity.AddKeyValue(TEXT("_light"), FString::Printf(TEXT("%d %d %d %d"),
			FMath::RoundToInt(Color.R * 255),
			FMath::RoundToInt(Color.G * 255),
			FMath::RoundToInt(Color.B * 255),
			FMath::RoundToInt(SourceBrightness)));

		Entity.AddKeyValue(TEXT("_ambient"), FString::Printf(TEXT("%d %d %d %d"),
			FMath::RoundToInt(Color.R * 200),
			FMath::RoundToInt(Color.G * 200),
			FMath::RoundToInt(Color.B * 200),
			FMath::RoundToInt(SourceBrightness * 0.3f)));

		FRotator Rot = DirLight->GetActorRotation();
		Entity.AddKeyValue(TEXT("pitch"), FString::FromInt(FMath::RoundToInt(Rot.Pitch)));
		Entity.AddKeyValue(TEXT("SunSpreadAngle"), TEXT("5"));

		ParseActorTags(DirLight, Entity);
		Result.Entities.Add(MoveTemp(Entity));
		return true;
	}

	return false;
}

bool FEntityExporter::TryExportTriggerVolume(AActor* Actor, FEntityExportResult& Result)
{
	// Check for TriggerBox or TriggerVolume
	ATriggerBox* TriggerBox = Cast<ATriggerBox>(Actor);
	ATriggerVolume* TriggerVolume = Cast<ATriggerVolume>(Actor);

	if (!TriggerBox && !TriggerVolume)
	{
		return false;
	}

	FSourceEntity Entity;
	Entity.ClassName = TEXT("trigger_multiple");
	Entity.Origin = Actor->GetActorLocation();
	Entity.Angles = Actor->GetActorRotation();

	// Default trigger properties
	Entity.AddKeyValue(TEXT("spawnflags"), TEXT("1")); // Clients only
	Entity.AddKeyValue(TEXT("StartDisabled"), TEXT("0"));
	Entity.AddKeyValue(TEXT("wait"), TEXT("1")); // 1 second between triggers

	// Parse actor tags for targetname, classname override, keyvalues, I/O
	ParseActorTags(Actor, Entity);

	Result.Entities.Add(MoveTemp(Entity));
	return true;
}
