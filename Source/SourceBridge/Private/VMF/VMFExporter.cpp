#include "VMF/VMFExporter.h"
#include "VMF/BrushConverter.h"
#include "VMF/SkyboxExporter.h"
#include "VMF/VisOptimizer.h"
#include "Entities/EntityExporter.h"
#include "Entities/PropExporter.h"
#include "Materials/MaterialMapper.h"
#include "Utilities/SourceCoord.h"
#include "Actors/SourceEntityActor.h"
#include "Engine/Brush.h"
#include "Engine/World.h"
#include "Engine/TriggerVolume.h"
#include "Engine/TriggerBox.h"
#include "EngineUtils.h"
#include "GameFramework/Volume.h"

FString FVMFExporter::ExportScene(UWorld* World, const FString& MapName, TSet<FString>* OutUsedMaterials)
{
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("SourceBridge: No world provided for export."));
		return FString();
	}

	FString Result;

	Result += BuildVersionInfo().Serialize();
	Result += BuildVisGroups().Serialize();
	Result += BuildViewSettings().Serialize();

	int32 SolidIdCounter = 2;  // worldspawn is id 1
	int32 SideIdCounter = 1;
	int32 BrushCount = 0;
	int32 SkippedCount = 0;

	// Export entities first so we can read results (skyname, light_environment, etc.)
	FEntityExportResult EntityResult = FEntityExporter::ExportEntities(World);

	for (const FString& Warning : EntityResult.Warnings)
	{
		UE_LOG(LogTemp, Warning, TEXT("SourceBridge: %s"), *Warning);
	}

	// Export skybox (detects sky_camera actors, skyname tags)
	FSkyboxSettings SkySettings;
	int32 EntityIdCounter = 1000; // temporary, reassigned below
	FSkyboxData SkyData = FSkyboxExporter::ExportSkybox(
		World, EntityIdCounter, SolidIdCounter, SideIdCounter, SkySettings);

	// Build world block with dynamic skyname
	FVMFKeyValues WorldNode(TEXT("world"));
	WorldNode.AddProperty(TEXT("id"), 1);
	WorldNode.AddProperty(TEXT("mapversion"), 1);
	WorldNode.AddProperty(TEXT("classname"), TEXT("worldspawn"));
	WorldNode.AddProperty(TEXT("skyname"), SkyData.SkyName.IsEmpty()
		? TEXT("sky_day01_01") : *SkyData.SkyName);
	WorldNode.AddProperty(TEXT("maxpropscreenwidth"), -1);
	WorldNode.AddProperty(TEXT("detailvbsp"), TEXT("detail.vbsp"));
	WorldNode.AddProperty(TEXT("detailmaterial"), TEXT("detail/detailsprites"));

	// Material mapper resolves UE materials to Source material paths
	FMaterialMapper MatMapper;
	if (!MapName.IsEmpty())
	{
		MatMapper.SetMapName(MapName);
	}

	// Deferred brush entities (func_detail, func_wall, etc.) - written after worldspawn
	TArray<FVMFKeyValues> BrushEntities;

	for (TActorIterator<ABrush> It(World); It; ++It)
	{
		ABrush* Brush = *It;

		// Skip the default builder brush (the wireframe template brush)
		if (Brush == World->GetDefaultBrush())
		{
			continue;
		}

		// Skip volume actors (blocking volumes, post-process, etc.)
		if (Brush->IsA<AVolume>())
		{
			continue;
		}

		// Skip hint brushes (handled separately by VisOptimizer)
		if (FVisOptimizer::IsHintBrush(Brush))
		{
			continue;
		}

		// Check if this brush should be a brush entity (func_detail, func_wall, etc.)
		FString BrushEntityClass;
		FString BrushTargetName;
		TArray<TPair<FString, FString>> BrushExtraKV;
		for (const FName& Tag : Brush->Tags)
		{
			FString TagStr = Tag.ToString();

			if (TagStr.StartsWith(TEXT("classname:"), ESearchCase::IgnoreCase))
			{
				BrushEntityClass = TagStr.Mid(10);
			}
			else if (TagStr.Equals(TEXT("func_detail"), ESearchCase::IgnoreCase))
			{
				BrushEntityClass = TEXT("func_detail");
			}
			else if (TagStr.Equals(TEXT("func_wall"), ESearchCase::IgnoreCase))
			{
				BrushEntityClass = TEXT("func_wall");
			}
			else if (TagStr.Equals(TEXT("func_door"), ESearchCase::IgnoreCase))
			{
				BrushEntityClass = TEXT("func_door");
			}
			else if (TagStr.Equals(TEXT("func_brush"), ESearchCase::IgnoreCase))
			{
				BrushEntityClass = TEXT("func_brush");
			}
			else if (TagStr.Equals(TEXT("func_illusionary"), ESearchCase::IgnoreCase))
			{
				BrushEntityClass = TEXT("func_illusionary");
			}
			else if (TagStr.Equals(TEXT("func_breakable"), ESearchCase::IgnoreCase))
			{
				BrushEntityClass = TEXT("func_breakable");
			}
			else if (TagStr.Equals(TEXT("func_areaportal"), ESearchCase::IgnoreCase))
			{
				BrushEntityClass = TEXT("func_areaportal");
			}
			else if (TagStr.Equals(TEXT("func_viscluster"), ESearchCase::IgnoreCase))
			{
				BrushEntityClass = TEXT("func_viscluster");
			}
			else if (TagStr.StartsWith(TEXT("targetname:"), ESearchCase::IgnoreCase))
			{
				BrushTargetName = TagStr.Mid(11);
			}
			else if (TagStr.StartsWith(TEXT("kv:"), ESearchCase::IgnoreCase))
			{
				FString Remainder = TagStr.Mid(3);
				int32 ColonIdx;
				if (Remainder.FindChar(TEXT(':'), ColonIdx))
				{
					BrushExtraKV.Emplace(Remainder.Left(ColonIdx), Remainder.Mid(ColonIdx + 1));
				}
			}
		}

		FBrushConversionResult ConvResult = FBrushConverter::ConvertBrush(
			Brush, SolidIdCounter, SideIdCounter, &MatMapper);

		for (const FString& Warning : ConvResult.Warnings)
		{
			UE_LOG(LogTemp, Warning, TEXT("SourceBridge: %s"), *Warning);
		}

		if (ConvResult.Solids.Num() == 0)
		{
			if (ConvResult.Warnings.Num() > 0)
			{
				SkippedCount++;
			}
			continue;
		}

		if (!BrushEntityClass.IsEmpty())
		{
			// Emit as a brush entity
			FVMFKeyValues BrushEntity(TEXT("entity"));
			BrushEntity.AddProperty(TEXT("id"), SolidIdCounter++);
			BrushEntity.AddProperty(TEXT("classname"), BrushEntityClass);

			if (!BrushTargetName.IsEmpty())
			{
				BrushEntity.AddProperty(TEXT("targetname"), BrushTargetName);
			}

			for (const auto& KV : BrushExtraKV)
			{
				BrushEntity.AddProperty(KV.Key, KV.Value);
			}

			for (FVMFKeyValues& Solid : ConvResult.Solids)
			{
				BrushEntity.Children.Add(MoveTemp(Solid));
				BrushCount++;
			}

			BrushEntities.Add(MoveTemp(BrushEntity));
		}
		else
		{
			// Add to worldspawn as structural geometry
			for (FVMFKeyValues& Solid : ConvResult.Solids)
			{
				WorldNode.Children.Add(MoveTemp(Solid));
				BrushCount++;
			}
		}
	}

	// Collect static mesh actors tagged for brush conversion (source:worldspawn, source:func_detail, etc.)
	int32 MeshBrushCount = 0;
	TArray<FMeshToBrushResult> MeshBrushes = FPropExporter::CollectMeshBrushes(
		World, SolidIdCounter, SideIdCounter);
	for (FMeshToBrushResult& MBR : MeshBrushes)
	{
		if (MBR.EntityClass.IsEmpty())
		{
			// Worldspawn — add solids to world node
			for (FVMFKeyValues& Solid : MBR.Solids)
			{
				WorldNode.Children.Add(MoveTemp(Solid));
				BrushCount++;
				MeshBrushCount++;
			}
		}
		else
		{
			// Brush entity (func_detail, etc.) — create entity block
			FVMFKeyValues MeshEntity(TEXT("entity"));
			MeshEntity.AddProperty(TEXT("id"), SolidIdCounter++);
			MeshEntity.AddProperty(TEXT("classname"), MBR.EntityClass);

			for (FVMFKeyValues& Solid : MBR.Solids)
			{
				MeshEntity.Children.Add(MoveTemp(Solid));
				BrushCount++;
				MeshBrushCount++;
			}

			BrushEntities.Add(MoveTemp(MeshEntity));
		}
	}

	// Inject worldspawn solids from imported ASourceBrushEntity actors (StoredBrushData)
	// These must go inside the worldspawn "world" block, NOT as separate entities
	int32 WorldspawnBrushCount = 0;
	TSet<FString> WorldspawnMaterialPaths;
	for (TActorIterator<ASourceBrushEntity> It(World); It; ++It)
	{
		ASourceBrushEntity* BrushEntity = *It;
		if (!BrushEntity) continue;
		if (BrushEntity->SourceClassname != TEXT("worldspawn")) continue;
		if (BrushEntity->StoredBrushData.Num() == 0) continue;

		for (const FImportedBrushData& BrushData : BrushEntity->StoredBrushData)
		{
			FVMFKeyValues SolidNode(TEXT("solid"));
			SolidNode.AddProperty(TEXT("id"), BrushData.SolidId > 0 ? BrushData.SolidId : SolidIdCounter++);

			for (const FImportedSideData& Side : BrushData.Sides)
			{
				FVMFKeyValues SideNode(TEXT("side"));
				SideNode.AddProperty(TEXT("id"), SideIdCounter++);

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

				SideNode.AddProperty(TEXT("rotation"), 0);
				SideNode.AddProperty(TEXT("lightmapscale"), FString::FromInt(Side.LightmapScale));
				SideNode.AddProperty(TEXT("smoothing_groups"), 0);

				SolidNode.Children.Add(MoveTemp(SideNode));

				if (!Side.Material.IsEmpty())
				{
					WorldspawnMaterialPaths.Add(Side.Material);
				}
			}

			WorldNode.Children.Add(MoveTemp(SolidNode));
			BrushCount++;
			WorldspawnBrushCount++;
		}
	}

	if (WorldspawnBrushCount > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Injected %d worldspawn solids from ASourceBrushEntity actors."), WorldspawnBrushCount);
	}

	// Add hint/skip brushes to worldspawn (visibility optimization)
	TArray<FVMFKeyValues> HintBrushes = FVisOptimizer::ExportHintBrushes(
		World, SolidIdCounter, SideIdCounter);
	for (FVMFKeyValues& HintBrush : HintBrushes)
	{
		WorldNode.Children.Add(MoveTemp(HintBrush));
	}

	// Add skybox shell brushes to worldspawn
	for (FVMFKeyValues& SkyBrush : SkyData.SkyboxBrushes)
	{
		WorldNode.Children.Add(MoveTemp(SkyBrush));
	}

	Result += WorldNode.Serialize();

	// Entity IDs continue after solid IDs
	EntityIdCounter = SolidIdCounter;

	// Write brush entities (func_detail, func_wall, func_door, etc.)
	for (FVMFKeyValues& BrushEntity : BrushEntities)
	{
		Result += BrushEntity.Serialize();
		EntityIdCounter++;
	}

	// Write point entities (spawns, lights, etc.) - skip brush entities handled separately
	for (const FSourceEntity& Entity : EntityResult.Entities)
	{
		if (Entity.bIsBrushEntity)
		{
			continue; // Handled by ExportBrushEntities below
		}
		Result += FEntityExporter::EntityToVMF(Entity, EntityIdCounter++).Serialize();
	}

	// Write sky_camera entity if present
	if (SkyData.bHasSkyCamera)
	{
		Result += SkyData.SkyCameraEntity.Serialize();
	}

	// Export static mesh actors as prop entities
	TArray<FVMFKeyValues> PropEntities = FPropExporter::ExportProps(
		World, EntityIdCounter);
	for (const FVMFKeyValues& PropEntity : PropEntities)
	{
		Result += PropEntity.Serialize();
	}

	// Export brush entities (triggers, water volumes) with solid geometry
	ExportBrushEntities(EntityResult.Entities, EntityIdCounter, SolidIdCounter, SideIdCounter, MatMapper, Result);

	Result += BuildCameras().Serialize();
	Result += BuildCordon().Serialize();

	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Exported %d brushes (%d from meshes, %d skipped), %d brush entities, %d entities, %d props to VMF."),
		BrushCount, MeshBrushCount, SkippedCount, BrushEntities.Num(), EntityResult.Entities.Num(), PropEntities.Num());

	// Return the set of all Source material paths used in this export
	if (OutUsedMaterials)
	{
		*OutUsedMaterials = MatMapper.GetUsedPaths();
		// Include material paths from worldspawn StoredBrushData solids
		OutUsedMaterials->Append(WorldspawnMaterialPaths);
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: %d unique material paths used in export."), OutUsedMaterials->Num());
	}

	return Result;
}

