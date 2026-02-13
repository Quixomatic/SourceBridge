#include "Import/VMFImporter.h"
#include "Import/VMFReader.h"
#include "Import/MaterialImporter.h"
#include "Import/ModelImporter.h"
#include "Import/MDLReader.h"
#include "Actors/SourceEntityActor.h"
#include "Runtime/SourceBridgeGameMode.h"
#include "Entities/EntityIOConnection.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Model.h"
#include "Editor.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/SphereReflectionCapture.h"
#include "Materials/MaterialInterface.h"
#include "Components/BrushComponent.h"
#include "BSPOps.h"
#include "EngineUtils.h"
#include "Misc/ScopedSlowTask.h"
#include "ProceduralMeshComponent.h"

FVMFImportResult FVMFImporter::ImportFile(const FString& FilePath, UWorld* World,
	const FVMFImportSettings& Settings)
{
	TArray<FVMFKeyValues> Blocks = FVMFReader::ParseFile(FilePath);
	if (Blocks.Num() == 0)
	{
		FVMFImportResult Result;
		Result.Warnings.Add(FString::Printf(TEXT("Failed to parse VMF file: %s"), *FilePath));
		return Result;
	}

	return ImportBlocks(Blocks, World, Settings);
}


FVMFImportResult FVMFImporter::ImportBlocks(const TArray<FVMFKeyValues>& Blocks, UWorld* World,
	const FVMFImportSettings& Settings)
{
	FVMFImportResult Result;

	if (!World)
	{
		Result.Warnings.Add(TEXT("No world provided for import."));
		return Result;
	}

	// Clear caches for fresh import
	FMaterialImporter::ClearCache();
	FModelImporter::ClearCache();

	// Set asset search path if provided (e.g., from BSP import with extracted assets)
	if (!Settings.AssetSearchPath.IsEmpty())
	{
		FMaterialImporter::SetAssetSearchPath(Settings.AssetSearchPath);
	}

	// Count total work items for progress bar
	int32 TotalItems = 0;
	for (const FVMFKeyValues& Block : Blocks)
	{
		if (Block.ClassName.Equals(TEXT("world"), ESearchCase::IgnoreCase) && Settings.bImportBrushes)
		{
			for (const FVMFKeyValues& Child : Block.Children)
			{
				if (Child.ClassName.Equals(TEXT("solid"), ESearchCase::IgnoreCase))
					TotalItems++;
			}
		}
		else if (Block.ClassName.Equals(TEXT("entity"), ESearchCase::IgnoreCase) && Settings.bImportEntities)
		{
			TotalItems++;
		}
	}

	FScopedSlowTask SlowTask((float)TotalItems, FText::FromString(
		FString::Printf(TEXT("Importing %d items..."), TotalItems)));
	SlowTask.MakeDialog(true);

	for (const FVMFKeyValues& Block : Blocks)
	{
		if (SlowTask.ShouldCancel())
			break;

		if (Block.ClassName.Equals(TEXT("world"), ESearchCase::IgnoreCase))
		{
			// Import worldspawn solids as plain brushes (not entities)
			if (Settings.bImportBrushes)
			{
				for (const FVMFKeyValues& Child : Block.Children)
				{
					if (SlowTask.ShouldCancel())
						break;

					if (Child.ClassName.Equals(TEXT("solid"), ESearchCase::IgnoreCase))
					{
						SlowTask.EnterProgressFrame(1.0f, FText::FromString(
							FString::Printf(TEXT("Brush %d/%d"), Result.BrushesImported + 1, TotalItems)));
						ImportSolid(Child, World, Settings, Result);
						if (Result.BrushesImported % 100 == 0) { GLog->Flush(); }
					}
				}
			}
		}
		else if (Block.ClassName.Equals(TEXT("entity"), ESearchCase::IgnoreCase))
		{
			if (!Settings.bImportEntities) continue;

			SlowTask.EnterProgressFrame(1.0f, FText::FromString(
				FString::Printf(TEXT("Entity %d/%d"), Result.EntitiesImported + 1, TotalItems)));
			GLog->Flush();

			// Check if brush entity (has solid children) or point entity
			bool bHasSolids = false;
			for (const FVMFKeyValues& Child : Block.Children)
			{
				if (Child.ClassName.Equals(TEXT("solid"), ESearchCase::IgnoreCase))
				{
					bHasSolids = true;
					break;
				}
			}

			if (bHasSolids && Settings.bImportBrushes)
			{
				// Brush entity: create ONE ASourceBrushEntity with all solids as children
				ASourceBrushEntity* BrushEntity = ImportBrushEntity(Block, World, Settings, Result);
				if (BrushEntity)
				{
					Result.SpawnedEntities.Add(BrushEntity);
				}
			}
			else
			{
				ImportPointEntity(Block, World, Settings, Result);
			}
		}
	}

	// Rebuild BSP after all brushes are imported so BSP rendering works correctly
	if (Result.BrushesImported > 0 && GEditor)
	{
		GEditor->csgRebuild(World);
		ULevel* Level = World->GetCurrentLevel();
		if (Level)
		{
			World->InvalidateModelGeometry(Level);
			Level->UpdateModelComponents();
		}
	}

	// Resolve parentname relationships after all entities are spawned
	ResolveParentNames(Result);

	// Redraw viewports
	if ((Result.BrushesImported > 0 || Result.EntitiesImported > 0) && GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);

		// Auto-set GameMode so Alt+P just works (Source spawns, tool texture hiding, etc.)
		if (World->GetWorldSettings())
		{
			World->GetWorldSettings()->DefaultGameMode = ASourceBridgeGameMode::StaticClass();
			UE_LOG(LogTemp, Log, TEXT("VMFImporter: Auto-set GameMode to SourceBridgeGameMode for PIE testing"));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("VMFImporter: Imported %d brushes, %d entities (%d warnings)"),
		Result.BrushesImported, Result.EntitiesImported, Result.Warnings.Num());

	return Result;
}

// ---- Coordinate Conversion ----

FVector FVMFImporter::SourceToUE(const FVector& SourcePos, float Scale)
{
	// Reverse of UEToSource: multiply by scale, negate Y
	return FVector(SourcePos.X * Scale, -SourcePos.Y * Scale, SourcePos.Z * Scale);
}

FVector FVMFImporter::SourceDirToUE(const FVector& SourceDir)
{
	// Direction only: negate Y for handedness change, no scaling
	return FVector(SourceDir.X, -SourceDir.Y, SourceDir.Z);
}

// ---- Parsing Helpers ----

