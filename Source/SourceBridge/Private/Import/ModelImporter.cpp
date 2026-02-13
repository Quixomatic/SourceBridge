#include "Import/ModelImporter.h"
#include "Import/MDLReader.h"
#include "Import/MaterialImporter.h"
#include "Models/SourceModelManifest.h"
#include "Compile/CompilePipeline.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "HAL/PlatformFileManager.h"

// Static member initialization
TMap<FString, UStaticMesh*> FModelImporter::ModelCache;
TMap<FString, TSharedPtr<FSourceModelData>> FModelImporter::ParsedModelCache;
FString FModelImporter::AssetSearchPath;
TArray<FString> FModelImporter::AdditionalSearchPaths;
TArray<TSharedPtr<FVPKReader>> FModelImporter::VPKArchives;
bool FModelImporter::bGamePathsInitialized = false;

// ============================================================================
// Search Path Configuration
// ============================================================================

void FModelImporter::SetAssetSearchPath(const FString& Path)
{
	AssetSearchPath = Path;
	UE_LOG(LogTemp, Log, TEXT("ModelImporter: Asset search path: %s"), *Path);
}

void FModelImporter::SetupGameSearchPaths(const FString& GameName)
{
	if (bGamePathsInitialized)
		return;

	// Use same game directory discovery as MaterialImporter
	FString GameDir = FCompilePipeline::FindGameDirectory(GameName);
	if (GameDir.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("ModelImporter: Game directory not found for '%s'"), *GameName);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("ModelImporter: Game directory: %s"), *GameDir);

	// Add game directory for model files (models/ is a subdirectory)
	if (FPaths::DirectoryExists(GameDir / TEXT("models")))
	{
		AdditionalSearchPaths.AddUnique(GameDir);
		UE_LOG(LogTemp, Log, TEXT("ModelImporter: Added game models path: %s"), *(GameDir / TEXT("models")));
	}

	// HL2 base models
	FString EngineRoot = FPaths::GetPath(GameDir);
	FString HL2Dir = EngineRoot / TEXT("hl2");
	if (FPaths::DirectoryExists(HL2Dir / TEXT("models")))
	{
		AdditionalSearchPaths.AddUnique(HL2Dir);
		UE_LOG(LogTemp, Log, TEXT("ModelImporter: Added HL2 models path: %s"), *(HL2Dir / TEXT("models")));
	}

	// Open VPK archives for model access
	TArray<FString> VPKSearchDirs;
	VPKSearchDirs.Add(GameDir);
	VPKSearchDirs.Add(HL2Dir);

	for (const FString& SearchDir : VPKSearchDirs)
	{
		TArray<FString> VPKDirFiles;
		IFileManager::Get().FindFiles(VPKDirFiles, *(SearchDir / TEXT("*_dir.vpk")), true, false);

		for (const FString& VPKFile : VPKDirFiles)
		{
			FString FullPath = SearchDir / VPKFile;
			TSharedPtr<FVPKReader> VPK = MakeShared<FVPKReader>();
			if (VPK->Open(FullPath))
			{
				VPKArchives.Add(VPK);
				UE_LOG(LogTemp, Log, TEXT("ModelImporter: Opened VPK: %s (%d entries)"),
					*FullPath, VPK->GetEntryCount());
			}
		}
	}

	bGamePathsInitialized = true;
}

void FModelImporter::ClearCache()
{
	ModelCache.Empty();
	ParsedModelCache.Empty();
	// Do NOT clear AdditionalSearchPaths, VPKArchives, or bGamePathsInitialized
}

const FSourceModelData* FModelImporter::GetParsedModelData(const FString& SourceModelPath)
{
	FString NormPath = SourceModelPath.ToLower();
	NormPath.ReplaceInline(TEXT("\\"), TEXT("/"));

	TSharedPtr<FSourceModelData>* CachedData = ParsedModelCache.Find(NormPath);
	if (CachedData && CachedData->IsValid())
	{
		return CachedData->Get();
	}
	return nullptr;
}

// ============================================================================
// File Search
// ============================================================================

