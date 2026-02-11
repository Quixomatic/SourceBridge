#include "VMF/VMFExporter.h"
#include "VMF/BrushConverter.h"
#include "Materials/MaterialMapper.h"
#include "Utilities/SourceCoord.h"
#include "Engine/Brush.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Volume.h"

FString FVMFExporter::ExportScene(UWorld* World)
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

	// Build world block
	FVMFKeyValues WorldNode(TEXT("world"));
	WorldNode.AddProperty(TEXT("id"), 1);
	WorldNode.AddProperty(TEXT("mapversion"), 1);
	WorldNode.AddProperty(TEXT("classname"), TEXT("worldspawn"));
	WorldNode.AddProperty(TEXT("skyname"), TEXT("sky_day01_01"));
	WorldNode.AddProperty(TEXT("maxpropscreenwidth"), -1);
	WorldNode.AddProperty(TEXT("detailvbsp"), TEXT("detail.vbsp"));
	WorldNode.AddProperty(TEXT("detailmaterial"), TEXT("detail/detailsprites"));

	int32 SolidIdCounter = 2;  // worldspawn is id 1
	int32 SideIdCounter = 1;
	int32 BrushCount = 0;
	int32 SkippedCount = 0;

	// Material mapper resolves UE materials to Source material paths
	FMaterialMapper MatMapper;

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

		FBrushConversionResult ConvResult = FBrushConverter::ConvertBrush(
			Brush, SolidIdCounter, SideIdCounter, &MatMapper);

		for (const FString& Warning : ConvResult.Warnings)
		{
			UE_LOG(LogTemp, Warning, TEXT("SourceBridge: %s"), *Warning);
		}

		for (FVMFKeyValues& Solid : ConvResult.Solids)
		{
			WorldNode.Children.Add(MoveTemp(Solid));
			BrushCount++;
		}

		if (ConvResult.Solids.Num() == 0 && ConvResult.Warnings.Num() > 0)
		{
			SkippedCount++;
		}
	}

	Result += WorldNode.Serialize();

	Result += BuildCameras().Serialize();
	Result += BuildCordon().Serialize();

	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Exported %d brushes (%d skipped) to VMF."),
		BrushCount, SkippedCount);

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
	// Plane points must be wound so (P2-P1)x(P3-P1) gives the outward normal.

	double x1 = Min.X, y1 = Min.Y, z1 = Min.Z;
	double x2 = Max.X, y2 = Max.Y, z2 = Max.Z;

	FString UAxis, VAxis;

	// Top face (+Z normal)
	FString TopPlane = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x1, y1, z2,  x2, y1, z2,  x2, y2, z2);
	GetDefaultUVAxes(FVector(0, 0, 1), UAxis, VAxis);
	Solid.Children.Add(BuildSide(SideIdCounter++, TopPlane, Material, UAxis, VAxis));

	// Bottom face (-Z normal)
	FString BottomPlane = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x1, y2, z1,  x2, y2, z1,  x2, y1, z1);
	GetDefaultUVAxes(FVector(0, 0, -1), UAxis, VAxis);
	Solid.Children.Add(BuildSide(SideIdCounter++, BottomPlane, Material, UAxis, VAxis));

	// Front face (+Y normal)
	FString FrontPlane = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x1, y2, z2,  x2, y2, z2,  x2, y2, z1);
	GetDefaultUVAxes(FVector(0, 1, 0), UAxis, VAxis);
	Solid.Children.Add(BuildSide(SideIdCounter++, FrontPlane, Material, UAxis, VAxis));

	// Back face (-Y normal)
	FString BackPlane = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x1, y1, z1,  x2, y1, z1,  x2, y1, z2);
	GetDefaultUVAxes(FVector(0, -1, 0), UAxis, VAxis);
	Solid.Children.Add(BuildSide(SideIdCounter++, BackPlane, Material, UAxis, VAxis));

	// Right face (+X normal)
	FString RightPlane = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x2, y1, z1,  x2, y2, z1,  x2, y2, z2);
	GetDefaultUVAxes(FVector(1, 0, 0), UAxis, VAxis);
	Solid.Children.Add(BuildSide(SideIdCounter++, RightPlane, Material, UAxis, VAxis));

	// Left face (-X normal)
	FString LeftPlane = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x1, y1, z2,  x1, y2, z2,  x1, y2, z1);
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
