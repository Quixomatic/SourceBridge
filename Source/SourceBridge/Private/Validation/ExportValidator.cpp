#include "Validation/ExportValidator.h"
#include "SourceBridgeModule.h"
#include "Entities/EntityExporter.h"
#include "Entities/FGDParser.h"
#include "Materials/SurfaceProperties.h"
#include "Engine/World.h"
#include "Engine/Brush.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/Light.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/TriggerVolume.h"
#include "Engine/TriggerBox.h"
#include "PhysicsEngine/BodySetup.h"
#include "Model.h"
#include "EngineUtils.h"

void FValidationResult::LogAll() const
{
	for (const FValidationMessage& Msg : Messages)
	{
		switch (Msg.Severity)
		{
		case EValidationSeverity::Error:
			UE_LOG(LogTemp, Error, TEXT("SourceBridge [%s]: %s"), *Msg.Category, *Msg.Message);
			break;
		case EValidationSeverity::Warning:
			UE_LOG(LogTemp, Warning, TEXT("SourceBridge [%s]: %s"), *Msg.Category, *Msg.Message);
			break;
		case EValidationSeverity::Info:
			UE_LOG(LogTemp, Log, TEXT("SourceBridge [%s]: %s"), *Msg.Category, *Msg.Message);
			break;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("SourceBridge Validation: %d errors, %d warnings, %d info"),
		ErrorCount, WarningCount, InfoCount);
}

FValidationResult FExportValidator::ValidateWorld(UWorld* World)
{
	FValidationResult Result;

	if (!World)
	{
		Result.AddMessage(EValidationSeverity::Error, TEXT("World"), TEXT("No world provided for validation."));
		return Result;
	}

	ValidateBrushLimits(World, Result);
	ValidateEntities(World, Result);
	ValidateGeometry(World, Result);
	ValidateLighting(World, Result);
	ValidateSpawns(World, Result);
	ValidateEntityClassnames(World, Result);
	ValidateStaticMeshes(World, Result);

	return Result;
}

void FExportValidator::ValidateBrushLimits(UWorld* World, FValidationResult& Result)
{
	int32 BrushCount = 0;
	int32 BrushSideCount = 0;

	for (TActorIterator<ABrush> It(World); It; ++It)
	{
		ABrush* Brush = *It;
		if (!Brush || Brush->IsA<AVolume>()) continue;
		if (Brush == World->GetDefaultBrush()) continue;

		BrushCount++;

		if (Brush->Brush && Brush->Brush->Polys)
		{
			BrushSideCount += Brush->Brush->Polys->Element.Num();
		}
	}

	Result.AddMessage(EValidationSeverity::Info, TEXT("Limits"),
		FString::Printf(TEXT("Brushes: %d / %d"), BrushCount, FSourceEngineLimits::MAX_MAP_BRUSHES));
	Result.AddMessage(EValidationSeverity::Info, TEXT("Limits"),
		FString::Printf(TEXT("Brush sides: %d / %d"), BrushSideCount, FSourceEngineLimits::MAX_MAP_BRUSHSIDES));

	if (BrushCount > FSourceEngineLimits::MAX_MAP_BRUSHES)
	{
		Result.AddMessage(EValidationSeverity::Error, TEXT("Limits"),
			FString::Printf(TEXT("Brush count %d exceeds Source limit of %d!"),
				BrushCount, FSourceEngineLimits::MAX_MAP_BRUSHES));
	}
	else if (BrushCount > FSourceEngineLimits::MAX_MAP_BRUSHES * 0.8)
	{
		Result.AddMessage(EValidationSeverity::Warning, TEXT("Limits"),
			FString::Printf(TEXT("Brush count %d is >80%% of Source limit."), BrushCount));
	}

	if (BrushSideCount > FSourceEngineLimits::MAX_MAP_BRUSHSIDES)
	{
		Result.AddMessage(EValidationSeverity::Error, TEXT("Limits"),
			FString::Printf(TEXT("Brush side count %d exceeds Source limit of %d!"),
				BrushSideCount, FSourceEngineLimits::MAX_MAP_BRUSHSIDES));
	}

	if (BrushCount == 0)
	{
		Result.AddMessage(EValidationSeverity::Warning, TEXT("Limits"),
			TEXT("No brushes found in scene. VMF will have no world geometry."));
	}
}

void FExportValidator::ValidateEntities(UWorld* World, FValidationResult& Result)
{
	int32 EntityCount = 0;
	int32 LightCount = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		// Count entities that would be exported
		if (Actor->IsA<APlayerStart>() ||
			Actor->IsA<ALight>() ||
			Actor->IsA<ATriggerVolume>() ||
			Actor->IsA<ATriggerBox>())
		{
			EntityCount++;
		}

		if (Actor->IsA<ALight>())
		{
			LightCount++;
		}
	}

	// +1 for worldspawn
	EntityCount += 1;

	Result.AddMessage(EValidationSeverity::Info, TEXT("Entities"),
		FString::Printf(TEXT("Entities: ~%d / %d"), EntityCount, FSourceEngineLimits::MAX_MAP_ENTITIES));

	if (EntityCount > FSourceEngineLimits::MAX_MAP_ENTITIES)
	{
		Result.AddMessage(EValidationSeverity::Error, TEXT("Entities"),
			FString::Printf(TEXT("Entity count ~%d exceeds Source limit of %d!"),
				EntityCount, FSourceEngineLimits::MAX_MAP_ENTITIES));
	}

	if (LightCount > FSourceEngineLimits::MAX_MAP_LIGHTS)
	{
		Result.AddMessage(EValidationSeverity::Error, TEXT("Entities"),
			FString::Printf(TEXT("Light count %d exceeds Source limit of %d!"),
				LightCount, FSourceEngineLimits::MAX_MAP_LIGHTS));
	}
}