bool FVMFImporter::ParsePlanePoints(const FString& PlaneStr, FVector& P1, FVector& P2, FVector& P3)
{
	// Format: "(x1 y1 z1) (x2 y2 z2) (x3 y3 z3)"
	TArray<FString> Parts;
	FString Clean = PlaneStr.Replace(TEXT("("), TEXT("")).Replace(TEXT(")"), TEXT(","));
	Clean.ParseIntoArray(Parts, TEXT(","), true);

	if (Parts.Num() < 3) return false;

	auto ParseVec = [](const FString& S, FVector& V) -> bool
	{
		TArray<FString> Comps;
		S.TrimStartAndEnd().ParseIntoArrayWS(Comps);
		if (Comps.Num() < 3) return false;
		V.X = FCString::Atod(*Comps[0]);
		V.Y = FCString::Atod(*Comps[1]);
		V.Z = FCString::Atod(*Comps[2]);
		return true;
	};

	return ParseVec(Parts[0], P1) && ParseVec(Parts[1], P2) && ParseVec(Parts[2], P3);
}

bool FVMFImporter::ParseUVAxis(const FString& AxisStr, FVector& Axis, float& Offset, float& Scale)
{
	// Format: "[x y z offset] scale"
	// Example: "[1 0 0 0] 0.25"
	int32 BracketStart = AxisStr.Find(TEXT("["));
	int32 BracketEnd = AxisStr.Find(TEXT("]"));
	if (BracketStart == INDEX_NONE || BracketEnd == INDEX_NONE) return false;

	FString Inside = AxisStr.Mid(BracketStart + 1, BracketEnd - BracketStart - 1).TrimStartAndEnd();
	FString After = AxisStr.Mid(BracketEnd + 1).TrimStartAndEnd();

	TArray<FString> Parts;
	Inside.ParseIntoArrayWS(Parts);
	if (Parts.Num() < 4) return false;

	Axis.X = FCString::Atod(*Parts[0]);
	Axis.Y = FCString::Atod(*Parts[1]);
	Axis.Z = FCString::Atod(*Parts[2]);
	Offset = FCString::Atof(*Parts[3]);
	Scale = FCString::Atof(*After);

	if (FMath::IsNearlyZero(Scale)) Scale = 0.25f;

	return true;
}

FVector FVMFImporter::ParseOrigin(const FString& OriginStr)
{
	TArray<FString> Parts;
	OriginStr.ParseIntoArrayWS(Parts);
	if (Parts.Num() >= 3)
	{
		return FVector(
			FCString::Atod(*Parts[0]),
			FCString::Atod(*Parts[1]),
			FCString::Atod(*Parts[2]));
	}
	return FVector::ZeroVector;
}

FRotator FVMFImporter::ParseAngles(const FString& AnglesStr)
{
	TArray<FString> Parts;
	AnglesStr.ParseIntoArrayWS(Parts);
	if (Parts.Num() >= 3)
	{
		// Source angles: pitch yaw roll
		float Pitch = FCString::Atof(*Parts[0]);
		float Yaw = FCString::Atof(*Parts[1]);
		float Roll = FCString::Atof(*Parts[2]);
		// Convert: negate yaw for handedness change
		return FRotator(Pitch, -Yaw, Roll);
	}
	return FRotator::ZeroRotator;
}

// ---- CSG Reconstruction ----

TArray<FVector> FVMFImporter::CreateLargePolygonOnPlane(const FPlane& Plane, const FVector& PointOnPlane)
{
	FVector Normal(Plane.X, Plane.Y, Plane.Z);

	// Pick a perpendicular "right" vector
	FVector Right;
	if (FMath::Abs(Normal.Z) > 0.9)
	{
		Right = FVector::CrossProduct(Normal, FVector(1, 0, 0));
	}
	else
	{
		Right = FVector::CrossProduct(Normal, FVector(0, 0, 1));
	}
	Right.Normalize();

	FVector Up = FVector::CrossProduct(Normal, Right);
	Up.Normalize();

	// Create a large quad (65536 = Source max map extent)
	const double HalfSize = 65536.0;
	TArray<FVector> Polygon;
	Polygon.Add(PointOnPlane - Right * HalfSize - Up * HalfSize);
	Polygon.Add(PointOnPlane + Right * HalfSize - Up * HalfSize);
	Polygon.Add(PointOnPlane + Right * HalfSize + Up * HalfSize);
	Polygon.Add(PointOnPlane - Right * HalfSize + Up * HalfSize);

	return Polygon;
}

TArray<FVector> FVMFImporter::ClipPolygonByPlane(const TArray<FVector>& Polygon, const FPlane& Plane)
{
	// Sutherland-Hodgman clipping: keep vertices on positive side of plane (where normal points)
	if (Polygon.Num() < 3) return {};

	TArray<FVector> Output;
	const double Epsilon = 0.01;

	for (int32 i = 0; i < Polygon.Num(); i++)
	{
		const FVector& Current = Polygon[i];
		const FVector& Next = Polygon[(i + 1) % Polygon.Num()];

		double DistCurrent = Plane.PlaneDot(Current);
		double DistNext = Plane.PlaneDot(Next);

		bool bCurrentInside = DistCurrent >= -Epsilon;
		bool bNextInside = DistNext >= -Epsilon;

		if (bCurrentInside)
		{
			Output.Add(Current);
		}

		// Edge crosses the plane
		if ((bCurrentInside && !bNextInside) || (!bCurrentInside && bNextInside))
		{
			double T = DistCurrent / (DistCurrent - DistNext);
			FVector Intersection = Current + T * (Next - Current);
			Output.Add(Intersection);
		}
	}

	return Output;
}

TArray<TArray<FVector>> FVMFImporter::ReconstructFacesFromPlanes(
	const TArray<FPlane>& Planes, const TArray<FVector>& PlanePoints,
	TArray<int32>& OutFaceToPlaneIdx)
{
	TArray<TArray<FVector>> Faces;
	OutFaceToPlaneIdx.Empty();

	for (int32 i = 0; i < Planes.Num(); i++)
	{
		// Start with a large polygon on this plane
		TArray<FVector> Polygon = CreateLargePolygonOnPlane(Planes[i], PlanePoints[i]);

		// Clip against all other planes
		for (int32 j = 0; j < Planes.Num(); j++)
		{
			if (i == j) continue;
			Polygon = ClipPolygonByPlane(Polygon, Planes[j]);
			if (Polygon.Num() < 3) break;
		}

		if (Polygon.Num() >= 3)
		{
			Faces.Add(MoveTemp(Polygon));
			OutFaceToPlaneIdx.Add(i);
		}
	}

	return Faces;
}

