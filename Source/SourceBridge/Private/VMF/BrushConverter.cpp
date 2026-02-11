#include "VMF/BrushConverter.h"
#include "Materials/MaterialMapper.h"
#include "Utilities/SourceCoord.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Model.h"

FBrushConversionResult FBrushConverter::ConvertBrush(
	ABrush* Brush,
	int32& SolidIdCounter,
	int32& SideIdCounter,
	const FMaterialMapper* MaterialMapper,
	const FString& DefaultMaterial,
	int32 LightmapScale)
{
	FBrushConversionResult Result;

	if (!Brush || !Brush->Brush || !Brush->Brush->Polys)
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("Brush '%s' has no valid model data, skipping."),
			*Brush->GetName()));
		return Result;
	}

	// Check brush type - Source has no subtractive CSG
	if (Brush->BrushType == Brush_Subtract)
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("Brush '%s' is subtractive. Source engine does not support CSG subtraction. Skipping."),
			*Brush->GetName()));
		return Result;
	}

	UModel* Model = Brush->Brush;
	const TArray<FPoly>& Polys = Model->Polys->Element;

	if (Polys.Num() < 4)
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("Brush '%s' has fewer than 4 faces (%d), not a valid solid. Skipping."),
			*Brush->GetName(), Polys.Num()));
		return Result;
	}

	FTransform BrushTransform = Brush->GetActorTransform();

	// Check for per-brush lightmap scale override via tag (e.g., "lightmapscale:8")
	int32 BrushLightmapScale = LightmapScale;
	for (const FName& Tag : Brush->Tags)
	{
		FString TagStr = Tag.ToString();
		if (TagStr.StartsWith(TEXT("lightmapscale:"), ESearchCase::IgnoreCase))
		{
			BrushLightmapScale = FCString::Atoi(*TagStr.Mid(14));
			if (BrushLightmapScale < 1) BrushLightmapScale = 16;
		}
	}

	// First pass: convert all face vertices to Source space for validation
	TArray<TArray<FVector>> AllFaceVerticesSource;
	TArray<FPlane> AllPlanesSource;
	TArray<FVector> AllNormalsSource;
	AllFaceVerticesSource.Reserve(Polys.Num());
	AllPlanesSource.Reserve(Polys.Num());
	AllNormalsSource.Reserve(Polys.Num());

	for (const FPoly& Poly : Polys)
	{
		if (Poly.Vertices.Num() < 3)
		{
			continue;
		}

		TArray<FVector> SourceVerts;
		SourceVerts.Reserve(Poly.Vertices.Num());

		for (const FVector3f& LocalVert : Poly.Vertices)
		{
			FVector WorldVert = BrushTransform.TransformPosition(FVector(LocalVert));
			FVector SourceVert = FSourceCoord::UEToSource(WorldVert);
			SourceVerts.Add(SourceVert);
		}

		// Convert normal to Source space (only direction matters, no translation)
		FVector WorldNormal = BrushTransform.TransformVectorNoScale(FVector(Poly.Normal));
		// For normal conversion: negate Y, no scaling needed (it's a direction)
		FVector SourceNormal(WorldNormal.X, -WorldNormal.Y, WorldNormal.Z);
		SourceNormal.Normalize();

		AllFaceVerticesSource.Add(MoveTemp(SourceVerts));
		AllNormalsSource.Add(SourceNormal);

		// Build plane from first vertex and normal
		if (AllFaceVerticesSource.Last().Num() > 0)
		{
			AllPlanesSource.Add(FPlane(AllFaceVerticesSource.Last()[0], SourceNormal));
		}
	}

	// Validate convexity
	if (!ValidateConvexity(AllPlanesSource, AllFaceVerticesSource))
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("Brush '%s' is non-convex. Source requires convex solids. Skipping."),
			*Brush->GetName()));
		return Result;
	}

	// Build the VMF solid
	FVMFKeyValues Solid(TEXT("solid"));
	Solid.AddProperty(TEXT("id"), SolidIdCounter++);

	int32 ValidPolyIdx = 0;
	for (int32 PolyIdx = 0; PolyIdx < Polys.Num(); ++PolyIdx)
	{
		const FPoly& Poly = Polys[PolyIdx];
		if (Poly.Vertices.Num() < 3)
		{
			continue;
		}

		// Use the pre-converted data at ValidPolyIdx
		if (ValidPolyIdx >= AllFaceVerticesSource.Num())
		{
			break;
		}

		const TArray<FVector>& Verts = AllFaceVerticesSource[ValidPolyIdx];
		const FVector& Normal = AllNormalsSource[ValidPolyIdx];
		ValidPolyIdx++;

		if (Verts.Num() < 3)
		{
			continue;
		}

		// Pick 3 non-collinear points for the plane definition.
		// After UE->Source conversion (Y negated), we need REVERSED winding
		// to maintain outward-facing normals in the right-handed system.
		FVector P1, P2, P3;
		if (!Pick3PlanePoints(Verts, P1, P2, P3))
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("Brush '%s' face %d has collinear vertices, skipping face."),
				*Brush->GetName(), PolyIdx));
			continue;
		}

		// Reverse winding: swap P2 and P3 to flip normal direction
		// because negating Y flips handedness
		FString PlaneStr = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
			FMath::RoundToFloat(P1.X), FMath::RoundToFloat(P1.Y), FMath::RoundToFloat(P1.Z),
			FMath::RoundToFloat(P3.X), FMath::RoundToFloat(P3.Y), FMath::RoundToFloat(P3.Z),
			FMath::RoundToFloat(P2.X), FMath::RoundToFloat(P2.Y), FMath::RoundToFloat(P2.Z));

		// Resolve material
		FString MaterialPath = DefaultMaterial;
		if (MaterialMapper)
		{
			MaterialPath = MaterialMapper->MapMaterial(Poly.Material);
		}

		// Compute UV axes - try to use FPoly texture data, fall back to defaults
		FString UAxis, VAxis;
		FVector TexU(Poly.TextureU);
		FVector TexV(Poly.TextureV);

		if (!TexU.IsNearlyZero() && !TexV.IsNearlyZero())
		{
			ComputeUVAxesFromPoly(
				FVector(Poly.TextureU), FVector(Poly.TextureV), FVector(Poly.Base),
				Normal, BrushTransform, UAxis, VAxis);
		}
		else
		{
			GetDefaultUVAxes(Normal, UAxis, VAxis);
		}

		FVMFKeyValues Side(TEXT("side"));
		Side.AddProperty(TEXT("id"), SideIdCounter++);
		Side.AddProperty(TEXT("plane"), PlaneStr);
		Side.AddProperty(TEXT("material"), MaterialPath);
		Side.AddProperty(TEXT("uaxis"), UAxis);
		Side.AddProperty(TEXT("vaxis"), VAxis);
		Side.AddProperty(TEXT("rotation"), 0);
		Side.AddProperty(TEXT("lightmapscale"), BrushLightmapScale);
		Side.AddProperty(TEXT("smoothing_groups"), 0);

		Solid.Children.Add(MoveTemp(Side));
	}

	if (Solid.Children.Num() >= 4)
	{
		Result.Solids.Add(MoveTemp(Solid));
	}
	else
	{
		Result.Warnings.Add(FString::Printf(
			TEXT("Brush '%s' produced fewer than 4 valid sides after conversion. Skipping."),
			*Brush->GetName()));
	}

	return Result;
}