FString FVMFExporter::GenerateBoxRoom()
{
	FString Result;

	// VMF header blocks
	Result += BuildVersionInfo().Serialize();
	Result += BuildVisGroups().Serialize();
	Result += BuildViewSettings().Serialize();

	// World block with box room geometry
	FVMFKeyValues World(TEXT("world"));
	World.AddProperty(TEXT("id"), 1);
	World.AddProperty(TEXT("mapversion"), 1);
	World.AddProperty(TEXT("classname"), TEXT("worldspawn"));
	World.AddProperty(TEXT("skyname"), TEXT("sky_day01_01"));
	World.AddProperty(TEXT("maxpropscreenwidth"), -1);
	World.AddProperty(TEXT("detailvbsp"), TEXT("detail.vbsp"));
	World.AddProperty(TEXT("detailmaterial"), TEXT("detail/detailsprites"));

	// Box room dimensions (Source units):
	// Interior: 512x512x256, centered on XY origin, floor at Z=0
	// Wall thickness: 16 units
	const FString InnerMat = TEXT("DEV/DEV_MEASUREWALL01A");
	const FString OuterMat = TEXT("TOOLS/TOOLSNODRAW");

	int32 SolidId = 2;  // worldspawn is id 1
	int32 SideId = 1;

	// Floor: full footprint, thin in Z
	// min(-272, -272, -16) max(272, 272, 0)
	World.Children.Add(BuildAABBSolid(SolidId++, SideId,
		FVector(-272, -272, -16), FVector(272, 272, 0), InnerMat));

	// Ceiling: full footprint, thin in Z
	// min(-272, -272, 256) max(272, 272, 272)
	World.Children.Add(BuildAABBSolid(SolidId++, SideId,
		FVector(-272, -272, 256), FVector(272, 272, 272), InnerMat));

	// North wall (+Y): full X extent, between floor and ceiling
	// min(-272, 256, 0) max(272, 272, 256)
	World.Children.Add(BuildAABBSolid(SolidId++, SideId,
		FVector(-272, 256, 0), FVector(272, 272, 256), InnerMat));

	// South wall (-Y): full X extent, between floor and ceiling
	// min(-272, -272, 0) max(272, -256, 256)
	World.Children.Add(BuildAABBSolid(SolidId++, SideId,
		FVector(-272, -272, 0), FVector(272, -256, 256), InnerMat));

	// East wall (+X): fits between N/S walls
	// min(256, -256, 0) max(272, 256, 256)
	World.Children.Add(BuildAABBSolid(SolidId++, SideId,
		FVector(256, -256, 0), FVector(272, 256, 256), InnerMat));

	// West wall (-X): fits between N/S walls
	// min(-272, -256, 0) max(-256, 256, 256)
	World.Children.Add(BuildAABBSolid(SolidId++, SideId,
		FVector(-272, -256, 0), FVector(-256, 256, 256), InnerMat));

	Result += World.Serialize();

	// Entities: player spawns + light
	int32 EntityId = SolidId;  // continue IDs after solids

	// T spawn
	FVMFKeyValues TSpawn(TEXT("entity"));
	TSpawn.AddProperty(TEXT("id"), EntityId++);
	TSpawn.AddProperty(TEXT("classname"), TEXT("info_player_terrorist"));
	TSpawn.AddProperty(TEXT("origin"), TEXT("0 -64 1"));
	TSpawn.AddProperty(TEXT("angles"), TEXT("0 90 0"));
	Result += TSpawn.Serialize();

	// CT spawn
	FVMFKeyValues CTSpawn(TEXT("entity"));
	CTSpawn.AddProperty(TEXT("id"), EntityId++);
	CTSpawn.AddProperty(TEXT("classname"), TEXT("info_player_counterterrorist"));
	CTSpawn.AddProperty(TEXT("origin"), TEXT("0 64 1"));
	CTSpawn.AddProperty(TEXT("angles"), TEXT("0 270 0"));
	Result += CTSpawn.Serialize();

	// Point light at center ceiling
	FVMFKeyValues Light(TEXT("entity"));
	Light.AddProperty(TEXT("id"), EntityId++);
	Light.AddProperty(TEXT("classname"), TEXT("light"));
	Light.AddProperty(TEXT("origin"), TEXT("0 0 200"));
	Light.AddProperty(TEXT("_light"), TEXT("255 255 255 300"));
	Light.AddProperty(TEXT("_quadratic_attn"), TEXT("1"));
	Light.AddProperty(TEXT("style"), TEXT("0"));
	Result += Light.Serialize();

	// Footer blocks
	Result += BuildCameras().Serialize();
	Result += BuildCordon().Serialize();

	return Result;
}