// ---- Solid Parsing (no actor creation) ----

bool FVMFImporter::ParseSolid(const FVMFKeyValues& SolidBlock,
	TArray<TArray<FVector>>& OutFaces,
	TArray<FVector>& OutFaceNormals,
	TArray<FVMFSideData>& OutSideData,
	TArray<int32>& OutFaceToSideMapping,
	FVMFImportResult& Result)
{
	TArray<FPlane> Planes;
	TArray<FVector> PlaneFirstPoints;

	OutFaceNormals.Empty();
	OutSideData.Empty();
	OutFaceToSideMapping.Empty();

	for (const FVMFKeyValues& Child : SolidBlock.Children)
	{
		if (!Child.ClassName.Equals(TEXT("side"), ESearchCase::IgnoreCase)) continue;

		FString PlaneStr;
		FVMFSideData SideData;

		for (const auto& Prop : Child.Properties)
		{
			if (Prop.Key.Equals(TEXT("plane"), ESearchCase::IgnoreCase))
				PlaneStr = Prop.Value;
			else if (Prop.Key.Equals(TEXT("material"), ESearchCase::IgnoreCase))
				SideData.Material = Prop.Value;
			else if (Prop.Key.Equals(TEXT("uaxis"), ESearchCase::IgnoreCase))
			{
				SideData.RawUAxisStr = Prop.Value;
				ParseUVAxis(Prop.Value, SideData.UAxis, SideData.UOffset, SideData.UScale);
			}
			else if (Prop.Key.Equals(TEXT("vaxis"), ESearchCase::IgnoreCase))
			{
				SideData.RawVAxisStr = Prop.Value;
				ParseUVAxis(Prop.Value, SideData.VAxis, SideData.VOffset, SideData.VScale);
			}
			else if (Prop.Key.Equals(TEXT("lightmapscale"), ESearchCase::IgnoreCase))
				SideData.LightmapScale = FCString::Atoi(*Prop.Value);
		}

		if (PlaneStr.IsEmpty()) continue;

		FVector P1, P2, P3;
		if (!ParsePlanePoints(PlaneStr, P1, P2, P3))
		{
			Result.Warnings.Add(FString::Printf(TEXT("Failed to parse plane: %s"), *PlaneStr));
			continue;
		}

		// Compute plane from 3 points
		// VMF convention: (P2-P1)x(P3-P1) points INWARD
		FVector Edge1 = P2 - P1;
		FVector Edge2 = P3 - P1;
		FVector Normal = FVector::CrossProduct(Edge1, Edge2);
		if (Normal.IsNearlyZero())
		{
			Result.Warnings.Add(TEXT("Degenerate plane (collinear points), skipping face."));
			continue;
		}
		Normal.Normalize();

		// FPlane with inward normal: the solid is on the positive side
		Planes.Add(FPlane(P1, Normal));
		PlaneFirstPoints.Add(P1);
		OutFaceNormals.Add(Normal);
		OutSideData.Add(MoveTemp(SideData));
	}

	if (Planes.Num() < 4)
	{
		Result.Warnings.Add(TEXT("Solid has fewer than 4 valid planes, skipping."));
		return false;
	}

	// Reconstruct face polygons via CSG clipping
	OutFaces = ReconstructFacesFromPlanes(Planes, PlaneFirstPoints, OutFaceToSideMapping);

	if (OutFaces.Num() < 4)
	{
		Result.Warnings.Add(TEXT("CSG reconstruction produced fewer than 4 faces, skipping solid."));
		return false;
	}

	return true;
}

// ---- ProceduralMesh Builder (reusable for any actor) ----

