#include "Utilities/ToolTextureClassifier.h"

EToolTextureType FToolTextureClassifier::Classify(const FString& SourceMaterialName)
{
	// Normalize: uppercase, strip path separators
	FString Upper = SourceMaterialName.ToUpper().Replace(TEXT("\\"), TEXT("/"));

	// Check for TOOLS/ prefix patterns
	if (Upper.Contains(TEXT("TOOLS/TOOLSNODRAW")))
		return EToolTextureType::NoDraw;
	if (Upper.Contains(TEXT("TOOLS/TOOLSTRIGGER")))
		return EToolTextureType::Trigger;
	if (Upper.Contains(TEXT("TOOLS/TOOLSPLAYERCLIP")))
		return EToolTextureType::PlayerClip;
	if (Upper.Contains(TEXT("TOOLS/TOOLSNPCCLIP")))
		return EToolTextureType::NPCClip;
	if (Upper.Contains(TEXT("TOOLS/TOOLSCLIP")))
		return EToolTextureType::Clip;
	if (Upper.Contains(TEXT("TOOLS/TOOLSINVISIBLE")))
		return EToolTextureType::Invisible;
	if (Upper.Contains(TEXT("TOOLS/TOOLSBLOCKBULLETS")))
		return EToolTextureType::BlockBullets;
	if (Upper.Contains(TEXT("TOOLS/TOOLSBLOCKLIGHT")))
		return EToolTextureType::BlockLight;
	if (Upper.Contains(TEXT("TOOLS/TOOLSSKYBOX")) || Upper.Contains(TEXT("TOOLS/TOOLSSKY")))
		return EToolTextureType::Sky;
	if (Upper.Contains(TEXT("TOOLS/TOOLSHINT")))
		return EToolTextureType::Hint;
	if (Upper.Contains(TEXT("TOOLS/TOOLSSKIP")))
		return EToolTextureType::Skip;

	return EToolTextureType::Normal;
}

bool FToolTextureClassifier::IsToolTexture(const FString& SourceMaterialName)
{
	return Classify(SourceMaterialName) != EToolTextureType::Normal;
}

bool FToolTextureClassifier::ShouldBlockPlayer(EToolTextureType Type)
{
	switch (Type)
	{
	case EToolTextureType::Clip:
	case EToolTextureType::PlayerClip:
	case EToolTextureType::Invisible:
		return true;
	default:
		return false;
	}
}

bool FToolTextureClassifier::ShouldBeVisibleInGame(EToolTextureType Type)
{
	// Only normal textures and sky are visible at runtime
	return Type == EToolTextureType::Normal || Type == EToolTextureType::Sky;
}

bool FToolTextureClassifier::ShouldGenerateOverlaps(EToolTextureType Type)
{
	return Type == EToolTextureType::Trigger;
}