bool FModelImporter::ReadFileFromDisk(const FString& RelativePath, TArray<uint8>& OutData)
{
	// Normalize path separators
	FString NormPath = RelativePath;
	NormPath.ReplaceInline(TEXT("/"), TEXT("\\"));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Try asset search path first (BSP extracted)
	if (!AssetSearchPath.IsEmpty())
	{
		FString FullPath = AssetSearchPath / NormPath;
		if (PlatformFile.FileExists(*FullPath))
		{
			if (FFileHelper::LoadFileToArray(OutData, *FullPath))
			{
				return true;
			}
		}
	}

	// Try additional search paths (game install directories)
	for (const FString& SearchPath : AdditionalSearchPaths)
	{
		FString FullPath = SearchPath / NormPath;
		if (PlatformFile.FileExists(*FullPath))
		{
			if (FFileHelper::LoadFileToArray(OutData, *FullPath))
			{
				return true;
			}
		}
	}

	return false;
}

bool FModelImporter::ReadFileFromVPK(const FString& RelativePath, TArray<uint8>& OutData)
{
	// VPK uses forward slashes, lowercase
	FString VPKPath = RelativePath.ToLower();
	VPKPath.ReplaceInline(TEXT("\\"), TEXT("/"));

	for (const auto& VPK : VPKArchives)
	{
		if (VPK->Contains(VPKPath))
		{
			if (VPK->ReadFile(VPKPath, OutData))
			{
				return true;
			}
		}
	}

	return false;
}

bool FModelImporter::FindModelFiles(const FString& SourceModelPath,
	TArray<uint8>& OutMDL, TArray<uint8>& OutVVD, TArray<uint8>& OutVTX,
	TArray<uint8>& OutPHY)
{
	// Construct companion file paths
	FString BasePath = FPaths::ChangeExtension(SourceModelPath, TEXT(""));

	FString MDLPath = BasePath + TEXT(".mdl");
	FString VVDPath = BasePath + TEXT(".vvd");
	FString PHYPath = BasePath + TEXT(".phy");

	// VTX has multiple extensions - try in priority order
	TArray<FString> VTXExtensions = {
		TEXT(".dx90.vtx"),
		TEXT(".dx80.vtx"),
		TEXT(".sw.vtx"),
		TEXT(".vtx")
	};

	// Find MDL
	bool bFoundMDL = ReadFileFromDisk(MDLPath, OutMDL);
	if (!bFoundMDL)
	{
		bFoundMDL = ReadFileFromVPK(MDLPath, OutMDL);
	}

	if (!bFoundMDL)
	{
		UE_LOG(LogTemp, Verbose, TEXT("ModelImporter: MDL not found: %s"), *MDLPath);
		return false;
	}

	// Find VVD
	bool bFoundVVD = ReadFileFromDisk(VVDPath, OutVVD);
	if (!bFoundVVD)
	{
		bFoundVVD = ReadFileFromVPK(VVDPath, OutVVD);
	}

	if (!bFoundVVD)
	{
		UE_LOG(LogTemp, Warning, TEXT("ModelImporter: VVD not found: %s"), *VVDPath);
		return false;
	}

	// Find VTX - try each extension
	bool bFoundVTX = false;
	for (const FString& Ext : VTXExtensions)
	{
		FString VTXPath = BasePath + Ext;
		bFoundVTX = ReadFileFromDisk(VTXPath, OutVTX);
		if (!bFoundVTX)
		{
			bFoundVTX = ReadFileFromVPK(VTXPath, OutVTX);
		}
		if (bFoundVTX)
		{
			UE_LOG(LogTemp, Verbose, TEXT("ModelImporter: Found VTX: %s"), *VTXPath);
			break;
		}
	}

	if (!bFoundVTX)
	{
		UE_LOG(LogTemp, Warning, TEXT("ModelImporter: VTX not found for: %s"), *SourceModelPath);
		return false;
	}

	// Find PHY (optional - not required for model import)
	bool bFoundPHY = ReadFileFromDisk(PHYPath, OutPHY);
	if (!bFoundPHY)
	{
		bFoundPHY = ReadFileFromVPK(PHYPath, OutPHY);
	}

	UE_LOG(LogTemp, Verbose, TEXT("ModelImporter: Found model files: %s (MDL=%d, VVD=%d, VTX=%d, PHY=%d bytes)"),
		*SourceModelPath, OutMDL.Num(), OutVVD.Num(), OutVTX.Num(), OutPHY.Num());

	return true;
}

// ============================================================================
// Stock Model Detection & Disk Path Resolution
// ============================================================================