UProceduralMeshComponent* FVMFImporter::BuildProceduralMesh(
	AActor* OwnerActor,
	const FString& MeshName,
	const TArray<TArray<FVector>>& Faces,
	const TArray<FVector>& FaceNormals,
	const TArray<FVMFSideData>& SideData,
	const TArray<int32>& FaceToSideMapping,
	const FVMFImportSettings& Settings,
	const FVector& ActorCenter)
{
	if (!OwnerActor || Faces.Num() < 3) return nullptr;

	float Scale = Settings.ScaleMultiplier;

	UProceduralMeshComponent* ProcMesh = NewObject<UProceduralMeshComponent>(OwnerActor, *MeshName);
	ProcMesh->AttachToComponent(OwnerActor->GetRootComponent(),
		FAttachmentTransformRules::KeepRelativeTransform);
	ProcMesh->SetRelativeTransform(FTransform::Identity);
	// Mark as instance component so it serializes with the level (survives save/reload)
	ProcMesh->CreationMethod = EComponentCreationMethod::Instance;

	// Resolve materials and compute normals for each face
	struct FFaceData
	{
		UMaterialInterface* Material = nullptr;
		const FVMFSideData* Side = nullptr;
		FVector OutwardNormal = FVector::ZeroVector;
		bool bFlipWinding = false;
	};
	TArray<FFaceData> FaceDataArray;
	FaceDataArray.SetNum(Faces.Num());

	for (int32 FaceIdx = 0; FaceIdx < Faces.Num(); FaceIdx++)
	{
		const TArray<FVector>& FaceVerts = Faces[FaceIdx];
		if (FaceVerts.Num() < 3) continue;

		int32 SideIdx = (FaceIdx < FaceToSideMapping.Num()) ? FaceToSideMapping[FaceIdx] : FaceIdx;
		FFaceData& FD = FaceDataArray[FaceIdx];

		if (SideIdx < SideData.Num())
		{
			FD.Side = &SideData[SideIdx];

			if (Settings.bImportMaterials && !FD.Side->Material.IsEmpty())
			{
				FD.Material = FMaterialImporter::ResolveSourceMaterial(FD.Side->Material);
			}
		}

		// Compute face normal from vertices (in UE local space)
		// and determine outward direction by checking dot with face center from actor center
		FVector FaceCenter = FVector::ZeroVector;
		for (const FVector& V : FaceVerts)
		{
			FaceCenter += SourceToUE(V, Scale) - ActorCenter;
		}
		FaceCenter /= FaceVerts.Num();

		// Compute winding normal
		FVector V0 = SourceToUE(FaceVerts[0], Scale) - ActorCenter;
		FVector V1 = SourceToUE(FaceVerts[1], Scale) - ActorCenter;
		FVector V2 = SourceToUE(FaceVerts[2], Scale) - ActorCenter;
		FVector WindingNormal = FVector::CrossProduct(V1 - V0, V2 - V0);
		if (!WindingNormal.IsNearlyZero())
		{
			WindingNormal.Normalize();
		}

		bool bNormalPointsOutward = FVector::DotProduct(WindingNormal, FaceCenter) > 0.0f;
		FD.OutwardNormal = bNormalPointsOutward ? WindingNormal : -WindingNormal;
		FD.bFlipWinding = bNormalPointsOutward;
	}

	// Group faces by material into mesh sections
	TMap<UMaterialInterface*, int32> MatToSection;
	struct FSectionData
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
	};
	TArray<FSectionData> Sections;
	TArray<UMaterialInterface*> SectionMaterials;

	for (int32 FaceIdx = 0; FaceIdx < Faces.Num(); FaceIdx++)
	{
		const TArray<FVector>& FaceVerts = Faces[FaceIdx];
		if (FaceVerts.Num() < 3) continue;

		const FFaceData& FD = FaceDataArray[FaceIdx];

		int32* SecIdx = MatToSection.Find(FD.Material);
		if (!SecIdx)
		{
			int32 NewIdx = Sections.Num();
			MatToSection.Add(FD.Material, NewIdx);
			Sections.AddDefaulted();
			SectionMaterials.Add(FD.Material);
			SecIdx = &MatToSection[FD.Material];
		}

		FSectionData& Sec = Sections[*SecIdx];
		int32 BaseVert = Sec.Vertices.Num();

		// Get texture dimensions for UV normalization
		FIntPoint TexSize(512, 512);
		if (FD.Side && !FD.Side->Material.IsEmpty())
		{
			TexSize = FMaterialImporter::GetTextureSize(FD.Side->Material);
		}

		for (const FVector& V : FaceVerts)
		{
			FVector LocalPos = SourceToUE(V, Scale) - ActorCenter;
			Sec.Vertices.Add(LocalPos);
			Sec.Normals.Add(FD.OutwardNormal);

			if (FD.Side)
			{
				// Convert to Source world space for UV formula
				FVector SourcePos(V);
				float UTexel = FVector::DotProduct(SourcePos, FD.Side->UAxis) / FD.Side->UScale + FD.Side->UOffset;
				float VTexel = FVector::DotProduct(SourcePos, FD.Side->VAxis) / FD.Side->VScale + FD.Side->VOffset;
				Sec.UVs.Add(FVector2D(UTexel / (float)TexSize.X, VTexel / (float)TexSize.Y));
			}
			else
			{
				Sec.UVs.Add(FVector2D(0.0f, 0.0f));
			}
		}

		// Fan triangulation
		for (int32 i = 1; i < FaceVerts.Num() - 1; i++)
		{
			Sec.Triangles.Add(BaseVert);
			if (FD.bFlipWinding)
			{
				Sec.Triangles.Add(BaseVert + i + 1);
				Sec.Triangles.Add(BaseVert + i);
			}
			else
			{
				Sec.Triangles.Add(BaseVert + i);
				Sec.Triangles.Add(BaseVert + i + 1);
			}
		}
	}

	for (int32 i = 0; i < Sections.Num(); i++)
	{
		ProcMesh->CreateMeshSection_LinearColor(i,
			Sections[i].Vertices, Sections[i].Triangles,
			Sections[i].Normals, Sections[i].UVs,
			TArray<FLinearColor>(), TArray<FProcMeshTangent>(), true);

		if (SectionMaterials[i])
		{
			ProcMesh->SetMaterial(i, SectionMaterials[i]);
		}
	}

	ProcMesh->RegisterComponent();
	return ProcMesh;
}

// ---- Brush Creation (worldspawn solids) ----

ABrush* FVMFImporter::CreateBrushFromFaces(
	UWorld* World,
	const TArray<TArray<FVector>>& Faces,
	const TArray<FVector>& FaceNormals,
	const TArray<FVMFSideData>& SideData,
	const TArray<int32>& FaceToSideMapping,
	const FVMFImportSettings& Settings)
{
	if (Faces.Num() < 4) return nullptr;

	float Scale = Settings.ScaleMultiplier;

	// Compute center of all vertices for the brush origin
	FVector Center = FVector::ZeroVector;
	int32 TotalVerts = 0;
	for (const auto& Face : Faces)
	{
		for (const FVector& V : Face)
		{
			Center += SourceToUE(V, Scale);
			TotalVerts++;
		}
	}
	if (TotalVerts == 0) return nullptr;
	Center /= TotalVerts;

	// Spawn brush actor at computed center
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FTransform SpawnTransform;
	SpawnTransform.SetLocation(Center);
	ABrush* Brush = World->SpawnActor<ABrush>(ABrush::StaticClass(), SpawnTransform, SpawnParams);
	if (!Brush) return nullptr;

	// Force the actor position in case ABrush constructor reset it
	Brush->SetActorLocation(Center);
	Brush->BrushType = Brush_Add;
	Brush->SetActorLabel(TEXT("ImportedBrush"));

	UE_LOG(LogTemp, Verbose, TEXT("VMFImporter: Brush at (%f, %f, %f) with %d faces, %d verts"),
		Brush->GetActorLocation().X, Brush->GetActorLocation().Y, Brush->GetActorLocation().Z,
		Faces.Num(), TotalVerts);

	// Create the model
	UModel* Model = NewObject<UModel>(Brush, NAME_None, RF_Transactional);
	Model->Initialize(nullptr, true);
	Model->Polys = NewObject<UPolys>(Model, NAME_None, RF_Transactional);
	Brush->Brush = Model;

	// Add faces as polys (matching UE's BrushBuilder pattern)
	for (int32 FaceIdx = 0; FaceIdx < Faces.Num(); FaceIdx++)
	{
		const TArray<FVector>& FaceVerts = Faces[FaceIdx];
		if (FaceVerts.Num() < 3) continue;

		FPoly Poly;
		Poly.Init();
		Poly.iLink = FaceIdx;

		for (const FVector& V : FaceVerts)
		{
			FVector UEPos = SourceToUE(V, Scale);
			Poly.Vertices.Add(FVector3f(UEPos - Center));
		}

		Poly.Base = Poly.Vertices[0];

		int32 SideIdx = (FaceIdx < FaceToSideMapping.Num()) ? FaceToSideMapping[FaceIdx] : FaceIdx;

		if (SideIdx < SideData.Num())
		{
			const FVMFSideData& Side = SideData[SideIdx];

			if (Settings.bImportMaterials && !Side.Material.IsEmpty())
			{
				Poly.ItemName = FName(*Side.Material);
				UMaterialInterface* Mat = FMaterialImporter::ResolveSourceMaterial(Side.Material);
				if (Mat)
				{
					Poly.Material = Mat;
				}
			}

			FVector UEUAxis = SourceDirToUE(Side.UAxis);
			FVector UEVAxis = SourceDirToUE(Side.VAxis);

			if (!FMath::IsNearlyZero(Side.UScale))
			{
				Poly.TextureU = FVector3f(UEUAxis / (Side.UScale * Scale));
			}
			if (!FMath::IsNearlyZero(Side.VScale))
			{
				Poly.TextureV = FVector3f(UEVAxis / (Side.VScale * Scale));
			}
		}

		if (Poly.Finalize(Brush, 1) == 0)
		{
			Model->Polys->Element.Add(MoveTemp(Poly));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("VMFImporter: Poly.Finalize failed for face %d"), FaceIdx);
		}
	}

	Model->BuildBound();

	// Link the brush component to the model
	Brush->GetBrushComponent()->Brush = Model;
	FBSPOps::csgPrepMovingBrush(Brush);

	// Build a ProceduralMeshComponent for solid rendering with materials
	BuildProceduralMesh(Brush, TEXT("BrushMesh"), Faces, FaceNormals, SideData, FaceToSideMapping, Settings, Center);

	return Brush;
}

