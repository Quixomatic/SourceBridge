#include "Entities/PropExporter.h"
#include "Utilities/SourceCoord.h"
#include "VMF/VMFExporter.h"
#include "Materials/MaterialMapper.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "StaticMeshResources.h"
#include "EngineUtils.h"

TArray<FVMFKeyValues> FPropExporter::ExportProps(
	UWorld* World,
	int32& EntityIdCounter,
	const FPropExportSettings& Settings)
{
	TArray<FVMFKeyValues> Entities;

	if (!World) return Entities;

	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* Actor = *It;
		if (!Actor) continue;

		// Skip actors tagged with "noexport"
		bool bSkip = false;
		for (const FName& Tag : Actor->Tags)
		{
			FString TagStr = Tag.ToString();
			if (TagStr.Equals(TEXT("noexport"), ESearchCase::IgnoreCase))
			{
				bSkip = true;
				break;
			}
			// Skip actors that will be converted to brushes
			if (TagStr.Equals(TEXT("source:worldspawn"), ESearchCase::IgnoreCase) ||
				TagStr.Equals(TEXT("source:func_detail"), ESearchCase::IgnoreCase) ||
				TagStr.StartsWith(TEXT("source:"), ESearchCase::IgnoreCase))
			{
				// Check if it's a brush conversion tag (not source:prop_static)
				if (!TagStr.Equals(TEXT("source:prop_static"), ESearchCase::IgnoreCase))
				{
					bSkip = true;
					break;
				}
			}
		}
		if (bSkip) continue;

		UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
		if (!MeshComp || !MeshComp->GetStaticMesh()) continue;

		EntityIdCounter++;
		FVMFKeyValues Entity = ExportProp(Actor, EntityIdCounter, Settings);
		Entities.Add(Entity);
	}

	if (Entities.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Exported %d prop entities."), Entities.Num());
	}

	return Entities;
}

FVMFKeyValues FPropExporter::ExportProp(
	AStaticMeshActor* Actor,
	int32 EntityId,
	const FPropExportSettings& Settings)
{
	EPropExportMode Mode = GetExportMode(Actor, Settings.DefaultMode);
	FString Classname = GetClassname(Mode);
	FString ModelPath = GetModelPath(Actor, Settings.ModelPathPrefix);

	FVector SrcPos = FSourceCoord::UEToSource(Actor->GetActorLocation());
	FString SrcAngles = FSourceCoord::UERotationToSourceAngles(Actor->GetActorRotation());

	FVMFKeyValues Entity;
	Entity.ClassName = TEXT("entity");
	Entity.Properties.Add(TPair<FString, FString>(TEXT("id"), FString::FromInt(EntityId)));
	Entity.Properties.Add(TPair<FString, FString>(TEXT("classname"), Classname));
	Entity.Properties.Add(TPair<FString, FString>(TEXT("origin"),
		FSourceCoord::FormatVector(SrcPos)));
	Entity.Properties.Add(TPair<FString, FString>(TEXT("angles"), SrcAngles));
	Entity.Properties.Add(TPair<FString, FString>(TEXT("model"), ModelPath));

	// Parse additional keyvalues from tags
	FString Skin = TEXT("0");
	FString Solid = TEXT("6"); // VPhysics by default for static
	FString TargetName;

	for (const FName& Tag : Actor->Tags)
	{
		FString TagStr = Tag.ToString();

		if (TagStr.StartsWith(TEXT("skin:"), ESearchCase::IgnoreCase))
		{
			Skin = TagStr.Mid(5).TrimStartAndEnd();
		}
		else if (TagStr.StartsWith(TEXT("solid:"), ESearchCase::IgnoreCase))
		{
			Solid = TagStr.Mid(6).TrimStartAndEnd();
		}
		else if (TagStr.StartsWith(TEXT("targetname:"), ESearchCase::IgnoreCase))
		{
			TargetName = TagStr.Mid(11).TrimStartAndEnd();
		}
		else if (TagStr.StartsWith(TEXT("kv:"), ESearchCase::IgnoreCase))
		{
			// kv:key:value format
			FString KVStr = TagStr.Mid(3);
			int32 ColonIdx;
			if (KVStr.FindChar(TEXT(':'), ColonIdx))
			{
				FString Key = KVStr.Left(ColonIdx);
				FString Value = KVStr.Mid(ColonIdx + 1);
				Entity.Properties.Add(TPair<FString, FString>(Key, Value));
			}
		}
	}

	Entity.Properties.Add(TPair<FString, FString>(TEXT("skin"), Skin));
	Entity.Properties.Add(TPair<FString, FString>(TEXT("solid"), Solid));

	if (!TargetName.IsEmpty())
	{
		Entity.Properties.Add(TPair<FString, FString>(TEXT("targetname"), TargetName));
	}

	// Scale - only if non-uniform (Source prop_static supports modelscale)
	FVector Scale = Actor->GetActorScale3D();
	if (!Scale.Equals(FVector::OneVector, 0.01f))
	{
		// Source only supports uniform scale for props
		float UniformScale = (Scale.X + Scale.Y + Scale.Z) / 3.0f;
		Entity.Properties.Add(TPair<FString, FString>(TEXT("modelscale"),
			FString::Printf(TEXT("%g"), UniformScale)));

		if (!FMath::IsNearlyEqual(Scale.X, Scale.Y, 0.01f) ||
			!FMath::IsNearlyEqual(Scale.Y, Scale.Z, 0.01f))
		{
			UE_LOG(LogTemp, Warning, TEXT("SourceBridge: Non-uniform scale on %s (%s). Source only supports uniform scale - using average."),
				*Actor->GetName(), *Scale.ToString());
		}
	}

	return Entity;
}

