#include "Import/MaterialImporter.h"
#include "Import/VTFReader.h"
#include "Import/VPKReader.h"
#include "Materials/SourceMaterialManifest.h"
#include "Compile/CompilePipeline.h"
#include "UI/SourceBridgeSettings.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"

// Static member initialization
TMap<FString, UMaterialInterface*> FMaterialImporter::MaterialCache;
TMap<FString, FMaterialImporter::FTextureCacheEntry> FMaterialImporter::TextureInfoCache;
TMap<FString, FString> FMaterialImporter::ReverseToolMappings;
FString FMaterialImporter::AssetSearchPath;
TArray<FString> FMaterialImporter::AdditionalSearchPaths;
TArray<TSharedPtr<FVPKReader>> FMaterialImporter::VPKArchives;
UMaterial* FMaterialImporter::CachedOpaqueMaterial = nullptr;
UMaterial* FMaterialImporter::CachedMaskedMaterial = nullptr;
UMaterial* FMaterialImporter::CachedTranslucentMaterial = nullptr;
UMaterial* FMaterialImporter::CachedColorMaterial = nullptr;

// ===========================================================================
// VMT Parsing (unchanged from original)
// ===========================================================================

FVMTParsedMaterial FMaterialImporter::ParseVMT(const FString& VMTContent)
{
	FVMTParsedMaterial Result;

	const TCHAR* Ptr = *VMTContent;

	auto SkipWS = [&]()
	{
		while (*Ptr)
		{
			if (*Ptr == ' ' || *Ptr == '\t' || *Ptr == '\r' || *Ptr == '\n')
			{
				Ptr++;
			}
			else if (*Ptr == '/' && *(Ptr + 1) == '/')
			{
				while (*Ptr && *Ptr != '\n') Ptr++;
			}
			else
			{
				break;
			}
		}
	};

	auto ReadQuoted = [&]() -> FString
	{
		if (*Ptr != '"') return FString();
		Ptr++;
		FString Value;
		while (*Ptr && *Ptr != '"')
		{
			Value += *Ptr;
			Ptr++;
		}
		if (*Ptr == '"') Ptr++;
		return Value;
	};

	auto ReadToken = [&]() -> FString
	{
		FString Token;
		while (*Ptr && *Ptr != ' ' && *Ptr != '\t' && *Ptr != '\r' &&
			*Ptr != '\n' && *Ptr != '{' && *Ptr != '}' && *Ptr != '"')
		{
			Token += *Ptr;
			Ptr++;
		}
		return Token;
	};

	auto SkipInlineWS = [&]()
	{
		while (*Ptr == ' ' || *Ptr == '\t')
		{
			Ptr++;
		}
	};

	auto ReadRestOfLine = [&]() -> FString
	{
		FString Value;
		while (*Ptr && *Ptr != '\r' && *Ptr != '\n' && *Ptr != '{' && *Ptr != '}')
		{
			if (*Ptr == '/' && *(Ptr + 1) == '/')
			{
				break;
			}
			Value += *Ptr;
			Ptr++;
		}
		return Value.TrimStartAndEnd();
	};

	// Read shader name
	SkipWS();
	if (*Ptr == '"')
	{
		Result.ShaderName = ReadQuoted();
	}
	else
	{
		Result.ShaderName = ReadToken();
	}

	// Find opening brace
	SkipWS();
	if (*Ptr == '{')
	{
		Ptr++;
		int32 Depth = 1;

		while (*Ptr && Depth > 0)
		{
			SkipWS();

			if (*Ptr == '{')
			{
				Depth++;
				Ptr++;
			}
			else if (*Ptr == '}')
			{
				Depth--;
				Ptr++;
			}
			else if (Depth == 1 && (*Ptr == '"' || *Ptr == '$' || *Ptr == '%' || FChar::IsAlpha(*Ptr)))
			{
				FString Key;
				if (*Ptr == '"')
				{
					Key = ReadQuoted();
				}
				else
				{
					Key = ReadToken();
				}

				SkipInlineWS();

				FString Value;
				if (*Ptr == '"')
				{
					Value = ReadQuoted();
				}
				else if (*Ptr && *Ptr != '\r' && *Ptr != '\n' && *Ptr != '{' && *Ptr != '}')
				{
					Value = ReadRestOfLine();
				}

				if (!Key.IsEmpty())
				{
					Result.Parameters.Add(Key.ToLower(), Value);
				}
			}
			else if (*Ptr == '"')
			{
				ReadQuoted();
			}
			else if (*Ptr)
			{
				FString Tok = ReadToken();
				if (Tok.IsEmpty())
				{
					Ptr++;
				}
			}
		}
	}

	return Result;
}

FVMTParsedMaterial FMaterialImporter::ParseVMTFile(const FString& FilePath)
{
	FString Content;
	if (FFileHelper::LoadFileToString(Content, *FilePath))
	{
		return ParseVMT(Content);
	}
	return FVMTParsedMaterial();
}

// ===========================================================================
// Search Path Configuration (unchanged)
// ===========================================================================

void FMaterialImporter::SetAssetSearchPath(const FString& Path)
{
	AssetSearchPath = Path;
	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Asset search path set to: %s"), *Path);
}