// ---- Solid Import (worldspawn) ----

ABrush* FVMFImporter::ImportSolid(const FVMFKeyValues& SolidBlock, UWorld* World,
	const FVMFImportSettings& Settings, FVMFImportResult& Result)
{
	TArray<TArray<FVector>> Faces;
	TArray<FVector> FaceNormals;
	TArray<FVMFSideData> SideDataArray;
	TArray<int32> FaceToSideMapping;

	if (!ParseSolid(SolidBlock, Faces, FaceNormals, SideDataArray, FaceToSideMapping, Result))
	{
		return nullptr;
	}

	ABrush* Brush = CreateBrushFromFaces(World, Faces, FaceNormals, SideDataArray, FaceToSideMapping, Settings);
	if (Brush)
	{
		Result.BrushesImported++;
		return Brush;
	}

	Result.Warnings.Add(TEXT("Failed to create brush from faces."));
	return nullptr;
}

// ---- Brush Entity Import ----

ASourceBrushEntity* FVMFImporter::ImportBrushEntity(const FVMFKeyValues& EntityBlock, UWorld* World,
	const FVMFImportSettings& Settings, FVMFImportResult& Result)
{
	float Scale = Settings.ScaleMultiplier;

	// First pass: parse all solids to compute the entity's overall center
	struct FSolidParseResult
	{
		TArray<TArray<FVector>> Faces;
		TArray<FVector> FaceNormals;
		TArray<FVMFSideData> SideData;
		TArray<int32> FaceToSideMapping;
		const FVMFKeyValues* OriginalBlock = nullptr;
	};
	TArray<FSolidParseResult> ParsedSolids;

	FVector AllVertsSum = FVector::ZeroVector;
	int32 AllVertsCount = 0;

	for (const FVMFKeyValues& Child : EntityBlock.Children)
	{
		if (!Child.ClassName.Equals(TEXT("solid"), ESearchCase::IgnoreCase)) continue;

		FSolidParseResult Parsed;
		Parsed.OriginalBlock = &Child;

		if (ParseSolid(Child, Parsed.Faces, Parsed.FaceNormals, Parsed.SideData, Parsed.FaceToSideMapping, Result))
		{
			// Accumulate vertex positions for center computation
			for (const auto& Face : Parsed.Faces)
			{
				for (const FVector& V : Face)
				{
					AllVertsSum += SourceToUE(V, Scale);
					AllVertsCount++;
				}
			}
			ParsedSolids.Add(MoveTemp(Parsed));
		}
	}

	if (ParsedSolids.Num() == 0 || AllVertsCount == 0)
	{
		Result.Warnings.Add(TEXT("Brush entity has no valid solids, skipping."));
		return nullptr;
	}

	// Entity center = average of all solid vertices
	FVector EntityCenter = AllVertsSum / AllVertsCount;

	// Check for an explicit "origin" keyvalue (some brush entities specify one)
	for (const auto& Prop : EntityBlock.Properties)
	{
		if (Prop.Key.Equals(TEXT("origin"), ESearchCase::IgnoreCase) && !Prop.Value.IsEmpty())
		{
			FVector SourceOrigin = ParseOrigin(Prop.Value);
			EntityCenter = SourceToUE(SourceOrigin, Scale);
			break;
		}
	}

	// Spawn the entity actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FTransform SpawnTransform;
	SpawnTransform.SetLocation(EntityCenter);

	ASourceBrushEntity* Entity = World->SpawnActor<ASourceBrushEntity>(
		ASourceBrushEntity::StaticClass(), SpawnTransform, SpawnParams);
	if (!Entity)
	{
		Result.Warnings.Add(TEXT("Failed to spawn ASourceBrushEntity."));
		return nullptr;
	}
	Entity->SetActorLocation(EntityCenter);

	// Apply all entity properties (classname, targetname, parentname, keyvalues, spawnflags, I/O)
	ApplyEntityProperties(Entity, EntityBlock);

	// Set actor label
	if (!Entity->TargetName.IsEmpty())
	{
		Entity->SetActorLabel(FString::Printf(TEXT("%s (%s)"), *Entity->TargetName, *Entity->SourceClassname));
	}
	else
	{
		Entity->SetActorLabel(Entity->SourceClassname);
	}

	// Build ProceduralMeshComponent for each solid and store brush data for re-export
	for (int32 SolidIdx = 0; SolidIdx < ParsedSolids.Num(); SolidIdx++)
	{
		const FSolidParseResult& Parsed = ParsedSolids[SolidIdx];

		// Build the visual mesh
		FString MeshName = FString::Printf(TEXT("BrushMesh_%d"), SolidIdx);
		UProceduralMeshComponent* ProcMesh = BuildProceduralMesh(
			Entity, MeshName,
			Parsed.Faces, Parsed.FaceNormals, Parsed.SideData, Parsed.FaceToSideMapping,
			Settings, EntityCenter);

		if (ProcMesh)
		{
			Entity->BrushMeshes.Add(ProcMesh);
		}

		// Store original solid data for lossless re-export
		FImportedBrushData BrushData;
		// Try to get solid ID from VMF
		if (Parsed.OriginalBlock)
		{
			for (const auto& Prop : Parsed.OriginalBlock->Properties)
			{
				if (Prop.Key.Equals(TEXT("id"), ESearchCase::IgnoreCase))
				{
					BrushData.SolidId = FCString::Atoi(*Prop.Value);
					break;
				}
			}
		}

		// Store per-side data for re-export
		for (int32 SideIdx = 0; SideIdx < Parsed.SideData.Num(); SideIdx++)
		{
			const FVMFSideData& Side = Parsed.SideData[SideIdx];

			FImportedSideData ImportedSide;
			ImportedSide.Material = Side.Material;
			ImportedSide.UAxisStr = Side.RawUAxisStr;
			ImportedSide.VAxisStr = Side.RawVAxisStr;
			ImportedSide.LightmapScale = Side.LightmapScale;

			// Get original plane points from the VMF block
			if (Parsed.OriginalBlock)
			{
				int32 CurrentSide = 0;
				for (const FVMFKeyValues& SideBlock : Parsed.OriginalBlock->Children)
				{
					if (!SideBlock.ClassName.Equals(TEXT("side"), ESearchCase::IgnoreCase)) continue;

					if (CurrentSide == SideIdx)
					{
						for (const auto& Prop : SideBlock.Properties)
						{
							if (Prop.Key.Equals(TEXT("plane"), ESearchCase::IgnoreCase))
							{
								ParsePlanePoints(Prop.Value, ImportedSide.PlaneP1, ImportedSide.PlaneP2, ImportedSide.PlaneP3);
								break;
							}
						}
						break;
					}
					CurrentSide++;
				}
			}

			BrushData.Sides.Add(MoveTemp(ImportedSide));
		}

		Entity->StoredBrushData.Add(MoveTemp(BrushData));
		Result.BrushesImported++;
	}

	Result.EntitiesImported++;

	UE_LOG(LogTemp, Log, TEXT("VMFImporter: Brush entity '%s' (%s) with %d solids at (%f, %f, %f)"),
		*Entity->TargetName, *Entity->SourceClassname, ParsedSolids.Num(),
		EntityCenter.X, EntityCenter.Y, EntityCenter.Z);

	return Entity;
}

