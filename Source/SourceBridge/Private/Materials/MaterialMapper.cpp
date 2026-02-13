#include "Materials/MaterialMapper.h"
#include "Materials/SourceMaterialManifest.h"
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

	// 1. Manifest lookup — the primary resolution path for imported/registered materials
	USourceMaterialManifest* Manifest = USourceMaterialManifest::Get();
	if (Manifest)
	{
		FString SourcePath = Manifest->GetSourcePath(Material);
		if (!SourcePath.IsEmpty())
		{
			return SourcePath;
		}
	}

	// 2. Fall through to name-based mapping (overrides, tool textures)
	return MapMaterialName(Material->GetName());
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

	// 3. Default fallback — no more broken name-based auto-mapping
	return DefaultMaterial;
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
