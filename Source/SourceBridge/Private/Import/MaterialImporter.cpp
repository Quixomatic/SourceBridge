#include "Import/MaterialImporter.h"
#include "Import/VTFReader.h"
#include "Import/VPKReader.h"
#include "Compile/CompilePipeline.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "UObject/ConstructorHelpers.h"
#include "HAL/FileManager.h"

TMap<FString, UMaterialInterface*> FMaterialImporter::MaterialCache;
TMap<FString, FString> FMaterialImporter::ReverseToolMappings;
TMap<FString, FMaterialImporter::FTextureCacheEntry> FMaterialImporter::TextureInfoCache;
FString FMaterialImporter::AssetSearchPath;
TArray<FString> FMaterialImporter::AdditionalSearchPaths;
TArray<TSharedPtr<FVPKReader>> FMaterialImporter::VPKArchives;
UMaterial* FMaterialImporter::TextureBaseMaterial = nullptr;
UMaterial* FMaterialImporter::MaskedBaseMaterial = nullptr;
UMaterial* FMaterialImporter::TranslucentBaseMaterial = nullptr;
UMaterial* FMaterialImporter::ColorBaseMaterial = nullptr;

// ---- VMT Parsing ----

FVMTParsedMaterial FMaterialImporter::ParseVMT(const FString& VMTContent)
{
	FVMTParsedMaterial Result;

	// VMT format:
	//   ShaderName
	//   {
	//       "$key" "value"
	//   }
	// Simplified parser - handles the common single-block case

	const TCHAR* Ptr = *VMTContent;

	// Skip whitespace and comments
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
				// Line comment
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
		Ptr++; // skip opening quote
		FString Value;
		while (*Ptr && *Ptr != '"')
		{
			Value += *Ptr;
			Ptr++;
		}
		if (*Ptr == '"') Ptr++; // skip closing quote
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

	// Skip whitespace but NOT newlines (for reading values on the same line as their key)
	auto SkipInlineWS = [&]()
	{
		while (*Ptr == ' ' || *Ptr == '\t')
		{
			Ptr++;
		}
	};

	// Read rest of line as a value (for unquoted values that may contain spaces/backslashes)
	auto ReadRestOfLine = [&]() -> FString
	{
		FString Value;
		while (*Ptr && *Ptr != '\r' && *Ptr != '\n' && *Ptr != '{' && *Ptr != '}')
		{
			if (*Ptr == '/' && *(Ptr + 1) == '/')
			{
				break; // Stop at line comment
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
				// Nested block (like $proxies) - skip it
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
				// Key-value pair at top level
				// Handles: "$key" "value", "$key" value, $key "value", $key value
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
					// Unquoted value - read to end of line
					Value = ReadRestOfLine();
				}

				if (!Key.IsEmpty())
				{
					Result.Parameters.Add(Key.ToLower(), Value);
				}
			}
			else if (*Ptr)
			{
				// Unknown token at non-top depth, skip
				ReadToken();
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

// ---- Asset Search Path ----

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

	// 1. Game's root directory (contains materials/ directly)
	//    e.g., C:/Steam/steamapps/common/Counter-Strike Source/cstrike/
	if (FPaths::DirectoryExists(GameDir / TEXT("materials")))
	{
		AdditionalSearchPaths.Add(GameDir);
		UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Added game materials path: %s"), *GameDir);
	}

	// 2. Game's custom/ folder - each subfolder can have its own materials/
	//    e.g., cstrike/custom/my_content/materials/
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

	// 3. Download folder (community server content)
	//    e.g., cstrike/download/materials/
	FString DownloadDir = GameDir / TEXT("download");
	if (FPaths::DirectoryExists(DownloadDir / TEXT("materials")))
	{
		AdditionalSearchPaths.Add(DownloadDir);
		UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Added download materials path: %s"), *DownloadDir);
	}

	// 4. Open VPK archives for stock game materials
	//    Source engine search order (from gameinfo.txt):
	//      - Game dir VPKs (cstrike/cstrike_pak)
	//      - HL2 base VPKs (hl2/hl2_textures, hl2/hl2_misc) - tool textures live here!
	//      - Platform VPKs (platform/platform_misc)
	VPKArchives.Empty();

	// Determine the engine root (parent of game dir, e.g., "Counter-Strike Source/")
	FString EngineRoot = FPaths::GetPath(GameDir); // e.g., .../Counter-Strike Source

	// Collect VPK search directories in priority order
	TArray<FString> VPKSearchDirs;
	VPKSearchDirs.Add(GameDir);                          // cstrike/
	VPKSearchDirs.Add(EngineRoot / TEXT("hl2"));         // hl2/ (base content, tool textures)
	VPKSearchDirs.Add(EngineRoot / TEXT("platform"));    // platform/

	// Also add hl2 and platform as loose file search paths
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
			// Skip sound VPKs - they don't contain materials
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

	UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: %d VPK archives ready for tool texture lookup"), VPKArchives.Num());
}

// ---- Material Resolution ----

void FMaterialImporter::EnsureReverseToolMappings()
{
	if (ReverseToolMappings.Num() > 0) return;

	// Build reverse mapping: Source path → UE tool material name
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

UMaterialInterface* FMaterialImporter::ResolveSourceMaterial(const FString& SourceMaterialPath)
{
	if (SourceMaterialPath.IsEmpty()) return nullptr;

	FString NormalizedPath = SourceMaterialPath.ToUpper();

	// Check cache first
	if (UMaterialInterface** Found = MaterialCache.Find(NormalizedPath))
	{
		return *Found;
	}

	UMaterialInterface* Material = nullptr;

	UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: Resolving '%s' (searchPath=%s, additionalPaths=%d, VPKs=%d)"),
		*SourceMaterialPath, *AssetSearchPath, AdditionalSearchPaths.Num(), VPKArchives.Num());

	// 1. Try to create from extracted VMT file or VPK archives
	if (!AssetSearchPath.IsEmpty() || AdditionalSearchPaths.Num() > 0 || VPKArchives.Num() > 0)
	{
		Material = CreateMaterialFromVMT(SourceMaterialPath);
		if (Material)
		{
			UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: '%s' resolved via VMT/VPK"), *SourceMaterialPath);
		}
	}

	// 2. Try to find existing UE material in asset registry
	if (!Material)
	{
		Material = FindExistingMaterial(SourceMaterialPath);
		if (Material)
		{
			UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: '%s' resolved via asset registry"), *SourceMaterialPath);
		}
	}

	// 3. Create placeholder if not found
	if (!Material)
	{
		UE_LOG(LogTemp, Warning, TEXT("MaterialImporter: '%s' not found in VMT/VPK/registry, creating placeholder"),
			*SourceMaterialPath);
		Material = CreatePlaceholderMaterial(SourceMaterialPath);
	}

	if (Material)
	{
		MaterialCache.Add(NormalizedPath, Material);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("MaterialImporter: FAILED to resolve material '%s' (returned nullptr)"),
			*SourceMaterialPath);
	}

	return Material;
}

UMaterialInterface* FMaterialImporter::FindExistingMaterial(const FString& SourceMaterialPath)
{
	EnsureReverseToolMappings();

	FString Upper = SourceMaterialPath.ToUpper();

	// 1. Check reverse tool mappings
	if (const FString* UEName = ReverseToolMappings.Find(Upper))
	{
		// Search for this material in the asset registry
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

	// 2. Try converting Source path to UE material name
	FString Cleaned = SourceMaterialPath;
	Cleaned = Cleaned.Replace(TEXT("/"), TEXT("__"));
	FString FileName = FPaths::GetCleanFilename(SourceMaterialPath);

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	AssetRegistry.Get().GetAssetsByClass(UMaterialInterface::StaticClass()->GetClassPathName(), Assets);

	for (const FAssetData& Asset : Assets)
	{
		FString AssetName = Asset.AssetName.ToString();
		if (AssetName.Equals(Cleaned, ESearchCase::IgnoreCase) ||
			AssetName.Equals(TEXT("M_") + Cleaned, ESearchCase::IgnoreCase) ||
			AssetName.Equals(TEXT("MI_") + Cleaned, ESearchCase::IgnoreCase) ||
			AssetName.Equals(FileName, ESearchCase::IgnoreCase) ||
			AssetName.Equals(TEXT("M_") + FileName, ESearchCase::IgnoreCase))
		{
			return Cast<UMaterialInterface>(Asset.GetAsset());
		}
	}

	return nullptr;
}

UMaterialInterface* FMaterialImporter::CreateMaterialFromVMT(const FString& SourceMaterialPath)
{
	// Build the list of all directories to search: extracted first, then game dirs
	TArray<FString> SearchRoots;
	if (!AssetSearchPath.IsEmpty())
	{
		SearchRoots.Add(AssetSearchPath);
	}
	SearchRoots.Append(AdditionalSearchPaths);

	// Search for VMT file across all disk paths
	FString VMTFullPath;
	FString VMTRelPath = TEXT("materials") / SourceMaterialPath + TEXT(".vmt");

	for (const FString& Root : SearchRoots)
	{
		// Direct path check
		FString CandidatePath = Root / VMTRelPath;
		CandidatePath = CandidatePath.Replace(TEXT("\\"), TEXT("/"));

		if (FPaths::FileExists(CandidatePath))
		{
			VMTFullPath = CandidatePath;
			break;
		}

		// Case-insensitive fallback
		FString MaterialsDir = Root / TEXT("materials");
		if (!FPaths::DirectoryExists(MaterialsDir)) continue;

		FString SearchPath = SourceMaterialPath + TEXT(".vmt");
		SearchPath = SearchPath.Replace(TEXT("\\"), TEXT("/"));

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

	// If not found on disk, try VPK archives
	FVMTParsedMaterial VMTData;
	if (VMTFullPath.IsEmpty())
	{
		FString VMTContent = FindVMTInVPK(SourceMaterialPath);
		if (VMTContent.IsEmpty())
		{
			return nullptr;
		}
		VMTData = ParseVMT(VMTContent);
	}
	else
	{
		VMTData = ParseVMTFile(VMTFullPath);
	}

	if (VMTData.ShaderName.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("MaterialImporter: Failed to parse VMT for: %s"), *SourceMaterialPath);
		return nullptr;
	}

	FString VMTSource = VMTFullPath.IsEmpty() ? TEXT("VPK") : VMTFullPath;
	UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: Parsed VMT '%s' (from %s) - shader: %s, basetexture: %s, params=%d"),
		*SourceMaterialPath, *VMTSource, *VMTData.ShaderName, *VMTData.GetBaseTexture(), VMTData.Parameters.Num());

	// Determine alpha mode from VMT shader parameters
	// $translucent = smooth partial opacity (BLEND_Translucent)
	// $alphatest   = binary alpha test (BLEND_Masked)
	// $nocull alone doesn't imply alpha - just two-sided rendering
	ESourceAlphaMode AlphaMode = ESourceAlphaMode::Opaque;
	if (VMTData.IsTranslucent())
	{
		AlphaMode = ESourceAlphaMode::Translucent;
	}
	else if (VMTData.Parameters.Contains(TEXT("$alphatest")))
	{
		AlphaMode = ESourceAlphaMode::Masked;
	}

	// Try to load the $basetexture as a VTF file
	FString BaseTexturePath = VMTData.GetBaseTexture();
	if (!BaseTexturePath.IsEmpty())
	{
		UTexture2D* Texture = FindAndLoadVTF(BaseTexturePath);
		if (Texture)
		{
			int32 TexW = Texture->GetSizeX();
			int32 TexH = Texture->GetSizeY();

			// Only use texture format for alpha detection if VMT didn't already specify
			// (many DXT5 textures use alpha for specular/gloss, not transparency)
			if (AlphaMode == ESourceAlphaMode::Opaque)
			{
				EPixelFormat PixFmt = Texture->GetPixelFormat();
				bool bTextureHasAlpha = (PixFmt == PF_DXT5 || PixFmt == PF_DXT3
					|| PixFmt == PF_B8G8R8A8 || PixFmt == PF_R8G8B8A8);
				if (bTextureHasAlpha && VMTData.Parameters.Contains(TEXT("$nocull")))
				{
					// $nocull + alpha-capable format is a strong hint for masked rendering
					// (e.g., foliage, fences)
					AlphaMode = ESourceAlphaMode::Masked;
				}
			}

			const TCHAR* AlphaModeStr = AlphaMode == ESourceAlphaMode::Translucent ? TEXT("translucent")
				: AlphaMode == ESourceAlphaMode::Masked ? TEXT("masked") : TEXT("opaque");
			UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: Loaded VTF texture '%s' (%dx%d, mode=%s)"),
				*BaseTexturePath, TexW, TexH, AlphaModeStr);

			// Cache texture info for UV normalization during import
			FTextureCacheEntry Entry;
			Entry.Size = FIntPoint(TexW, TexH);
			Entry.bHasAlpha = (AlphaMode != ESourceAlphaMode::Opaque);
			TextureInfoCache.Add(SourceMaterialPath.ToUpper(), Entry);

			return CreateTexturedMID(Texture, SourceMaterialPath, AlphaMode);
		}
	}

	// Fallback: create a colored placeholder
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

	return CreateColorMID(Color, SourceMaterialPath);
}

