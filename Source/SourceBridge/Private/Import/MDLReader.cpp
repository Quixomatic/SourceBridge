#include "Import/MDLReader.h"

// ============================================================================
// MDL/VVD/VTX Binary Format Offsets (from Valve SDK studio.h / optimize.h)
// ============================================================================

// MDL studiohdr_t field offsets
namespace MDLOffsets
{
	constexpr int32 ID = 0;
	constexpr int32 Version = 4;
	constexpr int32 Checksum = 8;
	constexpr int32 Name = 12;        // char[64]
	constexpr int32 DataLength = 76;

	constexpr int32 EyePosition = 80;   // Vector (12 bytes)
	constexpr int32 IllumPosition = 92;
	constexpr int32 HullMin = 104;
	constexpr int32 HullMax = 116;
	constexpr int32 ViewBBMin = 128;
	constexpr int32 ViewBBMax = 140;

	constexpr int32 Flags = 152;

	constexpr int32 BoneCount = 156;
	constexpr int32 BoneOffset = 160;
	constexpr int32 BoneControllerCount = 164;
	constexpr int32 BoneControllerOffset = 168;
	constexpr int32 HitboxCount = 172;
	constexpr int32 HitboxOffset = 176;
	constexpr int32 LocalAnimCount = 180;
	constexpr int32 LocalAnimOffset = 184;
	constexpr int32 LocalSeqCount = 188;
	constexpr int32 LocalSeqOffset = 192;

	constexpr int32 TextureCount = 200;
	constexpr int32 TextureOffset = 204;
	constexpr int32 TextureDirCount = 208;
	constexpr int32 TextureDirOffset = 212;

	constexpr int32 SkinRefCount = 216;
	constexpr int32 SkinFamilyCount = 220;
	constexpr int32 SkinRefIndex = 224;

	constexpr int32 BodyPartCount = 228;
	constexpr int32 BodyPartOffset = 232;

	constexpr int32 AttachmentCount = 236;
	constexpr int32 AttachmentOffset = 240;

	constexpr int32 SurfacePropIndex = 308;

	constexpr int32 KeyValueIndex = 312;
	constexpr int32 KeyValueCount = 316;

	constexpr int32 Mass = 328;
	constexpr int32 Contents = 332;

	constexpr int32 RootLOD = 393;       // byte
	constexpr int32 NumAllowedRootLODs = 394; // byte

	constexpr int32 HeaderSize = 408;
}

// MDL sub-structure sizes
namespace MDLStructSizes
{
	constexpr int32 Texture = 64;        // mstudiotexture_t
	constexpr int32 BodyPart = 16;       // mstudiobodyparts_t
	// mstudiomodel_t size varies by version, we read fields at known offsets
	// mstudiomesh_t size varies by version, we read fields at known offsets
}

// VVD offsets
namespace VVDOffsets
{
	constexpr int32 ID = 0;
	constexpr int32 Version = 4;
	constexpr int32 Checksum = 8;
	constexpr int32 NumLODs = 12;
	constexpr int32 NumLODVertexes = 16;  // int[8]
	constexpr int32 NumFixups = 48;
	constexpr int32 FixupTableStart = 52;
	constexpr int32 VertexDataStart = 56;
	constexpr int32 TangentDataStart = 60;
	constexpr int32 HeaderSize = 64;

	constexpr int32 VertexSize = 48;     // mstudiovertex_t
	constexpr int32 FixupSize = 12;      // vertexFileFixup_t
	constexpr int32 TangentSize = 16;    // float4
}

// VTX offsets
namespace VTXOffsets
{
	constexpr int32 Version = 0;
	constexpr int32 VertCacheSize = 4;
	constexpr int32 MaxBonesPerStrip = 8;  // unsigned short
	constexpr int32 MaxBonesPerTri = 10;   // unsigned short
	constexpr int32 MaxBonesPerVert = 12;
	constexpr int32 CheckSum = 16;
	constexpr int32 NumLODs = 20;
	constexpr int32 MatReplacementListOffset = 24;
	constexpr int32 NumBodyParts = 28;
	constexpr int32 BodyPartOffset = 32;
	constexpr int32 HeaderSize = 36;

	// VTX Vertex_t is 9 bytes on disk (NOT 10 due to struct padding)
	constexpr int32 VTXVertexSize = 9;
}

// Strip flags
constexpr uint8 STRIP_IS_TRILIST = 0x01;
constexpr uint8 STRIP_IS_TRISTRIP = 0x02;

// Model flags
constexpr int32 STUDIOHDR_FLAGS_STATIC_PROP = 0x00000010;