EPropExportMode FPropExporter::GetExportMode(AStaticMeshActor* Actor, EPropExportMode Default)
{
	for (const FName& Tag : Actor->Tags)
	{
		FString TagStr = Tag.ToString();

		if (TagStr.Equals(TEXT("prop_static"), ESearchCase::IgnoreCase))
			return EPropExportMode::PropStatic;
		if (TagStr.Equals(TEXT("prop_dynamic"), ESearchCase::IgnoreCase))
			return EPropExportMode::PropDynamic;
		if (TagStr.Equals(TEXT("prop_physics"), ESearchCase::IgnoreCase))
			return EPropExportMode::PropPhysics;
		if (TagStr.Equals(TEXT("func_detail"), ESearchCase::IgnoreCase))
			return EPropExportMode::FuncDetail;
	}

	// Physics-simulating actors default to prop_physics
	UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
	if (MeshComp && MeshComp->IsSimulatingPhysics())
	{
		return EPropExportMode::PropPhysics;
	}

	// Moveable actors default to prop_dynamic
	if (Actor->IsRootComponentMovable())
	{
		return EPropExportMode::PropDynamic;
	}

	return Default;
}

FString FPropExporter::GetModelPath(AStaticMeshActor* Actor, const FString& Prefix)
{
	// Check for mdl: tag override
	for (const FName& Tag : Actor->Tags)
	{
		FString TagStr = Tag.ToString();
		if (TagStr.StartsWith(TEXT("mdl:"), ESearchCase::IgnoreCase))
		{
			FString Path = TagStr.Mid(4).TrimStartAndEnd();
			// Ensure .mdl extension
			if (!Path.EndsWith(TEXT(".mdl")))
			{
				Path += TEXT(".mdl");
			}
			return Path;
		}
	}

	// Auto-generate from mesh name
	UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
	if (MeshComp && MeshComp->GetStaticMesh())
	{
		FString MeshName = MeshComp->GetStaticMesh()->GetName().ToLower();

		// Strip common UE prefixes
		if (MeshName.StartsWith(TEXT("sm_"))) MeshName = MeshName.Mid(3);
		else if (MeshName.StartsWith(TEXT("s_"))) MeshName = MeshName.Mid(2);

		return Prefix + MeshName + TEXT(".mdl");
	}

	return TEXT("models/error.mdl");
}

FString FPropExporter::GetClassname(EPropExportMode Mode)
{
	switch (Mode)
	{
	case EPropExportMode::PropStatic:  return TEXT("prop_static");
	case EPropExportMode::PropDynamic: return TEXT("prop_dynamic");
	case EPropExportMode::PropPhysics: return TEXT("prop_physics");
	case EPropExportMode::FuncDetail:  return TEXT("func_detail");
	default: return TEXT("prop_static");
	}
}

