#include "Models/SMDExporter.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Rendering/PositionVertexBuffer.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

FSMDExportResult FSMDExporter::ExportStaticMesh(UStaticMesh* Mesh, float Scale)
{
	FSMDExportResult Result;

	if (!Mesh)
	{
		Result.ErrorMessage = TEXT("Null mesh provided.");
		return Result;
	}

	// Get mesh description for LOD 0
	const FMeshDescription* MeshDesc = Mesh->GetMeshDescription(0);
	if (!MeshDesc)
	{
		Result.ErrorMessage = FString::Printf(TEXT("No mesh description for LOD 0 on %s"), *Mesh->GetName());
		return Result;
	}

	FStaticMeshConstAttributes Attributes(*MeshDesc);
	TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesConstRef<FVector3f> InstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesConstRef<FVector2f> InstanceUVs = Attributes.GetVertexInstanceUVs();

	// Get materials
	TArray<FStaticMaterial>& StaticMaterials = Mesh->GetStaticMaterials();

	// Single root bone for static meshes
	TArray<FSMDBone> Bones;
	FSMDBone RootBone;
	RootBone.Index = 0;
	RootBone.Name = TEXT("root");
	RootBone.ParentIndex = -1;
	RootBone.Position = FVector::ZeroVector;
	RootBone.Rotation = FVector::ZeroVector;
	Bones.Add(RootBone);

	// Extract triangles from mesh description
	TArray<FSMDTriangle> Triangles;
	TArray<FSMDTriangle> PhysicsTriangles;

	for (const FPolygonID PolygonID : MeshDesc->Polygons().GetElementIDs())
	{
		// Get the polygon's triangle vertex instances
		TArray<FTriangleID> TriangleIDs;
		MeshDesc->GetPolygonTriangles(PolygonID, TriangleIDs);

		// Get material for this polygon
		FPolygonGroupID GroupID = MeshDesc->GetPolygonPolygonGroup(PolygonID);
		int32 MaterialIndex = GroupID.GetValue();
		FString MaterialName = TEXT("default");

		if (StaticMaterials.IsValidIndex(MaterialIndex))
		{
			if (UMaterialInterface* Mat = StaticMaterials[MaterialIndex].MaterialInterface)
			{
				MaterialName = CleanMaterialName(Mat->GetName());
			}
			else if (!StaticMaterials[MaterialIndex].MaterialSlotName.IsNone())
			{
				MaterialName = CleanMaterialName(StaticMaterials[MaterialIndex].MaterialSlotName.ToString());
			}
		}

		if (!Result.MaterialNames.Contains(MaterialName))
		{
			Result.MaterialNames.Add(MaterialName);
		}

		for (const FTriangleID TriangleID : TriangleIDs)
		{
			FSMDTriangle Tri;
			Tri.MaterialName = MaterialName;

			TArrayView<const FVertexInstanceID> TriVertInstances = MeshDesc->GetTriangleVertexInstances(TriangleID);

			for (int32 i = 0; i < 3; i++)
			{
				FVertexInstanceID InstanceID = TriVertInstances[i];
				FVertexID VertexID = MeshDesc->GetVertexInstanceVertex(InstanceID);

				FSMDVertex& V = Tri.Vertices[i];
				V.BoneIndex = 0;

				FVector3f Pos = VertexPositions[VertexID];
				V.Position = ConvertPosition(FVector(Pos.X, Pos.Y, Pos.Z), Scale);

				FVector3f Norm = InstanceNormals[InstanceID];
				V.Normal = ConvertNormal(FVector(Norm.X, Norm.Y, Norm.Z));

				FVector2f UV = InstanceUVs[InstanceID];
				V.UV = FVector2D(UV.X, UV.Y);
			}

			Triangles.Add(Tri);
		}
	}

	if (Triangles.Num() == 0)
	{
		Result.ErrorMessage = FString::Printf(TEXT("No triangles extracted from %s"), *Mesh->GetName());
		return Result;
	}

	// Build reference SMD
	Result.ReferenceSMD = BuildSMD(Triangles, Bones);

	// Build physics SMD (same geometry for now - simplified collision would be better)
	// For static meshes, use the same mesh as a basic collision model
	Result.PhysicsSMD = BuildSMD(Triangles, Bones);

	// Build idle animation SMD
	Result.IdleSMD = BuildIdleSMD(Bones);

	Result.bSuccess = true;

	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Exported %s - %d triangles, %d materials"),
		*Mesh->GetName(), Triangles.Num(), Result.MaterialNames.Num());

	return Result;
}

