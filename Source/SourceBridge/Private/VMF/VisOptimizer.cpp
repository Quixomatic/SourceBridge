#include "VMF/VisOptimizer.h"
#include "VMF/VMFExporter.h"
#include "VMF/BrushConverter.h"
#include "Utilities/SourceCoord.h"
#include "Engine/Brush.h"
#include "Engine/World.h"
#include "GameFramework/Volume.h"
#include "EngineUtils.h"

bool FVisOptimizer::IsHintBrush(const ABrush* Brush)
{
	if (!Brush) return false;
	for (const FName& Tag : Brush->Tags)
	{
		if (Tag.ToString().Equals(TEXT("hint"), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

bool FVisOptimizer::IsAreaPortal(const ABrush* Brush)
{
	if (!Brush) return false;
	for (const FName& Tag : Brush->Tags)
	{
		FString TagStr = Tag.ToString();
		if (TagStr.Equals(TEXT("func_areaportal"), ESearchCase::IgnoreCase) ||
			TagStr.Equals(TEXT("classname:func_areaportal"), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

bool FVisOptimizer::IsVisCluster(const ABrush* Brush)
{
	if (!Brush) return false;
	for (const FName& Tag : Brush->Tags)
	{
		FString TagStr = Tag.ToString();
		if (TagStr.Equals(TEXT("func_viscluster"), ESearchCase::IgnoreCase) ||
			TagStr.Equals(TEXT("classname:func_viscluster"), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

FVMFKeyValues FVisOptimizer::GenerateHintBrush(
	int32& SolidIdCounter,
	int32& SideIdCounter,
	const FVector& Center,
	const FVector& HalfExtent,
	const FVector& Normal)
{
	// A hint brush is an AABB where:
	// - The face whose outward normal most closely matches Normal gets TOOLS/TOOLSHINT
	// - All other faces get TOOLS/TOOLSSKIP
	FVMFKeyValues Solid(TEXT("solid"));
	Solid.AddProperty(TEXT("id"), SolidIdCounter++);

	const FString HintMat = TEXT("TOOLS/TOOLSHINT");
	const FString SkipMat = TEXT("TOOLS/TOOLSSKIP");

	double x1 = Center.X - HalfExtent.X, y1 = Center.Y - HalfExtent.Y, z1 = Center.Z - HalfExtent.Z;
	double x2 = Center.X + HalfExtent.X, y2 = Center.Y + HalfExtent.Y, z2 = Center.Z + HalfExtent.Z;

	// 6 face normals and their plane strings
	struct FFaceInfo
	{
		FVector FaceNormal;
		FString PlaneStr;
	};

	TArray<FFaceInfo> Faces;

	// Top (+Z)
	Faces.Add({FVector(0, 0, 1), FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x1, y1, z2, x2, y1, z2, x2, y2, z2)});
	// Bottom (-Z)
	Faces.Add({FVector(0, 0, -1), FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x1, y2, z1, x2, y2, z1, x2, y1, z1)});
	// Front (+Y)
	Faces.Add({FVector(0, 1, 0), FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x1, y2, z2, x2, y2, z2, x2, y2, z1)});
	// Back (-Y)
	Faces.Add({FVector(0, -1, 0), FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x1, y1, z1, x2, y1, z1, x2, y1, z2)});
	// Right (+X)
	Faces.Add({FVector(1, 0, 0), FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x2, y1, z1, x2, y2, z1, x2, y2, z2)});
	// Left (-X)
	Faces.Add({FVector(-1, 0, 0), FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
		x1, y1, z2, x1, y2, z2, x1, y2, z1)});

	FVector NormalNorm = Normal.GetSafeNormal();
	if (NormalNorm.IsNearlyZero()) NormalNorm = FVector(1, 0, 0);

	for (const FFaceInfo& Face : Faces)
	{
		float Dot = FVector::DotProduct(Face.FaceNormal, NormalNorm);

		// The face most aligned with the hint normal gets TOOLSHINT
		// Its opposite face also gets TOOLSHINT (hint brushes cut both ways)
		FString Material = (FMath::Abs(Dot) > 0.9f) ? HintMat : SkipMat;

		FString UAxis, VAxis;
		// Default UV axes based on face normal
		if (FMath::Abs(Face.FaceNormal.Z) > 0.5)
		{
			UAxis = TEXT("[1 0 0 0] 0.25");
			VAxis = TEXT("[0 -1 0 0] 0.25");
		}
		else if (FMath::Abs(Face.FaceNormal.Y) > 0.5)
		{
			UAxis = TEXT("[1 0 0 0] 0.25");
			VAxis = TEXT("[0 0 -1 0] 0.25");
		}
		else
		{
			UAxis = TEXT("[0 1 0 0] 0.25");
			VAxis = TEXT("[0 0 -1 0] 0.25");
		}

		FVMFKeyValues Side(TEXT("side"));
		Side.AddProperty(TEXT("id"), SideIdCounter++);
		Side.AddProperty(TEXT("plane"), Face.PlaneStr);
		Side.AddProperty(TEXT("material"), Material);
		Side.AddProperty(TEXT("uaxis"), UAxis);
		Side.AddProperty(TEXT("vaxis"), VAxis);
		Side.AddProperty(TEXT("rotation"), 0);
		Side.AddProperty(TEXT("lightmapscale"), 16);
		Side.AddProperty(TEXT("smoothing_groups"), 0);
		Solid.Children.Add(MoveTemp(Side));
	}

	return Solid;
}

TArray<FVMFKeyValues> FVisOptimizer::ExportHintBrushes(
	UWorld* World,
	int32& SolidIdCounter,
	int32& SideIdCounter)
{
	TArray<FVMFKeyValues> HintSolids;

	if (!World) return HintSolids;

	for (TActorIterator<ABrush> It(World); It; ++It)
	{
		ABrush* Brush = *It;

		if (Brush == World->GetDefaultBrush()) continue;
		if (!IsHintBrush(Brush)) continue;

		// Determine the hint direction from an optional "hint_dir" tag
		// Default: use the longest axis of the brush bounding box
		FVector HintNormal = FVector(1, 0, 0);

		for (const FName& Tag : Brush->Tags)
		{
			FString TagStr = Tag.ToString();
			if (TagStr.Equals(TEXT("hint_x"), ESearchCase::IgnoreCase))
			{
				HintNormal = FVector(1, 0, 0);
			}
			else if (TagStr.Equals(TEXT("hint_y"), ESearchCase::IgnoreCase))
			{
				HintNormal = FVector(0, 1, 0);
			}
			else if (TagStr.Equals(TEXT("hint_z"), ESearchCase::IgnoreCase))
			{
				HintNormal = FVector(0, 0, 1);
			}
		}

		// Get the brush bounds and convert to Source coordinates
		FBoxSphereBounds Bounds = Brush->GetComponentsBoundingBox();
		FVector UEMin = Bounds.Origin - Bounds.BoxExtent;
		FVector UEMax = Bounds.Origin + Bounds.BoxExtent;

		FVector SourceMin = FSourceCoord::ConvertPosition(UEMin);
		FVector SourceMax = FSourceCoord::ConvertPosition(UEMax);

		// Ensure min < max after coordinate conversion (Y negation may swap)
		FVector FinalMin(
			FMath::Min(SourceMin.X, SourceMax.X),
			FMath::Min(SourceMin.Y, SourceMax.Y),
			FMath::Min(SourceMin.Z, SourceMax.Z));
		FVector FinalMax(
			FMath::Max(SourceMin.X, SourceMax.X),
			FMath::Max(SourceMin.Y, SourceMax.Y),
			FMath::Max(SourceMin.Z, SourceMax.Z));

		FVector Center = (FinalMin + FinalMax) * 0.5f;
		FVector HalfExtent = (FinalMax - FinalMin) * 0.5f;

		// If no explicit direction, auto-detect: use the thinnest axis as the hint normal
		bool bHasExplicitDir = false;
		for (const FName& Tag : Brush->Tags)
		{
			FString TagStr = Tag.ToString();
			if (TagStr.StartsWith(TEXT("hint_x"), ESearchCase::IgnoreCase) ||
				TagStr.StartsWith(TEXT("hint_y"), ESearchCase::IgnoreCase) ||
				TagStr.StartsWith(TEXT("hint_z"), ESearchCase::IgnoreCase))
			{
				bHasExplicitDir = true;
				break;
			}
		}

		if (!bHasExplicitDir)
		{
			// The thinnest dimension is the cutting direction
			if (HalfExtent.X <= HalfExtent.Y && HalfExtent.X <= HalfExtent.Z)
			{
				HintNormal = FVector(1, 0, 0);
			}
			else if (HalfExtent.Y <= HalfExtent.X && HalfExtent.Y <= HalfExtent.Z)
			{
				HintNormal = FVector(0, 1, 0);
			}
			else
			{
				HintNormal = FVector(0, 0, 1);
			}
		}

		// Convert hint normal for coordinate system
		// In Source coords, Y is negated relative to UE
		FVector SourceNormal = HintNormal;
		SourceNormal.Y = -SourceNormal.Y;

		HintSolids.Add(GenerateHintBrush(
			SolidIdCounter, SideIdCounter, Center, HalfExtent, SourceNormal));

		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Exported hint brush at (%g, %g, %g) normal (%g, %g, %g)"),
			Center.X, Center.Y, Center.Z,
			SourceNormal.X, SourceNormal.Y, SourceNormal.Z);
	}

	return HintSolids;
}

TArray<FVisOptSuggestion> FVisOptimizer::AnalyzeWorld(UWorld* World)
{
	TArray<FVisOptSuggestion> Suggestions;

	if (!World) return Suggestions;

	// Collect all structural brush bounding boxes
	struct FBrushInfo
	{
		FBox Bounds;
		FVector Center;
		FVector Extent;
	};

	TArray<FBrushInfo> Brushes;

	for (TActorIterator<ABrush> It(World); It; ++It)
	{
		ABrush* Brush = *It;
		if (Brush == World->GetDefaultBrush()) continue;
		if (Brush->IsA<AVolume>()) continue;
		if (IsHintBrush(Brush) || IsAreaPortal(Brush) || IsVisCluster(Brush)) continue;

		FBoxSphereBounds BSB = Brush->GetComponentsBoundingBox();
		FBrushInfo Info;
		Info.Bounds = FBox(BSB.Origin - BSB.BoxExtent, BSB.Origin + BSB.BoxExtent);
		Info.Center = BSB.Origin;
		Info.Extent = BSB.BoxExtent;
		Brushes.Add(Info);
	}

	if (Brushes.Num() < 2) return Suggestions;

	// Compute overall scene bounds
	FBox SceneBounds(ForceInit);
	for (const FBrushInfo& B : Brushes)
	{
		SceneBounds += B.Bounds;
	}

	FVector SceneSize = SceneBounds.GetSize();
	float MaxDim = FMath::Max3(SceneSize.X, SceneSize.Y, SceneSize.Z);

	// Analyze along each axis: find narrow sections between open areas
	// A "narrow section" is where brush coverage is high in a thin slice
	for (int32 Axis = 0; Axis < 3; Axis++)
	{
		float AxisMin = SceneBounds.Min[Axis];
		float AxisMax = SceneBounds.Max[Axis];
		float AxisLen = AxisMax - AxisMin;

		if (AxisLen < 200.0f) continue; // Too small to bother

		// Sample brush density along this axis
		const int32 NumSamples = 32;
		float StepSize = AxisLen / NumSamples;

		TArray<int32> BrushCounts;
		BrushCounts.SetNumZeroed(NumSamples);

		for (const FBrushInfo& B : Brushes)
		{
			int32 StartBucket = FMath::Clamp(
				FMath::FloorToInt((B.Bounds.Min[Axis] - AxisMin) / StepSize), 0, NumSamples - 1);
			int32 EndBucket = FMath::Clamp(
				FMath::FloorToInt((B.Bounds.Max[Axis] - AxisMin) / StepSize), 0, NumSamples - 1);

			for (int32 i = StartBucket; i <= EndBucket; i++)
			{
				BrushCounts[i]++;
			}
		}

		// Find local maxima in density (potential chokepoints for hint brushes)
		for (int32 i = 2; i < NumSamples - 2; i++)
		{
			int32 LocalCount = BrushCounts[i];
			int32 LeftAvg = (BrushCounts[i - 2] + BrushCounts[i - 1]) / 2;
			int32 RightAvg = (BrushCounts[i + 1] + BrushCounts[i + 2]) / 2;

			// A density spike suggests a wall or boundary between areas
			if (LocalCount > LeftAvg + 1 && LocalCount > RightAvg + 1 && LocalCount >= 3)
			{
				FVisOptSuggestion Suggestion;
				Suggestion.Type = FVisOptSuggestion::EType::HintBrush;

				float AxisPos = AxisMin + (i + 0.5f) * StepSize;
				Suggestion.Location = SceneBounds.GetCenter();
				Suggestion.Location[Axis] = AxisPos;

				Suggestion.Extent = SceneSize * 0.5f;
				Suggestion.Extent[Axis] = 8.0f; // Thin slab

				Suggestion.Normal = FVector::ZeroVector;
				Suggestion.Normal[Axis] = 1.0f;

				static const TCHAR* AxisNames[] = {TEXT("X"), TEXT("Y"), TEXT("Z")};
				Suggestion.Description = FString::Printf(
					TEXT("Hint brush along %s axis at %.0f (density spike: %d brushes vs %d/%d neighbors)"),
					AxisNames[Axis], AxisPos, LocalCount, LeftAvg, RightAvg);

				Suggestions.Add(MoveTemp(Suggestion));
			}
		}
	}

	// Detect large open areas (potential func_viscluster candidates)
	// An open area is where the bounding box is large but brush count is low relative to volume
	FVector SceneCenter = SceneBounds.GetCenter();
	float SceneVolume = SceneSize.X * SceneSize.Y * SceneSize.Z;
	float BrushDensity = (float)Brushes.Num() / FMath::Max(SceneVolume, 1.0f);

	// If overall brush density is very low, suggest a viscluster
	if (Brushes.Num() > 4 && SceneVolume > 1e9f && BrushDensity < 1e-8f)
	{
		FVisOptSuggestion Suggestion;
		Suggestion.Type = FVisOptSuggestion::EType::VisCluster;
		Suggestion.Location = SceneCenter;
		Suggestion.Extent = SceneSize * 0.4f; // Cover most of the open area
		Suggestion.Normal = FVector::ZeroVector;
		Suggestion.Description = FString::Printf(
			TEXT("Large open area (%d brushes in %.0f x %.0f x %.0f space) - consider func_viscluster"),
			Brushes.Num(), SceneSize.X, SceneSize.Y, SceneSize.Z);

		Suggestions.Add(MoveTemp(Suggestion));
	}

	return Suggestions;
}