// ---- Mesh to Brush Conversion ----

TOptional<FString> FPropExporter::ShouldConvertToBrush(AStaticMeshActor* Actor)
{
	if (!Actor) return TOptional<FString>();

	for (const FName& Tag : Actor->Tags)
	{
		FString TagStr = Tag.ToString();

		if (TagStr.Equals(TEXT("source:worldspawn"), ESearchCase::IgnoreCase))
		{
			return FString(); // Empty string = worldspawn
		}
		if (TagStr.Equals(TEXT("source:func_detail"), ESearchCase::IgnoreCase))
		{
			return FString(TEXT("func_detail"));
		}
		if (TagStr.StartsWith(TEXT("source:"), ESearchCase::IgnoreCase) &&
			!TagStr.Equals(TEXT("source:prop_static"), ESearchCase::IgnoreCase))
		{
			// Generic source:classname tag
			return FString(TagStr.Mid(7));
		}
		if (TagStr.Equals(TEXT("source:prop_static"), ESearchCase::IgnoreCase))
		{
			return TOptional<FString>(); // Force prop export
		}
	}

	// No tag — don't auto-convert (user must explicitly opt in)
	return TOptional<FString>();
}

TArray<FPropExporter::FMeshFace> FPropExporter::ExtractFaces(
	const TArray<FVector>& Vertices,
	const TArray<uint32>& Indices,
	const TArray<int32>& MaterialIds,
	const TArray<FString>& MaterialNames)
{
	TArray<FMeshFace> Faces;

	// Group triangles by their normal (merge coplanar triangles)
	const float CoplanarThreshold = 0.999f; // cos(~2.5 degrees)
	const float DistanceThreshold = 0.1f;   // Maximum distance from plane

	struct FTriData
	{
		FVector V0, V1, V2;
		FVector Normal;
		int32 MaterialId;
		bool bUsed = false;
	};

	TArray<FTriData> Triangles;
	for (int32 i = 0; i + 2 < Indices.Num(); i += 3)
	{
		FTriData Tri;
		Tri.V0 = Vertices[Indices[i]];
		Tri.V1 = Vertices[Indices[i + 1]];
		Tri.V2 = Vertices[Indices[i + 2]];
		Tri.Normal = FVector::CrossProduct(Tri.V1 - Tri.V0, Tri.V2 - Tri.V0).GetSafeNormal();
		Tri.MaterialId = (i / 3 < MaterialIds.Num()) ? MaterialIds[i / 3] : 0;

		if (!Tri.Normal.IsNearlyZero())
		{
			Triangles.Add(Tri);
		}
	}

	// Merge coplanar triangles into faces
	for (int32 i = 0; i < Triangles.Num(); i++)
	{
		if (Triangles[i].bUsed) continue;

		FMeshFace Face;
		Face.Normal = Triangles[i].Normal;
		Face.Material = MaterialNames.IsValidIndex(Triangles[i].MaterialId)
			? MaterialNames[Triangles[i].MaterialId]
			: TEXT("DEV/DEV_MEASUREWALL01A");

		// Collect all coplanar triangles
		TArray<FVector> AllVerts;
		float PlaneDist = FVector::DotProduct(Triangles[i].V0, Face.Normal);

		for (int32 j = i; j < Triangles.Num(); j++)
		{
			if (Triangles[j].bUsed) continue;

			// Check if coplanar: same normal direction and same plane distance
			float NormalDot = FVector::DotProduct(Triangles[j].Normal, Face.Normal);
			if (NormalDot < CoplanarThreshold) continue;

			float ThisDist = FVector::DotProduct(Triangles[j].V0, Face.Normal);
			if (FMath::Abs(ThisDist - PlaneDist) > DistanceThreshold) continue;

			AllVerts.Add(Triangles[j].V0);
			AllVerts.Add(Triangles[j].V1);
			AllVerts.Add(Triangles[j].V2);
			Triangles[j].bUsed = true;
		}

		// Remove duplicate vertices
		TArray<FVector> UniqueVerts;
		for (const FVector& V : AllVerts)
		{
			bool bDuplicate = false;
			for (const FVector& Existing : UniqueVerts)
			{
				if (FVector::DistSquared(V, Existing) < 0.01f)
				{
					bDuplicate = true;
					break;
				}
			}
			if (!bDuplicate)
			{
				UniqueVerts.Add(V);
			}
		}

		Face.Vertices = MoveTemp(UniqueVerts);
		if (Face.Vertices.Num() >= 3)
		{
			Faces.Add(MoveTemp(Face));
		}
	}

	return Faces;
}