// ============================================================================
// Helper: Read null-terminated string from buffer
// ============================================================================

FString FMDLReader::ReadNullTermString(const uint8* Data, int32 DataSize, int32 Offset)
{
	if (Offset < 0 || Offset >= DataSize) return FString();

	int32 End = Offset;
	while (End < DataSize && Data[End] != 0)
	{
		End++;
	}
	if (End == Offset) return FString();

	return FString(End - Offset, (const char*)(Data + Offset));
}

// ============================================================================
// MDL Header Parsing
// ============================================================================

bool FMDLReader::ParseMDLHeader(const uint8* Data, int32 DataSize, FSourceModelData& OutModel)
{
	if (DataSize < MDLOffsets::HeaderSize)
	{
		OutModel.ErrorMessage = TEXT("MDL file too small for header");
		return false;
	}

	int32 ID = ReadValue<int32>(Data, MDLOffsets::ID);
	if (ID != SOURCE_MDL_ID)
	{
		OutModel.ErrorMessage = FString::Printf(TEXT("Invalid MDL signature: 0x%08X (expected 0x%08X)"), ID, SOURCE_MDL_ID);
		return false;
	}

	OutModel.Version = ReadValue<int32>(Data, MDLOffsets::Version);
	if (OutModel.Version < 44 || OutModel.Version > 49)
	{
		OutModel.ErrorMessage = FString::Printf(TEXT("Unsupported MDL version: %d (expected 44-49)"), OutModel.Version);
		return false;
	}

	OutModel.Checksum = ReadValue<int32>(Data, MDLOffsets::Checksum);

	// Read name (64 bytes, null-padded)
	OutModel.Name = ReadNullTermString(Data, DataSize, MDLOffsets::Name);

	OutModel.Flags = ReadValue<int32>(Data, MDLOffsets::Flags);
	OutModel.bIsStaticProp = (OutModel.Flags & STUDIOHDR_FLAGS_STATIC_PROP) != 0;

	// Bounds
	auto ReadVector = [&](int32 Offset) -> FVector
	{
		float X = ReadValue<float>(Data, Offset);
		float Y = ReadValue<float>(Data, Offset + 4);
		float Z = ReadValue<float>(Data, Offset + 8);
		return FVector(X, Y, Z);
	};

	OutModel.EyePosition = ReadVector(MDLOffsets::EyePosition);
	OutModel.IllumPosition = ReadVector(MDLOffsets::IllumPosition);
	OutModel.HullMin = ReadVector(MDLOffsets::HullMin);
	OutModel.HullMax = ReadVector(MDLOffsets::HullMax);
	OutModel.ViewBBMin = ReadVector(MDLOffsets::ViewBBMin);
	OutModel.ViewBBMax = ReadVector(MDLOffsets::ViewBBMax);

	OutModel.Mass = ReadValue<float>(Data, MDLOffsets::Mass);
	OutModel.Contents = ReadValue<int32>(Data, MDLOffsets::Contents);

	OutModel.RootLOD = Data[MDLOffsets::RootLOD];

	UE_LOG(LogTemp, Log, TEXT("MDLReader: '%s' v%d checksum=%d flags=0x%X bones=%d textures=%d bodyparts=%d mass=%.1f %s"),
		*OutModel.Name, OutModel.Version, OutModel.Checksum, OutModel.Flags,
		ReadValue<int32>(Data, MDLOffsets::BoneCount),
		ReadValue<int32>(Data, MDLOffsets::TextureCount),
		ReadValue<int32>(Data, MDLOffsets::BodyPartCount),
		OutModel.Mass,
		OutModel.bIsStaticProp ? TEXT("[STATIC]") : TEXT(""));

	return true;
}

// ============================================================================
// MDL Material/Texture Name Parsing
// ============================================================================

bool FMDLReader::ParseMDLMaterials(const uint8* Data, int32 DataSize, FSourceModelData& OutModel)
{
	int32 TextureCount = ReadValue<int32>(Data, MDLOffsets::TextureCount);
	int32 TextureOffset = ReadValue<int32>(Data, MDLOffsets::TextureOffset);

	for (int32 i = 0; i < TextureCount; i++)
	{
		int32 TexBase = TextureOffset + i * MDLStructSizes::Texture;
		if (TexBase + MDLStructSizes::Texture > DataSize) break;

		// name_offset is relative to the start of this mstudiotexture_t struct
		int32 NameRelOffset = ReadValue<int32>(Data, TexBase);
		int32 NameAbsOffset = TexBase + NameRelOffset;

		FString TexName = ReadNullTermString(Data, DataSize, NameAbsOffset);
		OutModel.MaterialNames.Add(TexName);

		UE_LOG(LogTemp, Verbose, TEXT("MDLReader: Material[%d] = '%s'"), i, *TexName);
	}

	return true;
}

