#include "Materials/MaterialMapper.h"
#include "Materials/SourceMaterialManifest.h"
#include "Materials/MaterialAnalyzer.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"

FMaterialMapper::FMaterialMapper()
	: DefaultMaterial(TEXT("DEV/DEV_MEASUREWALL01A"))
{
	InitToolTextureMappings();
}

FString FMaterialMapper::MapMaterial(UMaterialInterface* Material) const
{
	if (!Material)
	{
		UsedMaterialPaths.Add(DefaultMaterial);
		return DefaultMaterial;
	}

	// 1. Manifest lookup — the primary resolution path for imported/registered materials
	USourceMaterialManifest* Manifest = USourceMaterialManifest::Get();
	if (Manifest)
	{
		FString SourcePath = Manifest->GetSourcePath(Material);
		if (!SourcePath.IsEmpty())
		{
			UsedMaterialPaths.Add(SourcePath);
			return SourcePath;
		}
	}

	// 2. Check manual overrides and tool textures by name
	FString NameResult = MapMaterialName(Material->GetName());
	if (NameResult != DefaultMaterial)
	{
		UsedMaterialPaths.Add(NameResult);
		return NameResult;
	}

	// 3. Material analysis — analyze UE material, assign custom Source path, register in manifest
	FString CustomPath = AnalyzeAndRegisterCustomMaterial(Material);
	if (!CustomPath.IsEmpty())
	{
		UsedMaterialPaths.Add(CustomPath);
		return CustomPath;
	}

	// 4. Default fallback
	UsedMaterialPaths.Add(DefaultMaterial);
	return DefaultMaterial;
}

FString FMaterialMapper::MapMaterialName(const FString& MaterialName) const
{
	if (MaterialName.IsEmpty())
	{
		return DefaultMaterial;
	}

	// 1. Check manual overrides first (case-insensitive)
	for (const auto& Pair : ManualOverrides)
	{
		if (Pair.Key.Equals(MaterialName, ESearchCase::IgnoreCase))
		{
			return Pair.Value;
		}
	}

	// 2. Check tool texture mappings
	for (const auto& Pair : ToolTextureMappings)
	{
		if (MaterialName.Equals(Pair.Key, ESearchCase::IgnoreCase))
		{
			return Pair.Value;
		}
	}

	// 3. Default fallback
	return DefaultMaterial;
}

FString FMaterialMapper::AnalyzeAndRegisterCustomMaterial(UMaterialInterface* Material) const
{
	if (!Material)
	{
		return FString();
	}

	// Skip our own persistent base/placeholder materials (M_SourceBridge_*)
	FString MatName = Material->GetName();
	if (MatName.StartsWith(TEXT("M_SourceBridge_")))
	{
		return FString();
	}

	// Analyze the material to see if it has a usable texture
	FSourceMaterialAnalysis Analysis = FMaterialAnalyzer::Analyze(Material);
	if (!Analysis.bHasValidTexture)
	{
		return FString();
	}

	// Build the Source material path: custom/<mapname>/<cleanname>
	FString CleanName = CleanMaterialName(MatName);
	FString Prefix = MapName.IsEmpty() ? TEXT("export") : MapName;
	FString SourcePath = FString::Printf(TEXT("custom/%s/%s"), *Prefix, *CleanName);

	// Register in manifest
	USourceMaterialManifest* Manifest = USourceMaterialManifest::Get();
	if (Manifest)
	{
		// Check if already registered (e.g. from a previous export)
		FSourceMaterialEntry* Existing = Manifest->FindBySourcePath(SourcePath);
		if (Existing)
		{
			return SourcePath;
		}

		FSourceMaterialEntry Entry;
		Entry.SourcePath = SourcePath;
		Entry.Type = ESourceMaterialType::Custom;
		Entry.VMTShader = TEXT("LightmappedGeneric");
		Entry.bIsInVPK = false;
		Entry.LastImported = FDateTime::Now();

		// Store the material asset reference
		Entry.MaterialAsset = FSoftObjectPath(Material);

		// Store the base texture reference
		if (Analysis.BaseColorTexture)
		{
			Entry.TextureAsset = FSoftObjectPath(Analysis.BaseColorTexture);
		}

		// Store normal map reference
		if (Analysis.NormalMapTexture)
		{
			Entry.NormalMapAsset = FSoftObjectPath(Analysis.NormalMapTexture);
		}

		// Store VMT params based on analysis
		if (Analysis.bIsMasked)
		{
			Entry.VMTParams.Add(TEXT("$alphatest"), TEXT("1"));
			Entry.VMTParams.Add(TEXT("$alphatestreference"), TEXT("0.5"));
		}
		else if (Analysis.bIsTranslucent)
		{
			Entry.VMTParams.Add(TEXT("$translucent"), TEXT("1"));
			if (Analysis.Opacity < 1.0f)
			{
				Entry.VMTParams.Add(TEXT("$alpha"), FString::Printf(TEXT("%.2f"), Analysis.Opacity));
			}
		}

		if (Analysis.bTwoSided)
		{
			Entry.VMTParams.Add(TEXT("$nocull"), TEXT("1"));
		}

		if (Analysis.EmissiveTexture)
		{
			Entry.VMTParams.Add(TEXT("$selfillum"), TEXT("1"));
		}

		Manifest->Register(Entry);
		Manifest->SaveManifest();

		UE_LOG(LogTemp, Log, TEXT("MaterialMapper: Registered custom material '%s' -> '%s'"),
			*MatName, *SourcePath);
	}

	return SourcePath;
}