UMaterialInterface* FMaterialImporter::CreatePlaceholderMaterial(const FString& SourceMaterialPath)
{
	FString Upper = SourceMaterialPath.ToUpper();

	// Give tool textures distinctive colors so they're visible during editing
	if (Upper.StartsWith(TEXT("TOOLS/")))
	{
		FLinearColor Color;
		if (Upper.Contains(TEXT("NODRAW")))
			Color = FLinearColor(0.8f, 0.3f, 0.3f);  // Red-ish
		else if (Upper.Contains(TEXT("TRIGGER")))
			Color = FLinearColor(0.8f, 0.5f, 0.0f);   // Orange
		else if (Upper.Contains(TEXT("CLIP")))
			Color = FLinearColor(0.5f, 0.0f, 0.8f);   // Purple
		else if (Upper.Contains(TEXT("INVISIBLE")))
			Color = FLinearColor(0.3f, 0.3f, 0.8f);   // Blue-ish
		else if (Upper.Contains(TEXT("SKYBOX")))
			Color = FLinearColor(0.2f, 0.6f, 0.9f);   // Sky blue
		else if (Upper.Contains(TEXT("HINT")))
			Color = FLinearColor(0.9f, 0.9f, 0.0f);   // Yellow
		else if (Upper.Contains(TEXT("SKIP")))
			Color = FLinearColor(0.6f, 0.6f, 0.0f);   // Dark yellow
		else if (Upper.Contains(TEXT("BLOCKLIGHT")))
			Color = FLinearColor(0.1f, 0.1f, 0.1f);   // Near black
		else if (Upper.Contains(TEXT("BLOCKLOS")))
			Color = FLinearColor(0.5f, 0.2f, 0.0f);   // Brown
		else if (Upper.Contains(TEXT("BLACK")))
			Color = FLinearColor(0.02f, 0.02f, 0.02f); // Black
		else if (Upper.Contains(TEXT("FOG")))
			Color = FLinearColor(0.6f, 0.6f, 0.7f);   // Gray-blue
		else
			Color = ColorFromName(SourceMaterialPath);
		return CreateColorMID(Color, SourceMaterialPath);
	}

	FLinearColor Color = ColorFromName(SourceMaterialPath);
	return CreateColorMID(Color, SourceMaterialPath);
}

