#pragma once

#include "CoreMinimal.h"

/**
 * A single Source engine surface property definition.
 * Mirrors surfaceproperties.txt entries.
 */
struct SOURCEBRIDGE_API FSourceSurfaceProperty
{
	/** Surface type name (e.g., "concrete", "metal", "wood") */
	FString Name;

	/** Base surface type to inherit from (empty = no inheritance) */
	FString BaseName;

	/** Material density in kg/m^3 */
	float Density = 2000.0f;

	/** Bounciness (0-1.0 normally, can exceed 1.0 for custom types) */
	float Elasticity = 0.25f;

	/** Friction coefficient (0-1.0) */
	float Friction = 0.8f;

	/** Drag on contact */
	float Dampening = 0.0f;

	/** Jump height multiplier (1.0 = normal) */
	float JumpFactor = 1.0f;

	/** Maximum speed multiplier on this surface (0 = no override) */
	float MaxSpeedFactor = 0.0f;

	/** Single-char game material code for impact effects */
	TCHAR GameMaterial = TEXT('C');

	/** Audio: step sound left */
	FString StepLeft;

	/** Audio: step sound right */
	FString StepRight;

	/** Audio: bullet impact sound */
	FString ImpactHard;

	/** Audio: scrape sound */
	FString ScrapeSmooth;
};

/**
 * Database of all Source engine surface properties.
 *
 * This replicates the data from surfaceproperties.txt and makes it
 * available for:
 * - Auto-selecting $surfaceprop in VMT files based on UE material names
 * - Physics mass calculation (mass = volume x density)
 * - Validation of surface property names
 *
 * Data sourced from CS:S surfaceproperties.txt and surfaceproperties_manifest.txt
 */
class SOURCEBRIDGE_API FSurfacePropertiesDatabase
{
public:
	/** Get the singleton database instance. */
	static FSurfacePropertiesDatabase& Get();

	/** Look up a surface property by name. Returns nullptr if not found. */
	const FSourceSurfaceProperty* Find(const FString& Name) const;

	/** Get all surface property names. */
	TArray<FString> GetAllNames() const;

	/** Check if a surface property name is valid. */
	bool IsValid(const FString& Name) const;

	/**
	 * Calculate mass for a physics object.
	 * Mass = Volume (in Source cubic units) x Density
	 */
	float CalculateMass(const FString& SurfacePropName, float VolumeSourceUnits) const;

	/**
	 * Auto-detect a Source surface property from a UE material name.
	 * Looks for keywords in the material name (e.g., "concrete", "metal", "wood").
	 * Falls back to "default" if no match found.
	 */
	FString DetectSurfaceProp(const FString& UEMaterialName) const;

	/** Get the resolved property (with base class inheritance applied). */
	FSourceSurfaceProperty GetResolved(const FString& Name) const;

private:
	FSurfacePropertiesDatabase();

	void InitializeDefaults();

	TMap<FString, FSourceSurfaceProperty> Properties;
};