void FExportValidator::ValidateGeometry(UWorld* World, FValidationResult& Result)
{
	int32 SubtractiveCount = 0;
	int32 TooFewFaces = 0;

	for (TActorIterator<ABrush> It(World); It; ++It)
	{
		ABrush* Brush = *It;
		if (!Brush || Brush->IsA<AVolume>()) continue;
		if (Brush == World->GetDefaultBrush()) continue;

		if (Brush->BrushType == EBrushType::Brush_Subtract)
		{
			SubtractiveCount++;
		}

		if (Brush->Brush && Brush->Brush->Polys)
		{
			int32 FaceCount = Brush->Brush->Polys->Element.Num();
			if (FaceCount < 4)
			{
				TooFewFaces++;
				Result.AddMessage(EValidationSeverity::Warning, TEXT("Geometry"),
					FString::Printf(TEXT("Brush %s has only %d faces (minimum 4 for a convex solid)."),
						*Brush->GetName(), FaceCount));
			}
		}
	}

	if (SubtractiveCount > 0)
	{
		Result.AddMessage(EValidationSeverity::Warning, TEXT("Geometry"),
			FString::Printf(TEXT("%d subtractive brushes found. Source doesn't support subtraction - these will be skipped."),
				SubtractiveCount));
	}
}

void FExportValidator::ValidateLighting(UWorld* World, FValidationResult& Result)
{
	bool bHasDirectionalLight = false;
	bool bHasAnyLight = false;

	for (TActorIterator<ALight> It(World); It; ++It)
	{
		bHasAnyLight = true;
		if ((*It)->IsA<ADirectionalLight>())
		{
			bHasDirectionalLight = true;
		}
	}

	if (!bHasAnyLight)
	{
		Result.AddMessage(EValidationSeverity::Warning, TEXT("Lighting"),
			TEXT("No lights in scene. Map will be fullbright (no shadows)."));
	}

	if (!bHasDirectionalLight)
	{
		Result.AddMessage(EValidationSeverity::Info, TEXT("Lighting"),
			TEXT("No directional light found. No light_environment will be exported (no sun/sky lighting)."));
	}
}