UTexture2D* FMaterialImporter::FindAndLoadVTF(const FString& TexturePath)
{
	if (TexturePath.IsEmpty()) return nullptr;

	// Build the list of all directories to search: extracted first, then game dirs
	TArray<FString> SearchRoots;
	if (!AssetSearchPath.IsEmpty())
	{
		SearchRoots.Add(AssetSearchPath);
	}
	SearchRoots.Append(AdditionalSearchPaths);

	FString VTFRelPath = TEXT("materials") / TexturePath + TEXT(".vtf");

	for (const FString& Root : SearchRoots)
	{
		// Direct path check first
		FString VTFFullPath = Root / VTFRelPath;
		VTFFullPath = VTFFullPath.Replace(TEXT("\\"), TEXT("/"));

		if (FPaths::FileExists(VTFFullPath))
		{
			UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: Found VTF at: %s"), *VTFFullPath);
			return FVTFReader::LoadVTF(VTFFullPath);
		}

		// Case-insensitive fallback search
		FString MaterialsDir = Root / TEXT("materials");
		if (!FPaths::DirectoryExists(MaterialsDir)) continue;

		FString SearchPath = TexturePath + TEXT(".vtf");
		SearchPath = SearchPath.Replace(TEXT("\\"), TEXT("/"));

		TArray<FString> AllVTFs;
		IFileManager::Get().FindFilesRecursive(AllVTFs, *MaterialsDir, TEXT("*.vtf"), true, false);

		for (const FString& FoundVTF : AllVTFs)
		{
			FString RelPath = FoundVTF;
			FPaths::MakePathRelativeTo(RelPath, *(MaterialsDir + TEXT("/")));
			RelPath = RelPath.Replace(TEXT("\\"), TEXT("/"));

			if (RelPath.Equals(SearchPath, ESearchCase::IgnoreCase))
			{
				UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: Found VTF (case-insensitive) at: %s"), *FoundVTF);
				return FVTFReader::LoadVTF(FoundVTF);
			}
		}
	}

	// Fallback: try VPK archives
	UTexture2D* VPKTexture = FindAndLoadVTFFromVPK(TexturePath);
	if (VPKTexture)
	{
		return VPKTexture;
	}

	UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: VTF not found for '%s' (searched %d paths + %d VPKs)"),
		*TexturePath, SearchRoots.Num(), VPKArchives.Num());
	return nullptr;
}