FVMFKeyValues FVMFExporter::BuildAABBSolid(
	int32 SolidId,
	int32& SideIdCounter,
	const FVector& Min,
	const FVector& Max,
	const FString& Material)
{
	FVMFKeyValues Solid(TEXT("solid"));
	Solid.AddProperty(TEXT("id"), SolidId);

	// An axis-aligned box has 6 faces.
	// VMF convention: (P2-P1)x(P3-P1) must point INWARD into the solid.
	// Points appear clockwise when viewed from outside the solid.

	double x1 = Min.X, y1 = Min.Y, z1 = Min.Z;
	double x2 = Max.X, y2 = Max.Y, z2 = Max.Z;

	FString UAxis, VAxis;

	// Top face (z=z2, inward normal = -Z)
	FString TopPlane = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x1, y2, z2,  x2, y2, z2,  x2, y1, z2);
	GetDefaultUVAxes(FVector(0, 0, 1), UAxis, VAxis);
	Solid.Children.Add(BuildSide(SideIdCounter++, TopPlane, Material, UAxis, VAxis));

	// Bottom face (z=z1, inward normal = +Z)
	FString BottomPlane = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x1, y1, z1,  x2, y1, z1,  x2, y2, z1);
	GetDefaultUVAxes(FVector(0, 0, -1), UAxis, VAxis);
	Solid.Children.Add(BuildSide(SideIdCounter++, BottomPlane, Material, UAxis, VAxis));

	// Front face (y=y2, inward normal = -Y)
	FString FrontPlane = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x2, y2, z2,  x1, y2, z2,  x1, y2, z1);
	GetDefaultUVAxes(FVector(0, 1, 0), UAxis, VAxis);
	Solid.Children.Add(BuildSide(SideIdCounter++, FrontPlane, Material, UAxis, VAxis));

	// Back face (y=y1, inward normal = +Y)
	FString BackPlane = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x2, y1, z1,  x1, y1, z1,  x1, y1, z2);
	GetDefaultUVAxes(FVector(0, -1, 0), UAxis, VAxis);
	Solid.Children.Add(BuildSide(SideIdCounter++, BackPlane, Material, UAxis, VAxis));

	// Right face (x=x2, inward normal = -X)
	FString RightPlane = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x2, y2, z1,  x2, y1, z1,  x2, y1, z2);
	GetDefaultUVAxes(FVector(1, 0, 0), UAxis, VAxis);
	Solid.Children.Add(BuildSide(SideIdCounter++, RightPlane, Material, UAxis, VAxis));

	// Left face (x=x1, inward normal = +X)
	FString LeftPlane = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x1, y2, z2,  x1, y1, z2,  x1, y1, z1);
	GetDefaultUVAxes(FVector(-1, 0, 0), UAxis, VAxis);
	Solid.Children.Add(BuildSide(SideIdCounter++, LeftPlane, Material, UAxis, VAxis));

	return Solid;
}

