#include "Import/VMFImporter.h"
#include "Import/VMFReader.h"
#include "Import/MaterialImporter.h"
#include "Actors/SourceEntityActor.h"
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

	// Clear material cache for fresh import
	FMaterialImporter::ClearCache();

	for (const FVMFKeyValues& Block : Blocks)
	{
		if (Block.ClassName.Equals(TEXT("world"), ESearchCase::IgnoreCase))
		{
			// Import worldspawn solids
			if (Settings.bImportBrushes)
			{
				for (const FVMFKeyValues& Child : Block.Children)
				{
					if (Child.ClassName.Equals(TEXT("solid"), ESearchCase::IgnoreCase))
					{
						ImportSolid(Child, World, Settings, Result);
					}
				}
			}
		}
		else if (Block.ClassName.Equals(TEXT("entity"), ESearchCase::IgnoreCase))
		{
			if (!Settings.bImportEntities) continue;

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
				// Brush entity: import solids, tag with classname
				FString EntityClass;
				FString TargetName;
				for (const auto& Prop : Block.Properties)
				{
					if (Prop.Key.Equals(TEXT("classname"), ESearchCase::IgnoreCase))
						EntityClass = Prop.Value;
					else if (Prop.Key.Equals(TEXT("targetname"), ESearchCase::IgnoreCase))
						TargetName = Prop.Value;
				}

				for (const FVMFKeyValues& Child : Block.Children)
				{
					if (Child.ClassName.Equals(TEXT("solid"), ESearchCase::IgnoreCase))
					{
						ABrush* Brush = ImportSolid(Child, World, Settings, Result);
						if (Brush)
						{
							// Tag the brush with entity class info
							if (!EntityClass.IsEmpty())
							{
								Brush->Tags.Add(*FString::Printf(TEXT("source:%s"), *EntityClass));
							}
							if (!TargetName.IsEmpty())
							{
								Brush->Tags.Add(*FString::Printf(TEXT("targetname:%s"), *TargetName));
								Brush->SetActorLabel(FString::Printf(TEXT("%s (%s)"), *TargetName, *EntityClass));
							}
							else if (!EntityClass.IsEmpty())
							{
								Brush->SetActorLabel(EntityClass);
							}
						}
					}
				}
			}
			else
			{
				ImportPointEntity(Block, World, Settings, Result);
			}
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
	const TArray<FPlane>& Planes, const TArray<FVector>& PlanePoints)
{
	TArray<TArray<FVector>> Faces;

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
		}
	}

	return Faces;
}

// ---- Brush Creation ----

ABrush* FVMFImporter::CreateBrushFromFaces(
	UWorld* World,
	const TArray<TArray<FVector>>& Faces,
	const TArray<FVector>& FaceNormals,
	const TArray<FVMFSideData>& SideData,
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

	// Spawn brush actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABrush* Brush = World->SpawnActor<ABrush>(ABrush::StaticClass(), FTransform(Center), SpawnParams);
	if (!Brush) return nullptr;

	Brush->BrushType = Brush_Add;
	Brush->SetActorLabel(TEXT("ImportedBrush"));

	// Create the model
	UModel* Model = NewObject<UModel>(Brush, NAME_None, RF_Transactional);
	Model->Initialize(Brush, true);
	Model->Polys = NewObject<UPolys>(Model, NAME_None, RF_Transactional);
	Brush->Brush = Model;

	// Add faces as polys
	for (int32 FaceIdx = 0; FaceIdx < Faces.Num(); FaceIdx++)
	{
		const TArray<FVector>& FaceVerts = Faces[FaceIdx];
		if (FaceVerts.Num() < 3) continue;

		FPoly Poly;
		Poly.Init();

		// Add vertices in local space (relative to brush center)
		for (const FVector& V : FaceVerts)
		{
			FVector UEPos = SourceToUE(V, Scale);
			Poly.Vertices.Add(FVector3f(UEPos - Center));
		}

		// Compute normal from vertices
		if (FaceIdx < FaceNormals.Num())
		{
			// Reverse the inward VMF normal to get UE outward normal, and convert coords
			FVector SourceNormal = FaceNormals[FaceIdx];
			FVector UENormal(-SourceNormal.X, SourceNormal.Y, -SourceNormal.Z);
			Poly.Normal = FVector3f(UENormal.GetSafeNormal());
		}
		else
		{
			Poly.CalcNormal();
		}

		// Apply per-face data (material, UV axes)
		if (FaceIdx < SideData.Num())
		{
			const FVMFSideData& Side = SideData[FaceIdx];

			// Material name
			if (Settings.bImportMaterials && !Side.Material.IsEmpty())
			{
				Poly.ItemName = FName(*Side.Material);
			}

			// Texture axes: convert Source UV axes to UE FPoly texture vectors
			// Source: U_texel = (Pos · UAxis) / UScale + UOffset
			// UE FPoly: TextureU/TextureV define the texture projection direction
			FVector UEUAxis = SourceDirToUE(Side.UAxis);
			FVector UEVAxis = SourceDirToUE(Side.VAxis);

			// TextureU/V magnitude = 1/scale (texels per world unit)
			// Also divide by import scale since vertices are scaled
			if (!FMath::IsNearlyZero(Side.UScale))
			{
				Poly.TextureU = FVector3f(UEUAxis / (Side.UScale * Scale));
			}
			if (!FMath::IsNearlyZero(Side.VScale))
			{
				Poly.TextureV = FVector3f(UEVAxis / (Side.VScale * Scale));
			}

			// Base point for texture offset
			Poly.Base = Poly.Vertices.Num() > 0 ? Poly.Vertices[0] : FVector3f::ZeroVector;
		}

		Model->Polys->Element.Add(MoveTemp(Poly));
	}

	Model->BuildBound();

	return Brush;
}