bool FModelImporter::IsStockModel(const FString& SourceModelPath)
{
	FString VPKPath = SourceModelPath.ToLower();
	VPKPath.ReplaceInline(TEXT("\\"), TEXT("/"));

	for (const auto& VPK : VPKArchives)
	{
		if (VPK->Contains(VPKPath))
		{
			return true;
		}
	}
	return false;
}

bool FModelImporter::FindModelDiskPaths(const FString& SourceModelPath,
	TMap<FString, FString>& OutFilePaths)
{
	FString BasePath = FPaths::ChangeExtension(SourceModelPath, TEXT(""));
	FString NormBase = BasePath;
	NormBase.ReplaceInline(TEXT("/"), TEXT("\\"));

	// Extensions to search for
	TArray<FString> Extensions = {
		TEXT(".mdl"),
		TEXT(".vvd"),
		TEXT(".dx90.vtx"),
		TEXT(".dx80.vtx"),
		TEXT(".sw.vtx"),
		TEXT(".phy")
	};

	auto FindOnDisk = [&](const FString& RelPath) -> FString
	{
		if (!AssetSearchPath.IsEmpty())
		{
			FString FullPath = AssetSearchPath / RelPath;
			if (FPaths::FileExists(FullPath))
				return FullPath;
		}
		for (const FString& SearchPath : AdditionalSearchPaths)
		{
			FString FullPath = SearchPath / RelPath;
			if (FPaths::FileExists(FullPath))
				return FullPath;
		}
		return FString();
	};

	bool bFoundMDL = false;
	for (const FString& Ext : Extensions)
	{
		FString RelPath = NormBase + Ext;
		FString DiskPath = FindOnDisk(RelPath);
		if (!DiskPath.IsEmpty())
		{
			OutFilePaths.Add(Ext, DiskPath);
			if (Ext == TEXT(".mdl"))
				bFoundMDL = true;
		}
	}

	return bFoundMDL;
}

// ============================================================================
// Material Resolution
// ============================================================================

TArray<UMaterialInterface*> FModelImporter::ResolveMaterials(const FSourceModelData& ModelData, int32 SkinIndex)
{
	TArray<UMaterialInterface*> Materials;

	UE_LOG(LogTemp, Log, TEXT("ModelImporter: Resolving %d materials (%d search dirs)"),
		ModelData.MaterialNames.Num(), ModelData.MaterialSearchDirs.Num());
	GLog->Flush();

	// Build material path for each material name using texture directories
	for (int32 i = 0; i < ModelData.MaterialNames.Num(); i++)
	{
		// If skin table exists and skin index is valid, remap texture index
		int32 TextureIndex = i;
		if (SkinIndex > 0 && SkinIndex < ModelData.SkinFamilies.Num() && i < ModelData.NumSkinReferences)
		{
			TextureIndex = ModelData.SkinFamilies[SkinIndex][i];
		}
		else if (ModelData.SkinFamilies.Num() > 0 && i < ModelData.NumSkinReferences)
		{
			TextureIndex = ModelData.SkinFamilies[0][i];
		}

		FString MatName = (TextureIndex >= 0 && TextureIndex < ModelData.MaterialNames.Num())
			? ModelData.MaterialNames[TextureIndex]
			: ModelData.MaterialNames[i];

		// Try each texture directory to find the material
		UMaterialInterface* ResolvedMat = nullptr;

		for (const FString& Dir : ModelData.MaterialSearchDirs)
		{
			FString FullPath = Dir + MatName;
			ResolvedMat = FMaterialImporter::ResolveSourceMaterial(FullPath);
			if (ResolvedMat)
			{
				break;
			}
		}

		// Fallback: try just the material name alone
		if (!ResolvedMat)
		{
			ResolvedMat = FMaterialImporter::ResolveSourceMaterial(MatName);
		}

		// Last resort: create placeholder
		if (!ResolvedMat)
		{
			FString FallbackPath = ModelData.MaterialSearchDirs.Num() > 0
				? ModelData.MaterialSearchDirs[0] + MatName
				: MatName;
			ResolvedMat = FMaterialImporter::CreatePlaceholderMaterial(FallbackPath);
		}

		Materials.Add(ResolvedMat);
	}

	return Materials;
}