FVMFKeyValues FVMFExporter::BuildSide(
	int32 SideId,
	const FString& PlaneStr,
	const FString& Material,
	const FString& UAxis,
	const FString& VAxis)
{
	FVMFKeyValues Side(TEXT("side"));
	Side.AddProperty(TEXT("id"), SideId);
	Side.AddProperty(TEXT("plane"), PlaneStr);
	Side.AddProperty(TEXT("material"), Material);
	Side.AddProperty(TEXT("uaxis"), UAxis);
	Side.AddProperty(TEXT("vaxis"), VAxis);
	Side.AddProperty(TEXT("rotation"), 0);
	Side.AddProperty(TEXT("lightmapscale"), 16);
	Side.AddProperty(TEXT("smoothing_groups"), 0);
	return Side;
}

void FVMFExporter::GetDefaultUVAxes(const FVector& Normal, FString& OutUAxis, FString& OutVAxis)
{
	// Default texture axes for axis-aligned faces at 0.25 scale (standard Source default).
	// These match what Hammer generates for new brushes.
	if (FMath::Abs(Normal.Z) > 0.5)
	{
		// Top or bottom face: U = +X, V = -Y
		OutUAxis = TEXT("[1 0 0 0] 0.25");
		OutVAxis = TEXT("[0 -1 0 0] 0.25");
	}
	else if (FMath::Abs(Normal.Y) > 0.5)
	{
		// Front or back face: U = +X, V = -Z
		OutUAxis = TEXT("[1 0 0 0] 0.25");
		OutVAxis = TEXT("[0 0 -1 0] 0.25");
	}
	else
	{
		// Left or right face: U = +Y, V = -Z
		OutUAxis = TEXT("[0 1 0 0] 0.25");
		OutVAxis = TEXT("[0 0 -1 0] 0.25");
	}
}