// ---- Solid Import ----

ABrush* FVMFImporter::ImportSolid(const FVMFKeyValues& SolidBlock, UWorld* World,
	const FVMFImportSettings& Settings, FVMFImportResult& Result)
{
	TArray<FPlane> Planes;
	TArray<FVector> PlaneFirstPoints;
	TArray<FVector> InwardNormals;
	TArray<FVMFSideData> SideDataArray;

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
				ParseUVAxis(Prop.Value, SideData.UAxis, SideData.UOffset, SideData.UScale);
			else if (Prop.Key.Equals(TEXT("vaxis"), ESearchCase::IgnoreCase))
				ParseUVAxis(Prop.Value, SideData.VAxis, SideData.VOffset, SideData.VScale);
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
		// VMF convention: (P2-P1)×(P3-P1) points INWARD
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
		InwardNormals.Add(Normal);
		SideDataArray.Add(MoveTemp(SideData));
	}

	if (Planes.Num() < 4)
	{
		Result.Warnings.Add(TEXT("Solid has fewer than 4 valid planes, skipping."));
		return nullptr;
	}

	// Reconstruct face polygons via CSG clipping
	TArray<TArray<FVector>> Faces = ReconstructFacesFromPlanes(Planes, PlaneFirstPoints);

	if (Faces.Num() < 4)
	{
		Result.Warnings.Add(TEXT("CSG reconstruction produced fewer than 4 faces, skipping solid."));
		return nullptr;
	}

	ABrush* Brush = CreateBrushFromFaces(World, Faces, InwardNormals, SideDataArray, Settings);
	if (Brush)
	{
		Result.BrushesImported++;
		return Brush;
	}

	Result.Warnings.Add(TEXT("Failed to create brush from faces."));
	return nullptr;
}

// ---- Entity Import ----

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
			for (const auto& KV : KeyValues)
			{
				if (KV.Key.Equals(TEXT("model"), ESearchCase::IgnoreCase))
					Prop->ModelPath = KV.Value;
				else if (KV.Key.Equals(TEXT("skin"), ESearchCase::IgnoreCase))
					Prop->Skin = FCString::Atoi(*KV.Value);
				else if (KV.Key.Equals(TEXT("solid"), ESearchCase::IgnoreCase))
					Prop->Solid = FCString::Atoi(*KV.Value);
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

	// Set common properties
	Entity->SourceClassname = ClassName;
	if (!TargetName.IsEmpty())
	{
		Entity->TargetName = TargetName;
		Entity->SetActorLabel(TargetName);
	}
	else
	{
		Entity->SetActorLabel(ClassName);
	}

	// Store remaining key-values
	for (const auto& KV : KeyValues)
	{
		Entity->KeyValues.Add(KV.Key, KV.Value);
	}

	// Parse spawnflags
	const FString* SpawnFlagsStr = Entity->KeyValues.Find(TEXT("spawnflags"));
	if (SpawnFlagsStr)
	{
		Entity->SpawnFlags = FCString::Atoi(**SpawnFlagsStr);
	}

	// Parse I/O connections and store as actor tags (io:OutputName:target,input,param,delay,refire)
	for (const FVMFKeyValues& Child : EntityBlock.Children)
	{
		if (Child.ClassName.Equals(TEXT("connections"), ESearchCase::IgnoreCase))
		{
			for (const auto& Conn : Child.Properties)
			{
				// Conn.Key = OutputName, Conn.Value = "target,input,param,delay,refire"
				FString Tag = FString::Printf(TEXT("io:%s:%s"), *Conn.Key, *Conn.Value);
				Entity->Tags.Add(*Tag);
			}
		}
	}

	Result.EntitiesImported++;
	return true;
}
