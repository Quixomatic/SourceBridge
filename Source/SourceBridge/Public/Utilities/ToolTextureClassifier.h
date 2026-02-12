#pragma once

#include "CoreMinimal.h"

/**
 * Classification of Source engine tool textures.
 * Determines visibility and collision behavior at runtime.
 */
enum class EToolTextureType : uint8
{
	Normal,			// Regular renderable texture
	NoDraw,			// TOOLS/TOOLSNODRAW — stripped entirely
	Trigger,		// TOOLS/TOOLSTRIGGER — invisible, overlap only
	Clip,			// TOOLS/TOOLSCLIP — invisible, blocks all
	PlayerClip,		// TOOLS/TOOLSPLAYERCLIP — invisible, blocks players
	NPCClip,		// TOOLS/TOOLSNPCCLIP — invisible, blocks NPCs only
	Invisible,		// TOOLS/TOOLSINVISIBLE — invisible, solid collision
	BlockBullets,	// TOOLS/TOOLSBLOCKBULLETS — invisible, blocks traces
	BlockLight,		// TOOLS/TOOLSBLOCKLIGHT — compile-time only
	Sky,			// TOOLS/TOOLSSKYBOX — sky rendering
	Hint,			// TOOLS/TOOLSHINT — BSP optimization hint
	Skip			// TOOLS/TOOLSSKIP — BSP skip face
};

/**
 * Static utility for classifying Source engine tool textures.
 * Used by PIE runtime to set visibility/collision, and by editor toggle.
 */
class SOURCEBRIDGE_API FToolTextureClassifier
{
public:
	/** Classify a Source material name into a tool texture type. */
	static EToolTextureType Classify(const FString& SourceMaterialName);

	/** Returns true if the material is any type of tool texture (not Normal). */
	static bool IsToolTexture(const FString& SourceMaterialName);

	/** Returns true if this tool type should block player movement in PIE. */
	static bool ShouldBlockPlayer(EToolTextureType Type);

	/** Returns true if this tool type should be visible in PIE. */
	static bool ShouldBeVisibleInGame(EToolTextureType Type);

	/** Returns true if this tool type should generate overlap events (triggers). */
	static bool ShouldGenerateOverlaps(EToolTextureType Type);
};