void FExportValidator::ValidateSpawns(UWorld* World, FValidationResult& Result)
{
	int32 TSpawnCount = 0;
	int32 CTSpawnCount = 0;
	int32 UntaggedSpawnCount = 0;

	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		APlayerStart* Start = *It;
		if (!Start) continue;

		bool bTagged = false;
		for (const FName& Tag : Start->Tags)
		{
			FString TagStr = Tag.ToString();
			if (TagStr.Equals(TEXT("T"), ESearchCase::IgnoreCase) ||
				TagStr.Equals(TEXT("Terrorist"), ESearchCase::IgnoreCase))
			{
				TSpawnCount++;
				bTagged = true;
				break;
			}
			else if (TagStr.Equals(TEXT("CT"), ESearchCase::IgnoreCase) ||
				TagStr.Equals(TEXT("CounterTerrorist"), ESearchCase::IgnoreCase))
			{
				CTSpawnCount++;
				bTagged = true;
				break;
			}
		}

		if (!bTagged)
		{
			UntaggedSpawnCount++;
		}
	}

	int32 TotalSpawns = TSpawnCount + CTSpawnCount + UntaggedSpawnCount;

	if (TotalSpawns == 0)
	{
		Result.AddMessage(EValidationSeverity::Error, TEXT("Spawns"),
			TEXT("No player spawns (PlayerStart actors) found. Map needs at least 1 T and 1 CT spawn."));
		return;
	}

	// Untagged spawns get auto-alternated between T and CT
	int32 EffectiveT = TSpawnCount + (UntaggedSpawnCount / 2);
	int32 EffectiveCT = CTSpawnCount + ((UntaggedSpawnCount + 1) / 2);

	Result.AddMessage(EValidationSeverity::Info, TEXT("Spawns"),
		FString::Printf(TEXT("Spawns: %d T, %d CT (%d untagged auto-assigned)"),
			EffectiveT, EffectiveCT, UntaggedSpawnCount));

	if (EffectiveT == 0)
	{
		Result.AddMessage(EValidationSeverity::Error, TEXT("Spawns"),
			TEXT("No Terrorist (T) spawns. Tag PlayerStart actors with 'T' tag."));
	}

	if (EffectiveCT == 0)
	{
		Result.AddMessage(EValidationSeverity::Error, TEXT("Spawns"),
			TEXT("No Counter-Terrorist (CT) spawns. Tag PlayerStart actors with 'CT' tag."));
	}

	if (EffectiveT != EffectiveCT && EffectiveT > 0 && EffectiveCT > 0)
	{
		Result.AddMessage(EValidationSeverity::Warning, TEXT("Spawns"),
			FString::Printf(TEXT("Unbalanced spawns: %d T vs %d CT. Consider adding spawns."),
				EffectiveT, EffectiveCT));
	}

	if (UntaggedSpawnCount > 0)
	{
		Result.AddMessage(EValidationSeverity::Warning, TEXT("Spawns"),
			FString::Printf(TEXT("%d untagged PlayerStart actors. Tag with 'T' or 'CT' for explicit team assignment."),
				UntaggedSpawnCount));
	}
}

