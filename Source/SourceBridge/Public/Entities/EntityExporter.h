#pragma once

#include "CoreMinimal.h"
#include "VMF/VMFKeyValues.h"
#include "Entities/EntityIOConnection.h"

class UWorld;
class AActor;

/**
 * Represents a Source engine entity for VMF export.
 * Contains classname, origin, angles, and arbitrary key-value properties.
 */
struct SOURCEBRIDGE_API FSourceEntity
{
	FString ClassName;
	FString TargetName;
	FVector Origin;
	FRotator Angles;
	TArray<TPair<FString, FString>> KeyValues;

	/** Entity I/O connections */
	TArray<FEntityIOConnection> Connections;

	/** True if this entity requires brush geometry (trigger_multiple, func_water_analog, etc.) */
	bool bIsBrushEntity = false;

	/** The UE actor this entity was exported from (for brush entity geometry lookup) */
	TWeakObjectPtr<AActor> SourceActor;

	FSourceEntity()
		: Origin(FVector::ZeroVector)
		, Angles(FRotator::ZeroRotator)
	{
	}

	void AddKeyValue(const FString& Key, const FString& Value);
	void AddKeyValue(const FString& Key, int32 Value);
	void AddKeyValue(const FString& Key, float Value);
};

/**
 * Result of scanning a UE scene for exportable entities.
 */
struct SOURCEBRIDGE_API FEntityExportResult
{
	TArray<FSourceEntity> Entities;
	TArray<FString> Warnings;

	/** Whether a light_environment entity was exported. */
	bool bHasLightEnvironment = false;

	/** All unique targetnames found (for I/O validation). */
	TArray<FString> TargetNames;
};

/**
 * Scans a UE world and converts actors to Source engine entities.
 *
 * Recognizes:
 * - PlayerStart actors -> info_player_terrorist / info_player_counterterrorist
 * - Point lights -> light entities
 * - Spot lights -> light_spot entities
 * - Directional lights -> light_environment entities
 * - TriggerBox/TriggerVolume actors -> trigger_multiple brush entities
 *
 * Entity I/O is driven by actor tags in the format:
 *   io:OutputName:targetname,InputName,parameter,delay,refireCount
 *
 * Entity targetname is driven by actor tags:
 *   targetname:my_entity_name
 *
 * Source keyvalues can be set via actor tags:
 *   kv:key:value
 */
class SOURCEBRIDGE_API FEntityExporter
{
public:
	/**
	 * Scan the world for all exportable entities.
	 * Returns Source entities with converted coordinates.
	 */
	static FEntityExportResult ExportEntities(UWorld* World);

	/**
	 * Convert a FSourceEntity to a VMF entity block.
	 */
	static FVMFKeyValues EntityToVMF(const FSourceEntity& Entity, int32 EntityId);

private:
	/** Try to export a PlayerStart actor as team spawns. */
	static bool TryExportPlayerStart(AActor* Actor, FEntityExportResult& Result);

	/** Try to export a light actor. */
	static bool TryExportLight(AActor* Actor, FEntityExportResult& Result);

	/** Try to export a trigger volume actor. */
	static bool TryExportTriggerVolume(AActor* Actor, FEntityExportResult& Result);

	/** Try to export a water volume (actor tagged with "water"). */
	static bool TryExportWaterVolume(AActor* Actor, FEntityExportResult& Result);

	/** Try to export an overlay/decal (actor tagged with "overlay:material"). */
	static bool TryExportOverlay(AActor* Actor, FEntityExportResult& Result);

	/** Parse actor tags for I/O connections, targetname, and keyvalues. */
	static void ParseActorTags(AActor* Actor, FSourceEntity& Entity);
};