void FMaterialImporter::SetupGameSearchPaths(const FString& GameName)
{
	AdditionalSearchPaths.Empty();

	FString GameDir = FCompilePipeline::FindGameDirectory(GameName);
	if (GameDir.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("MaterialImporter: Could not find game directory for '%s'"), *GameName);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Game directory: %s"), *GameDir);

	if (FPaths::DirectoryExists(GameDir / TEXT("materials")))
	{
		AdditionalSearchPaths.Add(GameDir);
		UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Added game materials path: %s"), *GameDir);
	}

	FString CustomDir = GameDir / TEXT("custom");
	if (FPaths::DirectoryExists(CustomDir))
	{
		TArray<FString> CustomSubDirs;
		IFileManager::Get().FindFiles(CustomSubDirs, *(CustomDir / TEXT("*")), false, true);
		for (const FString& SubDir : CustomSubDirs)
		{
			FString SubDirFull = CustomDir / SubDir;
			if (FPaths::DirectoryExists(SubDirFull / TEXT("materials")))
			{
				AdditionalSearchPaths.Add(SubDirFull);
				UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Added custom materials path: %s"), *SubDirFull);
			}
		}
	}

	FString DownloadDir = GameDir / TEXT("download");
	if (FPaths::DirectoryExists(DownloadDir / TEXT("materials")))
	{
		AdditionalSearchPaths.Add(DownloadDir);
		UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Added download materials path: %s"), *DownloadDir);
	}

	VPKArchives.Empty();
	FString EngineRoot = FPaths::GetPath(GameDir);

	TArray<FString> VPKSearchDirs;
	VPKSearchDirs.Add(GameDir);
	VPKSearchDirs.Add(EngineRoot / TEXT("hl2"));
	VPKSearchDirs.Add(EngineRoot / TEXT("platform"));

	FString HL2Dir = EngineRoot / TEXT("hl2");
	if (FPaths::DirectoryExists(HL2Dir / TEXT("materials")))
	{
		AdditionalSearchPaths.Add(HL2Dir);
		UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Added HL2 base materials path: %s"), *HL2Dir);
	}

	for (const FString& VPKDir : VPKSearchDirs)
	{
		if (!FPaths::DirectoryExists(VPKDir)) continue;

		TArray<FString> VPKDirFiles;
		IFileManager::Get().FindFiles(VPKDirFiles, *(VPKDir / TEXT("*_dir.vpk")), true, false);
		for (const FString& VPKFile : VPKDirFiles)
		{
			if (VPKFile.Contains(TEXT("sound"))) continue;

			FString FullPath = VPKDir / VPKFile;
			TSharedPtr<FVPKReader> Reader = MakeShared<FVPKReader>();
			if (Reader->Open(FullPath))
			{
				UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Opened VPK: %s (%d entries)"),
					*FullPath, Reader->GetEntryCount());
				VPKArchives.Add(Reader);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: %d additional search paths + %d VPK archives configured"),
		AdditionalSearchPaths.Num(), VPKArchives.Num());
}

void FMaterialImporter::EnsureReverseToolMappings()
{
	if (ReverseToolMappings.Num() > 0) return;

	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSNODRAW"), TEXT("Tool_Nodraw"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSCLIP"), TEXT("Tool_Clip"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSPLAYERCLIP"), TEXT("Tool_PlayerClip"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSNPCCLIP"), TEXT("Tool_NPCClip"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSTRIGGER"), TEXT("Tool_Trigger"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSSKYBOX"), TEXT("Tool_Skybox"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSSKIP"), TEXT("Tool_Skip"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSHINT"), TEXT("Tool_Hint"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSINVISIBLE"), TEXT("Tool_Invisible"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSAREAPORTAL"), TEXT("Tool_Areaportal"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSBLOCKLIGHT"), TEXT("Tool_Blocklight"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSBLOCK_LOS"), TEXT("Tool_BlockLOS"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSBLOCKBULLETS"), TEXT("Tool_BlockBullets"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSFOG"), TEXT("Tool_Fog"));
	ReverseToolMappings.Add(TEXT("TOOLS/TOOLSBLACK"), TEXT("Tool_Black"));
}

