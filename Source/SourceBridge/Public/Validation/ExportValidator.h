#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Severity levels for validation messages.
 */
enum class EValidationSeverity : uint8
{
	Info,
	Warning,
	Error
};

/**
 * A single validation message.
 */
struct FValidationMessage
{
	EValidationSeverity Severity;
	FString Category;
	FString Message;

	FValidationMessage() : Severity(EValidationSeverity::Info) {}
	FValidationMessage(EValidationSeverity InSev, const FString& InCat, const FString& InMsg)
		: Severity(InSev), Category(InCat), Message(InMsg) {}
};

/**
 * Result of a validation pass.
 */
struct FValidationResult
{
	TArray<FValidationMessage> Messages;
	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	int32 InfoCount = 0;

	bool HasErrors() const { return ErrorCount > 0; }

	void AddMessage(EValidationSeverity Severity, const FString& Category, const FString& Message)
	{
		Messages.Add(FValidationMessage(Severity, Category, Message));
		switch (Severity)
		{
		case EValidationSeverity::Error: ErrorCount++; break;
		case EValidationSeverity::Warning: WarningCount++; break;
		case EValidationSeverity::Info: InfoCount++; break;
		}
	}

	void LogAll() const;
};

/**
 * Source engine limits.
 */
struct FSourceEngineLimits
{
	static constexpr int32 MAX_MAP_BRUSHES = 8192;
	static constexpr int32 MAX_MAP_BRUSHSIDES = 65536;
	static constexpr int32 MAX_MAP_PLANES = 65536;
	static constexpr int32 MAX_MAP_ENTITIES = 8192;
	static constexpr int32 MAX_MAP_TEXINFO = 12288;
	static constexpr int32 MAX_MAP_OVERLAYS = 512;
	static constexpr int32 MAX_MAP_LIGHTS = 12288;
};

/**
 * Validates a UE scene before export to Source engine VMF.
 *
 * Checks for Source engine limits, required entities, geometry issues,
 * and other common problems that would cause compile failures.
 */
class SOURCEBRIDGE_API FExportValidator
{
public:
	/**
	 * Run all validation checks on a world.
	 */
	static FValidationResult ValidateWorld(UWorld* World);

private:
	/** Check brush counts against Source limits. */
	static void ValidateBrushLimits(UWorld* World, FValidationResult& Result);

	/** Check entity counts and required entities. */
	static void ValidateEntities(UWorld* World, FValidationResult& Result);

	/** Check for geometry issues (non-convex brushes, etc). */
	static void ValidateGeometry(UWorld* World, FValidationResult& Result);

	/** Check lighting setup. */
	static void ValidateLighting(UWorld* World, FValidationResult& Result);

	/** Check team spawn balance for CS:S. */
	static void ValidateSpawns(UWorld* World, FValidationResult& Result);

	/** Validate entity classnames and keyvalues against loaded FGD. */
	static void ValidateEntityClassnames(UWorld* World, FValidationResult& Result);

	/** Validate surface property names. */
	static void ValidateSurfaceProperties(UWorld* World, FValidationResult& Result);
};
