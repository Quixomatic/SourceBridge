#include "Materials/MaterialMapper.h"
#include "Materials/MaterialInterface.h"

FMaterialMapper::FMaterialMapper()
	: DefaultMaterial(TEXT("DEV/DEV_MEASUREWALL01A"))
{
	InitToolTextureMappings();
}

FString FMaterialMapper::MapMaterial(UMaterialInterface* Material) const
{
	if (!Material)
	{
		return DefaultMaterial;
	}

	return MapMaterialName(Material->GetName());
}

FString FMaterialMapper::MapMaterialName(const FString& MaterialName) const
{
	if (MaterialName.IsEmpty())
	{
		return DefaultMaterial;
	}

	// 1. Check manual overrides first (case-insensitive)
	const FString* Override = ManualOverrides.Find(MaterialName);
	if (Override)
	{
		return *Override;
	}

	// Also try case-insensitive lookup
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

	// 3. Name-based auto-mapping
	// Strip common UE prefixes (M_, MI_, Mat_) and convert to Source path style
	FString Cleaned = MaterialName;

	// Strip instance suffix
	if (Cleaned.EndsWith(TEXT("_Inst")))
	{
		Cleaned = Cleaned.LeftChop(5);
	}

	// Strip common prefixes
	if (Cleaned.StartsWith(TEXT("M_")))
	{
		Cleaned = Cleaned.RightChop(2);
	}
	else if (Cleaned.StartsWith(TEXT("MI_")))
	{
		Cleaned = Cleaned.RightChop(3);
	}
	else if (Cleaned.StartsWith(TEXT("Mat_")))
	{
		Cleaned = Cleaned.RightChop(4);
	}

	// Convert to lowercase Source-style path
	Cleaned = Cleaned.ToLower();

	// Replace double underscores with path separators
	Cleaned = Cleaned.Replace(TEXT("__"), TEXT("/"));

	return Cleaned;
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
	// Standard Source tool textures.
	// UE materials named with these patterns auto-map to the correct tool texture.
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