void FMaterialImporter::EnsureVPKArchivesLoaded()
{
	if (VPKArchives.Num() > 0 || AdditionalSearchPaths.Num() > 0)
	{
		return;
	}

	FString GameName = TEXT("cstrike");
	if (USourceBridgeSettings* Settings = USourceBridgeSettings::Get())
	{
		if (!Settings->TargetGame.IsEmpty())
		{
			GameName = Settings->TargetGame;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Lazy-loading VPK archives for game '%s'..."), *GameName);
	SetupGameSearchPaths(GameName);
}

// ===========================================================================
// Material Resolution (persistent)
// ===========================================================================

UMaterialInterface* FMaterialImporter::ResolveSourceMaterial(const FString& SourceMaterialPath)
{
	if (SourceMaterialPath.IsEmpty()) return nullptr;

	FString NormalizedPath = SourceMaterialPath.ToUpper();

	// 1. Check runtime pointer cache
	if (UMaterialInterface** Found = MaterialCache.Find(NormalizedPath))
	{
		if (*Found && (*Found)->IsValidLowLevel())
		{
			return *Found;
		}
		MaterialCache.Remove(NormalizedPath);
	}

	// 2. Check manifest for existing persistent asset
	USourceMaterialManifest* Manifest = USourceMaterialManifest::Get();
	if (Manifest)
	{
		FSourceMaterialEntry* Entry = Manifest->FindBySourcePath(SourceMaterialPath);
		if (Entry && !Entry->MaterialAsset.GetAssetPathString().IsEmpty())
		{
			UMaterialInterface* Loaded = Cast<UMaterialInterface>(Entry->MaterialAsset.TryLoad());
			if (Loaded)
			{
				MaterialCache.Add(NormalizedPath, Loaded);
				UE_LOG(LogTemp, Log, TEXT("MaterialImporter: '%s' -> loaded from manifest"), *SourceMaterialPath);
				return Loaded;
			}
		}
	}

	// Lazily load VPK archives if not yet initialized
	EnsureVPKArchivesLoaded();

	UMaterialInterface* Material = nullptr;

	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Resolving '%s'..."), *SourceMaterialPath);

	// 3. Try to create from VMT/VTF
	if (!AssetSearchPath.IsEmpty() || AdditionalSearchPaths.Num() > 0 || VPKArchives.Num() > 0)
	{
		Material = CreateMaterialFromVMT(SourceMaterialPath);
		if (Material)
		{
			UE_LOG(LogTemp, Log, TEXT("MaterialImporter: '%s' -> created persistent material from VMT/VTF"), *SourceMaterialPath);
		}
	}

	// 4. Try to find existing UE material in asset registry
	if (!Material)
	{
		Material = FindExistingMaterial(SourceMaterialPath);
		if (Material)
		{
			UE_LOG(LogTemp, Log, TEXT("MaterialImporter: '%s' -> found via asset registry"), *SourceMaterialPath);
		}
	}

	// 5. Create placeholder
	if (!Material)
	{
		UE_LOG(LogTemp, Warning, TEXT("MaterialImporter: '%s' not found, creating placeholder"), *SourceMaterialPath);
		Material = CreatePlaceholderMaterial(SourceMaterialPath);
	}

	if (Material)
	{
		MaterialCache.Add(NormalizedPath, Material);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("MaterialImporter: FAILED to resolve material '%s'"), *SourceMaterialPath);
	}

	return Material;
}

UMaterialInterface* FMaterialImporter::FindExistingMaterial(const FString& SourceMaterialPath)
{
	EnsureReverseToolMappings();

	FString Upper = SourceMaterialPath.ToUpper();

	// Check reverse tool mappings
	if (const FString* UEName = ReverseToolMappings.Find(Upper))
	{
		FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		AssetRegistry.Get().GetAssetsByClass(UMaterialInterface::StaticClass()->GetClassPathName(), Assets);

		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString().Equals(*UEName, ESearchCase::IgnoreCase))
			{
				return Cast<UMaterialInterface>(Asset.GetAsset());
			}
		}
	}

	return nullptr;
}

