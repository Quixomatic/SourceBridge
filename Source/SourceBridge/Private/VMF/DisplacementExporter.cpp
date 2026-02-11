#include "VMF/DisplacementExporter.h"
#include "VMF/VMFExporter.h"
#include "Utilities/SourceCoord.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeDataAccess.h"
#include "Engine/World.h"
#include "EngineUtils.h"

TArray<FDisplacementData> FDisplacementExporter::ExportLandscapes(
	UWorld* World,
	int32& SolidIdCounter,
	int32& SideIdCounter,
	const FDisplacementSettings& Settings)
{
	TArray<FDisplacementData> AllDisplacements;

	if (!World) return AllDisplacements;

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Landscape = *It;
		if (!Landscape) continue;

		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Exporting landscape %s (%d components)"),
			*Landscape->GetName(), Landscape->LandscapeComponents.Num());

		for (ULandscapeComponent* Component : Landscape->LandscapeComponents)
		{
			if (!Component) continue;

			TArray<FDisplacementData> CompDisps = ExportLandscapeComponent(
				Component, SolidIdCounter, SideIdCounter, Settings);
			AllDisplacements.Append(CompDisps);
		}
	}

	if (AllDisplacements.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Exported %d displacement surfaces from landscapes."),
			AllDisplacements.Num());
	}

	return AllDisplacements;
}

TArray<FDisplacementData> FDisplacementExporter::ExportLandscapeComponent(
	ULandscapeComponent* Component,
	int32& SolidIdCounter,
	int32& SideIdCounter,
	const FDisplacementSettings& Settings)
{
	TArray<FDisplacementData> Results;

	if (!Component) return Results;

	int32 GridSize = (1 << Settings.Power) + 1; // 5, 9, or 17

	// Sample heights from the landscape component
	TArray<TArray<float>> Heights;
	float MinHeight, MaxHeight;
	SampleLandscapeHeights(Component, GridSize, Heights, MinHeight, MaxHeight);

	if (Heights.Num() == 0) return Results;

	// Get component world bounds
	FBoxSphereBounds Bounds = Component->CalcBounds(Component->GetComponentTransform());
	FVector WorldMin = Bounds.Origin - Bounds.BoxExtent;
	FVector WorldMax = Bounds.Origin + Bounds.BoxExtent;

	// Convert to Source coordinates
	FVector SrcMin = FSourceCoord::UEToSource(FVector(WorldMin.X, WorldMin.Y, MinHeight));
	FVector SrcMax = FSourceCoord::UEToSource(FVector(WorldMax.X, WorldMax.Y, MinHeight));

	// Ensure min < max in Source coords (Y is negated)
	FVector BrushMin(
		FMath::Min(SrcMin.X, SrcMax.X),
		FMath::Min(SrcMin.Y, SrcMax.Y),
		FMath::Min(SrcMin.Z, SrcMax.Z) - 16.0f // Brush extends 16 units below base
	);
	FVector BrushMax(
		FMath::Max(SrcMin.X, SrcMax.X),
		FMath::Max(SrcMin.Y, SrcMax.Y),
		FMath::Max(SrcMin.Z, SrcMax.Z)
	);

	float BaseHeight = BrushMax.Z;

	// Build the displacement
	FDisplacementData DispData;
	DispData.Power = Settings.Power;
	DispData.GridSize = GridSize;

	SolidIdCounter++;
	DispData.BrushSolid = BuildDisplacementBrush(
		SolidIdCounter, SideIdCounter,
		BrushMin, BrushMax,
		Settings.Material,
		BaseHeight);

	FVector StartPos(BrushMin.X, BrushMin.Y, BaseHeight);
	DispData.DispInfo = BuildDispInfo(Heights, BaseHeight, StartPos, Settings.Power);

	Results.Add(DispData);

	return Results;
}