FString FSMDExporter::BuildSMD(const TArray<FSMDTriangle>& Triangles, const TArray<FSMDBone>& Bones)
{
	FString Out;
	Out.Reserve(Triangles.Num() * 256);

	// Header
	Out += TEXT("version 1\n");

	// Nodes
	Out += TEXT("nodes\n");
	for (const FSMDBone& Bone : Bones)
	{
		Out += FString::Printf(TEXT("  %d \"%s\" %d\n"), Bone.Index, *Bone.Name, Bone.ParentIndex);
	}
	Out += TEXT("end\n");

	// Skeleton (bind pose)
	Out += TEXT("skeleton\n");
	Out += TEXT("  time 0\n");
	for (const FSMDBone& Bone : Bones)
	{
		Out += FString::Printf(TEXT("    %d  %.6f %.6f %.6f  %.6f %.6f %.6f\n"),
			Bone.Index,
			Bone.Position.X, Bone.Position.Y, Bone.Position.Z,
			Bone.Rotation.X, Bone.Rotation.Y, Bone.Rotation.Z);
	}
	Out += TEXT("end\n");

	// Triangles
	Out += TEXT("triangles\n");
	for (const FSMDTriangle& Tri : Triangles)
	{
		Out += Tri.MaterialName + TEXT("\n");
		for (int32 i = 0; i < 3; i++)
		{
			const FSMDVertex& V = Tri.Vertices[i];
			Out += FString::Printf(TEXT("  %d  %.6f %.6f %.6f  %.6f %.6f %.6f  %.6f %.6f\n"),
				V.BoneIndex,
				V.Position.X, V.Position.Y, V.Position.Z,
				V.Normal.X, V.Normal.Y, V.Normal.Z,
				V.UV.X, V.UV.Y);
		}
	}
	Out += TEXT("end\n");

	return Out;
}

FString FSMDExporter::BuildIdleSMD(const TArray<FSMDBone>& Bones)
{
	FString Out;

	Out += TEXT("version 1\n");

	Out += TEXT("nodes\n");
	for (const FSMDBone& Bone : Bones)
	{
		Out += FString::Printf(TEXT("  %d \"%s\" %d\n"), Bone.Index, *Bone.Name, Bone.ParentIndex);
	}
	Out += TEXT("end\n");

	Out += TEXT("skeleton\n");
	Out += TEXT("  time 0\n");
	for (const FSMDBone& Bone : Bones)
	{
		Out += FString::Printf(TEXT("    %d  %.6f %.6f %.6f  %.6f %.6f %.6f\n"),
			Bone.Index,
			Bone.Position.X, Bone.Position.Y, Bone.Position.Z,
			Bone.Rotation.X, Bone.Rotation.Y, Bone.Rotation.Z);
	}
	Out += TEXT("end\n");

	return Out;
}

FVector FSMDExporter::ConvertPosition(const FVector& UEPos, float Scale)
{
	// UE: Z-up, left-handed, 1 unit = 1cm
	// Source: Z-up, right-handed, 1 unit ~ 1.905cm
	// Scale factor 0.525, negate Y for handedness
	return FVector(
		UEPos.X * Scale,
		-UEPos.Y * Scale,
		UEPos.Z * Scale
	);
}

FVector FSMDExporter::ConvertNormal(const FVector& UENormal)
{
	// Negate Y for handedness conversion (normals don't get scaled)
	return FVector(UENormal.X, -UENormal.Y, UENormal.Z);
}

FString FSMDExporter::CleanMaterialName(const FString& MaterialPath)
{
	FString Name = FPaths::GetBaseFilename(MaterialPath);

	// Strip common UE prefixes
	if (Name.StartsWith(TEXT("M_"))) Name = Name.Mid(2);
	else if (Name.StartsWith(TEXT("MI_"))) Name = Name.Mid(3);
	else if (Name.StartsWith(TEXT("Mat_"))) Name = Name.Mid(4);

	return Name.ToLower();
}