// ============================================================================
// MDL Texture Directory (cdmaterials) Parsing
// ============================================================================

bool FMDLReader::ParseMDLTextureDirs(const uint8* Data, int32 DataSize, FSourceModelData& OutModel)
{
	int32 DirCount = ReadValue<int32>(Data, MDLOffsets::TextureDirCount);
	int32 DirOffset = ReadValue<int32>(Data, MDLOffsets::TextureDirOffset);

	for (int32 i = 0; i < DirCount; i++)
	{
		int32 IntOffset = DirOffset + i * 4;
		if (IntOffset + 4 > DataSize) break;

		// Each entry is an int offset to a null-terminated string
		int32 StringOffset = ReadValue<int32>(Data, IntOffset);
		FString DirPath = ReadNullTermString(Data, DataSize, StringOffset);

		// Normalize: ensure trailing slash, use forward slashes
		DirPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (!DirPath.EndsWith(TEXT("/")))
		{
			DirPath += TEXT("/");
		}

		OutModel.MaterialSearchDirs.Add(DirPath);
		UE_LOG(LogTemp, Verbose, TEXT("MDLReader: TextureDir[%d] = '%s'"), i, *DirPath);
	}

	return true;
}

// ============================================================================
// MDL Skin Replacement Table
// ============================================================================

bool FMDLReader::ParseMDLSkinTable(const uint8* Data, int32 DataSize, FSourceModelData& OutModel)
{
	int32 SkinRefCount = ReadValue<int32>(Data, MDLOffsets::SkinRefCount);
	int32 SkinFamilyCount = ReadValue<int32>(Data, MDLOffsets::SkinFamilyCount);
	int32 SkinRefIndex = ReadValue<int32>(Data, MDLOffsets::SkinRefIndex);

	OutModel.NumSkinReferences = SkinRefCount;

	int32 TableSize = SkinRefCount * SkinFamilyCount * 2; // shorts
	if (SkinRefIndex + TableSize > DataSize)
	{
		UE_LOG(LogTemp, Warning, TEXT("MDLReader: Skin table exceeds file bounds"));
		return true; // Non-fatal
	}

	for (int32 Family = 0; Family < SkinFamilyCount; Family++)
	{
		TArray<int16> FamilyRefs;
		for (int32 Ref = 0; Ref < SkinRefCount; Ref++)
		{
			int32 Offset = SkinRefIndex + (Family * SkinRefCount + Ref) * 2;
			int16 TexIndex = ReadValue<int16>(Data, Offset);
			FamilyRefs.Add(TexIndex);
		}
		OutModel.SkinFamilies.Add(MoveTemp(FamilyRefs));
	}

	UE_LOG(LogTemp, Log, TEXT("MDLReader: Skin table: %d families × %d references"), SkinFamilyCount, SkinRefCount);
	return true;
}

// ============================================================================
// MDL Bone Hierarchy
// ============================================================================

bool FMDLReader::ParseMDLBones(const uint8* Data, int32 DataSize, FSourceModelData& OutModel)
{
	int32 BoneCount = ReadValue<int32>(Data, MDLOffsets::BoneCount);
	int32 BoneOffset = ReadValue<int32>(Data, MDLOffsets::BoneOffset);

	// mstudiobone_t is a large struct (~216 bytes depending on version)
	// Key fields at known offsets from each bone struct start:
	//   0: name_index (int, relative to struct start)
	//   4: parent (int)
	//   8-12: bonecontroller[6] (int[6])
	//  32: pos (Vector = float[3])
	//  44: quat (Quaternion = float[4])
	//  60: rot (RadianEuler = float[3])
	// The struct size varies, but 216 is typical for v48
	constexpr int32 BoneStructSize = 216;

	for (int32 i = 0; i < BoneCount; i++)
	{
		int32 BoneBase = BoneOffset + i * BoneStructSize;
		if (BoneBase + 72 > DataSize) break; // At minimum need through rotation

		FSourceModelBone Bone;

		int32 NameRelOffset = ReadValue<int32>(Data, BoneBase + 0);
		Bone.Name = ReadNullTermString(Data, DataSize, BoneBase + NameRelOffset);
		Bone.ParentIndex = ReadValue<int32>(Data, BoneBase + 4);

		float PosX = ReadValue<float>(Data, BoneBase + 32);
		float PosY = ReadValue<float>(Data, BoneBase + 36);
		float PosZ = ReadValue<float>(Data, BoneBase + 40);
		Bone.Position = FVector(PosX, PosY, PosZ);

		float QuatX = ReadValue<float>(Data, BoneBase + 44);
		float QuatY = ReadValue<float>(Data, BoneBase + 48);
		float QuatZ = ReadValue<float>(Data, BoneBase + 52);
		float QuatW = ReadValue<float>(Data, BoneBase + 56);
		Bone.Rotation = FQuat(QuatX, QuatY, QuatZ, QuatW);

		OutModel.Bones.Add(MoveTemp(Bone));
	}

	UE_LOG(LogTemp, Log, TEXT("MDLReader: Parsed %d bones"), OutModel.Bones.Num());
	return true;
}