UMaterial* FMaterialImporter::GetOrCreateTextureBaseMaterial()
{
	if (TextureBaseMaterial && TextureBaseMaterial->IsValidLowLevel())
	{
		return TextureBaseMaterial;
	}

	TextureBaseMaterial = NewObject<UMaterial>(GetTransientPackage(),
		FName(TEXT("M_SourceBridge_TextureBase")), RF_Transient);

	UMaterialExpressionTextureSampleParameter2D* TexParam =
		NewObject<UMaterialExpressionTextureSampleParameter2D>(TextureBaseMaterial);
	TexParam->ParameterName = FName(TEXT("BaseTexture"));
	TexParam->SamplerType = SAMPLERTYPE_Color;
	TextureBaseMaterial->GetExpressionCollection().AddExpression(TexParam);
	TextureBaseMaterial->GetEditorOnlyData()->BaseColor.Connect(0, TexParam);

	TextureBaseMaterial->PreEditChange(nullptr);
	TextureBaseMaterial->PostEditChange();

	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Created shared texture base material"));
	return TextureBaseMaterial;
}

UMaterial* FMaterialImporter::GetOrCreateMaskedBaseMaterial()
{
	if (MaskedBaseMaterial && MaskedBaseMaterial->IsValidLowLevel())
	{
		return MaskedBaseMaterial;
	}

	MaskedBaseMaterial = NewObject<UMaterial>(GetTransientPackage(),
		FName(TEXT("M_SourceBridge_MaskedBase")), RF_Transient);

	// Set blend mode to Masked for alpha-tested materials
	MaskedBaseMaterial->BlendMode = BLEND_Masked;
	MaskedBaseMaterial->TwoSided = true;

	UMaterialExpressionTextureSampleParameter2D* TexParam =
		NewObject<UMaterialExpressionTextureSampleParameter2D>(MaskedBaseMaterial);
	TexParam->ParameterName = FName(TEXT("BaseTexture"));
	TexParam->SamplerType = SAMPLERTYPE_Color;
	MaskedBaseMaterial->GetExpressionCollection().AddExpression(TexParam);

	// Connect RGB to BaseColor, Alpha to OpacityMask
	MaskedBaseMaterial->GetEditorOnlyData()->BaseColor.Connect(0, TexParam);
	MaskedBaseMaterial->GetEditorOnlyData()->OpacityMask.Connect(4, TexParam); // Output 4 = Alpha

	MaskedBaseMaterial->PreEditChange(nullptr);
	MaskedBaseMaterial->PostEditChange();

	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Created shared masked base material"));
	return MaskedBaseMaterial;
}