UMaterialInterface* FMaterialImporter::CreateMaterialFromVMT(const FString& SourceMaterialPath)
{
	// Build the list of directories to search
	TArray<FString> SearchRoots;
	if (!AssetSearchPath.IsEmpty())
	{
		SearchRoots.Add(AssetSearchPath);
	}
	SearchRoots.Append(AdditionalSearchPaths);

	// ---- Find VMT ----
	FString VMTFullPath;
	FString VMTRelPath = TEXT("materials") / SourceMaterialPath + TEXT(".vmt");

	// 1. Exact path on disk
	for (const FString& Root : SearchRoots)
	{
		FString CandidatePath = Root / VMTRelPath;
		CandidatePath = CandidatePath.Replace(TEXT("\\"), TEXT("/"));

		if (FPaths::FileExists(CandidatePath))
		{
			VMTFullPath = CandidatePath;
			break;
		}
	}

	// 2. VPK archives
	FVMTParsedMaterial VMTData;
	bool bFoundInVPK = false;
	if (VMTFullPath.IsEmpty())
	{
		FString VMTContent = FindVMTInVPK(SourceMaterialPath);
		if (!VMTContent.IsEmpty())
		{
			VMTData = ParseVMT(VMTContent);
			bFoundInVPK = true;
		}
	}

	// 3. Case-insensitive disk search (last resort)
	if (VMTFullPath.IsEmpty() && VMTData.ShaderName.IsEmpty())
	{
		FString SearchPath = SourceMaterialPath + TEXT(".vmt");
		SearchPath = SearchPath.Replace(TEXT("\\"), TEXT("/"));

		for (const FString& Root : SearchRoots)
		{
			FString MaterialsDir = Root / TEXT("materials");
			if (!FPaths::DirectoryExists(MaterialsDir)) continue;

			TArray<FString> AllVMTs;
			IFileManager::Get().FindFilesRecursive(AllVMTs, *MaterialsDir, TEXT("*.vmt"), true, false);

			for (const FString& FoundVMT : AllVMTs)
			{
				FString RelPath = FoundVMT;
				FPaths::MakePathRelativeTo(RelPath, *(MaterialsDir + TEXT("/")));
				RelPath = RelPath.Replace(TEXT("\\"), TEXT("/"));

				if (RelPath.Equals(SearchPath, ESearchCase::IgnoreCase))
				{
					VMTFullPath = FoundVMT;
					break;
				}
			}

			if (!VMTFullPath.IsEmpty()) break;
		}

		if (VMTFullPath.IsEmpty())
		{
			return nullptr;
		}
	}

	// Parse from disk if not already parsed from VPK
	if (!VMTFullPath.IsEmpty())
	{
		VMTData = ParseVMTFile(VMTFullPath);
	}

	if (VMTData.ShaderName.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("MaterialImporter: Failed to parse VMT for: %s"), *SourceMaterialPath);
		return nullptr;
	}

	// ---- Determine alpha mode ----
	ESourceAlphaMode AlphaMode = ESourceAlphaMode::Opaque;
	if (VMTData.IsTranslucent())
	{
		AlphaMode = ESourceAlphaMode::Translucent;
	}
	else if (VMTData.Parameters.Contains(TEXT("$alphatest")))
	{
		AlphaMode = ESourceAlphaMode::Masked;
	}

	// ---- Load base texture ----
	FString BaseTexturePath = VMTData.GetBaseTexture();
	UTexture2D* BaseTexture = nullptr;

	if (!BaseTexturePath.IsEmpty())
	{
		TArray<uint8> VTFBytes;
		if (FindVTFBytes(BaseTexturePath, VTFBytes))
		{
			TArray<uint8> BGRAData;
			int32 TexW, TexH;
			bool bHasAlpha;
			if (FVTFReader::DecodeToBGRA(VTFBytes, BaseTexturePath, BGRAData, TexW, TexH, bHasAlpha))
			{
				// Refine alpha mode: $nocull + alpha format → likely masked
				if (AlphaMode == ESourceAlphaMode::Opaque && bHasAlpha
					&& VMTData.Parameters.Contains(TEXT("$nocull")))
				{
					AlphaMode = ESourceAlphaMode::Masked;
				}

				// Cache texture info for UV normalization
				FTextureCacheEntry CacheEntry;
				CacheEntry.Size = FIntPoint(TexW, TexH);
				CacheEntry.bHasAlpha = bHasAlpha;
				TextureInfoCache.Add(SourceMaterialPath.ToUpper(), CacheEntry);

				BaseTexture = CreatePersistentTexture(BGRAData, TexW, TexH, BaseTexturePath, false);
			}
		}
	}

	// ---- Load normal map (if present) ----
	UTexture2D* NormalMap = nullptr;
	FString BumpMapPath = VMTData.GetBumpMap();
	if (!BumpMapPath.IsEmpty())
	{
		TArray<uint8> BumpBytes;
		if (FindVTFBytes(BumpMapPath, BumpBytes))
		{
			TArray<uint8> BumpBGRA;
			int32 BumpW, BumpH;
			bool bBumpAlpha;
			if (FVTFReader::DecodeToBGRA(BumpBytes, BumpMapPath, BumpBGRA, BumpW, BumpH, bBumpAlpha))
			{
				NormalMap = CreatePersistentTexture(BumpBGRA, BumpW, BumpH, BumpMapPath, true);
			}
		}
	}

	// ---- Create material ----
	if (BaseTexture)
	{
		UMaterialInstanceConstant* MIC = CreatePersistentMaterial(
			BaseTexture, NormalMap, AlphaMode, SourceMaterialPath, VMTData);

		if (MIC)
		{
			// Register in manifest
			USourceMaterialManifest* Manifest = USourceMaterialManifest::Get();
			if (Manifest)
			{
				FSourceMaterialEntry Entry;
				Entry.SourcePath = SourceMaterialPath;
				Entry.Type = bFoundInVPK ? ESourceMaterialType::Stock : ESourceMaterialType::Imported;
				Entry.TextureAsset = FSoftObjectPath(BaseTexture);
				Entry.MaterialAsset = FSoftObjectPath(MIC);
				if (NormalMap)
				{
					Entry.NormalMapAsset = FSoftObjectPath(NormalMap);
				}
				Entry.VMTShader = VMTData.ShaderName;
				Entry.VMTParams = VMTData.Parameters;
				Entry.bIsInVPK = bFoundInVPK;
				Entry.LastImported = FDateTime::Now();
				Manifest->Register(Entry);
				Manifest->SaveManifest();
			}

			return MIC;
		}
	}

	// ---- Fallback: color placeholder ----
	FLinearColor Color = ColorFromName(SourceMaterialPath);

	FString ColorStr = VMTData.Parameters.FindRef(TEXT("$color"));
	if (ColorStr.IsEmpty()) ColorStr = VMTData.Parameters.FindRef(TEXT("$color2"));

	if (!ColorStr.IsEmpty())
	{
		FString Clean = ColorStr.Replace(TEXT("["), TEXT("")).Replace(TEXT("]"), TEXT(""))
			.Replace(TEXT("{"), TEXT("")).Replace(TEXT("}"), TEXT("")).TrimStartAndEnd();
		TArray<FString> Parts;
		Clean.ParseIntoArrayWS(Parts);
		if (Parts.Num() >= 3)
		{
			float R = FCString::Atof(*Parts[0]);
			float G = FCString::Atof(*Parts[1]);
			float B = FCString::Atof(*Parts[2]);
			if (R > 2.0f || G > 2.0f || B > 2.0f) { R /= 255.0f; G /= 255.0f; B /= 255.0f; }
			Color = FLinearColor(R, G, B);
		}
	}

	FString ShaderLower = VMTData.ShaderName.ToLower();
	if (ShaderLower.Contains(TEXT("water")))
	{
		Color = FLinearColor(0.1f, 0.3f, 0.7f);
	}

	UMaterialInstanceConstant* ColorMat = CreatePersistentColorMaterial(Color, SourceMaterialPath);
	if (ColorMat)
	{
		// Register color placeholder in manifest too
		USourceMaterialManifest* Manifest = USourceMaterialManifest::Get();
		if (Manifest)
		{
			FSourceMaterialEntry Entry;
			Entry.SourcePath = SourceMaterialPath;
			Entry.Type = bFoundInVPK ? ESourceMaterialType::Stock : ESourceMaterialType::Imported;
			Entry.MaterialAsset = FSoftObjectPath(ColorMat);
			Entry.VMTShader = VMTData.ShaderName;
			Entry.VMTParams = VMTData.Parameters;
			Entry.bIsInVPK = bFoundInVPK;
			Entry.LastImported = FDateTime::Now();
			Manifest->Register(Entry);
			Manifest->SaveManifest();
		}
	}
	return ColorMat;
}

