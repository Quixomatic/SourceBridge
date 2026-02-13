#include "Entities/EntityExporter.h"
#include "Utilities/SourceCoord.h"
#include "Actors/SourceEntityActor.h"
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
#include "Import/VMFImporter.h"

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

		if (TryExportWaterVolume(Actor, Result))
		{
			continue;
		}

		if (TryExportOverlay(Actor, Result))
		{
			continue;
		}

		if (TryExportBrushEntity(Actor, Result))
		{
			continue;
		}

		// Export custom ASourceEntityActor instances
		ASourceEntityActor* SourceActor = Cast<ASourceEntityActor>(Actor);
		if (SourceActor && !SourceActor->SourceClassname.IsEmpty())
		{
			FSourceEntity Entity;
			Entity.ClassName = SourceActor->SourceClassname;
			Entity.TargetName = SourceActor->TargetName;
			Entity.Origin = SourceActor->GetActorLocation();
			Entity.Angles = SourceActor->GetActorRotation();

			// Parentname
			if (!SourceActor->ParentName.IsEmpty())
			{
				Entity.AddKeyValue(TEXT("parentname"), SourceActor->ParentName);
			}

			// Copy user-defined keyvalues
			for (const auto& KV : SourceActor->KeyValues)
			{
				Entity.AddKeyValue(KV.Key, KV.Value);
			}

			// Spawnflags
			if (SourceActor->SpawnFlags != 0)
			{
				Entity.AddKeyValue(TEXT("spawnflags"), SourceActor->SpawnFlags);
			}

			// Handle subclass-specific properties
			if (ASourceLight* LightActor = Cast<ASourceLight>(SourceActor))
			{
				Entity.AddKeyValue(TEXT("_light"), FString::Printf(TEXT("%d %d %d %d"),
					LightActor->LightColor.R, LightActor->LightColor.G,
					LightActor->LightColor.B, LightActor->Brightness));
				Entity.AddKeyValue(TEXT("style"), LightActor->Style);
			}
			else if (ASourceProp* PropActor = Cast<ASourceProp>(SourceActor))
			{
				if (!PropActor->ModelPath.IsEmpty())
				{
					Entity.AddKeyValue(TEXT("model"), PropActor->ModelPath);
				}
				Entity.AddKeyValue(TEXT("skin"), PropActor->Skin);
				Entity.AddKeyValue(TEXT("solid"), PropActor->Solid);

				// Model scale
				if (!FMath::IsNearlyEqual(PropActor->ModelScale, 1.0f, 0.001f))
				{
					Entity.AddKeyValue(TEXT("modelscale"),
						FString::Printf(TEXT("%g"), PropActor->ModelScale));
				}

				// Fade distances
				if (PropActor->FadeMinDist > 0.0f)
				{
					Entity.AddKeyValue(TEXT("fademindist"),
						FString::Printf(TEXT("%g"), PropActor->FadeMinDist));
				}
				if (PropActor->FadeMaxDist > 0.0f)
				{
					Entity.AddKeyValue(TEXT("fademaxdist"),
						FString::Printf(TEXT("%g"), PropActor->FadeMaxDist));
				}

				// Shadow toggle
				if (PropActor->bDisableShadows)
				{
					Entity.AddKeyValue(TEXT("disableshadows"), 1);
				}

				// Render color/alpha
				if (PropActor->RenderColor != FColor(255, 255, 255))
				{
					Entity.AddKeyValue(TEXT("rendercolor"), FString::Printf(TEXT("%d %d %d"),
						PropActor->RenderColor.R, PropActor->RenderColor.G, PropActor->RenderColor.B));
				}
				if (PropActor->RenderAmt != 255)
				{
					Entity.AddKeyValue(TEXT("renderamt"), PropActor->RenderAmt);
				}
			}
			else if (ASourceTrigger* TriggerActor = Cast<ASourceTrigger>(SourceActor))
			{
				Entity.AddKeyValue(TEXT("wait"), TriggerActor->WaitTime);
				if (SourceActor->SpawnFlags == 0)
				{
					Entity.AddKeyValue(TEXT("spawnflags"), 1); // Clients only default
				}
			}
			else if (ASourceEnvSprite* SpriteActor = Cast<ASourceEnvSprite>(SourceActor))
			{
				Entity.AddKeyValue(TEXT("model"), SpriteActor->SpriteModel);
				Entity.AddKeyValue(TEXT("rendermode"), SpriteActor->RenderMode);
				Entity.AddKeyValue(TEXT("renderamt"), TEXT("255"));
				Entity.AddKeyValue(TEXT("rendercolor"), FString::Printf(TEXT("%d %d %d"),
					SpriteActor->RenderColor.R, SpriteActor->RenderColor.G, SpriteActor->RenderColor.B));
				Entity.AddKeyValue(TEXT("scale"), SpriteActor->SourceSpriteScale);
			}
			else if (ASourceSoundscape* SoundActor = Cast<ASourceSoundscape>(SourceActor))
			{
				if (!SoundActor->SoundscapeName.IsEmpty())
				{
					Entity.AddKeyValue(TEXT("soundscape"), SoundActor->SoundscapeName);
				}
				Entity.AddKeyValue(TEXT("radius"), SoundActor->Radius);
			}
			else if (ASourceGoalTrigger* GoalActor = Cast<ASourceGoalTrigger>(SourceActor))
			{
				Entity.AddKeyValue(TEXT("wait"), GoalActor->WaitTime);
				Entity.AddKeyValue(TEXT("TeamNum"), GoalActor->TeamNumber);
				if (SourceActor->SpawnFlags == 0)
				{
					Entity.AddKeyValue(TEXT("spawnflags"), 1); // Clients only
				}
			}
			else if (ASourceSpectatorCamera* CamActor = Cast<ASourceSpectatorCamera>(SourceActor))
			{
				Entity.AddKeyValue(TEXT("fov"), CamActor->FOV);
			}

			// Parse any additional actor tags
			ParseActorTags(SourceActor, Entity);

			Result.Entities.Add(MoveTemp(Entity));
			continue;
		}
	}

	// Collect all unique targetnames for I/O validation
	for (const FSourceEntity& Entity : Result.Entities)
	{
		if (!Entity.TargetName.IsEmpty() && !Result.TargetNames.Contains(Entity.TargetName))
		{
			Result.TargetNames.Add(Entity.TargetName);
		}
	}

	// Validate I/O connections: warn about targets that don't exist
	for (const FSourceEntity& Entity : Result.Entities)
	{
		for (const FEntityIOConnection& Conn : Entity.Connections)
		{
			if (!Conn.TargetEntity.IsEmpty() && !Result.TargetNames.Contains(Conn.TargetEntity))
			{
				// Check for special target names that Source resolves at runtime
				if (Conn.TargetEntity != TEXT("!activator") &&
					Conn.TargetEntity != TEXT("!caller") &&
					Conn.TargetEntity != TEXT("!self") &&
					Conn.TargetEntity != TEXT("!player") &&
					!Conn.TargetEntity.StartsWith(TEXT("!")))
				{
					Result.Warnings.Add(FString::Printf(
						TEXT("Entity '%s' (%s): I/O target '%s' not found in scene. Output '%s' -> '%s.%s' may be broken."),
						*Entity.TargetName, *Entity.ClassName,
						*Conn.TargetEntity, *Conn.OutputName,
						*Conn.TargetEntity, *Conn.InputName));
				}
			}
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

		// Better intensity mapping: use attenuation radius
		float AttenuationRadius = Comp->AttenuationRadius;
		float FiftyPercDist = AttenuationRadius * 0.525f * 0.5f; // Convert to Source units, 50% distance
		if (FiftyPercDist > 0)
		{
			Entity.AddKeyValue(TEXT("_fifty_percent_distance"), FString::SanitizeFloat(FiftyPercDist));
		}

		Entity.AddKeyValue(TEXT("_constant_attn"), TEXT("0"));
		Entity.AddKeyValue(TEXT("_linear_attn"), TEXT("0"));
		Entity.AddKeyValue(TEXT("_quadratic_attn"), TEXT("1"));
		Entity.AddKeyValue(TEXT("style"), TEXT("0"));

		ParseActorTags(PointLight, Entity);
		Result.Entities.Add(MoveTemp(Entity));

		// Export env_sprite alongside point light for glow effect
		{
			FSourceEntity Sprite;
			Sprite.ClassName = TEXT("env_sprite");
			Sprite.Origin = PointLight->GetActorLocation();
			Sprite.AddKeyValue(TEXT("model"), TEXT("sprites/glow01.spr"));
			Sprite.AddKeyValue(TEXT("rendermode"), TEXT("5")); // Additive
			Sprite.AddKeyValue(TEXT("renderamt"), TEXT("255"));
			Sprite.AddKeyValue(TEXT("rendercolor"), FString::Printf(TEXT("%d %d %d"),
				FMath::RoundToInt(Color.R * 255),
				FMath::RoundToInt(Color.G * 255),
				FMath::RoundToInt(Color.B * 255)));
			Sprite.AddKeyValue(TEXT("scale"), TEXT("0.25"));
			Sprite.AddKeyValue(TEXT("GlowProxySize"), TEXT("2.0"));
			Result.Entities.Add(MoveTemp(Sprite));
		}

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

		// Pitch from rotation
		FRotator SpotRot = SpotLight->GetActorRotation();
		Entity.AddKeyValue(TEXT("pitch"), FString::FromInt(FMath::RoundToInt(SpotRot.Pitch)));

		ParseActorTags(SpotLight, Entity);
		Result.Entities.Add(MoveTemp(Entity));

		// Export env_sprite alongside spot light
		{
			FSourceEntity Sprite;
			Sprite.ClassName = TEXT("env_sprite");
			Sprite.Origin = SpotLight->GetActorLocation();
			Sprite.AddKeyValue(TEXT("model"), TEXT("sprites/glow01.spr"));
			Sprite.AddKeyValue(TEXT("rendermode"), TEXT("5"));
			Sprite.AddKeyValue(TEXT("renderamt"), TEXT("255"));
			Sprite.AddKeyValue(TEXT("rendercolor"), FString::Printf(TEXT("%d %d %d"),
				FMath::RoundToInt(Color.R * 255),
				FMath::RoundToInt(Color.G * 255),
				FMath::RoundToInt(Color.B * 255)));
			Sprite.AddKeyValue(TEXT("scale"), TEXT("0.25"));
			Result.Entities.Add(MoveTemp(Sprite));
		}

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
		Result.bHasLightEnvironment = true;
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
	Entity.bIsBrushEntity = true;
	Entity.SourceActor = Actor;

	// Default trigger properties
	Entity.AddKeyValue(TEXT("spawnflags"), TEXT("1")); // Clients only
	Entity.AddKeyValue(TEXT("StartDisabled"), TEXT("0"));
	Entity.AddKeyValue(TEXT("wait"), TEXT("1")); // 1 second between triggers

	// Parse actor tags for targetname, classname override, keyvalues, I/O
	ParseActorTags(Actor, Entity);

	Result.Entities.Add(MoveTemp(Entity));
	return true;
}

bool FEntityExporter::TryExportWaterVolume(AActor* Actor, FEntityExportResult& Result)
{
	// Water volumes are detected by actor tags:
	//   "water" - default cheap water with default material
	//   "water:material_name" - cheap water with specified material
	//   "water:cheap:material_name" - explicitly cheap water (scrolling texture)
	//   "water:expensive:material_name" - expensive water (real-time reflections/refractions)
	bool bIsWater = false;
	bool bExpensiveWater = false;
	FString WaterMaterial = TEXT("nature/water_canals01");

	for (const FName& Tag : Actor->Tags)
	{
		FString TagStr = Tag.ToString();

		if (TagStr.Equals(TEXT("water"), ESearchCase::IgnoreCase))
		{
			bIsWater = true;
		}
		else if (TagStr.StartsWith(TEXT("water:expensive:"), ESearchCase::IgnoreCase))
		{
			bIsWater = true;
			bExpensiveWater = true;
			WaterMaterial = TagStr.Mid(16);
		}
		else if (TagStr.StartsWith(TEXT("water:cheap:"), ESearchCase::IgnoreCase))
		{
			bIsWater = true;
			bExpensiveWater = false;
			WaterMaterial = TagStr.Mid(12);
		}
		else if (TagStr.StartsWith(TEXT("water:"), ESearchCase::IgnoreCase))
		{
			bIsWater = true;
			WaterMaterial = TagStr.Mid(6);
		}
	}

	if (!bIsWater)
	{
		return false;
	}

	FSourceEntity Entity;
	Entity.ClassName = TEXT("func_water_analog");
	Entity.Origin = Actor->GetActorLocation();
	Entity.Angles = Actor->GetActorRotation();
	Entity.bIsBrushEntity = true;
	Entity.SourceActor = Actor;

	// Water-specific keyvalues
	Entity.AddKeyValue(TEXT("WaveHeight"), bExpensiveWater ? TEXT("3.0") : TEXT("1.0"));
	Entity.AddKeyValue(TEXT("MoveDirIsLocal"), TEXT("0"));

	// Store internal metadata for brush face texturing and VMT generation
	Entity.AddKeyValue(TEXT("_water_material"), WaterMaterial);
	Entity.AddKeyValue(TEXT("_water_expensive"), bExpensiveWater ? TEXT("1") : TEXT("0"));

	ParseActorTags(Actor, Entity);

	Result.Entities.Add(MoveTemp(Entity));
	return true;
}

bool FEntityExporter::TryExportBrushEntity(AActor* Actor, FEntityExportResult& Result)
{
	ASourceBrushEntity* BrushEntity = Cast<ASourceBrushEntity>(Actor);
	if (!BrushEntity || BrushEntity->SourceClassname.IsEmpty())
	{
		return false;
	}

	FSourceEntity Entity;
	Entity.ClassName = BrushEntity->SourceClassname;
	Entity.TargetName = BrushEntity->TargetName;
	Entity.Origin = BrushEntity->GetActorLocation();
	Entity.Angles = BrushEntity->GetActorRotation();
	Entity.bIsBrushEntity = true;
	Entity.SourceActor = BrushEntity;

	// Parentname
	if (!BrushEntity->ParentName.IsEmpty())
	{
		Entity.AddKeyValue(TEXT("parentname"), BrushEntity->ParentName);
	}

	// Copy all keyvalues
	for (const auto& KV : BrushEntity->KeyValues)
	{
		Entity.AddKeyValue(KV.Key, KV.Value);
	}

	// Spawnflags
	if (BrushEntity->SpawnFlags != 0)
	{
		Entity.AddKeyValue(TEXT("spawnflags"), BrushEntity->SpawnFlags);
	}

	// Parse actor tags for I/O connections
	ParseActorTags(BrushEntity, Entity);

	Result.Entities.Add(MoveTemp(Entity));
	return true;
}

FVMFKeyValues FEntityExporter::BrushEntityToVMF(const FSourceEntity& Entity, int32 EntityId,
	ASourceBrushEntity* BrushActor)
{
	FVMFKeyValues Node(TEXT("entity"));
	Node.AddProperty(TEXT("id"), EntityId);
	Node.AddProperty(TEXT("classname"), Entity.ClassName);

	if (!Entity.TargetName.IsEmpty())
	{
		Node.AddProperty(TEXT("targetname"), Entity.TargetName);
	}

	// Write stored key-values (includes origin/angles only if the original entity had them).
	// Brush entities like func_detail do NOT have origin — their geometry planes are in world space.
	// Movable brush entities like triggers DO have origin stored in their keyvalues from import.
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

	// Write solid children from stored brush data
	if (BrushActor)
	{
		int32 SolidIdCounter = EntityId * 100; // Generate unique IDs
		for (const FImportedBrushData& BrushData : BrushActor->StoredBrushData)
		{
			FVMFKeyValues& SolidNode = Node.AddChild(TEXT("solid"));
			SolidNode.AddProperty(TEXT("id"), BrushData.SolidId > 0 ? BrushData.SolidId : SolidIdCounter++);

			int32 SideIdCounter = SolidIdCounter * 10;
			for (const FImportedSideData& Side : BrushData.Sides)
			{
				FVMFKeyValues& SideNode = SolidNode.AddChild(TEXT("side"));
				SideNode.AddProperty(TEXT("id"), SideIdCounter++);

				// Plane: reconstruct from stored points
				FString PlaneStr = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
					Side.PlaneP1.X, Side.PlaneP1.Y, Side.PlaneP1.Z,
					Side.PlaneP2.X, Side.PlaneP2.Y, Side.PlaneP2.Z,
					Side.PlaneP3.X, Side.PlaneP3.Y, Side.PlaneP3.Z);
				SideNode.AddProperty(TEXT("plane"), PlaneStr);

				SideNode.AddProperty(TEXT("material"), Side.Material);

				if (!Side.UAxisStr.IsEmpty())
				{
					SideNode.AddProperty(TEXT("uaxis"), Side.UAxisStr);
				}
				if (!Side.VAxisStr.IsEmpty())
				{
					SideNode.AddProperty(TEXT("vaxis"), Side.VAxisStr);
				}

				SideNode.AddProperty(TEXT("lightmapscale"), FString::FromInt(Side.LightmapScale));
			}
		}
	}

	return Node;
}