bool FPropExporter::IsMeshConvex(const TArray<FMeshFace>& Faces)
{
	if (Faces.Num() < 4) return false;

	// For a convex mesh, every vertex must be on or behind every face plane
	for (const FMeshFace& Face : Faces)
	{
		if (Face.Vertices.Num() < 3) return false;

		FVector PlanePoint = Face.Vertices[0];
		FVector PlaneNormal = Face.Normal;

		for (const FMeshFace& OtherFace : Faces)
		{
			for (const FVector& V : OtherFace.Vertices)
			{
				float Dist = FVector::DotProduct(V - PlanePoint, PlaneNormal);
				if (Dist > 1.0f) // Allow small tolerance
				{
					return false;
				}
			}
		}
	}

	return true;
}

FMeshToBrushResult FPropExporter::ConvertMeshToBrush(
	AStaticMeshActor* Actor,
	int32& SolidIdCounter,
	int32& SideIdCounter,
	const FString& ForcedEntityClass)
{
	FMeshToBrushResult Result;
	Result.EntityClass = ForcedEntityClass;

	if (!Actor) return Result;

	UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
	if (!MeshComp || !MeshComp->GetStaticMesh())
	{
		Result.Warnings.Add(FString::Printf(TEXT("Static mesh '%s' has no mesh data"), *Actor->GetName()));
		return Result;
	}

	UStaticMesh* Mesh = MeshComp->GetStaticMesh();

	// Get render data from LOD 0
	if (!Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Static mesh '%s' has no LOD data"), *Actor->GetName()));
		return Result;
	}

	const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
	const FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;
	const FRawStaticIndexBuffer& IdxBuffer = LOD.IndexBuffer;

	// Extract vertices in world space (with actor transform applied)
	FTransform ActorTransform = Actor->GetActorTransform();
	TArray<FVector> WorldVerts;
	WorldVerts.Reserve(PosBuffer.GetNumVertices());
	for (uint32 i = 0; i < PosBuffer.GetNumVertices(); i++)
	{
		FVector LocalPos = FVector(PosBuffer.VertexPosition(i));
		WorldVerts.Add(ActorTransform.TransformPosition(LocalPos));
	}

	// Extract indices
	TArray<uint32> Indices;
	TArray<int32> TriMaterialIds;
	IdxBuffer.GetCopy(Indices);

	// Collect material names and per-triangle material IDs
	TArray<FString> MaterialNames;
	for (int32 i = 0; i < LOD.Sections.Num(); i++)
	{
		UMaterialInterface* Mat = MeshComp->GetMaterial(i);
		FString MatName = Mat ? Mat->GetName() : TEXT("DEV/DEV_MEASUREWALL01A");
		MaterialNames.Add(MatName);
	}

	// Build per-triangle material ID array
	for (const FStaticMeshSection& Section : LOD.Sections)
	{
		int32 TriCount = Section.NumTriangles;
		for (int32 t = 0; t < TriCount; t++)
		{
			TriMaterialIds.Add(Section.MaterialIndex);
		}
	}

	// Extract faces by merging coplanar triangles
	TArray<FMeshFace> Faces = ExtractFaces(WorldVerts, Indices, TriMaterialIds, MaterialNames);

	if (Faces.Num() < 4)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Static mesh '%s' has only %d faces (need >=4 for a solid)"),
			*Actor->GetName(), Faces.Num()));
		return Result;
	}

	// Check convexity
	if (!IsMeshConvex(Faces))
	{
		Result.Warnings.Add(FString::Printf(TEXT("Static mesh '%s' is non-convex, cannot convert to brush"),
			*Actor->GetName()));
		return Result;
	}

	// Build VMF solid from faces
	FVMFKeyValues Solid(TEXT("solid"));
	Solid.AddProperty(TEXT("id"), SolidIdCounter++);

	for (const FMeshFace& Face : Faces)
	{
		if (Face.Vertices.Num() < 3) continue;

		// Pick 3 non-collinear vertices for the plane definition
		// Convert to Source coordinates
		FVector P1 = FSourceCoord::UEToSource(Face.Vertices[0]);
		FVector P2 = FSourceCoord::UEToSource(Face.Vertices[1]);

		// Find a third point that's not collinear with P1-P2
		FVector P3 = FSourceCoord::UEToSource(Face.Vertices[2]);
		for (int32 i = 2; i < Face.Vertices.Num(); i++)
		{
			FVector Candidate = FSourceCoord::UEToSource(Face.Vertices[i]);
			FVector Cross = FVector::CrossProduct(P2 - P1, Candidate - P1);
			if (Cross.SizeSquared() > 0.01f)
			{
				P3 = Candidate;
				break;
			}
		}

		// VMF plane convention: (P2-P1)x(P3-P1) points INWARD
		// Our face normal points outward, so we need to arrange points so cross product points inward
		// Cross = (P2-P1)x(P3-P1). If this points outward (same dir as face normal), swap P2 and P3.
		FVector SourceNormal = FSourceCoord::UEToSourceDirection(Face.Normal);
		FVector Cross = FVector::CrossProduct(P2 - P1, P3 - P1);
		if (FVector::DotProduct(Cross, SourceNormal) > 0)
		{
			// Cross points outward — swap P2/P3 so it points inward
			Swap(P2, P3);
		}

		FString PlaneStr = FString::Printf(TEXT("(%g %g %g) (%g %g %g) (%g %g %g)"),
			P1.X, P1.Y, P1.Z, P2.X, P2.Y, P2.Z, P3.X, P3.Y, P3.Z);

		// Get UV axes based on the Source-space normal
		FString UAxis, VAxis;
		FVector AbsNormal(FMath::Abs(SourceNormal.X), FMath::Abs(SourceNormal.Y), FMath::Abs(SourceNormal.Z));
		if (AbsNormal.Z >= AbsNormal.X && AbsNormal.Z >= AbsNormal.Y)
		{
			UAxis = TEXT("[1 0 0 0] 0.25");
			VAxis = TEXT("[0 -1 0 0] 0.25");
		}
		else if (AbsNormal.Y >= AbsNormal.X)
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
		Side.AddProperty(TEXT("plane"), PlaneStr);
		Side.AddProperty(TEXT("material"), Face.Material);
		Side.AddProperty(TEXT("uaxis"), UAxis);
		Side.AddProperty(TEXT("vaxis"), VAxis);
		Side.AddProperty(TEXT("rotation"), 0);
		Side.AddProperty(TEXT("lightmapscale"), 16);
		Side.AddProperty(TEXT("smoothing_groups"), 0);
		Solid.Children.Add(MoveTemp(Side));
	}

	Result.Solids.Add(MoveTemp(Solid));
	Result.bSuccess = true;

	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Converted static mesh '%s' to brush (%d faces, entity: %s)"),
		*Actor->GetName(), Faces.Num(),
		ForcedEntityClass.IsEmpty() ? TEXT("worldspawn") : *ForcedEntityClass);

	return Result;
}

TArray<FMeshToBrushResult> FPropExporter::CollectMeshBrushes(
	UWorld* World,
	int32& SolidIdCounter,
	int32& SideIdCounter)
{
	TArray<FMeshToBrushResult> Results;

	if (!World) return Results;

	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* Actor = *It;
		if (!Actor) continue;

		// Check for noexport tag
		bool bNoExport = false;
		for (const FName& Tag : Actor->Tags)
		{
			if (Tag.ToString().Equals(TEXT("noexport"), ESearchCase::IgnoreCase))
			{
				bNoExport = true;
				break;
			}
		}
		if (bNoExport) continue;

		TOptional<FString> BrushClass = ShouldConvertToBrush(Actor);
		if (!BrushClass.IsSet()) continue;

		FMeshToBrushResult Result = ConvertMeshToBrush(Actor, SolidIdCounter, SideIdCounter, BrushClass.GetValue());

		if (!Result.bSuccess)
		{
			for (const FString& Warning : Result.Warnings)
			{
				UE_LOG(LogTemp, Warning, TEXT("SourceBridge: %s"), *Warning);
			}
		}
		else
		{
			Results.Add(MoveTemp(Result));
		}
	}

	return Results;
}