UMaterial* FMaterialImporter::GetOrCreateTranslucentBaseMaterial()
{
	if (TranslucentBaseMaterial && TranslucentBaseMaterial->IsValidLowLevel())
	{
		return TranslucentBaseMaterial;
	}

	TranslucentBaseMaterial = NewObject<UMaterial>(GetTransientPackage(),
		FName(TEXT("M_SourceBridge_TranslucentBase")), RF_Transient);

	// Set blend mode to Translucent for smooth partial opacity
	TranslucentBaseMaterial->BlendMode = BLEND_Translucent;
	TranslucentBaseMaterial->TwoSided = true;

	UMaterialExpressionTextureSampleParameter2D* TexParam =
		NewObject<UMaterialExpressionTextureSampleParameter2D>(TranslucentBaseMaterial);
	TexParam->ParameterName = FName(TEXT("BaseTexture"));
	TexParam->SamplerType = SAMPLERTYPE_Color;
	TranslucentBaseMaterial->GetExpressionCollection().AddExpression(TexParam);

	// OpacityScale parameter (default 1.0, can be reduced for tool textures)
	UMaterialExpressionScalarParameter* OpacityScaleParam =
		NewObject<UMaterialExpressionScalarParameter>(TranslucentBaseMaterial);
	OpacityScaleParam->ParameterName = FName(TEXT("OpacityScale"));
	OpacityScaleParam->DefaultValue = 1.0f;
	TranslucentBaseMaterial->GetExpressionCollection().AddExpression(OpacityScaleParam);

	// Multiply texture alpha by OpacityScale → Opacity
	UMaterialExpressionMultiply* MultiplyNode =
		NewObject<UMaterialExpressionMultiply>(TranslucentBaseMaterial);
	MultiplyNode->A.Connect(4, TexParam);        // Texture Alpha
	MultiplyNode->B.Connect(0, OpacityScaleParam); // OpacityScale scalar
	TranslucentBaseMaterial->GetExpressionCollection().AddExpression(MultiplyNode);

	// Connect RGB to BaseColor, multiplied alpha to Opacity
	TranslucentBaseMaterial->GetEditorOnlyData()->BaseColor.Connect(0, TexParam);
	TranslucentBaseMaterial->GetEditorOnlyData()->Opacity.Connect(0, MultiplyNode);

	TranslucentBaseMaterial->PreEditChange(nullptr);
	TranslucentBaseMaterial->PostEditChange();

	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Created shared translucent base material (with OpacityScale)"));
	return TranslucentBaseMaterial;
}