TArray<UMaterialInterface*> FModelImporter::GetMaterialsForSkin(const FString& SourceModelPath, int32 SkinIndex)
{
	FString NormPath = SourceModelPath.ToLower();
	NormPath.ReplaceInline(TEXT("\\"), TEXT("/"));

	TSharedPtr<FSourceModelData>* CachedData = ParsedModelCache.Find(NormPath);
	if (CachedData && CachedData->IsValid())
	{
		return ResolveMaterials(**CachedData, SkinIndex);
	}

	return TArray<UMaterialInterface*>();
}

// ============================================================================
// UStaticMesh Creation
// ============================================================================

UStaticMesh* FModelImporter::CreateStaticMesh(const FSourceModelData& ModelData,
	const FString& SourceModelPath, int32 SkinIndex)
{
	if (ModelData.Vertices.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ModelImporter: No vertices in model '%s'"), *SourceModelPath);
		return nullptr;
	}

	// Count total triangles
	int32 TotalTris = 0;
	for (const auto& Mesh : ModelData.Meshes)
	{
		TotalTris += Mesh.Triangles.Num();
	}

	if (TotalTris == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ModelImporter: No triangles in model '%s'"), *SourceModelPath);
		return nullptr;
	}

	// Resolve materials
	TArray<UMaterialInterface*> Materials = ResolveMaterials(ModelData, SkinIndex);

	// Create asset path from source model path: /Game/SourceBridge/Models/<path>
	FString CleanPath = SourceModelPath.ToLower();
	CleanPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	CleanPath.RemoveFromStart(TEXT("models/"));
	// Remove .mdl extension for asset name
	CleanPath = FPaths::GetPath(CleanPath) / FPaths::GetBaseFilename(CleanPath);
	CleanPath = CleanPath.Replace(TEXT(" "), TEXT("_"));

	FString AssetPath = FString::Printf(TEXT("/Game/SourceBridge/Models/%s"), *CleanPath);
	FString AssetName = FPaths::GetCleanFilename(AssetPath);
	FString MeshName = TEXT("SM_") + AssetName;

	// Create persistent package
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("ModelImporter: Failed to create package for model: %s"), *AssetPath);
		return nullptr;
	}
	Package->FullyLoad();

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!StaticMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("ModelImporter: Failed to create UStaticMesh for '%s'"), *SourceModelPath);
		return nullptr;
	}

	// Initialize mesh description
	FMeshDescription MeshDesc;
	FStaticMeshAttributes StaticMeshAttrs(MeshDesc);
	StaticMeshAttrs.Register();

	// Get attribute accessors
	TVertexAttributesRef<FVector3f> VertexPositions = MeshDesc.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> VertexNormals =
		MeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector3f>(MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesRef<FVector3f> VertexTangents =
		MeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector3f>(MeshAttribute::VertexInstance::Tangent);
	TVertexInstanceAttributesRef<float> VertexBinormalSigns =
		MeshDesc.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);
	TVertexInstanceAttributesRef<FVector2f> VertexUVs =
		MeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector2f>(MeshAttribute::VertexInstance::TextureCoordinate);
	TVertexInstanceAttributesRef<FVector4f> VertexColors =
		MeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector4f>(MeshAttribute::VertexInstance::Color);

	// Reserve polygon groups (one per unique material)
	TMap<int32, FPolygonGroupID> MaterialToGroupID;
	for (const auto& Mesh : ModelData.Meshes)
	{
		if (!MaterialToGroupID.Contains(Mesh.MaterialIndex))
		{
			FPolygonGroupID GroupID = MeshDesc.CreatePolygonGroup();
			MaterialToGroupID.Add(Mesh.MaterialIndex, GroupID);
		}
	}

	// Create vertices (positions only - vertex instances hold normals/UVs)
	// Source → UE coordinate conversion: ÷ 0.525, negate Y
	constexpr float InvScale = 1.0f / 0.525f;

	TArray<FVertexID> VertexIDs;
	VertexIDs.SetNum(ModelData.Vertices.Num());
	for (int32 i = 0; i < ModelData.Vertices.Num(); i++)
	{
		const FSourceModelVertex& SrcV = ModelData.Vertices[i];
		FVertexID VID = MeshDesc.CreateVertex();
		VertexIDs[i] = VID;

		// Source → UE position conversion
		VertexPositions[VID] = FVector3f(
			SrcV.Position.X * InvScale,
			-SrcV.Position.Y * InvScale,  // Negate Y
			SrcV.Position.Z * InvScale
		);
	}

	// Create triangles
	for (const auto& SrcMesh : ModelData.Meshes)
	{
		FPolygonGroupID* GroupID = MaterialToGroupID.Find(SrcMesh.MaterialIndex);
		if (!GroupID) continue;

		for (const auto& Tri : SrcMesh.Triangles)
		{
			// Validate vertex indices
			bool bValid = true;
			for (int32 v = 0; v < 3; v++)
			{
				if (Tri.VertexIndices[v] < 0 || Tri.VertexIndices[v] >= ModelData.Vertices.Num())
				{
					bValid = false;
					break;
				}
			}
			if (!bValid) continue;

			// Create vertex instances (one per triangle corner)
			TArray<FVertexInstanceID> TriVerts;
			TriVerts.SetNum(3);

			for (int32 v = 0; v < 3; v++)
			{
				int32 SrcIdx = Tri.VertexIndices[v];
				const FSourceModelVertex& SrcV = ModelData.Vertices[SrcIdx];

				FVertexInstanceID VIID = MeshDesc.CreateVertexInstance(VertexIDs[SrcIdx]);
				TriVerts[v] = VIID;

				// Normal: negate Y only (no scaling)
				VertexNormals[VIID] = FVector3f(SrcV.Normal.X, -SrcV.Normal.Y, SrcV.Normal.Z);

				// Tangent
				VertexTangents[VIID] = FVector3f(SrcV.Tangent.X, -SrcV.Tangent.Y, SrcV.Tangent.Z);
				VertexBinormalSigns[VIID] = SrcV.Tangent.W;

				// UV - direct copy (both engines use DirectX UV convention)
				VertexUVs.Set(VIID, 0, FVector2f(SrcV.UV.X, SrcV.UV.Y));

				// Vertex color (white default)
				VertexColors[VIID] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
			}

			// Create triangle (reversed winding for Source→UE handedness)
			TArray<FVertexInstanceID> WindedVerts = { TriVerts[0], TriVerts[2], TriVerts[1] };
			MeshDesc.CreatePolygon(*GroupID, WindedVerts);
		}
	}

	// Build LOD mesh descriptions
	TArray<FMeshDescription> LODMeshDescs;
	LODMeshDescs.Add(MoveTemp(MeshDesc));  // LOD 0 is what we just built

	// Build additional LODs from parsed data
	for (int32 LOD = 1; LOD < ModelData.LODs.Num(); LOD++)
	{
		const auto& LODData = ModelData.LODs[LOD];
		if (LODData.Vertices.Num() == 0 || LODData.Meshes.Num() == 0)
			continue;

		FMeshDescription LODMeshDesc;
		FStaticMeshAttributes LODAttrs(LODMeshDesc);
		LODAttrs.Register();

		TVertexAttributesRef<FVector3f> LODPositions = LODMeshDesc.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> LODNormals =
			LODMeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector3f>(MeshAttribute::VertexInstance::Normal);
		TVertexInstanceAttributesRef<FVector3f> LODTangents =
			LODMeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector3f>(MeshAttribute::VertexInstance::Tangent);
		TVertexInstanceAttributesRef<float> LODBinormalSigns =
			LODMeshDesc.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);
		TVertexInstanceAttributesRef<FVector2f> LODUVs =
			LODMeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector2f>(MeshAttribute::VertexInstance::TextureCoordinate);
		TVertexInstanceAttributesRef<FVector4f> LODColors =
			LODMeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector4f>(MeshAttribute::VertexInstance::Color);

		// Create polygon groups matching LOD 0's material mapping
		TMap<int32, FPolygonGroupID> LODMatGroups;
		for (const auto& M : LODData.Meshes)
		{
			if (!LODMatGroups.Contains(M.MaterialIndex))
			{
				LODMatGroups.Add(M.MaterialIndex, LODMeshDesc.CreatePolygonGroup());
			}
		}

		// Create vertices
		TArray<FVertexID> LODVertexIDs;
		LODVertexIDs.SetNum(LODData.Vertices.Num());
		for (int32 i = 0; i < LODData.Vertices.Num(); i++)
		{
			const FSourceModelVertex& SrcV = LODData.Vertices[i];
			FVertexID VID = LODMeshDesc.CreateVertex();
			LODVertexIDs[i] = VID;
			LODPositions[VID] = FVector3f(
				SrcV.Position.X * InvScale,
				-SrcV.Position.Y * InvScale,
				SrcV.Position.Z * InvScale);
		}

		// Create triangles
		for (const auto& SrcMesh : LODData.Meshes)
		{
			FPolygonGroupID* GID = LODMatGroups.Find(SrcMesh.MaterialIndex);
			if (!GID) continue;

			for (const auto& Tri : SrcMesh.Triangles)
			{
				bool bValid = true;
				for (int32 v = 0; v < 3; v++)
				{
					if (Tri.VertexIndices[v] < 0 || Tri.VertexIndices[v] >= LODData.Vertices.Num())
					{
						bValid = false;
						break;
					}
				}
				if (!bValid) continue;

				TArray<FVertexInstanceID> TriVerts;
				TriVerts.SetNum(3);

				for (int32 v = 0; v < 3; v++)
				{
					int32 SrcIdx = Tri.VertexIndices[v];
					const FSourceModelVertex& SrcV = LODData.Vertices[SrcIdx];

					FVertexInstanceID VIID = LODMeshDesc.CreateVertexInstance(LODVertexIDs[SrcIdx]);
					TriVerts[v] = VIID;

					LODNormals[VIID] = FVector3f(SrcV.Normal.X, -SrcV.Normal.Y, SrcV.Normal.Z);
					LODTangents[VIID] = FVector3f(SrcV.Tangent.X, -SrcV.Tangent.Y, SrcV.Tangent.Z);
					LODBinormalSigns[VIID] = SrcV.Tangent.W;
					LODUVs.Set(VIID, 0, FVector2f(SrcV.UV.X, SrcV.UV.Y));
					LODColors[VIID] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
				}

				TArray<FVertexInstanceID> WindedVerts = { TriVerts[0], TriVerts[2], TriVerts[1] };
				LODMeshDesc.CreatePolygon(*GID, WindedVerts);
			}
		}

		LODMeshDescs.Add(MoveTemp(LODMeshDesc));
	}

	// Assign to static mesh
	TArray<const FMeshDescription*> MeshDescPtrs;
	for (const auto& Desc : LODMeshDescs)
	{
		MeshDescPtrs.Add(&Desc);
	}

	// Set up material slots
	TArray<FStaticMaterial> StaticMaterials;
	for (const auto& Pair : MaterialToGroupID)
	{
		int32 MatIdx = Pair.Key;
		UMaterialInterface* Mat = (MatIdx >= 0 && MatIdx < Materials.Num()) ? Materials[MatIdx] : nullptr;
		FString SlotName = (MatIdx >= 0 && MatIdx < ModelData.MaterialNames.Num())
			? ModelData.MaterialNames[MatIdx] : FString::Printf(TEXT("Material_%d"), MatIdx);
		StaticMaterials.Add(FStaticMaterial(Mat, FName(*SlotName)));
	}

	StaticMesh->GetStaticMaterials() = StaticMaterials;

	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	BuildParams.bMarkPackageDirty = false;
	BuildParams.bBuildSimpleCollision = false;
	BuildParams.bFastBuild = true;
	StaticMesh->BuildFromMeshDescriptions(MeshDescPtrs, BuildParams);

	// Save as persistent asset
	FString PackageFileName;
	if (FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		FString Dir = FPaths::GetPath(PackageFileName);
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*Dir);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, StaticMesh, *PackageFileName, SaveArgs);
	}

	UE_LOG(LogTemp, Log, TEXT("ModelImporter: Created UStaticMesh '%s' at %s (%d verts, %d tris, %d materials, %d LODs)"),
		*MeshName, *AssetPath, ModelData.Vertices.Num(), TotalTris, Materials.Num(), LODMeshDescs.Num());

	return StaticMesh;
}