void FExportValidator::ValidateEntityClassnames(UWorld* World, FValidationResult& Result)
{
	const FFGDDatabase& FGD = FSourceBridgeModule::GetFGDDatabase();

	if (FGD.Classes.Num() == 0)
	{
		Result.AddMessage(EValidationSeverity::Info, TEXT("FGD"),
			TEXT("No FGD loaded. Entity classname validation skipped. Use SourceBridge.LoadFGD to enable."));
		return;
	}

	// Export entities to check their classnames against FGD
	FEntityExportResult EntityResult = FEntityExporter::ExportEntities(World);

	int32 ValidCount = 0;
	int32 UnknownCount = 0;

	for (const FSourceEntity& Entity : EntityResult.Entities)
	{
		const FFGDEntityClass* FGDClass = FGD.FindClass(Entity.ClassName);

		if (!FGDClass)
		{
			UnknownCount++;
			Result.AddMessage(EValidationSeverity::Warning, TEXT("FGD"),
				FString::Printf(TEXT("Entity classname '%s' not found in FGD schema."),
					*Entity.ClassName));
		}
		else
		{
			ValidCount++;

			// Validate keyvalues against FGD
			TArray<FString> KVWarnings = FGD.ValidateEntity(
				Entity.ClassName, Entity.KeyValues);
			for (const FString& Warning : KVWarnings)
			{
				Result.AddMessage(EValidationSeverity::Warning, TEXT("FGD"), Warning);
			}

			// Validate I/O connections
			for (const FEntityIOConnection& Conn : Entity.Connections)
			{
				// Check output exists on this entity
				FFGDEntityClass Resolved = FGD.GetResolved(Entity.ClassName);
				if (!Resolved.FindOutput(Conn.OutputName))
				{
					Result.AddMessage(EValidationSeverity::Warning, TEXT("FGD"),
						FString::Printf(TEXT("Entity '%s' (%s): output '%s' not found in FGD."),
							*Entity.TargetName, *Entity.ClassName, *Conn.OutputName));
				}

				// Check if target entity's input exists (if we know the target's class)
				if (!Conn.TargetEntity.IsEmpty() && EntityResult.TargetNames.Contains(Conn.TargetEntity))
				{
					// Find the target entity to get its classname
					for (const FSourceEntity& Target : EntityResult.Entities)
					{
						if (Target.TargetName == Conn.TargetEntity)
						{
							FString IOWarning = FGD.ValidateIOConnection(
								Entity.ClassName, Conn.OutputName,
								Target.ClassName, Conn.InputName);
							if (!IOWarning.IsEmpty())
							{
								Result.AddMessage(EValidationSeverity::Warning, TEXT("FGD"), IOWarning);
							}
							break;
						}
					}
				}
			}
		}
	}

	Result.AddMessage(EValidationSeverity::Info, TEXT("FGD"),
		FString::Printf(TEXT("Entity validation: %d valid, %d unknown classnames (FGD has %d classes)."),
			ValidCount, UnknownCount, FGD.Classes.Num()));
}

void FExportValidator::ValidateSurfaceProperties(UWorld* World, FValidationResult& Result)
{
	// This can be called separately for surface prop validation
	// Currently a placeholder for when material export adds $surfaceprop
}

void FExportValidator::ValidateStaticMeshes(UWorld* World, FValidationResult& Result)
{
	int32 StaticMeshCount = 0;
	int32 HighPolyCount = 0;
	int32 NoCollisionCount = 0;

	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* Actor = *It;
		if (!Actor) continue;

		UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
		if (!Comp) continue;

		UStaticMesh* Mesh = Comp->GetStaticMesh();
		if (!Mesh) continue;

		StaticMeshCount++;

		// Warn about high-poly meshes that may cause performance issues
		if (Mesh->GetNumTriangles(0) > 10000)
		{
			HighPolyCount++;
			Result.AddMessage(EValidationSeverity::Warning, TEXT("Geometry"),
				FString::Printf(TEXT("Static mesh '%s' has %d triangles. Consider simplifying for Source."),
					*Mesh->GetName(), Mesh->GetNumTriangles(0)));
		}

		// Warn about meshes without collision
		if (!Mesh->GetBodySetup() ||
			(Mesh->GetBodySetup()->AggGeom.ConvexElems.Num() == 0 &&
			 Mesh->GetBodySetup()->AggGeom.BoxElems.Num() == 0 &&
			 Mesh->GetBodySetup()->AggGeom.SphereElems.Num() == 0))
		{
			NoCollisionCount++;
		}
	}

	if (StaticMeshCount > 0)
	{
		Result.AddMessage(EValidationSeverity::Info, TEXT("Geometry"),
			FString::Printf(TEXT("Static mesh actors: %d (will export as props)"), StaticMeshCount));
	}

	if (HighPolyCount > 0)
	{
		Result.AddMessage(EValidationSeverity::Warning, TEXT("Geometry"),
			FString::Printf(TEXT("%d static meshes exceed 10K triangles. Source models should be <10K for performance."),
				HighPolyCount));
	}

	if (NoCollisionCount > 0)
	{
		Result.AddMessage(EValidationSeverity::Info, TEXT("Geometry"),
			FString::Printf(TEXT("%d static meshes have no simple collision. Physics mesh will use render mesh as fallback."),
				NoCollisionCount));
	}
}