UMaterialInterface* FMaterialImporter::CreatePlaceholderMaterial(const FString& SourceMaterialPath)
{
	FString Upper = SourceMaterialPath.ToUpper();

	FLinearColor Color;
	if (Upper.StartsWith(TEXT("TOOLS/")))
	{
		if (Upper.Contains(TEXT("NODRAW")))
			Color = FLinearColor(0.8f, 0.3f, 0.3f);
		else if (Upper.Contains(TEXT("TRIGGER")))
			Color = FLinearColor(0.8f, 0.5f, 0.0f);
		else if (Upper.Contains(TEXT("CLIP")))
			Color = FLinearColor(0.5f, 0.0f, 0.8f);
		else if (Upper.Contains(TEXT("INVISIBLE")))
			Color = FLinearColor(0.3f, 0.3f, 0.8f);
		else if (Upper.Contains(TEXT("SKYBOX")))
			Color = FLinearColor(0.2f, 0.6f, 0.9f);
		else if (Upper.Contains(TEXT("HINT")))
			Color = FLinearColor(0.9f, 0.9f, 0.0f);
		else if (Upper.Contains(TEXT("SKIP")))
			Color = FLinearColor(0.6f, 0.6f, 0.0f);
		else if (Upper.Contains(TEXT("BLOCKLIGHT")))
			Color = FLinearColor(0.1f, 0.1f, 0.1f);
		else if (Upper.Contains(TEXT("BLOCKLOS")))
			Color = FLinearColor(0.5f, 0.2f, 0.0f);
		else if (Upper.Contains(TEXT("BLACK")))
			Color = FLinearColor(0.02f, 0.02f, 0.02f);
		else if (Upper.Contains(TEXT("FOG")))
			Color = FLinearColor(0.6f, 0.6f, 0.7f);
		else
			Color = ColorFromName(SourceMaterialPath);
	}
	else
	{
		Color = ColorFromName(SourceMaterialPath);
	}

	UMaterialInstanceConstant* MIC = CreatePersistentColorMaterial(Color, SourceMaterialPath);

	// Register placeholder in manifest
	if (MIC)
	{
		USourceMaterialManifest* Manifest = USourceMaterialManifest::Get();
		if (Manifest)
		{
			FSourceMaterialEntry Entry;
			Entry.SourcePath = SourceMaterialPath;
			Entry.Type = ESourceMaterialType::Imported;
			Entry.MaterialAsset = FSoftObjectPath(MIC);
			Entry.LastImported = FDateTime::Now();
			Manifest->Register(Entry);
			// Don't save after every placeholder — batch save later
		}
	}

	return MIC;
}

// ===========================================================================
// Persistent Asset Creation
// ===========================================================================

FString FMaterialImporter::SourcePathToAssetPath(const FString& Category, const FString& SourcePath)
{
	FString Clean = SourcePath.ToLower();
	Clean.ReplaceInline(TEXT("\\"), TEXT("/"));
	Clean.RemoveFromStart(TEXT("materials/"));

	// Sanitize: replace any characters not valid in UE asset names
	Clean = Clean.Replace(TEXT(" "), TEXT("_"));

	return FString::Printf(TEXT("/Game/SourceBridge/%s/%s"), *Category, *Clean);
}

bool FMaterialImporter::SaveAsset(UObject* Asset)
{
	if (!Asset) return false;

	UPackage* Package = Asset->GetOutermost();
	if (!Package) return false;

	FString PackageFileName;
	if (!FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		UE_LOG(LogTemp, Error, TEXT("MaterialImporter: Failed to resolve save path for %s"), *Package->GetName());
		return false;
	}

	// Ensure directory exists
	FString Dir = FPaths::GetPath(PackageFileName);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*Dir);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	bool bSaved = UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);

	if (!bSaved)
	{
		UE_LOG(LogTemp, Error, TEXT("MaterialImporter: Failed to save asset to %s"), *PackageFileName);
	}

	return bSaved;
}

UTexture2D* FMaterialImporter::CreatePersistentTexture(const TArray<uint8>& BGRAData, int32 Width, int32 Height,
	const FString& SourceTexturePath, bool bIsNormalMap)
{
	FString AssetPath = SourcePathToAssetPath(TEXT("Textures"), SourceTexturePath);
	FString AssetName = FPaths::GetCleanFilename(AssetPath);

	// Check if already exists
	FString FullObjectPath = AssetPath + TEXT(".") + AssetName;
	UTexture2D* Existing = LoadObject<UTexture2D>(nullptr, *FullObjectPath);
	if (Existing)
	{
		UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: Texture already exists: %s"), *AssetPath);
		return Existing;
	}

	// Create package and texture
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("MaterialImporter: Failed to create package for texture: %s"), *AssetPath);
		return nullptr;
	}

	UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!Texture)
	{
		UE_LOG(LogTemp, Error, TEXT("MaterialImporter: Failed to create texture object: %s"), *AssetName);
		return nullptr;
	}

	// Initialize source data with BGRA8 pixels
	Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, BGRAData.GetData());
	Texture->SRGB = !bIsNormalMap;
	Texture->CompressionSettings = bIsNormalMap ? TC_Normalmap : TC_Default;
	Texture->LODGroup = TEXTUREGROUP_World;
	Texture->Filter = TF_Bilinear;
	Texture->MipGenSettings = TMGS_FromTextureGroup;
	Texture->UpdateResource();

	// Register and save
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Texture);
	SaveAsset(Texture);

	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Created persistent texture %dx%d: %s"), Width, Height, *AssetPath);
	return Texture;
}