// ============================================================================
// Main Entry Point
// ============================================================================

UStaticMesh* FModelImporter::ResolveModel(const FString& SourceModelPath, int32 SkinIndex)
{
	// Normalize path
	FString NormPath = SourceModelPath.ToLower();
	NormPath.ReplaceInline(TEXT("\\"), TEXT("/"));

	// Check cache
	FString CacheKey = FString::Printf(TEXT("%s_skin%d"), *NormPath, SkinIndex);
	if (UStaticMesh** Cached = ModelCache.Find(CacheKey))
	{
		return *Cached;
	}

	// Check if a persistent asset already exists on disk
	FString CleanPath = NormPath;
	CleanPath.RemoveFromStart(TEXT("models/"));
	CleanPath = FPaths::GetPath(CleanPath) / FPaths::GetBaseFilename(CleanPath);
	CleanPath = CleanPath.Replace(TEXT(" "), TEXT("_"));
	FString AssetPath = FString::Printf(TEXT("/Game/SourceBridge/Models/%s"), *CleanPath);
	FString AssetName = FPaths::GetCleanFilename(AssetPath);
	FString FullObjectPath = AssetPath + TEXT(".") + AssetName;
	UStaticMesh* ExistingMesh = LoadObject<UStaticMesh>(nullptr, *FullObjectPath);
	if (ExistingMesh)
	{
		UE_LOG(LogTemp, Log, TEXT("ModelImporter: '%s' -> loaded from persistent asset"), *SourceModelPath);
		ModelCache.Add(CacheKey, ExistingMesh);
		return ExistingMesh;
	}

	UE_LOG(LogTemp, Log, TEXT("ModelImporter: Resolving model '%s' (skin %d)..."), *SourceModelPath, SkinIndex);
	GLog->Flush();

	// Check if we already parsed this model (different skin)
	TSharedPtr<FSourceModelData> ParsedData;
	if (TSharedPtr<FSourceModelData>* CachedParsed = ParsedModelCache.Find(NormPath))
	{
		ParsedData = *CachedParsed;
	}
	else
	{
		// Find and load companion files
		TArray<uint8> MDLData, VVDData, VTXData, PHYData;
		if (!FindModelFiles(NormPath, MDLData, VVDData, VTXData, PHYData))
		{
			UE_LOG(LogTemp, Warning, TEXT("ModelImporter: Model files not found: %s"), *SourceModelPath);
			ModelCache.Add(CacheKey, nullptr);
			return nullptr;
		}

		// Parse with all LODs
		ParsedData = MakeShared<FSourceModelData>(FMDLReader::ReadModelAllLODs(MDLData, VVDData, VTXData));
		if (!ParsedData->bSuccess)
		{
			UE_LOG(LogTemp, Warning, TEXT("ModelImporter: Failed to parse model '%s': %s"),
				*SourceModelPath, *ParsedData->ErrorMessage);
			ModelCache.Add(CacheKey, nullptr);
			return nullptr;
		}

		// Parse PHY if available
		if (PHYData.Num() > 0)
		{
			FMDLReader::ParsePHY(PHYData, *ParsedData);
		}

		ParsedModelCache.Add(NormPath, ParsedData);
	}

	// Create UStaticMesh from parsed data
	UStaticMesh* Mesh = CreateStaticMesh(*ParsedData, SourceModelPath, SkinIndex);
	ModelCache.Add(CacheKey, Mesh);

	// Register in model manifest for asset management and export packing
	if (Mesh)
	{
		USourceModelManifest* ModelManifest = USourceModelManifest::Get();
		if (ModelManifest)
		{
			FSourceModelEntry ManifestEntry;
			ManifestEntry.SourcePath = NormPath;
			ManifestEntry.MeshAsset = FSoftObjectPath(Mesh);
			ManifestEntry.bIsStock = IsStockModel(NormPath);
			ManifestEntry.Type = ManifestEntry.bIsStock ? ESourceModelType::Stock : ESourceModelType::Imported;
			ManifestEntry.SurfaceProp = ParsedData->SurfaceProp;
			ManifestEntry.bIsStaticProp = ParsedData->bIsStaticProp;
			ManifestEntry.ModelMass = ParsedData->Mass;
			ManifestEntry.CDMaterials = ParsedData->MaterialSearchDirs;
			ManifestEntry.LastImported = FDateTime::Now();

			// Collect disk paths for non-stock models (used by export packing)
			if (!ManifestEntry.bIsStock)
			{
				TMap<FString, FString> FilePaths;
				if (FindModelDiskPaths(NormPath, FilePaths))
				{
					ManifestEntry.DiskPaths = MoveTemp(FilePaths);
				}
			}

			ModelManifest->Register(ManifestEntry);
		}
	}

	return Mesh;
}
