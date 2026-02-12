#pragma once

#include "CoreMinimal.h"

/**
 * FGD property types.
 */
enum class EFGDPropertyType : uint8
{
	String,
	Integer,
	Float,
	Choices,
	Flags,
	Color255,
	Studio,		// model path
	Sprite,		// sprite path
	Sound,		// sound path
	Decal,
	Material,
	Scene,		// choreography scene
	SideList,	// brush side IDs
	Origin,
	VecLine,
	Axis,
	Angle,
	NPCClass,
	FilterClass,
	PointEntityClass,
	TargetSource,
	TargetDestination,
	Unknown
};

/**
 * A single choice in a choices() property.
 */
struct FFGDChoice
{
	FString Value;
	FString DisplayName;
};

/**
 * A single spawnflag bit.
 */
struct FFGDFlag
{
	int32 Bit;
	FString DisplayName;
	bool bDefaultOn;
};

/**
 * A property (keyvalue) definition in an FGD entity class.
 */
struct FFGDProperty
{
	/** Internal keyvalue name (e.g., "targetname", "health") */
	FString Name;

	/** Display-friendly name (e.g., "Target Name", "Health") */
	FString DisplayName;

	/** Property type */
	EFGDPropertyType Type = EFGDPropertyType::String;

	/** Default value */
	FString DefaultValue;

	/** Description/help text */
	FString Description;

	/** Choices for choices() type */
	TArray<FFGDChoice> Choices;

	/** Flags for flags type (spawnflags) */
	TArray<FFGDFlag> Flags;

	/** Whether this property is read-only */
	bool bReadOnly = false;
};

/**
 * An input or output definition in an FGD entity class.
 */
struct FFGDIODef
{
	/** I/O name (e.g., "OnTrigger", "Kill") */
	FString Name;

	/** Parameter type (void, float, string, integer, bool, etc.) */
	FString ParamType;

	/** Description */
	FString Description;
};

/**
 * An entity class definition from an FGD file.
 */
struct SOURCEBRIDGE_API FFGDEntityClass
{
	/** Entity classname (e.g., "trigger_multiple", "info_player_terrorist") */
	FString ClassName;

	/** Class type: PointClass, SolidClass, BaseClass, etc. */
	FString ClassType;

	/** Display description */
	FString Description;

	/** Base classes this inherits from */
	TArray<FString> BaseClasses;

	/** Model to display in editor (from studio() or iconsprite()) */
	FString EditorModel;

	/** Editor icon sprite */
	FString IconSprite;

	/** Color in editor (R G B) */
	FString Color;

	/** Size bounding box (mins and maxs) */
	FString SizeMins;
	FString SizeMaxs;

	/** All keyvalue properties */
	TArray<FFGDProperty> Properties;

	/** Input definitions */
	TArray<FFGDIODef> Inputs;

	/** Output definitions */
	TArray<FFGDIODef> Outputs;

	/** Whether this entity uses brush geometry (SolidClass) */
	bool bIsSolid = false;

	/** Whether this is a base class (not directly placeable) */
	bool bIsBase = false;

	/** Find a property by name. Returns nullptr if not found. */
	const FFGDProperty* FindProperty(const FString& Name) const;

	/** Find an input by name. Returns nullptr if not found. */
	const FFGDIODef* FindInput(const FString& Name) const;

	/** Find an output by name. Returns nullptr if not found. */
	const FFGDIODef* FindOutput(const FString& Name) const;
};

/**
 * Result of parsing one or more FGD files.
 */
struct SOURCEBRIDGE_API FFGDDatabase
{
	/** All entity classes (including base classes). Key = classname (lowercase). */
	TMap<FString, FFGDEntityClass> Classes;

	/** Parsing warnings/errors */
	TArray<FString> Warnings;

	/** Look up an entity class by name. Returns nullptr if not found. */
	const FFGDEntityClass* FindClass(const FString& ClassName) const;

	/** Get all placeable (non-base) entity classnames. */
	TArray<FString> GetPlaceableClassNames() const;

	/** Get all point entity classnames. */
	TArray<FString> GetPointClassNames() const;

	/** Get all solid entity classnames. */
	TArray<FString> GetSolidClassNames() const;

	/**
	 * Get a fully resolved entity class with all base class properties merged.
	 * Properties from derived classes override base class properties.
	 */
	FFGDEntityClass GetResolved(const FString& ClassName) const;

private:
	FFGDEntityClass GetResolvedInternal(const FString& ClassName, TSet<FString>& Visited) const;

public:
	/** Validate an entity's keyvalues against the FGD schema. Returns warnings. */
	TArray<FString> ValidateEntity(
		const FString& ClassName,
		const TArray<TPair<FString, FString>>& KeyValues) const;

	/** Validate an I/O connection. Returns empty string if valid, warning if invalid. */
	FString ValidateIOConnection(
		const FString& SourceClass,
		const FString& OutputName,
		const FString& TargetClass,
		const FString& InputName) const;
};

/**
 * Parses Valve FGD (Forge Game Data) files.
 *
 * FGD files define the complete entity schema for a Source game:
 * - Entity classnames, types (point vs solid), descriptions
 * - Keyvalue properties with types, defaults, choices, flags
 * - Input and output definitions for entity I/O
 * - Base class inheritance
 *
 * Supports @include directives for nested FGD files.
 *
 * Usage:
 *   FFGDDatabase DB = FFGDParser::ParseFile("path/to/cstrike.fgd");
 *   const FFGDEntityClass* Trigger = DB.FindClass("trigger_multiple");
 */
class SOURCEBRIDGE_API FFGDParser
{
public:
	/**
	 * Parse an FGD file and all @include'd files.
	 * Returns a database of all parsed entity classes.
	 */
	static FFGDDatabase ParseFile(const FString& FilePath);

	/**
	 * Parse FGD content from a string.
	 * BaseDirectory is used to resolve @include paths.
	 */
	static FFGDDatabase ParseString(const FString& Content, const FString& BaseDirectory = TEXT(""));

private:
	struct FParseContext
	{
		FFGDDatabase& Database;
		FString BaseDirectory;
		TSet<FString> IncludedFiles; // prevent circular includes
	};

	static void ParseContent(const FString& Content, FParseContext& Context);
	static void ParseEntityClass(const FString& Content, int32& Pos, FParseContext& Context);
	static void ParseProperty(const FString& Content, int32& Pos, FFGDEntityClass& EntityClass, FParseContext& Context);
	static void ParseIODef(const FString& Content, int32& Pos, bool bIsInput, FFGDEntityClass& EntityClass);

	/** Skip whitespace and comments (// to end of line) */
	static void SkipWhitespaceAndComments(const FString& Content, int32& Pos);

	/** Read a quoted string or unquoted token */
	static FString ReadToken(const FString& Content, int32& Pos);

	/** Read a quoted string (returns content without quotes) */
	static FString ReadQuotedString(const FString& Content, int32& Pos);

	/** Read until a specific character is found */
	static FString ReadUntil(const FString& Content, int32& Pos, TCHAR StopChar);

	/** Map FGD type string to enum */
	static EFGDPropertyType ParsePropertyType(const FString& TypeStr);
};
