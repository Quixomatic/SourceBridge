#include "Models/SMDExporter.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "StaticMeshResources.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConvexElem.h"
#include "ReferenceSkeleton.h"

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

	// Build physics SMD from UE collision geometry if available
	TArray<FSMDTriangle> CollisionTris = ExtractCollisionMesh(Mesh, Scale);
	if (CollisionTris.Num() > 0)
	{
		Result.PhysicsSMD = BuildSMD(CollisionTris, Bones);
	}
	else
	{
		// Fallback: use render mesh as collision
		Result.PhysicsSMD = BuildSMD(Triangles, Bones);
	}

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
			Out += FString::Printf(TEXT("  %d  %.6f %.6f %.6f  %.6f %.6f %.6f  %.6f %.6f"),
				V.BoneIndex,
				V.Position.X, V.Position.Y, V.Position.Z,
				V.Normal.X, V.Normal.Y, V.Normal.Z,
				V.UV.X, V.UV.Y);

			// Extended bone weights (SMD format: numlinks bone weight bone weight ...)
			if (V.BoneWeights.Num() > 1)
			{
				Out += FString::Printf(TEXT("  %d"), V.BoneWeights.Num());
				for (const FSMDBoneWeight& BW : V.BoneWeights)
				{
					Out += FString::Printf(TEXT(" %d %.6f"), BW.BoneIndex, BW.Weight);
				}
			}

			Out += TEXT("\n");
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