FVMFKeyValues FVMFExporter::BuildVersionInfo()
{
	FVMFKeyValues Node(TEXT("versioninfo"));
	Node.AddProperty(TEXT("editorversion"), 400);
	Node.AddProperty(TEXT("editorbuild"), 8973);
	Node.AddProperty(TEXT("mapversion"), 1);
	Node.AddProperty(TEXT("formatversion"), 100);
	Node.AddProperty(TEXT("prefab"), 0);
	return Node;
}

FVMFKeyValues FVMFExporter::BuildVisGroups()
{
	return FVMFKeyValues(TEXT("visgroups"));
}

FVMFKeyValues FVMFExporter::BuildViewSettings()
{
	FVMFKeyValues Node(TEXT("viewsettings"));
	Node.AddProperty(TEXT("bSnapToGrid"), 1);
	Node.AddProperty(TEXT("bShowGrid"), 1);
	Node.AddProperty(TEXT("bShowLogicalGrid"), 0);
	Node.AddProperty(TEXT("nGridSpacing"), 64);
	return Node;
}

FVMFKeyValues FVMFExporter::BuildCameras()
{
	FVMFKeyValues Node(TEXT("cameras"));
	Node.AddProperty(TEXT("activecamera"), -1);
	return Node;
}

FVMFKeyValues FVMFExporter::BuildCordon()
{
	FVMFKeyValues Node(TEXT("cordon"));
	Node.AddProperty(TEXT("mins"), TEXT("(-1024 -1024 -1024)"));
	Node.AddProperty(TEXT("maxs"), TEXT("(1024 1024 1024)"));
	Node.AddProperty(TEXT("active"), 0);
	return Node;
}