FString FMaterialMapper::CleanMaterialName(const FString& UEName)
{
	FString Clean = UEName;

	// Remove common UE prefixes
	if (Clean.StartsWith(TEXT("M_"))) Clean = Clean.Mid(2);
	else if (Clean.StartsWith(TEXT("MI_"))) Clean = Clean.Mid(3);
	else if (Clean.StartsWith(TEXT("Mat_"))) Clean = Clean.Mid(4);

	// Convert to lowercase
	Clean = Clean.ToLower();

	// Replace spaces and special chars with underscores
	Clean.ReplaceInline(TEXT(" "), TEXT("_"));
	Clean.ReplaceInline(TEXT("-"), TEXT("_"));

	// Remove any characters not allowed in Source paths (only alphanumeric, underscore, forward slash)
	FString Result;
	for (TCHAR Ch : Clean)
	{
		if (FChar::IsAlnum(Ch) || Ch == TEXT('_') || Ch == TEXT('/'))
		{
			Result.AppendChar(Ch);
		}
	}

	if (Result.IsEmpty())
	{
		Result = TEXT("unnamed");
	}

	return Result;
}

void FMaterialMapper::AddOverride(const FString& UEMaterialName, const FString& SourceMaterialPath)
{
	ManualOverrides.Add(UEMaterialName, SourceMaterialPath);
}

void FMaterialMapper::SetDefaultMaterial(const FString& SourceMaterialPath)
{
	DefaultMaterial = SourceMaterialPath;
}

void FMaterialMapper::InitToolTextureMappings()
{
	ToolTextureMappings.Add(TEXT("Tool_Nodraw"),     TEXT("TOOLS/TOOLSNODRAW"));
	ToolTextureMappings.Add(TEXT("Tool_Clip"),        TEXT("TOOLS/TOOLSCLIP"));
	ToolTextureMappings.Add(TEXT("Tool_PlayerClip"),  TEXT("TOOLS/TOOLSPLAYERCLIP"));
	ToolTextureMappings.Add(TEXT("Tool_NPCClip"),     TEXT("TOOLS/TOOLSNPCCLIP"));
	ToolTextureMappings.Add(TEXT("Tool_Trigger"),      TEXT("TOOLS/TOOLSTRIGGER"));
	ToolTextureMappings.Add(TEXT("Tool_Skybox"),       TEXT("TOOLS/TOOLSSKYBOX"));
	ToolTextureMappings.Add(TEXT("Tool_Skip"),         TEXT("TOOLS/TOOLSSKIP"));
	ToolTextureMappings.Add(TEXT("Tool_Hint"),         TEXT("TOOLS/TOOLSHINT"));
	ToolTextureMappings.Add(TEXT("Tool_Invisible"),    TEXT("TOOLS/TOOLSINVISIBLE"));
	ToolTextureMappings.Add(TEXT("Tool_Areaportal"),   TEXT("TOOLS/TOOLSAREAPORTAL"));
	ToolTextureMappings.Add(TEXT("Tool_Blocklight"),   TEXT("TOOLS/TOOLSBLOCKLIGHT"));
	ToolTextureMappings.Add(TEXT("Tool_BlockLOS"),     TEXT("TOOLS/TOOLSBLOCK_LOS"));
	ToolTextureMappings.Add(TEXT("Tool_BlockBullets"), TEXT("TOOLS/TOOLSBLOCKBULLETS"));
	ToolTextureMappings.Add(TEXT("Tool_Fog"),          TEXT("TOOLS/TOOLSFOG"));
	ToolTextureMappings.Add(TEXT("Tool_Black"),        TEXT("TOOLS/TOOLSBLACK"));
}