bool FEntityExporter::TryExportOverlay(AActor* Actor, FEntityExportResult& Result)
{
	// Overlays/decals are detected by actor tag "overlay:material_path"
	// They export as info_overlay point entities in Source
	FString OverlayMaterial;

	for (const FName& Tag : Actor->Tags)
	{
		FString TagStr = Tag.ToString();

		if (TagStr.StartsWith(TEXT("overlay:"), ESearchCase::IgnoreCase))
		{
			OverlayMaterial = TagStr.Mid(8);
			break;
		}
		else if (TagStr.StartsWith(TEXT("decal:"), ESearchCase::IgnoreCase))
		{
			OverlayMaterial = TagStr.Mid(6);
			break;
		}
	}

	if (OverlayMaterial.IsEmpty())
	{
		return false;
	}

	FSourceEntity Entity;
	Entity.ClassName = TEXT("info_overlay");
	Entity.Origin = Actor->GetActorLocation();
	Entity.Angles = Actor->GetActorRotation();

	Entity.AddKeyValue(TEXT("material"), OverlayMaterial);
	Entity.AddKeyValue(TEXT("RenderOrder"), TEXT("0"));

	// Default overlay dimensions (can be overridden via kv: tags)
	FVector Scale = Actor->GetActorScale3D();
	float StartU = -Scale.X * 16.0f;  // Scale-based UV extents
	float EndU = Scale.X * 16.0f;
	float StartV = -Scale.Y * 16.0f;
	float EndV = Scale.Y * 16.0f;

	Entity.AddKeyValue(TEXT("StartU"), FString::SanitizeFloat(StartU));
	Entity.AddKeyValue(TEXT("EndU"), FString::SanitizeFloat(EndU));
	Entity.AddKeyValue(TEXT("StartV"), FString::SanitizeFloat(StartV));
	Entity.AddKeyValue(TEXT("EndV"), FString::SanitizeFloat(EndV));

	// Basis normal from actor forward vector
	FVector Forward = Actor->GetActorForwardVector();
	Entity.AddKeyValue(TEXT("BasisNormal"), FString::Printf(TEXT("%.4f %.4f %.4f"),
		Forward.X, -Forward.Y, Forward.Z));

	ParseActorTags(Actor, Entity);

	Result.Entities.Add(MoveTemp(Entity));
	return true;
}