UMaterialInstanceConstant* FMaterialImporter::CreatePersistentMaterial(
	UTexture2D* BaseTexture, UTexture2D* NormalMap,
	ESourceAlphaMode AlphaMode, const FString& SourceMaterialPath,
	const FVMTParsedMaterial& VMTData)
{
	FString AssetPath = SourcePathToAssetPath(TEXT("Materials"), SourceMaterialPath);
	FString AssetName = FPaths::GetCleanFilename(AssetPath);

	// Check if already exists
	FString FullObjectPath = AssetPath + TEXT(".") + AssetName;
	UMaterialInstanceConstant* Existing = LoadObject<UMaterialInstanceConstant>(nullptr, *FullObjectPath);
	if (Existing)
	{
		UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: Material already exists: %s"), *AssetPath);
		return Existing;
	}

	// Get parent base material
	UMaterial* Parent = GetOrCreateBaseMaterial(AlphaMode);
	if (!Parent)
	{
		UE_LOG(LogTemp, Error, TEXT("MaterialImporter: Failed to get base material for: %s"), *SourceMaterialPath);
		return nullptr;
	}

	// Create package and material instance
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package) return nullptr;

	UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!MIC) return nullptr;

	MIC->Parent = Parent;

	// Set base texture parameter
	if (BaseTexture)
	{
		FTextureParameterValue& TexParam = MIC->TextureParameterValues.AddDefaulted_GetRef();
		TexParam.ParameterInfo.Name = FName(TEXT("BaseTexture"));
		TexParam.ParameterValue = BaseTexture;
	}

	// Set normal map parameter (if base material supports it)
	// Note: currently our base materials don't have a normal map parameter.
	// This is left as a hook for future enhancement.

	// Tool texture opacity
	if (AlphaMode == ESourceAlphaMode::Translucent
		&& SourceMaterialPath.ToUpper().StartsWith(TEXT("TOOLS/")))
	{
		FScalarParameterValue& ScalarParam = MIC->ScalarParameterValues.AddDefaulted_GetRef();
		ScalarParam.ParameterInfo.Name = FName(TEXT("OpacityScale"));
		ScalarParam.ParameterValue = 0.25f;
	}

	MIC->PreEditChange(nullptr);
	MIC->PostEditChange();

	// Register and save
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(MIC);
	SaveAsset(MIC);

	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Created persistent material: %s (parent=%s)"),
		*AssetPath, *Parent->GetName());
	return MIC;
}

UMaterialInstanceConstant* FMaterialImporter::CreatePersistentColorMaterial(
	const FLinearColor& Color, const FString& SourceMaterialPath)
{
	FString AssetPath = SourcePathToAssetPath(TEXT("Materials"), SourceMaterialPath);
	FString AssetName = FPaths::GetCleanFilename(AssetPath);

	// Check if already exists
	FString FullObjectPath = AssetPath + TEXT(".") + AssetName;
	UMaterialInstanceConstant* Existing = LoadObject<UMaterialInstanceConstant>(nullptr, *FullObjectPath);
	if (Existing) return Existing;

	UMaterial* Parent = GetOrCreateColorBaseMaterial();
	if (!Parent) return nullptr;

	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package) return nullptr;

	UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!MIC) return nullptr;

	MIC->Parent = Parent;

	FVectorParameterValue& ColorParam = MIC->VectorParameterValues.AddDefaulted_GetRef();
	ColorParam.ParameterInfo.Name = FName(TEXT("Color"));
	ColorParam.ParameterValue = Color;

	MIC->PreEditChange(nullptr);
	MIC->PostEditChange();

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(MIC);
	SaveAsset(MIC);

	return MIC;
}

// ===========================================================================
// Persistent Base Materials
// ===========================================================================

