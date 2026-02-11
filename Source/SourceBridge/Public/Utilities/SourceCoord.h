#pragma once

#include "CoreMinimal.h"

/**
 * Coordinate conversion between Unreal Engine and Source Engine.
 *
 * UE:     Z-up, left-handed, 1 unit = 1cm
 * Source: Z-up, right-handed, 1 unit ~ 1.905cm
 *
 * Conversion: scale * 0.525, negate Y axis
 */
struct SOURCEBRIDGE_API FSourceCoord
{
	/** Scale factor: UE centimeters to Source units */
	static constexpr double ScaleFactor = 0.525;

	/** Convert a UE position (cm, left-handed) to Source position (Source units, right-handed) */
	static FVector UEToSource(const FVector& UEPos);

	/** Convert a Source position to UE position */
	static FVector SourceToUE(const FVector& SourcePos);

	/** Convert a UE rotation (FRotator) to Source angles string "pitch yaw roll" */
	static FString UERotationToSourceAngles(const FRotator& UERot);

	/** Format a Source-space vector as "x y z" for VMF output (integer coordinates) */
	static FString FormatVector(const FVector& V);

	/** Format a Source-space vector as "(x y z)" for VMF plane definitions */
	static FString FormatPlanePoint(const FVector& V);
};
