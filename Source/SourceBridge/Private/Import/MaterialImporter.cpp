#include "Import/MaterialImporter.h"
#include "Import/VTFReader.h"
#include "Compile/CompilePipeline.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "UObject/ConstructorHelpers.h"
#include "HAL/FileManager.h"

TMap<FString, UMaterialInterface*> FMaterialImporter::MaterialCache;
TMap<FString, FString> FMaterialImporter::ReverseToolMappings;
FString FMaterialImporter::AssetSearchPath;
TArray<FString> FMaterialImporter::AdditionalSearchPaths;
UMaterial* FMaterialImporter::TextureBaseMaterial = nullptr;
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
			*Ptr != '\n' && *Ptr != '{' && *Ptr != '}')
		{
			Token += *Ptr;
			Ptr++;
		}
		return Token;
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
			else if (*Ptr == '"' && Depth == 1)
			{
				// Key-value pair at top level
				FString Key = ReadQuoted();
				SkipWS();
				if (*Ptr == '"')
				{
					FString Value = ReadQuoted();
					if (!Key.IsEmpty())
					{
						// Normalize key to lowercase
						Result.Parameters.Add(Key.ToLower(), Value);
					}
				}
			}
			else if (*Ptr)
			{
				// Unquoted token (skip)
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

	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: %d additional search paths configured"), AdditionalSearchPaths.Num());
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

	// 1. Try to create from extracted VMT file
	if (!AssetSearchPath.IsEmpty())
	{
		Material = CreateMaterialFromVMT(SourceMaterialPath);
	}

	// 2. Try to find existing UE material in asset registry
	if (!Material)
	{
		Material = FindExistingMaterial(SourceMaterialPath);
	}

	// 3. Create placeholder if not found
	if (!Material)
	{
		Material = CreatePlaceholderMaterial(SourceMaterialPath);
	}

	if (Material)
	{
		MaterialCache.Add(NormalizedPath, Material);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("MaterialImporter: Failed to resolve material '%s'"), *SourceMaterialPath);
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

	if (SearchRoots.Num() == 0) return nullptr;

	// Search for VMT file across all paths
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

	if (VMTFullPath.IsEmpty())
	{
		return nullptr;
	}

	// Parse the VMT
	FVMTParsedMaterial VMTData = ParseVMTFile(VMTFullPath);
	if (VMTData.ShaderName.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("MaterialImporter: Failed to parse VMT: %s"), *VMTFullPath);
		return nullptr;
	}

	UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Parsed VMT '%s' - shader: %s, basetexture: %s"),
		*SourceMaterialPath, *VMTData.ShaderName, *VMTData.GetBaseTexture());

	// Try to load the $basetexture as a VTF file
	FString BaseTexturePath = VMTData.GetBaseTexture();
	if (!BaseTexturePath.IsEmpty())
	{
		UTexture2D* Texture = FindAndLoadVTF(BaseTexturePath);
		if (Texture)
		{
			UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Loaded VTF texture '%s' (%dx%d)"),
				*BaseTexturePath, Texture->GetSizeX(), Texture->GetSizeY());
			return CreateTexturedMID(Texture, SourceMaterialPath);
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
	// Skip creating placeholders for tool textures (they're invisible in Source)
	FString Upper = SourceMaterialPath.ToUpper();
	if (Upper.StartsWith(TEXT("TOOLS/")))
	{
		return nullptr;
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
			UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Found VTF at: %s"), *VTFFullPath);
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
				UE_LOG(LogTemp, Log, TEXT("MaterialImporter: Found VTF (case-insensitive) at: %s"), *FoundVTF);
				return FVTFReader::LoadVTF(FoundVTF);
			}
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("MaterialImporter: VTF not found for '%s' (searched %d paths)"),
		*TexturePath, SearchRoots.Num());
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

UMaterialInstanceDynamic* FMaterialImporter::CreateTexturedMID(UTexture2D* Texture, const FString& SourceMaterialPath)
{
	UMaterial* BaseMat = GetOrCreateTextureBaseMaterial();
	if (!BaseMat) return nullptr;

	FString SafeName = SourceMaterialPath.Replace(TEXT("/"), TEXT("_")).Replace(TEXT("\\"), TEXT("_"));
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, GetTransientPackage(),
		FName(*FString::Printf(TEXT("MID_Tex_%s"), *SafeName)));

	if (MID)
	{
		MID->SetTextureParameterValue(FName(TEXT("BaseTexture")), Texture);
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

void FMaterialImporter::ClearCache()
{
	MaterialCache.Empty();
	AdditionalSearchPaths.Empty();
	TextureBaseMaterial = nullptr;
	ColorBaseMaterial = nullptr;
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