UMaterial* FMaterialImporter::GetOrCreateBaseMaterial(ESourceAlphaMode AlphaMode)
{
	UMaterial** CachePtr = nullptr;
	FString MaterialName;

	switch (AlphaMode)
	{
	case ESourceAlphaMode::Opaque:
		CachePtr = &CachedOpaqueMaterial;
		MaterialName = TEXT("M_SourceBridge_Opaque");
		break;
	case ESourceAlphaMode::Masked:
		CachePtr = &CachedMaskedMaterial;
		MaterialName = TEXT("M_SourceBridge_Masked");
		break;
	case ESourceAlphaMode::Translucent:
		CachePtr = &CachedTranslucentMaterial;
		MaterialName = TEXT("M_SourceBridge_Translucent");
		break;
	}

	// Return cached if valid
	if (*CachePtr && (*CachePtr)->IsValidLowLevel())
	{
		return *CachePtr;
	}

	// Try to load existing from disk
	FString AssetPath = FString::Printf(TEXT("/Game/SourceBridge/BaseMaterials/%s"), *MaterialName);
	FString FullObjectPath = AssetPath + TEXT(".") + MaterialName;
	UMaterial* Mat = LoadObject<UMaterial>(nullptr, *FullObjectPath);
	if (Mat)
	{
		*CachePtr = Mat;
		UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Loaded base material from disk: %s"), *MaterialName);
		return Mat;
	}

	// Create new persistent base material
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package) return nullptr;

	Mat = NewObject<UMaterial>(Package, *MaterialName, RF_Public | RF_Standalone);
	if (!Mat) return nullptr;

	// Build expression graph based on alpha mode
	UMaterialExpressionTextureSampleParameter2D* TexParam =
		NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat);
	TexParam->ParameterName = FName(TEXT("BaseTexture"));
	TexParam->SamplerType = SAMPLERTYPE_Color;
	Mat->GetExpressionCollection().AddExpression(TexParam);
	Mat->GetEditorOnlyData()->BaseColor.Connect(0, TexParam);

	if (AlphaMode == ESourceAlphaMode::Masked)
	{
		Mat->BlendMode = BLEND_Masked;
		Mat->TwoSided = true;
		Mat->GetEditorOnlyData()->OpacityMask.Connect(4, TexParam); // Alpha output
	}
	else if (AlphaMode == ESourceAlphaMode::Translucent)
	{
		Mat->BlendMode = BLEND_Translucent;
		Mat->TwoSided = true;

		// OpacityScale parameter
		UMaterialExpressionScalarParameter* OpacityScaleParam =
			NewObject<UMaterialExpressionScalarParameter>(Mat);
		OpacityScaleParam->ParameterName = FName(TEXT("OpacityScale"));
		OpacityScaleParam->DefaultValue = 1.0f;
		Mat->GetExpressionCollection().AddExpression(OpacityScaleParam);

		// Multiply texture alpha by OpacityScale
		UMaterialExpressionMultiply* MultiplyNode =
			NewObject<UMaterialExpressionMultiply>(Mat);
		MultiplyNode->A.Connect(4, TexParam);
		MultiplyNode->B.Connect(0, OpacityScaleParam);
		Mat->GetExpressionCollection().AddExpression(MultiplyNode);

		Mat->GetEditorOnlyData()->Opacity.Connect(0, MultiplyNode);
	}

	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Mat);
	SaveAsset(Mat);

	*CachePtr = Mat;
	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Created persistent base material: %s"), *MaterialName);
	return Mat;
}

UMaterial* FMaterialImporter::GetOrCreateColorBaseMaterial()
{
	if (CachedColorMaterial && CachedColorMaterial->IsValidLowLevel())
	{
		return CachedColorMaterial;
	}

	FString MaterialName = TEXT("M_SourceBridge_Color");
	FString AssetPath = FString::Printf(TEXT("/Game/SourceBridge/BaseMaterials/%s"), *MaterialName);
	FString FullObjectPath = AssetPath + TEXT(".") + MaterialName;

	UMaterial* Mat = LoadObject<UMaterial>(nullptr, *FullObjectPath);
	if (Mat)
	{
		CachedColorMaterial = Mat;
		return Mat;
	}

	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package) return nullptr;

	Mat = NewObject<UMaterial>(Package, *MaterialName, RF_Public | RF_Standalone);
	if (!Mat) return nullptr;

	UMaterialExpressionVectorParameter* ColorParam =
		NewObject<UMaterialExpressionVectorParameter>(Mat);
	ColorParam->ParameterName = FName(TEXT("Color"));
	ColorParam->DefaultValue = FLinearColor(0.5f, 0.5f, 0.5f);
	Mat->GetExpressionCollection().AddExpression(ColorParam);
	Mat->GetEditorOnlyData()->BaseColor.Connect(0, ColorParam);

	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Mat);
	SaveAsset(Mat);

	CachedColorMaterial = Mat;
	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Created persistent color base material"));
	return Mat;
}

// ===========================================================================
// VTF Loading (returns raw bytes)
// ===========================================================================