bool FBrushConverter::ValidateConvexity(
	const TArray<FPlane>& Planes,
	const TArray<TArray<FVector>>& FaceVertices,
	float Tolerance)
{
	for (int32 PlaneIdx = 0; PlaneIdx < Planes.Num(); ++PlaneIdx)
	{
		const FPlane& Plane = Planes[PlaneIdx];

		for (int32 FaceIdx = 0; FaceIdx < FaceVertices.Num(); ++FaceIdx)
		{
			if (FaceIdx == PlaneIdx)
			{
				continue;
			}

			for (const FVector& Vert : FaceVertices[FaceIdx])
			{
				float Dist = Plane.PlaneDot(Vert);
				if (Dist > Tolerance)
				{
					return false;
				}
			}
		}
	}

	return true;
}

bool FBrushConverter::Pick3PlanePoints(
	const TArray<FVector>& Vertices,
	FVector& OutP1,
	FVector& OutP2,
	FVector& OutP3)
{
	if (Vertices.Num() < 3)
	{
		return false;
	}

	OutP1 = Vertices[0];

	// Find a second point that's not coincident with P1
	int32 Idx2 = -1;
	for (int32 i = 1; i < Vertices.Num(); ++i)
	{
		if (!Vertices[i].Equals(OutP1, 0.1))
		{
			OutP2 = Vertices[i];
			Idx2 = i;
			break;
		}
	}
	if (Idx2 < 0)
	{
		return false;
	}

	// Find a third point that's not collinear with P1-P2
	FVector Edge1 = (OutP2 - OutP1).GetSafeNormal();
	for (int32 i = Idx2 + 1; i < Vertices.Num(); ++i)
	{
		FVector Edge2 = (Vertices[i] - OutP1).GetSafeNormal();
		FVector Cross = FVector::CrossProduct(Edge1, Edge2);
		if (Cross.SizeSquared() > 0.001)
		{
			OutP3 = Vertices[i];
			return true;
		}
	}

	// Fallback: try all pairs
	for (int32 i = 1; i < Vertices.Num(); ++i)
	{
		if (i == Idx2) continue;
		FVector Edge2 = (Vertices[i] - OutP1).GetSafeNormal();
		FVector Cross = FVector::CrossProduct(Edge1, Edge2);
		if (Cross.SizeSquared() > 0.001)
		{
			OutP3 = Vertices[i];
			return true;
		}
	}

	return false;
}

