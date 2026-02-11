#include "Import/MaterialImporter.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "UObject/ConstructorHelpers.h"

TMap<FString, UMaterialInterface*> FMaterialImporter::MaterialCache;
TMap<FString, FString> FMaterialImporter::ReverseToolMappings;

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

	// Try to find existing UE material
	UMaterialInterface* Material = FindExistingMaterial(SourceMaterialPath);

	// Create placeholder if not found
	if (!Material)
	{
		Material = CreatePlaceholderMaterial(SourceMaterialPath);
	}

	if (Material)
	{
		MaterialCache.Add(NormalizedPath, Material);
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
	// Source: "concrete/concretefloor001" → UE: "M_Concrete__Concretefloor001" or similar
	FString Cleaned = SourceMaterialPath;
	Cleaned = Cleaned.Replace(TEXT("/"), TEXT("__")); // path separators → double underscore
	// Also try just the filename part
	FString FileName = FPaths::GetCleanFilename(SourceMaterialPath);

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	AssetRegistry.Get().GetAssetsByClass(UMaterialInterface::StaticClass()->GetClassPathName(), Assets);

	for (const FAssetData& Asset : Assets)
	{
		FString AssetName = Asset.AssetName.ToString();
		// Check various naming patterns
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

UMaterialInterface* FMaterialImporter::CreatePlaceholderMaterial(const FString& SourceMaterialPath)
{
	// Skip creating placeholders for tool textures (they're invisible)
	FString Upper = SourceMaterialPath.ToUpper();
	if (Upper.StartsWith(TEXT("TOOLS/")))
	{
		return nullptr;
	}

	// Find the default engine material to use as a base
	UMaterial* BaseMaterial = LoadObject<UMaterial>(nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!BaseMaterial)
	{
		// Fallback to WorldGridMaterial
		BaseMaterial = LoadObject<UMaterial>(nullptr,
			TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
	}
	if (!BaseMaterial) return nullptr;

	// Create dynamic material instance with a color derived from the name
	FString SafeName = SourceMaterialPath.Replace(TEXT("/"), TEXT("_")).Replace(TEXT("\\"), TEXT("_"));
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, GetTransientPackage(),
		FName(*FString::Printf(TEXT("MI_Import_%s"), *SafeName)));

	if (MID)
	{
		FLinearColor Color = ColorFromName(SourceMaterialPath);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
	}

	return MID;
}

void FMaterialImporter::ClearCache()
{
	MaterialCache.Empty();
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