bool FMaterialImporter::FindVTFBytes(const FString& TexturePath, TArray<uint8>& OutFileData)
{
	if (TexturePath.IsEmpty()) return false;

	TArray<FString> SearchRoots;
	if (!AssetSearchPath.IsEmpty())
	{
		SearchRoots.Add(AssetSearchPath);
	}
	SearchRoots.Append(AdditionalSearchPaths);

	FString VTFRelPath = TEXT("materials") / TexturePath + TEXT(".vtf");

	// 1. Exact path on disk
	for (const FString& Root : SearchRoots)
	{
		FString VTFFullPath = Root / VTFRelPath;
		VTFFullPath = VTFFullPath.Replace(TEXT("\\"), TEXT("/"));

		if (FPaths::FileExists(VTFFullPath))
		{
			if (FFileHelper::LoadFileToArray(OutFileData, *VTFFullPath))
			{
				UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: Found VTF at: %s"), *VTFFullPath);
				return true;
			}
		}
	}

	// 2. VPK archives
	if (FindVTFBytesFromVPK(TexturePath, OutFileData))
	{
		return true;
	}

	// 3. Case-insensitive disk search (slow)
	FString SearchPath = TexturePath + TEXT(".vtf");
	SearchPath = SearchPath.Replace(TEXT("\\"), TEXT("/"));

	for (const FString& Root : SearchRoots)
	{
		FString MaterialsDir = Root / TEXT("materials");
		if (!FPaths::DirectoryExists(MaterialsDir)) continue;

		TArray<FString> AllVTFs;
		IFileManager::Get().FindFilesRecursive(AllVTFs, *MaterialsDir, TEXT("*.vtf"), true, false);

		for (const FString& FoundVTF : AllVTFs)
		{
			FString RelPath = FoundVTF;
			FPaths::MakePathRelativeTo(RelPath, *(MaterialsDir + TEXT("/")));
			RelPath = RelPath.Replace(TEXT("\\"), TEXT("/"));

			if (RelPath.Equals(SearchPath, ESearchCase::IgnoreCase))
			{
				if (FFileHelper::LoadFileToArray(OutFileData, *FoundVTF))
				{
					return true;
				}
			}
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: VTF not found for '%s'"), *TexturePath);
	return false;
}

bool FMaterialImporter::FindVTFBytesFromVPK(const FString& TexturePath, TArray<uint8>& OutData)
{
	if (VPKArchives.Num() == 0) return false;

	FString VPKPath = (TEXT("materials/") + TexturePath + TEXT(".vtf")).ToLower();
	VPKPath = VPKPath.Replace(TEXT("\\"), TEXT("/"));

	for (int32 i = 0; i < VPKArchives.Num(); i++)
	{
		const TSharedPtr<FVPKReader>& VPK = VPKArchives[i];
		if (VPK->Contains(VPKPath))
		{
			if (VPK->ReadFile(VPKPath, OutData))
			{
				UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: Found VTF in VPK[%d]: %s (%d bytes)"),
					i, *VPKPath, OutData.Num());
				return true;
			}
		}
	}

	return false;
}

// ===========================================================================
// VMT Search (unchanged)
// ===========================================================================

FString FMaterialImporter::FindVMTInVPK(const FString& SourceMaterialPath)
{
	if (VPKArchives.Num() == 0)
	{
		return FString();
	}

	FString VPKPath = (TEXT("materials/") + SourceMaterialPath + TEXT(".vmt")).ToLower();
	VPKPath = VPKPath.Replace(TEXT("\\"), TEXT("/"));

	for (int32 i = 0; i < VPKArchives.Num(); i++)
	{
		const TSharedPtr<FVPKReader>& VPK = VPKArchives[i];
		if (VPK->Contains(VPKPath))
		{
			TArray<uint8> Data;
			if (VPK->ReadFile(VPKPath, Data))
			{
				FString Content;
				FFileHelper::BufferToString(Content, Data.GetData(), Data.Num());
				UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: Found VMT in VPK: %s (%d bytes)"),
					*VPKPath, Data.Num());
				return Content;
			}
		}
	}

	return FString();
}

// ===========================================================================
// Utility
// ===========================================================================

void FMaterialImporter::ClearCache()
{
	MaterialCache.Empty();
	TextureInfoCache.Empty();
	// NOTE: Do NOT clear AdditionalSearchPaths, VPKArchives, or base material pointers.
	// Those are session-level configuration. ClearCache() only resets per-import state.
}

FIntPoint FMaterialImporter::GetTextureSize(const FString& SourceMaterialPath)
{
	FString NormalizedPath = SourceMaterialPath.ToUpper();
	if (const FTextureCacheEntry* Found = TextureInfoCache.Find(NormalizedPath))
	{
		return Found->Size;
	}
	return FIntPoint(512, 512);
}

TArray<FString> FMaterialImporter::GetStockMaterialPaths()
{
	EnsureVPKArchivesLoaded();

	TSet<FString> UniqueMatPaths;

	for (const TSharedPtr<FVPKReader>& VPK : VPKArchives)
	{
		TArray<FString> VMTPaths = VPK->GetAllPaths(TEXT("vmt"));
		for (const FString& VMTPath : VMTPaths)
		{
			// VPK paths look like "materials/concrete/concretefloor001a.vmt"
			// Strip "materials/" prefix and ".vmt" suffix to get Source path
			FString MatPath = VMTPath;
			if (MatPath.StartsWith(TEXT("materials/")))
			{
				MatPath = MatPath.Mid(10);
			}
			if (MatPath.EndsWith(TEXT(".vmt")))
			{
				MatPath = MatPath.LeftChop(4);
			}
			UniqueMatPaths.Add(MatPath);
		}
	}

	TArray<FString> Result = UniqueMatPaths.Array();
	Result.Sort();
	return Result;
}

TArray<FString> FMaterialImporter::GetStockMaterialDirectories()
{
	EnsureVPKArchivesLoaded();

	TSet<FString> UniqueDirs;

	for (const TSharedPtr<FVPKReader>& VPK : VPKArchives)
	{
		TArray<FString> Dirs = VPK->GetAllDirectories(TEXT("vmt"));
		for (const FString& Dir : Dirs)
		{
			// Strip "materials/" prefix
			FString CleanDir = Dir;
			if (CleanDir.StartsWith(TEXT("materials/")))
			{
				CleanDir = CleanDir.Mid(10);
			}
			if (!CleanDir.IsEmpty())
			{
				UniqueDirs.Add(CleanDir);
			}
		}
	}

	TArray<FString> Result = UniqueDirs.Array();
	Result.Sort();
	return Result;
}

FLinearColor FMaterialImporter::ColorFromName(const FString& Name)
{
	uint32 Hash = GetTypeHash(Name.ToUpper());
	float H = (float)(Hash % 360) / 360.0f;
	float S = 0.4f + (float)((Hash >> 8) % 40) / 100.0f;
	float V = 0.5f + (float)((Hash >> 16) % 30) / 100.0f;

	return FLinearColor::MakeFromHSV8(
		(uint8)(H * 255),
		(uint8)(S * 255),
		(uint8)(V * 255));
}