void FBrushConverter::GetDefaultUVAxes(
	const FVector& Normal,
	FString& OutUAxis,
	FString& OutVAxis)
{
	if (FMath::Abs(Normal.Z) > 0.5)
	{
		OutUAxis = TEXT("[1 0 0 0] 0.25");
		OutVAxis = TEXT("[0 -1 0 0] 0.25");
	}
	else if (FMath::Abs(Normal.Y) > 0.5)
	{
		OutUAxis = TEXT("[1 0 0 0] 0.25");
		OutVAxis = TEXT("[0 0 -1 0] 0.25");
	}
	else
	{
		OutUAxis = TEXT("[0 1 0 0] 0.25");
		OutVAxis = TEXT("[0 0 -1 0] 0.25");
	}
}

void FBrushConverter::ComputeUVAxesFromPoly(
	const FVector& TextureU,
	const FVector& TextureV,
	const FVector& TextureBase,
	const FVector& FaceNormal,
	const FTransform& BrushTransform,
	FString& OutUAxis,
	FString& OutVAxis)
{
	// UE's FPoly stores TextureU and TextureV as direction vectors
	// in local space. We need to:
	// 1. Transform to world space
	// 2. Convert to Source coordinates (negate Y)
	// 3. Compute offset from texture base point
	// 4. Format as Source uaxis/vaxis: "[Ux Uy Uz offset] scale"

	// Transform texture axes to world space (direction only, no translation)
	FVector WorldU = BrushTransform.TransformVectorNoScale(TextureU);
	FVector WorldV = BrushTransform.TransformVectorNoScale(TextureV);
	FVector WorldBase = BrushTransform.TransformPosition(TextureBase);

	// Convert to Source coordinate system
	FVector SourceU(WorldU.X, -WorldU.Y, WorldU.Z);
	FVector SourceV(WorldV.X, -WorldV.Y, WorldV.Z);
	FVector SourceBase = FSourceCoord::UEToSource(WorldBase);

	// Source uaxis/vaxis format: [Ux Uy Uz offset] scale
	// The texture axes need to be normalized, with the length encoding the scale.
	// Source scale = 1.0 / (texels_per_unit), default 0.25 = 4 texels per Source unit.
	// UE's TextureU/V vectors encode direction and scale together.

	double ULen = SourceU.Size();
	double VLen = SourceV.Size();

	// Avoid division by zero
	if (ULen < KINDA_SMALL_NUMBER || VLen < KINDA_SMALL_NUMBER)
	{
		GetDefaultUVAxes(FaceNormal, OutUAxis, OutVAxis);
		return;
	}

	FVector UDir = SourceU / ULen;
	FVector VDir = SourceV / VLen;

	// Scale: Source uses texels-per-world-unit inverted.
	// UE TextureU length is typically 1/TextureSize, so we derive scale from it.
	// Default to 0.25 if the length doesn't make sense.
	double UScale = 0.25;
	double VScale = 0.25;

	// Compute texture offset: dot product of base point with texture axis
	double UOffset = FVector::DotProduct(SourceBase, UDir);
	double VOffset = FVector::DotProduct(SourceBase, VDir);

	OutUAxis = FString::Printf(TEXT("[%g %g %g %g] %g"),
		UDir.X, UDir.Y, UDir.Z, UOffset, UScale);
	OutVAxis = FString::Printf(TEXT("[%g %g %g %g] %g"),
		VDir.X, VDir.Y, VDir.Z, VOffset, VScale);
}