FVMFKeyValues FDisplacementExporter::BuildDispInfo(
	const TArray<TArray<float>>& Heights,
	float BaseHeight,
	const FVector& StartPos,
	int32 Power)
{
	FVMFKeyValues DispInfo;
	DispInfo.ClassName = TEXT("dispinfo");
	DispInfo.Properties.Add(TPair<FString, FString>(TEXT("power"), FString::FromInt(Power)));
	DispInfo.Properties.Add(TPair<FString, FString>(TEXT("startposition"),
		FString::Printf(TEXT("[%g %g %g]"), StartPos.X, StartPos.Y, StartPos.Z)));
	DispInfo.Properties.Add(TPair<FString, FString>(TEXT("elevation"), TEXT("0")));
	DispInfo.Properties.Add(TPair<FString, FString>(TEXT("subdiv"), TEXT("0")));

	int32 GridSize = Heights.Num();

	// Build normals block (all pointing up for terrain)
	FVMFKeyValues Normals;
	Normals.ClassName = TEXT("normals");
	for (int32 Row = 0; Row < GridSize; Row++)
	{
		FString RowStr;
		for (int32 Col = 0; Col < GridSize; Col++)
		{
			if (Col > 0) RowStr += TEXT(" ");
			RowStr += TEXT("0 0 1"); // Default up normal
		}
		Normals.Properties.Add(TPair<FString, FString>(
			FString::Printf(TEXT("row%d"), Row), RowStr));
	}
	DispInfo.Children.Add(Normals);

	// Build distances block
	FVMFKeyValues Distances;
	Distances.ClassName = TEXT("distances");
	for (int32 Row = 0; Row < GridSize; Row++)
	{
		FString RowStr;
		for (int32 Col = 0; Col < GridSize; Col++)
		{
			if (Col > 0) RowStr += TEXT(" ");
			float Distance = Heights[Row][Col] - BaseHeight;
			RowStr += FString::Printf(TEXT("%g"), Distance);
		}
		Distances.Properties.Add(TPair<FString, FString>(
			FString::Printf(TEXT("row%d"), Row), RowStr));
	}
	DispInfo.Children.Add(Distances);

	// Build offsets block (all zero for simple heightmap)
	FVMFKeyValues Offsets;
	Offsets.ClassName = TEXT("offsets");
	for (int32 Row = 0; Row < GridSize; Row++)
	{
		FString RowStr;
		for (int32 Col = 0; Col < GridSize; Col++)
		{
			if (Col > 0) RowStr += TEXT(" ");
			RowStr += TEXT("0 0 0");
		}
		Offsets.Properties.Add(TPair<FString, FString>(
			FString::Printf(TEXT("row%d"), Row), RowStr));
	}
	DispInfo.Children.Add(Offsets);

	// Build offset_normals block (all zero)
	FVMFKeyValues OffsetNormals;
	OffsetNormals.ClassName = TEXT("offset_normals");
	for (int32 Row = 0; Row < GridSize; Row++)
	{
		FString RowStr;
		for (int32 Col = 0; Col < GridSize; Col++)
		{
			if (Col > 0) RowStr += TEXT(" ");
			RowStr += TEXT("0 0 1");
		}
		OffsetNormals.Properties.Add(TPair<FString, FString>(
			FString::Printf(TEXT("row%d"), Row), RowStr));
	}
	DispInfo.Children.Add(OffsetNormals);

	// Build alphas block (all zero for single material)
	FVMFKeyValues Alphas;
	Alphas.ClassName = TEXT("alphas");
	for (int32 Row = 0; Row < GridSize; Row++)
	{
		FString RowStr;
		for (int32 Col = 0; Col < GridSize; Col++)
		{
			if (Col > 0) RowStr += TEXT(" ");
			RowStr += TEXT("0");
		}
		Alphas.Properties.Add(TPair<FString, FString>(
			FString::Printf(TEXT("row%d"), Row), RowStr));
	}
	DispInfo.Children.Add(Alphas);

	// Triangle tags (required, all 0)
	FVMFKeyValues TriangleTags;
	TriangleTags.ClassName = TEXT("triangle_tags");
	int32 TriRows = GridSize - 1;
	for (int32 Row = 0; Row < TriRows; Row++)
	{
		FString RowStr;
		int32 TriCols = (GridSize - 1) * 2; // Two triangles per quad
		for (int32 Col = 0; Col < TriCols; Col++)
		{
			if (Col > 0) RowStr += TEXT(" ");
			RowStr += TEXT("0");
		}
		TriangleTags.Properties.Add(TPair<FString, FString>(
			FString::Printf(TEXT("row%d"), Row), RowStr));
	}
	DispInfo.Children.Add(TriangleTags);

	// Allowed verts (all allowed by default)
	FVMFKeyValues AllowedVerts;
	AllowedVerts.ClassName = TEXT("allowed_verts");
	AllowedVerts.Properties.Add(TPair<FString, FString>(
		TEXT("10"), TEXT("-1 -1 -1 -1 -1 -1 -1 -1 -1 -1")));
	DispInfo.Children.Add(AllowedVerts);

	return DispInfo;
}

FVMFKeyValues FDisplacementExporter::BuildDisplacementBrush(
	int32 SolidId,
	int32& SideIdCounter,
	const FVector& Min,
	const FVector& Max,
	const FString& Material,
	float BaseHeight)
{
	// Build a standard AABB brush - the top face will get the displacement
	return FVMFExporter::BuildAABBSolid(SolidId, SideIdCounter, Min, Max, Material);
}

void FDisplacementExporter::SampleLandscapeHeights(
	ULandscapeComponent* Component,
	int32 GridSize,
	TArray<TArray<float>>& OutHeights,
	float& OutMinHeight,
	float& OutMaxHeight)
{
	OutMinHeight = MAX_FLT;
	OutMaxHeight = -MAX_FLT;

	if (!Component) return;

	// Get the component's heightmap data
	FLandscapeComponentDataInterface DataInterface(Component);
	int32 CompSize = Component->ComponentSizeQuads + 1;

	if (CompSize <= 0) return;

	// Sample heights at grid positions
	OutHeights.SetNum(GridSize);
	for (int32 Row = 0; Row < GridSize; Row++)
	{
		OutHeights[Row].SetNum(GridSize);
	}

	FTransform ComponentTransform = Component->GetComponentTransform();

	for (int32 Row = 0; Row < GridSize; Row++)
	{
		for (int32 Col = 0; Col < GridSize; Col++)
		{
			// Map grid position to landscape position
			float U = (float)Col / (float)(GridSize - 1);
			float V = (float)Row / (float)(GridSize - 1);

			int32 LandX = FMath::Clamp(FMath::RoundToInt(U * (CompSize - 1)), 0, CompSize - 1);
			int32 LandY = FMath::Clamp(FMath::RoundToInt(V * (CompSize - 1)), 0, CompSize - 1);

			// Get world position from landscape data
			FVector WorldPos;
			DataInterface.GetWorldPositionTangentVectors(LandX, LandY, WorldPos);

			// Convert to Source coordinates
			FVector SrcPos = FSourceCoord::UEToSource(WorldPos);
			float Height = SrcPos.Z;

			OutHeights[Row][Col] = Height;
			OutMinHeight = FMath::Min(OutMinHeight, Height);
			OutMaxHeight = FMath::Max(OutMaxHeight, Height);
		}
	}
}