// ---- Common Entity Property Setter ----

void FVMFImporter::ApplyEntityProperties(ASourceEntityActor* Entity, const FVMFKeyValues& EntityBlock)
{
	if (!Entity) return;

	for (const auto& Prop : EntityBlock.Properties)
	{
		if (Prop.Key.Equals(TEXT("classname"), ESearchCase::IgnoreCase))
		{
			Entity->SourceClassname = Prop.Value;
		}
		else if (Prop.Key.Equals(TEXT("targetname"), ESearchCase::IgnoreCase))
		{
			Entity->TargetName = Prop.Value;
		}
		else if (Prop.Key.Equals(TEXT("parentname"), ESearchCase::IgnoreCase))
		{
			Entity->ParentName = Prop.Value;
		}
		else if (Prop.Key.Equals(TEXT("spawnflags"), ESearchCase::IgnoreCase))
		{
			Entity->SpawnFlags = FCString::Atoi(*Prop.Value);
		}
		else if (!Prop.Key.Equals(TEXT("origin"), ESearchCase::IgnoreCase) &&
				 !Prop.Key.Equals(TEXT("angles"), ESearchCase::IgnoreCase))
		{
			// Store all other keyvalues (origin/angles handled separately by caller)
			Entity->KeyValues.Add(Prop.Key, Prop.Value);
		}
	}

	// Parse I/O connections and store as actor tags
	for (const FVMFKeyValues& Child : EntityBlock.Children)
	{
		if (Child.ClassName.Equals(TEXT("connections"), ESearchCase::IgnoreCase))
		{
			for (const auto& Conn : Child.Properties)
			{
				FString Tag = FString::Printf(TEXT("io:%s:%s"), *Conn.Key, *Conn.Value);
				Entity->Tags.Add(*Tag);
			}
		}
	}

#if WITH_EDITORONLY_DATA
	// Update editor sprite based on classname
	Entity->UpdateEditorSprite();
#endif
}

// ---- Parentname Resolution ----

void FVMFImporter::ResolveParentNames(const FVMFImportResult& Result)
{
	// Build a map from targetname to entity actor
	TMap<FString, ASourceEntityActor*> TargetNameMap;
	for (const TWeakObjectPtr<ASourceEntityActor>& WeakEntity : Result.SpawnedEntities)
	{
		ASourceEntityActor* Entity = WeakEntity.Get();
		if (Entity && !Entity->TargetName.IsEmpty())
		{
			TargetNameMap.Add(Entity->TargetName, Entity);
		}
	}

	if (TargetNameMap.Num() == 0) return;

	int32 AttachmentsResolved = 0;
	for (const TWeakObjectPtr<ASourceEntityActor>& WeakEntity : Result.SpawnedEntities)
	{
		ASourceEntityActor* Entity = WeakEntity.Get();
		if (!Entity || Entity->ParentName.IsEmpty()) continue;

		// Handle "entity,attachment" syntax - extract just the entity targetname
		FString ParentTargetName = Entity->ParentName;
		int32 CommaIdx;
		if (ParentTargetName.FindChar(TEXT(','), CommaIdx))
		{
			ParentTargetName = ParentTargetName.Left(CommaIdx);
		}

		ASourceEntityActor** ParentPtr = TargetNameMap.Find(ParentTargetName);
		if (ParentPtr && *ParentPtr)
		{
			Entity->AttachToActor(*ParentPtr, FAttachmentTransformRules::KeepWorldTransform);
			AttachmentsResolved++;
			UE_LOG(LogTemp, Log, TEXT("VMFImporter: Attached '%s' to parent '%s'"),
				*Entity->TargetName, *ParentTargetName);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("VMFImporter: Entity '%s' has parentname '%s' but no matching entity found"),
				*Entity->TargetName, *Entity->ParentName);
		}
	}

	if (AttachmentsResolved > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("VMFImporter: Resolved %d parent-child attachments"), AttachmentsResolved);
	}
}

// ---- Point Entity Import ----