void FVMFExporter::ExportBrushEntities(
	const TArray<FSourceEntity>& Entities,
	int32& EntityIdCounter,
	int32& SolidIdCounter,
	int32& SideIdCounter,
	const FMaterialMapper& MatMapper,
	FString& Result)
{
	for (const FSourceEntity& Entity : Entities)
	{
		// Worldspawn solids are injected directly into the world block — skip here
		if (Entity.ClassName == TEXT("worldspawn"))
		{
			continue;
		}

		if (!Entity.bIsBrushEntity || !Entity.SourceActor.IsValid())
		{
			continue;
		}

		AActor* Actor = Entity.SourceActor.Get();

		// Handle ASourceBrushEntity (palette-spawned and imported brush entities)
		ASourceBrushEntity* SourceBrush = Cast<ASourceBrushEntity>(Actor);
		if (SourceBrush && SourceBrush->StoredBrushData.Num() > 0)
		{
			Result += FEntityExporter::BrushEntityToVMF(Entity, EntityIdCounter++, SourceBrush).Serialize();
			continue;
		}

		ABrush* BrushActor = Cast<ABrush>(Actor);
		if (!BrushActor)
		{
			// Non-brush actor tagged as brush entity — fallback to point entity
			Result += FEntityExporter::EntityToVMF(Entity, EntityIdCounter++).Serialize();
			continue;
		}

		// Determine default material for brush faces based on entity type
		FString DefaultMaterial = TEXT("TOOLS/TOOLSTRIGGER");
		if (Entity.ClassName.Contains(TEXT("water")))
		{
			// Check for water material stored as internal keyvalue
			for (const auto& KV : Entity.KeyValues)
			{
				if (KV.Key == TEXT("_water_material"))
				{
					DefaultMaterial = KV.Value;
					break;
				}
			}
		}
		else if (Entity.ClassName.StartsWith(TEXT("func_")))
		{
			DefaultMaterial = TEXT("DEV/DEV_MEASUREWALL01A");
		}

		// Convert UE brush geometry to VMF solids
		FBrushConversionResult ConvResult = FBrushConverter::ConvertBrush(
			BrushActor, SolidIdCounter, SideIdCounter, &MatMapper,
			DefaultMaterial);

		if (ConvResult.Solids.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("SourceBridge: Brush entity '%s' (%s) has no convertible geometry, exporting as point entity."),
				*Entity.TargetName, *Entity.ClassName);
			Result += FEntityExporter::EntityToVMF(Entity, EntityIdCounter++).Serialize();
			continue;
		}

		// Build the brush entity with embedded solids
		FVMFKeyValues BrushEntity(TEXT("entity"));
		BrushEntity.AddProperty(TEXT("id"), EntityIdCounter++);
		BrushEntity.AddProperty(TEXT("classname"), Entity.ClassName);

		if (!Entity.TargetName.IsEmpty())
		{
			BrushEntity.AddProperty(TEXT("targetname"), Entity.TargetName);
		}

		// Key-values (skip internal markers like _water_material)
		for (const auto& KV : Entity.KeyValues)
		{
			if (!KV.Key.StartsWith(TEXT("_")))
			{
				BrushEntity.AddProperty(KV.Key, KV.Value);
			}
		}

		// I/O connections
		if (Entity.Connections.Num() > 0)
		{
			FVMFKeyValues& ConnBlock = BrushEntity.AddChild(TEXT("connections"));
			for (const FEntityIOConnection& Conn : Entity.Connections)
			{
				ConnBlock.AddProperty(Conn.OutputName, Conn.FormatValue());
			}
		}

		// Embed solid geometry
		for (FVMFKeyValues& Solid : ConvResult.Solids)
		{
			BrushEntity.Children.Add(MoveTemp(Solid));
		}

		Result += BrushEntity.Serialize();
	}
}