UMaterial* FMaterialImporter::GetOrCreateColorBaseMaterial()
{
	if (ColorBaseMaterial && ColorBaseMaterial->IsValidLowLevel())
	{
		return ColorBaseMaterial;
	}

	ColorBaseMaterial = NewObject<UMaterial>(GetTransientPackage(),
		FName(TEXT("M_SourceBridge_ColorBase")), RF_Transient);

	UMaterialExpressionVectorParameter* ColorParam =
		NewObject<UMaterialExpressionVectorParameter>(ColorBaseMaterial);
	ColorParam->ParameterName = FName(TEXT("Color"));
	ColorParam->DefaultValue = FLinearColor(0.5f, 0.5f, 0.5f);
	ColorBaseMaterial->GetExpressionCollection().AddExpression(ColorParam);
	ColorBaseMaterial->GetEditorOnlyData()->BaseColor.Connect(0, ColorParam);

	ColorBaseMaterial->PreEditChange(nullptr);
	ColorBaseMaterial->PostEditChange();

	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Created shared color base material"));
	return ColorBaseMaterial;
}

UMaterialInstanceDynamic* FMaterialImporter::CreateTexturedMID(UTexture2D* Texture, const FString& SourceMaterialPath, ESourceAlphaMode AlphaMode)
{
	UMaterial* BaseMat = nullptr;
	FString Prefix;
	switch (AlphaMode)
	{
	case ESourceAlphaMode::Translucent:
		BaseMat = GetOrCreateTranslucentBaseMaterial();
		Prefix = TEXT("MID_Translucent_");
		break;
	case ESourceAlphaMode::Masked:
		BaseMat = GetOrCreateMaskedBaseMaterial();
		Prefix = TEXT("MID_Masked_");
		break;
	default:
		BaseMat = GetOrCreateTextureBaseMaterial();
		Prefix = TEXT("MID_Tex_");
		break;
	}
	if (!BaseMat) return nullptr;

	FString SafeName = SourceMaterialPath.Replace(TEXT("/"), TEXT("_")).Replace(TEXT("\\"), TEXT("_"));
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, GetTransientPackage(),
		FName(*FString::Printf(TEXT("%s%s"), *Prefix, *SafeName)));

	if (MID)
	{
		MID->SetTextureParameterValue(FName(TEXT("BaseTexture")), Texture);

		// Tool textures should be much more transparent so you can see through them
		// (Hammer renders them as translucent overlays, not solid surfaces)
		if (AlphaMode == ESourceAlphaMode::Translucent
			&& SourceMaterialPath.ToUpper().StartsWith(TEXT("TOOLS/")))
		{
			MID->SetScalarParameterValue(FName(TEXT("OpacityScale")), 0.25f);
		}
	}

	return MID;
}

