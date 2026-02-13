#include "Utilities/SourceCoord.h"

FVector FSourceCoord::UEToSource(const FVector& UEPos)
{
	// UE: X forward, Y right, Z up (left-handed)
	// Source: X forward, Y left, Z up (right-handed)
	// Scale: 1 UE cm -> 0.525 Source units
	return FVector(
		UEPos.X * ScaleFactor,
		-UEPos.Y * ScaleFactor,  // Negate Y for handedness flip
		UEPos.Z * ScaleFactor
	);
}

FVector FSourceCoord::SourceToUE(const FVector& SourcePos)
{
	double InvScale = 1.0 / ScaleFactor;
	return FVector(
		SourcePos.X * InvScale,
		-SourcePos.Y * InvScale,  // Negate Y back
		SourcePos.Z * InvScale
	);
}

FVector FSourceCoord::UEToSourceDirection(const FVector& UEDir)
{
	// Direction only: negate Y for handedness flip, no scaling
	return FVector(UEDir.X, -UEDir.Y, UEDir.Z);
}

FString FSourceCoord::UERotationToSourceAngles(const FRotator& UERot)
{
	// Source angles: pitch yaw roll
	// UE pitch = Source pitch (but may need negation depending on convention)
	// UE yaw -> Source yaw (negate for handedness)
	// UE roll -> Source roll
	float Pitch = UERot.Pitch;
	float Yaw = -UERot.Yaw;
	float Roll = UERot.Roll;
	return FString::Printf(TEXT("%d %d %d"),
		FMath::RoundToInt(Pitch),
		FMath::RoundToInt(Yaw),
		FMath::RoundToInt(Roll));
}

FString FSourceCoord::FormatVector(const FVector& V)
{
	return FString::Printf(TEXT("%d %d %d"),
		FMath::RoundToInt(V.X),
		FMath::RoundToInt(V.Y),
		FMath::RoundToInt(V.Z));
}

FString FSourceCoord::FormatPlanePoint(const FVector& V)
{
	return FString::Printf(TEXT("(%d %d %d)"),
		FMath::RoundToInt(V.X),
		FMath::RoundToInt(V.Y),
		FMath::RoundToInt(V.Z));
}