bool FVMFImporter::ImportPointEntity(const FVMFKeyValues& EntityBlock, UWorld* World,
	const FVMFImportSettings& Settings, FVMFImportResult& Result)
{
	FString ClassName;
	FString TargetName;
	FString OriginStr;
	FString AnglesStr;
	TArray<TPair<FString, FString>> KeyValues;

	for (const auto& Prop : EntityBlock.Properties)
	{
		if (Prop.Key.Equals(TEXT("classname"), ESearchCase::IgnoreCase))
			ClassName = Prop.Value;
		else if (Prop.Key.Equals(TEXT("targetname"), ESearchCase::IgnoreCase))
			TargetName = Prop.Value;
		else if (Prop.Key.Equals(TEXT("origin"), ESearchCase::IgnoreCase))
			OriginStr = Prop.Value;
		else if (Prop.Key.Equals(TEXT("angles"), ESearchCase::IgnoreCase))
			AnglesStr = Prop.Value;
		else
			KeyValues.Emplace(Prop.Key, Prop.Value);
	}

	if (ClassName.IsEmpty()) return false;

	// Convert position from Source to UE
	FVector SourceOrigin = ParseOrigin(OriginStr);
	FVector UEOrigin = SourceToUE(SourceOrigin, Settings.ScaleMultiplier);
	FRotator UERotation = AnglesStr.IsEmpty() ? FRotator::ZeroRotator : ParseAngles(AnglesStr);

	// prop_static stores pitch negated in BSP (and BSPSource-decompiled VMFs)
	if (ClassName.Equals(TEXT("prop_static"), ESearchCase::IgnoreCase))
	{
		UERotation.Pitch = -UERotation.Pitch;
	}

	// Determine which actor class to spawn based on Source classname
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FTransform SpawnTransform(UERotation, UEOrigin);

	ASourceEntityActor* Entity = nullptr;

	if (ClassName.Equals(TEXT("info_player_terrorist"), ESearchCase::IgnoreCase))
	{
		Entity = World->SpawnActor<ASourceTSpawn>(ASourceTSpawn::StaticClass(), SpawnTransform, SpawnParams);
	}
	else if (ClassName.Equals(TEXT("info_player_counterterrorist"), ESearchCase::IgnoreCase))
	{
		Entity = World->SpawnActor<ASourceCTSpawn>(ASourceCTSpawn::StaticClass(), SpawnTransform, SpawnParams);
	}
	else if (ClassName.Equals(TEXT("info_player_spectator"), ESearchCase::IgnoreCase))
	{
		Entity = World->SpawnActor<ASourceSpectatorSpawn>(ASourceSpectatorSpawn::StaticClass(), SpawnTransform, SpawnParams);
	}
	else if (ClassName.Equals(TEXT("light"), ESearchCase::IgnoreCase))
	{
		ASourceLight* Light = World->SpawnActor<ASourceLight>(ASourceLight::StaticClass(), SpawnTransform, SpawnParams);
		if (Light)
		{
			for (const auto& KV : KeyValues)
			{
				if (KV.Key.Equals(TEXT("_light"), ESearchCase::IgnoreCase))
				{
					TArray<FString> LightParts;
					KV.Value.ParseIntoArrayWS(LightParts);
					if (LightParts.Num() >= 4)
					{
						Light->LightColor = FColor(
							FCString::Atoi(*LightParts[0]),
							FCString::Atoi(*LightParts[1]),
							FCString::Atoi(*LightParts[2]));
						Light->Brightness = FCString::Atoi(*LightParts[3]);
					}
				}
				else if (KV.Key.Equals(TEXT("style"), ESearchCase::IgnoreCase))
				{
					Light->Style = FCString::Atoi(*KV.Value);
				}
			}
		}
		Entity = Light;
	}
	else if (ClassName.Equals(TEXT("light_spot"), ESearchCase::IgnoreCase))
	{
		// Spawn as UE SpotLight for visual preview, plus a SourceGenericEntity for data
		ASpotLight* SpotLight = World->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), SpawnTransform, SpawnParams);
		if (SpotLight)
		{
			for (const auto& KV : KeyValues)
			{
				if (KV.Key.Equals(TEXT("_light"), ESearchCase::IgnoreCase))
				{
					TArray<FString> LightParts;
					KV.Value.ParseIntoArrayWS(LightParts);
					if (LightParts.Num() >= 4)
					{
						float R = FCString::Atof(*LightParts[0]) / 255.0f;
						float G = FCString::Atof(*LightParts[1]) / 255.0f;
						float B = FCString::Atof(*LightParts[2]) / 255.0f;
						float Brightness = FCString::Atof(*LightParts[3]);
						SpotLight->SpotLightComponent->SetLightColor(FLinearColor(R, G, B));
						SpotLight->SpotLightComponent->SetIntensity(Brightness * 10.0f);
					}
				}
				else if (KV.Key.Equals(TEXT("_cone"), ESearchCase::IgnoreCase))
				{
					float ConeAngle = FCString::Atof(*KV.Value);
					SpotLight->SpotLightComponent->SetOuterConeAngle(ConeAngle);
				}
				else if (KV.Key.Equals(TEXT("_inner_cone"), ESearchCase::IgnoreCase))
				{
					float InnerCone = FCString::Atof(*KV.Value);
					SpotLight->SpotLightComponent->SetInnerConeAngle(InnerCone);
				}
			}
			SpotLight->SetActorLabel(TargetName.IsEmpty() ? ClassName : TargetName);
			SpotLight->Tags.Add(TEXT("source:light_spot"));
			Result.EntitiesImported++;
			return true;
		}
	}
	else if (ClassName.Equals(TEXT("light_environment"), ESearchCase::IgnoreCase))
	{
		// Create a directional light for the sun, plus store as generic entity
		ADirectionalLight* DirLight = World->SpawnActor<ADirectionalLight>(
			ADirectionalLight::StaticClass(), SpawnTransform, SpawnParams);
		if (DirLight)
		{
			for (const auto& KV : KeyValues)
			{
				if (KV.Key.Equals(TEXT("_light"), ESearchCase::IgnoreCase))
				{
					TArray<FString> LightParts;
					KV.Value.ParseIntoArrayWS(LightParts);
					if (LightParts.Num() >= 4)
					{
						float R = FCString::Atof(*LightParts[0]) / 255.0f;
						float G = FCString::Atof(*LightParts[1]) / 255.0f;
						float B = FCString::Atof(*LightParts[2]) / 255.0f;
						float Brightness = FCString::Atof(*LightParts[3]);
						DirLight->GetComponent()->SetLightColor(FLinearColor(R, G, B));
						DirLight->GetComponent()->SetIntensity(Brightness * 0.5f);
					}
				}
			}
			DirLight->SetActorLabel(TargetName.IsEmpty() ? TEXT("light_environment") : TargetName);
			DirLight->Tags.Add(TEXT("source:light_environment"));
			Result.EntitiesImported++;
			return true;
		}
	}
	else if (ClassName.Equals(TEXT("env_cubemap"), ESearchCase::IgnoreCase))
	{
		// Create a UE reflection capture at the cubemap position
		ASphereReflectionCapture* Capture = World->SpawnActor<ASphereReflectionCapture>(
			ASphereReflectionCapture::StaticClass(), SpawnTransform, SpawnParams);
		if (Capture)
		{
			Capture->SetActorLabel(TargetName.IsEmpty() ? TEXT("env_cubemap") : TargetName);
			Capture->Tags.Add(TEXT("source:env_cubemap"));
			Result.EntitiesImported++;
			return true;
		}
	}
	else if (ClassName.StartsWith(TEXT("prop_"), ESearchCase::IgnoreCase))
	{
		ASourceProp* Prop = World->SpawnActor<ASourceProp>(ASourceProp::StaticClass(), SpawnTransform, SpawnParams);
		if (Prop)
		{
			// Override the default classname from constructor with the actual one
			Prop->SourceClassname = ClassName;
			float PropModelScale = 1.0f;

			for (const auto& KV : KeyValues)
			{
				if (KV.Key.Equals(TEXT("model"), ESearchCase::IgnoreCase))
					Prop->ModelPath = KV.Value;
				else if (KV.Key.Equals(TEXT("skin"), ESearchCase::IgnoreCase))
					Prop->Skin = FCString::Atoi(*KV.Value);
				else if (KV.Key.Equals(TEXT("solid"), ESearchCase::IgnoreCase))
					Prop->Solid = FCString::Atoi(*KV.Value);
				else if (KV.Key.Equals(TEXT("modelscale"), ESearchCase::IgnoreCase))
					PropModelScale = FCString::Atof(*KV.Value);
				else if (KV.Key.Equals(TEXT("disableshadows"), ESearchCase::IgnoreCase))
					Prop->bDisableShadows = FCString::Atoi(*KV.Value) != 0;
				else if (KV.Key.Equals(TEXT("fademindist"), ESearchCase::IgnoreCase))
					Prop->FadeMinDist = FCString::Atof(*KV.Value);
				else if (KV.Key.Equals(TEXT("fademaxdist"), ESearchCase::IgnoreCase))
					Prop->FadeMaxDist = FCString::Atof(*KV.Value);
				else if (KV.Key.Equals(TEXT("rendercolor"), ESearchCase::IgnoreCase))
				{
					TArray<FString> Parts;
					KV.Value.ParseIntoArray(Parts, TEXT(" "));
					if (Parts.Num() >= 3)
					{
						Prop->RenderColor = FColor(
							FCString::Atoi(*Parts[0]),
							FCString::Atoi(*Parts[1]),
							FCString::Atoi(*Parts[2]));
					}
				}
				else if (KV.Key.Equals(TEXT("renderamt"), ESearchCase::IgnoreCase))
					Prop->RenderAmt = FCString::Atoi(*KV.Value);
			}

			Prop->ModelScale = PropModelScale;

			// Try to resolve model geometry
			if (!Prop->ModelPath.IsEmpty())
			{
				UStaticMesh* ModelMesh = FModelImporter::ResolveModel(Prop->ModelPath, Prop->Skin);
				if (ModelMesh)
				{
					Prop->SetStaticMesh(ModelMesh);

					// Apply model scale
					if (!FMath::IsNearlyEqual(PropModelScale, 1.0f, 0.001f))
					{
						Prop->SetActorScale3D(FVector(PropModelScale));
					}

					// Apply shadow settings
					if (Prop->bDisableShadows && Prop->MeshComponent)
					{
						Prop->MeshComponent->SetCastShadow(false);
					}

					// Apply fade distances to component
					if (Prop->MeshComponent)
					{
						if (Prop->FadeMinDist > 0.0f)
						{
							Prop->MeshComponent->LDMaxDrawDistance = Prop->FadeMaxDist / 0.525f;
							Prop->MeshComponent->bNeverDistanceCull = false;
						}
					}

					// Store model metadata from parsed data for re-export
					const FSourceModelData* ParsedModel = FModelImporter::GetParsedModelData(Prop->ModelPath);
					if (ParsedModel)
					{
						Prop->SurfaceProp = ParsedModel->SurfaceProp;
						Prop->bIsStaticProp = ParsedModel->bIsStaticProp;
						Prop->ModelMass = ParsedModel->Mass;
						Prop->CDMaterials = ParsedModel->MaterialSearchDirs;
					}
				}
			}
		}
		Entity = Prop;
	}
	else if (ClassName.Equals(TEXT("env_sprite"), ESearchCase::IgnoreCase))
	{
		ASourceEnvSprite* Sprite = World->SpawnActor<ASourceEnvSprite>(
			ASourceEnvSprite::StaticClass(), SpawnTransform, SpawnParams);
		if (Sprite)
		{
			for (const auto& KV : KeyValues)
			{
				if (KV.Key.Equals(TEXT("model"), ESearchCase::IgnoreCase))
					Sprite->SpriteModel = KV.Value;
				else if (KV.Key.Equals(TEXT("rendermode"), ESearchCase::IgnoreCase))
					Sprite->RenderMode = FCString::Atoi(*KV.Value);
				else if (KV.Key.Equals(TEXT("scale"), ESearchCase::IgnoreCase))
					Sprite->SourceSpriteScale = FCString::Atof(*KV.Value);
			}
		}
		Entity = Sprite;
	}
	else if (ClassName.Equals(TEXT("env_soundscape"), ESearchCase::IgnoreCase))
	{
		ASourceSoundscape* Soundscape = World->SpawnActor<ASourceSoundscape>(
			ASourceSoundscape::StaticClass(), SpawnTransform, SpawnParams);
		if (Soundscape)
		{
			for (const auto& KV : KeyValues)
			{
				if (KV.Key.Equals(TEXT("soundscape"), ESearchCase::IgnoreCase))
					Soundscape->SoundscapeName = KV.Value;
				else if (KV.Key.Equals(TEXT("radius"), ESearchCase::IgnoreCase))
					Soundscape->Radius = FCString::Atof(*KV.Value);
			}
		}
		Entity = Soundscape;
	}
	else
	{
		// Generic entity
		Entity = World->SpawnActor<ASourceGenericEntity>(ASourceGenericEntity::StaticClass(), SpawnTransform, SpawnParams);
	}

	if (!Entity) return false;

	// Apply common properties using shared helper
	ApplyEntityProperties(Entity, EntityBlock);

	// Set actor label
	if (!Entity->TargetName.IsEmpty())
	{
		Entity->SetActorLabel(Entity->TargetName);
	}
	else
	{
		Entity->SetActorLabel(ClassName);
	}

	// Track for parentname resolution
	Result.SpawnedEntities.Add(Entity);

	Result.EntitiesImported++;
	return true;
}