UMaterialInstanceDynamic* FMaterialImporter::CreateColorMID(const FLinearColor& Color, const FString& SourceMaterialPath)
{
	UMaterial* BaseMat = GetOrCreateColorBaseMaterial();
	if (!BaseMat) return nullptr;

	FString SafeName = SourceMaterialPath.Replace(TEXT("/"), TEXT("_")).Replace(TEXT("\\"), TEXT("_"));
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, GetTransientPackage(),
		FName(*FString::Printf(TEXT("MID_Color_%s"), *SafeName)));

	if (MID)
	{
		MID->SetVectorParameterValue(FName(TEXT("Color")), Color);
	}

	return MID;
}

FString FMaterialImporter::FindVMTInVPK(const FString& SourceMaterialPath)
{
	if (VPKArchives.Num() == 0)
	{
		return FString();
	}

	// VPK paths are lowercase with forward slashes, no leading slash
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
				// Convert raw bytes to FString (VMT files are ASCII/UTF-8)
				FString Content;
				FFileHelper::BufferToString(Content, Data.GetData(), Data.Num());
				UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: Found VMT in VPK: %s (%d bytes)"),
					*VPKPath, Data.Num());
				return Content;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("MaterialImporter: VPK Contains '%s' but ReadFile failed!"), *VPKPath);
			}
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: VMT not found in any VPK: %s"), *VPKPath);
	return FString();
}

UTexture2D* FMaterialImporter::FindAndLoadVTFFromVPK(const FString& TexturePath)
{
	if (VPKArchives.Num() == 0) return nullptr;

	// VPK paths are lowercase with forward slashes
	FString VPKPath = (TEXT("materials/") + TexturePath + TEXT(".vtf")).ToLower();
	VPKPath = VPKPath.Replace(TEXT("\\"), TEXT("/"));

	for (int32 i = 0; i < VPKArchives.Num(); i++)
	{
		const TSharedPtr<FVPKReader>& VPK = VPKArchives[i];
		if (VPK->Contains(VPKPath))
		{
			TArray<uint8> Data;
			if (VPK->ReadFile(VPKPath, Data))
			{
				UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: Found VTF in VPK[%d]: %s (%d bytes)"),
					i, *VPKPath, Data.Num());
				return FVTFReader::LoadVTFFromMemory(Data, VPKPath);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("MaterialImporter: VPK Contains VTF '%s' but ReadFile failed!"), *VPKPath);
			}
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: VTF not found in any VPK: %s"), *VPKPath);
	return nullptr;
}

void FMaterialImporter::ClearCache()
{
	MaterialCache.Empty();
	TextureInfoCache.Empty();
	// NOTE: Do NOT clear AdditionalSearchPaths or VPKArchives here.
	// Those are session-level configuration set by SetupGameSearchPaths(),
	// which is expensive (opens/parses VPK directory files). ClearCache()
	// is called per-import (e.g., by VMFImporter::ImportBlocks) and should
	// only reset transient per-import state like resolved materials.
	TextureBaseMaterial = nullptr;
	MaskedBaseMaterial = nullptr;
	TranslucentBaseMaterial = nullptr;
	ColorBaseMaterial = nullptr;
}

FIntPoint FMaterialImporter::GetTextureSize(const FString& SourceMaterialPath)
{
	FString NormalizedPath = SourceMaterialPath.ToUpper();
	if (const FTextureCacheEntry* Found = TextureInfoCache.Find(NormalizedPath))
	{
		return Found->Size;
	}
	return FIntPoint(512, 512); // Default assumption
}

FLinearColor FMaterialImporter::ColorFromName(const FString& Name)
{
	// Generate a deterministic but visually distinct color from the material name
	uint32 Hash = GetTypeHash(Name.ToUpper());
	float H = (float)(Hash % 360) / 360.0f;
	float S = 0.4f + (float)((Hash >> 8) % 40) / 100.0f;    // 0.4 - 0.8
	float V = 0.5f + (float)((Hash >> 16) % 30) / 100.0f;   // 0.5 - 0.8

	return FLinearColor::MakeFromHSV8(
		(uint8)(H * 255),
		(uint8)(S * 255),
		(uint8)(V * 255));
}