// ============================================================================
// MDL Surface Property String
// ============================================================================

bool FMDLReader::ParseMDLSurfaceProp(const uint8* Data, int32 DataSize, FSourceModelData& OutModel)
{
	int32 SurfacePropIndex = ReadValue<int32>(Data, MDLOffsets::SurfacePropIndex);
	if (SurfacePropIndex > 0 && SurfacePropIndex < DataSize)
	{
		OutModel.SurfaceProp = ReadNullTermString(Data, DataSize, SurfacePropIndex);
	}
	return true;
}

// ============================================================================
// VVD Parsing - Vertex Data
// ============================================================================

bool FMDLReader::ParseVVD(const uint8* Data, int32 DataSize, int32 ExpectedChecksum,
	int32 RequestedLOD, FSourceModelData& OutModel)
{
	if (DataSize < VVDOffsets::HeaderSize)
	{
		OutModel.ErrorMessage = TEXT("VVD file too small for header");
		return false;
	}

	int32 ID = ReadValue<int32>(Data, VVDOffsets::ID);
	if (ID != SOURCE_VVD_ID)
	{
		OutModel.ErrorMessage = FString::Printf(TEXT("Invalid VVD signature: 0x%08X"), ID);
		return false;
	}

	int32 Version = ReadValue<int32>(Data, VVDOffsets::Version);
	int32 Checksum = ReadValue<int32>(Data, VVDOffsets::Checksum);

	if (Checksum != ExpectedChecksum)
	{
		UE_LOG(LogTemp, Warning, TEXT("MDLReader: VVD checksum mismatch (VVD=%d, MDL=%d)"), Checksum, ExpectedChecksum);
	}

	int32 NumLODs = ReadValue<int32>(Data, VVDOffsets::NumLODs);
	OutModel.NumLODs = NumLODs;

	// LOD vertex counts
	int32 LODVertexCounts[SOURCE_MAX_NUM_LODS];
	for (int32 i = 0; i < SOURCE_MAX_NUM_LODS; i++)
	{
		LODVertexCounts[i] = ReadValue<int32>(Data, VVDOffsets::NumLODVertexes + i * 4);
	}

	int32 NumFixups = ReadValue<int32>(Data, VVDOffsets::NumFixups);
	int32 FixupTableStart = ReadValue<int32>(Data, VVDOffsets::FixupTableStart);
	int32 VertexDataStart = ReadValue<int32>(Data, VVDOffsets::VertexDataStart);
	int32 TangentDataStart = ReadValue<int32>(Data, VVDOffsets::TangentDataStart);

	int32 EffectiveLOD = FMath::Clamp(RequestedLOD, 0, NumLODs - 1);
	int32 TotalVertices = LODVertexCounts[EffectiveLOD];

	UE_LOG(LogTemp, Log, TEXT("MDLReader: VVD v%d checksum=%d LODs=%d vertices[LOD%d]=%d fixups=%d"),
		Version, Checksum, NumLODs, EffectiveLOD, TotalVertices, NumFixups);

	// Read vertices
	if (NumFixups == 0)
	{
		// No fixup table - read vertices directly
		OutModel.Vertices.SetNum(TotalVertices);
		for (int32 i = 0; i < TotalVertices; i++)
		{
			int32 VtxOffset = VertexDataStart + i * VVDOffsets::VertexSize;
			if (VtxOffset + VVDOffsets::VertexSize > DataSize) break;

			FSourceModelVertex& V = OutModel.Vertices[i];

			// Bone weights (16 bytes)
			V.BoneWeights[0] = ReadValue<float>(Data, VtxOffset + 0);
			V.BoneWeights[1] = ReadValue<float>(Data, VtxOffset + 4);
			V.BoneWeights[2] = ReadValue<float>(Data, VtxOffset + 8);
			V.BoneIndices[0] = (int32)(int8)Data[VtxOffset + 12];
			V.BoneIndices[1] = (int32)(int8)Data[VtxOffset + 13];
			V.BoneIndices[2] = (int32)(int8)Data[VtxOffset + 14];
			V.NumBones = Data[VtxOffset + 15];

			// Position (12 bytes)
			V.Position.X = ReadValue<float>(Data, VtxOffset + 16);
			V.Position.Y = ReadValue<float>(Data, VtxOffset + 20);
			V.Position.Z = ReadValue<float>(Data, VtxOffset + 24);

			// Normal (12 bytes)
			V.Normal.X = ReadValue<float>(Data, VtxOffset + 28);
			V.Normal.Y = ReadValue<float>(Data, VtxOffset + 32);
			V.Normal.Z = ReadValue<float>(Data, VtxOffset + 36);

			// UV (8 bytes)
			V.UV.X = ReadValue<float>(Data, VtxOffset + 40);
			V.UV.Y = ReadValue<float>(Data, VtxOffset + 44);
		}
	}
	else
	{
		// Use fixup table to load vertices for the requested LOD
		OutModel.Vertices.Reserve(TotalVertices);
		for (int32 f = 0; f < NumFixups; f++)
		{
			int32 FixupOffset = FixupTableStart + f * VVDOffsets::FixupSize;
			if (FixupOffset + VVDOffsets::FixupSize > DataSize) break;

			int32 FixupLOD = ReadValue<int32>(Data, FixupOffset + 0);
			int32 SourceVertexID = ReadValue<int32>(Data, FixupOffset + 4);
			int32 NumVertexes = ReadValue<int32>(Data, FixupOffset + 8);

			// Include fixup if its LOD is >= our requested LOD
			if (FixupLOD < EffectiveLOD)
				continue;

			for (int32 v = 0; v < NumVertexes; v++)
			{
				int32 VtxIdx = SourceVertexID + v;
				int32 VtxOffset = VertexDataStart + VtxIdx * VVDOffsets::VertexSize;
				if (VtxOffset + VVDOffsets::VertexSize > DataSize) break;

				FSourceModelVertex Vertex;
				Vertex.BoneWeights[0] = ReadValue<float>(Data, VtxOffset + 0);
				Vertex.BoneWeights[1] = ReadValue<float>(Data, VtxOffset + 4);
				Vertex.BoneWeights[2] = ReadValue<float>(Data, VtxOffset + 8);
				Vertex.BoneIndices[0] = (int32)(int8)Data[VtxOffset + 12];
				Vertex.BoneIndices[1] = (int32)(int8)Data[VtxOffset + 13];
				Vertex.BoneIndices[2] = (int32)(int8)Data[VtxOffset + 14];
				Vertex.NumBones = Data[VtxOffset + 15];

				Vertex.Position.X = ReadValue<float>(Data, VtxOffset + 16);
				Vertex.Position.Y = ReadValue<float>(Data, VtxOffset + 20);
				Vertex.Position.Z = ReadValue<float>(Data, VtxOffset + 24);

				Vertex.Normal.X = ReadValue<float>(Data, VtxOffset + 28);
				Vertex.Normal.Y = ReadValue<float>(Data, VtxOffset + 32);
				Vertex.Normal.Z = ReadValue<float>(Data, VtxOffset + 36);

				Vertex.UV.X = ReadValue<float>(Data, VtxOffset + 40);
				Vertex.UV.Y = ReadValue<float>(Data, VtxOffset + 44);

				OutModel.Vertices.Add(MoveTemp(Vertex));
			}
		}
	}

	// Read tangents if available
	if (TangentDataStart > 0 && TangentDataStart < DataSize)
	{
		int32 NumTangents = FMath::Min(OutModel.Vertices.Num(),
			(DataSize - TangentDataStart) / VVDOffsets::TangentSize);

		for (int32 i = 0; i < NumTangents; i++)
		{
			int32 TanOffset = TangentDataStart + i * VVDOffsets::TangentSize;
			OutModel.Vertices[i].Tangent.X = ReadValue<float>(Data, TanOffset + 0);
			OutModel.Vertices[i].Tangent.Y = ReadValue<float>(Data, TanOffset + 4);
			OutModel.Vertices[i].Tangent.Z = ReadValue<float>(Data, TanOffset + 8);
			OutModel.Vertices[i].Tangent.W = ReadValue<float>(Data, TanOffset + 12);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("MDLReader: Loaded %d vertices from VVD"), OutModel.Vertices.Num());
	return true;
}

// ============================================================================
// VTX Parsing - Triangle Index Data
// ============================================================================

bool FMDLReader::ParseVTX(const uint8* Data, int32 DataSize, int32 MDLVersion, int32 ExpectedChecksum,
	const uint8* MDLData, int32 MDLDataSize,
	int32 RequestedLOD, FSourceModelData& OutModel)
{
	if (DataSize < VTXOffsets::HeaderSize)
	{
		OutModel.ErrorMessage = TEXT("VTX file too small for header");
		return false;
	}

	int32 VTXVersion = ReadValue<int32>(Data, VTXOffsets::Version);
	if (VTXVersion != SOURCE_VTX_VERSION)
	{
		OutModel.ErrorMessage = FString::Printf(TEXT("Unsupported VTX version: %d (expected %d)"), VTXVersion, SOURCE_VTX_VERSION);
		return false;
	}

	int32 Checksum = ReadValue<int32>(Data, VTXOffsets::CheckSum);
	if (Checksum != ExpectedChecksum)
	{
		UE_LOG(LogTemp, Warning, TEXT("MDLReader: VTX checksum mismatch (VTX=%d, MDL=%d)"), Checksum, ExpectedChecksum);
	}

	int32 NumBodyParts = ReadValue<int32>(Data, VTXOffsets::NumBodyParts);
	int32 BodyPartOffset = ReadValue<int32>(Data, VTXOffsets::BodyPartOffset);

	// Also need MDL body part info for mesh material indices and vertex offsets
	int32 MDLBodyPartCount = ReadValue<int32>(MDLData, MDLOffsets::BodyPartCount);
	int32 MDLBodyPartOffset = ReadValue<int32>(MDLData, MDLOffsets::BodyPartOffset);

	if (NumBodyParts != MDLBodyPartCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("MDLReader: Body part count mismatch (VTX=%d, MDL=%d)"), NumBodyParts, MDLBodyPartCount);
		NumBodyParts = FMath::Min(NumBodyParts, MDLBodyPartCount);
	}

	// StripGroup/Strip header sizes depend on MDL version
	// Version >=49 adds 8 bytes (numTopologyIndices + topologyOffset) to both
	int32 StripGroupHeaderSize = (MDLVersion >= 49) ? 33 : 25;
	int32 StripHeaderSize = (MDLVersion >= 49) ? 35 : 27;

	int32 TotalTriangles = 0;

	// Traverse: BodyPart → Model → LOD → Mesh → StripGroup → Strip
	for (int32 bp = 0; bp < NumBodyParts; bp++)
	{
		// VTX body part
		int32 VTXBPBase = BodyPartOffset + bp * 8; // BodyPartHeader_t = 8 bytes
		if (VTXBPBase + 8 > DataSize) break;

		int32 NumModels = ReadValue<int32>(Data, VTXBPBase + 0);
		int32 ModelOffset = ReadValue<int32>(Data, VTXBPBase + 4); // relative to this struct

		// MDL body part for model/mesh info
		int32 MDLBPBase = MDLBodyPartOffset + bp * MDLStructSizes::BodyPart;
		if (MDLBPBase + MDLStructSizes::BodyPart > MDLDataSize) break;

		int32 MDLNumModels = ReadValue<int32>(MDLData, MDLBPBase + 4);
		int32 MDLModelIndex = ReadValue<int32>(MDLData, MDLBPBase + 12); // relative to body part

		for (int32 m = 0; m < FMath::Min(NumModels, MDLNumModels); m++)
		{
			// VTX model: ModelHeader_t = 8 bytes (numLODs + lodOffset)
			int32 VTXModelBase = VTXBPBase + ModelOffset + m * 8;
			if (VTXModelBase + 8 > DataSize) break;

			int32 NumLODs = ReadValue<int32>(Data, VTXModelBase + 0);
			int32 LODOffset = ReadValue<int32>(Data, VTXModelBase + 4); // relative

			// MDL model: mstudiomodel_t - varies by version but key fields at known offsets
			// name[64]=0, type=64, boundingradius=68, nummeshes=72, meshindex=76
			// numvertices=80, vertexindex=84, tangentsindex=88
			int32 MDLModelBase = MDLBPBase + MDLModelIndex + m * 148; // approximate struct size
			if (MDLModelBase + 92 > MDLDataSize) break;

			int32 MDLNumMeshes = ReadValue<int32>(MDLData, MDLModelBase + 72);
			int32 MDLMeshIndex = ReadValue<int32>(MDLData, MDLModelBase + 76);
			int32 MDLModelVertexOffset = ReadValue<int32>(MDLData, MDLModelBase + 84);

			int32 EffectiveLOD = FMath::Clamp(RequestedLOD, 0, NumLODs - 1);

			// VTX LOD: ModelLODHeader_t = 12 bytes (numMeshes + meshOffset + switchPoint)
			int32 VTXLODBase = VTXModelBase + LODOffset + EffectiveLOD * 12;
			if (VTXLODBase + 12 > DataSize) break;

			int32 LODNumMeshes = ReadValue<int32>(Data, VTXLODBase + 0);
			int32 MeshOffset = ReadValue<int32>(Data, VTXLODBase + 4); // relative

			for (int32 mesh = 0; mesh < FMath::Min(LODNumMeshes, MDLNumMeshes); mesh++)
			{
				// VTX mesh: MeshHeader_t = 9 bytes (numStripGroups=4, stripGroupHeaderOffset=4, flags=1)
				int32 VTXMeshBase = VTXLODBase + MeshOffset + mesh * 9;
				if (VTXMeshBase + 9 > DataSize) break;

				int32 NumStripGroups = ReadValue<int32>(Data, VTXMeshBase + 0);
				int32 StripGroupOffset = ReadValue<int32>(Data, VTXMeshBase + 4); // relative

				// MDL mesh info for material index and vertex offset
				// mstudiomesh_t: material=0, modelindex=4, numvertices=8, vertexoffset=12
				int32 MDLMeshBase = MDLModelBase + MDLMeshIndex + mesh * 116; // approximate struct size
				int32 MeshMaterialIndex = 0;
				int32 MeshVertexOffset = 0;
				if (MDLMeshBase + 16 <= MDLDataSize)
				{
					MeshMaterialIndex = ReadValue<int32>(MDLData, MDLMeshBase + 0);
					MeshVertexOffset = ReadValue<int32>(MDLData, MDLMeshBase + 12);
				}

				FSourceModelMesh& OutMesh = OutModel.Meshes.AddDefaulted_GetRef();
				OutMesh.MaterialIndex = MeshMaterialIndex;

				for (int32 sg = 0; sg < NumStripGroups; sg++)
				{
					int32 SGBase = VTXMeshBase + StripGroupOffset + sg * StripGroupHeaderSize;
					if (SGBase + StripGroupHeaderSize > DataSize) break;

					int32 SGNumVerts = ReadValue<int32>(Data, SGBase + 0);
					int32 SGVertOffset = ReadValue<int32>(Data, SGBase + 4);  // relative
					int32 SGNumIndices = ReadValue<int32>(Data, SGBase + 8);
					int32 SGIndexOffset = ReadValue<int32>(Data, SGBase + 12); // relative
					int32 SGNumStrips = ReadValue<int32>(Data, SGBase + 16);
					int32 SGStripOffset = ReadValue<int32>(Data, SGBase + 20); // relative

					// Absolute positions of vertex and index arrays
					int32 VertArrayBase = SGBase + SGVertOffset;
					int32 IndexArrayBase = SGBase + SGIndexOffset;

					for (int32 s = 0; s < SGNumStrips; s++)
					{
						int32 StripBase = SGBase + SGStripOffset + s * StripHeaderSize;
						if (StripBase + StripHeaderSize > DataSize) break;

						int32 StripNumIndices = ReadValue<int32>(Data, StripBase + 0);
						int32 StripIndexOffset = ReadValue<int32>(Data, StripBase + 4);
						int32 StripNumVerts = ReadValue<int32>(Data, StripBase + 8);
						int32 StripVertOffset = ReadValue<int32>(Data, StripBase + 12);
						// numBones at offset 16 (short = 2 bytes)
						uint8 StripFlags = Data[StripBase + 18];

						if (StripFlags & STRIP_IS_TRILIST)
						{
							// Triangle list - every 3 indices form a triangle
							for (int32 idx = 0; idx + 2 < StripNumIndices; idx += 3)
							{
								FSourceModelTriangle Tri;
								Tri.MaterialIndex = MeshMaterialIndex;

								for (int32 v = 0; v < 3; v++)
								{
									// Read index from strip group's index array
									int32 IdxPos = IndexArrayBase + (StripIndexOffset + idx + v) * 2;
									if (IdxPos + 2 > DataSize) break;
									uint16 VtxIndex = ReadValue<uint16>(Data, IdxPos);

									// Read VTX Vertex_t (9 bytes!) from strip group's vertex array
									int32 VtxPos = VertArrayBase + (StripVertOffset + VtxIndex) * VTXOffsets::VTXVertexSize;
									if (VtxPos + VTXOffsets::VTXVertexSize > DataSize) break;

									// origMeshVertID at byte offset 4 (after boneWeightIndex[3] + numBones)
									uint16 OrigMeshVertID = ReadValue<uint16>(Data, VtxPos + 4);

									// Convert mesh-relative vertex ID to absolute VVD index
									Tri.VertexIndices[v] = MeshVertexOffset + OrigMeshVertID;
								}

								OutMesh.Triangles.Add(Tri);
								TotalTriangles++;
							}
						}
						else if (StripFlags & STRIP_IS_TRISTRIP)
						{
							// Triangle strip - every 3 consecutive, alternating winding
							for (int32 idx = 0; idx + 2 < StripNumIndices; idx++)
							{
								uint16 VtxIndices[3];
								bool bValid = true;

								for (int32 v = 0; v < 3; v++)
								{
									int32 IdxPos = IndexArrayBase + (StripIndexOffset + idx + v) * 2;
									if (IdxPos + 2 > DataSize) { bValid = false; break; }
									uint16 StripVtxIdx = ReadValue<uint16>(Data, IdxPos);

									int32 VtxPos = VertArrayBase + (StripVertOffset + StripVtxIdx) * VTXOffsets::VTXVertexSize;
									if (VtxPos + VTXOffsets::VTXVertexSize > DataSize) { bValid = false; break; }

									VtxIndices[v] = ReadValue<uint16>(Data, VtxPos + 4); // origMeshVertID
								}

								if (!bValid) break;

								// Skip degenerate triangles
								if (VtxIndices[0] == VtxIndices[1] || VtxIndices[1] == VtxIndices[2] || VtxIndices[0] == VtxIndices[2])
									continue;

								FSourceModelTriangle Tri;
								Tri.MaterialIndex = MeshMaterialIndex;

								// Alternate winding for triangle strips
								if (idx % 2 == 0)
								{
									Tri.VertexIndices[0] = MeshVertexOffset + VtxIndices[0];
									Tri.VertexIndices[1] = MeshVertexOffset + VtxIndices[1];
									Tri.VertexIndices[2] = MeshVertexOffset + VtxIndices[2];
								}
								else
								{
									Tri.VertexIndices[0] = MeshVertexOffset + VtxIndices[2];
									Tri.VertexIndices[1] = MeshVertexOffset + VtxIndices[1];
									Tri.VertexIndices[2] = MeshVertexOffset + VtxIndices[0];
								}

								OutMesh.Triangles.Add(Tri);
								TotalTriangles++;
							}
						}
					}
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("MDLReader: Extracted %d triangles across %d meshes from VTX"), TotalTriangles, OutModel.Meshes.Num());
	return true;
}

// ============================================================================
// Main Entry Point
// ============================================================================

FSourceModelData FMDLReader::ReadModel(
	const TArray<uint8>& MDLData,
	const TArray<uint8>& VVDData,
	const TArray<uint8>& VTXData,
	int32 RequestedLOD)
{
	FSourceModelData Model;

	// Parse MDL header
	if (!ParseMDLHeader(MDLData.GetData(), MDLData.Num(), Model))
	{
		return Model;
	}

	// Parse MDL sub-sections
	ParseMDLMaterials(MDLData.GetData(), MDLData.Num(), Model);
	ParseMDLTextureDirs(MDLData.GetData(), MDLData.Num(), Model);
	ParseMDLSkinTable(MDLData.GetData(), MDLData.Num(), Model);
	ParseMDLBones(MDLData.GetData(), MDLData.Num(), Model);
	ParseMDLSurfaceProp(MDLData.GetData(), MDLData.Num(), Model);

	// Parse VVD vertices
	if (!ParseVVD(VVDData.GetData(), VVDData.Num(), Model.Checksum, RequestedLOD, Model))
	{
		return Model;
	}

	// Parse VTX triangles
	if (!ParseVTX(VTXData.GetData(), VTXData.Num(), Model.Version, Model.Checksum,
		MDLData.GetData(), MDLData.Num(), RequestedLOD, Model))
	{
		return Model;
	}

	Model.bSuccess = true;
	UE_LOG(LogTemp, Log, TEXT("MDLReader: Successfully parsed '%s': %d verts, %d meshes, %d materials, %d bones"),
		*Model.Name, Model.Vertices.Num(), Model.Meshes.Num(), Model.MaterialNames.Num(), Model.Bones.Num());

	return Model;
}