TArray<FSMDTriangle> FSMDExporter::ExtractCollisionMesh(UStaticMesh* Mesh, float Scale)
{
	TArray<FSMDTriangle> Triangles;

	if (!Mesh) return Triangles;

	UBodySetup* BodySetup = Mesh->GetBodySetup();
	if (!BodySetup) return Triangles;

	// Extract triangulated convex hulls from the body setup
	for (const FKConvexElem& ConvexElem : BodySetup->AggGeom.ConvexElems)
	{
		const TArray<FVector>& Vertices = ConvexElem.VertexData;
		const FTransform ElemTransform = ConvexElem.GetTransform();

		// Get the index buffer from the convex hull
		// FKConvexElem stores pre-triangulated index data
		if (ConvexElem.IndexData.Num() >= 3)
		{
			for (int32 i = 0; i + 2 < ConvexElem.IndexData.Num(); i += 3)
			{
				int32 Idx0 = ConvexElem.IndexData[i];
				int32 Idx1 = ConvexElem.IndexData[i + 1];
				int32 Idx2 = ConvexElem.IndexData[i + 2];

				if (!Vertices.IsValidIndex(Idx0) || !Vertices.IsValidIndex(Idx1) || !Vertices.IsValidIndex(Idx2))
				{
					continue;
				}

				FVector V0 = ElemTransform.TransformPosition(Vertices[Idx0]);
				FVector V1 = ElemTransform.TransformPosition(Vertices[Idx1]);
				FVector V2 = ElemTransform.TransformPosition(Vertices[Idx2]);

				// Compute face normal
				FVector Edge1 = V1 - V0;
				FVector Edge2 = V2 - V0;
				FVector FaceNormal = FVector::CrossProduct(Edge1, Edge2).GetSafeNormal();

				FSMDTriangle Tri;
				Tri.MaterialName = TEXT("phys");

				Tri.Vertices[0].BoneIndex = 0;
				Tri.Vertices[0].Position = ConvertPosition(V0, Scale);
				Tri.Vertices[0].Normal = ConvertNormal(FaceNormal);
				Tri.Vertices[0].UV = FVector2D(0, 0);

				Tri.Vertices[1].BoneIndex = 0;
				Tri.Vertices[1].Position = ConvertPosition(V1, Scale);
				Tri.Vertices[1].Normal = ConvertNormal(FaceNormal);
				Tri.Vertices[1].UV = FVector2D(1, 0);

				Tri.Vertices[2].BoneIndex = 0;
				Tri.Vertices[2].Position = ConvertPosition(V2, Scale);
				Tri.Vertices[2].Normal = ConvertNormal(FaceNormal);
				Tri.Vertices[2].UV = FVector2D(0, 1);

				Triangles.Add(Tri);
			}
		}
	}

	// Also handle box collision elements
	for (const FKBoxElem& BoxElem : BodySetup->AggGeom.BoxElems)
	{
		FVector HalfExtent(BoxElem.X * 0.5f, BoxElem.Y * 0.5f, BoxElem.Z * 0.5f);
		FTransform BoxTransform = BoxElem.GetTransform();

		// 8 vertices of a box
		FVector BoxVerts[8];
		BoxVerts[0] = BoxTransform.TransformPosition(FVector(-HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z));
		BoxVerts[1] = BoxTransform.TransformPosition(FVector( HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z));
		BoxVerts[2] = BoxTransform.TransformPosition(FVector( HalfExtent.X,  HalfExtent.Y, -HalfExtent.Z));
		BoxVerts[3] = BoxTransform.TransformPosition(FVector(-HalfExtent.X,  HalfExtent.Y, -HalfExtent.Z));
		BoxVerts[4] = BoxTransform.TransformPosition(FVector(-HalfExtent.X, -HalfExtent.Y,  HalfExtent.Z));
		BoxVerts[5] = BoxTransform.TransformPosition(FVector( HalfExtent.X, -HalfExtent.Y,  HalfExtent.Z));
		BoxVerts[6] = BoxTransform.TransformPosition(FVector( HalfExtent.X,  HalfExtent.Y,  HalfExtent.Z));
		BoxVerts[7] = BoxTransform.TransformPosition(FVector(-HalfExtent.X,  HalfExtent.Y,  HalfExtent.Z));

		// 12 triangles (2 per face, 6 faces)
		static const int32 BoxIndices[36] = {
			0,2,1, 0,3,2, // bottom
			4,5,6, 4,6,7, // top
			0,1,5, 0,5,4, // front
			2,3,7, 2,7,6, // back
			0,4,7, 0,7,3, // left
			1,2,6, 1,6,5  // right
		};

		for (int32 i = 0; i < 36; i += 3)
		{
			FVector V0 = BoxVerts[BoxIndices[i]];
			FVector V1 = BoxVerts[BoxIndices[i + 1]];
			FVector V2 = BoxVerts[BoxIndices[i + 2]];

			FVector FaceNormal = FVector::CrossProduct(V1 - V0, V2 - V0).GetSafeNormal();

			FSMDTriangle Tri;
			Tri.MaterialName = TEXT("phys");

			Tri.Vertices[0].BoneIndex = 0;
			Tri.Vertices[0].Position = ConvertPosition(V0, Scale);
			Tri.Vertices[0].Normal = ConvertNormal(FaceNormal);
			Tri.Vertices[0].UV = FVector2D(0, 0);

			Tri.Vertices[1].BoneIndex = 0;
			Tri.Vertices[1].Position = ConvertPosition(V1, Scale);
			Tri.Vertices[1].Normal = ConvertNormal(FaceNormal);
			Tri.Vertices[1].UV = FVector2D(1, 0);

			Tri.Vertices[2].BoneIndex = 0;
			Tri.Vertices[2].Position = ConvertPosition(V2, Scale);
			Tri.Vertices[2].Normal = ConvertNormal(FaceNormal);
			Tri.Vertices[2].UV = FVector2D(0, 1);

			Triangles.Add(Tri);
		}
	}

	// Sphere collision elements - approximate with an icosphere
	for (const FKSphereElem& SphereElem : BodySetup->AggGeom.SphereElems)
	{
		float Radius = SphereElem.Radius;
		FTransform SphereTransform = SphereElem.GetTransform();

		// Generate icosphere vertices (subdivision level 1 for physics)
		const float T = (1.0f + FMath::Sqrt(5.0f)) / 2.0f;
		TArray<FVector> IcoVerts;
		IcoVerts.Add(FVector(-1,  T, 0).GetSafeNormal() * Radius);
		IcoVerts.Add(FVector( 1,  T, 0).GetSafeNormal() * Radius);
		IcoVerts.Add(FVector(-1, -T, 0).GetSafeNormal() * Radius);
		IcoVerts.Add(FVector( 1, -T, 0).GetSafeNormal() * Radius);
		IcoVerts.Add(FVector(0, -1,  T).GetSafeNormal() * Radius);
		IcoVerts.Add(FVector(0,  1,  T).GetSafeNormal() * Radius);
		IcoVerts.Add(FVector(0, -1, -T).GetSafeNormal() * Radius);
		IcoVerts.Add(FVector(0,  1, -T).GetSafeNormal() * Radius);
		IcoVerts.Add(FVector( T, 0, -1).GetSafeNormal() * Radius);
		IcoVerts.Add(FVector( T, 0,  1).GetSafeNormal() * Radius);
		IcoVerts.Add(FVector(-T, 0, -1).GetSafeNormal() * Radius);
		IcoVerts.Add(FVector(-T, 0,  1).GetSafeNormal() * Radius);

		static const int32 IcoIndices[60] = {
			0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
			1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
			3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
			4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1
		};

		for (int32 i = 0; i < 60; i += 3)
		{
			FVector V0 = SphereTransform.TransformPosition(IcoVerts[IcoIndices[i]]);
			FVector V1 = SphereTransform.TransformPosition(IcoVerts[IcoIndices[i + 1]]);
			FVector V2 = SphereTransform.TransformPosition(IcoVerts[IcoIndices[i + 2]]);

			FVector FaceNormal = FVector::CrossProduct(V1 - V0, V2 - V0).GetSafeNormal();

			FSMDTriangle Tri;
			Tri.MaterialName = TEXT("phys");

			Tri.Vertices[0].BoneIndex = 0;
			Tri.Vertices[0].Position = ConvertPosition(V0, Scale);
			Tri.Vertices[0].Normal = ConvertNormal(FaceNormal);
			Tri.Vertices[0].UV = FVector2D(0, 0);

			Tri.Vertices[1].BoneIndex = 0;
			Tri.Vertices[1].Position = ConvertPosition(V1, Scale);
			Tri.Vertices[1].Normal = ConvertNormal(FaceNormal);
			Tri.Vertices[1].UV = FVector2D(1, 0);

			Tri.Vertices[2].BoneIndex = 0;
			Tri.Vertices[2].Position = ConvertPosition(V2, Scale);
			Tri.Vertices[2].Normal = ConvertNormal(FaceNormal);
			Tri.Vertices[2].UV = FVector2D(0, 1);

			Triangles.Add(Tri);
		}
	}

	return Triangles;
}

FSMDExportResult FSMDExporter::ExportSkeletalMesh(USkeletalMesh* Mesh, float Scale)
{
	FSMDExportResult Result;

	if (!Mesh)
	{
		Result.ErrorMessage = TEXT("Null skeletal mesh provided.");
		return Result;
	}

	// Get render data for LOD 0
	FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
	if (!RenderData || RenderData->LODRenderData.Num() == 0)
	{
		Result.ErrorMessage = FString::Printf(TEXT("No render data for %s"), *Mesh->GetName());
		return Result;
	}

	const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();

	// Build bone hierarchy
	TArray<FSMDBone> Bones;
	for (int32 BoneIdx = 0; BoneIdx < RefSkeleton.GetNum(); BoneIdx++)
	{
		FSMDBone Bone;
		Bone.Index = BoneIdx;
		Bone.Name = RefSkeleton.GetBoneName(BoneIdx).ToString();
		Bone.ParentIndex = RefSkeleton.GetParentIndex(BoneIdx);

		// Get bone-space (local) transform
		FTransform BoneLocal = RefSkeleton.GetRefBonePose()[BoneIdx];
		FVector Pos = BoneLocal.GetTranslation();
		FRotator Rot = BoneLocal.GetRotation().Rotator();

		// Convert to Source coordinates
		Bone.Position = ConvertPosition(Pos, Scale);
		// Convert rotation to radians for SMD
		Bone.Rotation = FVector(
			FMath::DegreesToRadians(Rot.Roll),
			FMath::DegreesToRadians(-Rot.Pitch),  // negate for handedness
			FMath::DegreesToRadians(Rot.Yaw)
		);

		Bones.Add(Bone);
	}

	if (Bones.Num() == 0)
	{
		Result.ErrorMessage = FString::Printf(TEXT("No bones found in %s"), *Mesh->GetName());
		return Result;
	}

	// Get materials
	const TArray<FSkeletalMaterial>& SkeletalMaterials = Mesh->GetMaterials();

	// Extract triangles from render data
	TArray<FSMDTriangle> Triangles;

	const FPositionVertexBuffer& PositionBuffer = LODData.StaticVertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& VertexBuffer = LODData.StaticVertexBuffers.StaticMeshVertexBuffer;
	const FSkinWeightVertexBuffer* SkinWeightBuffer = LODData.GetSkinWeightVertexBuffer();

	// Process each mesh section (one per material)
	for (int32 SectionIdx = 0; SectionIdx < LODData.RenderSections.Num(); SectionIdx++)
	{
		const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIdx];

		// Get material name
		FString MaterialName = TEXT("default");
		int32 MaterialIndex = Section.MaterialIndex;
		if (SkeletalMaterials.IsValidIndex(MaterialIndex))
		{
			if (UMaterialInterface* Mat = SkeletalMaterials[MaterialIndex].MaterialInterface)
			{
				MaterialName = CleanMaterialName(Mat->GetName());
			}
			else if (!SkeletalMaterials[MaterialIndex].MaterialSlotName.IsNone())
			{
				MaterialName = CleanMaterialName(SkeletalMaterials[MaterialIndex].MaterialSlotName.ToString());
			}
		}

		if (!Result.MaterialNames.Contains(MaterialName))
		{
			Result.MaterialNames.Add(MaterialName);
		}

		// Process triangles in this section
		uint32 IndexStart = Section.BaseIndex;
		uint32 NumTriangles = Section.NumTriangles;

		for (uint32 TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
		{
			FSMDTriangle Tri;
			Tri.MaterialName = MaterialName;

			for (int32 CornerIdx = 0; CornerIdx < 3; CornerIdx++)
			{
				uint32 Index = LODData.MultiSizeIndexContainer.GetIndexBuffer()->Get(IndexStart + TriIdx * 3 + CornerIdx);

				FSMDVertex& V = Tri.Vertices[CornerIdx];

				// Position
				FVector3f Pos = PositionBuffer.VertexPosition(Index);
				V.Position = ConvertPosition(FVector(Pos.X, Pos.Y, Pos.Z), Scale);

				// Normal
				FVector4f TangentZ = VertexBuffer.VertexTangentZ(Index);
				V.Normal = ConvertNormal(FVector(TangentZ.X, TangentZ.Y, TangentZ.Z));

				// UV
				FVector2f UV = VertexBuffer.GetVertexUV(Index, 0);
				V.UV = FVector2D(UV.X, UV.Y);

				// Bone weights
				if (SkinWeightBuffer)
				{
					// Find the primary bone (highest weight)
					int32 PrimaryBone = 0;
					float MaxWeight = 0.0f;

					// SMD supports extended bone weights
					int32 MaxInfluences = FMath::Min((int32)SkinWeightBuffer->GetMaxBoneInfluences(), 4);
					for (int32 InfluenceIdx = 0; InfluenceIdx < MaxInfluences; InfluenceIdx++)
					{
						int32 BoneIdx = SkinWeightBuffer->GetBoneIndex(Index, InfluenceIdx);
						uint8 WeightByte = SkinWeightBuffer->GetBoneWeight(Index, InfluenceIdx);
						float Weight = WeightByte / 255.0f;

						if (Weight > 0.0f)
						{
							// Map section bone index to skeleton bone index
							int32 SkeletonBoneIdx = BoneIdx;
							if (Section.BoneMap.IsValidIndex(BoneIdx))
							{
								SkeletonBoneIdx = Section.BoneMap[BoneIdx];
							}

							if (Weight > MaxWeight)
							{
								MaxWeight = Weight;
								PrimaryBone = SkeletonBoneIdx;
							}

							FSMDBoneWeight BW;
							BW.BoneIndex = SkeletonBoneIdx;
							BW.Weight = Weight;
							V.BoneWeights.Add(BW);
						}
					}

					V.BoneIndex = PrimaryBone;
				}
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

	// Physics: use the render mesh for now (skeletal meshes usually have per-bone collision in PhysicsAsset)
	Result.PhysicsSMD = BuildSMD(Triangles, Bones);

	// Build idle animation SMD
	Result.IdleSMD = BuildIdleSMD(Bones);

	Result.bSuccess = true;

	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Exported skeletal mesh %s - %d bones, %d triangles, %d materials"),
		*Mesh->GetName(), Bones.Num(), Triangles.Num(), Result.MaterialNames.Num());

	return Result;
}
