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

FString FSourceCoord::UERotationToSourceAngles(const FRotator& UERot)
{
	// Convert UE FRotator to Source "pitch yaw roll" string.
	// This is the inverse of VMFImporter::ParseAngles.
	// Build the UE rotation matrix, apply Y-negate to get Source matrix,
	// then extract Source Euler angles (Yaw→Pitch→Roll order).

	FMatrix UEMat = FRotationMatrix(UERot);

	// Convert UE matrix to Source: negate Y axis
	// M_src = Diag(1,-1,1) * M_ue * Diag(1,-1,1)
	FMatrix SrcMat = FMatrix::Identity;
	for (int32 r = 0; r < 3; r++)
	{
		for (int32 c = 0; c < 3; c++)
		{
			float Sign = 1.0f;
			if (r == 1) Sign = -Sign;  // negate row 1
			if (c == 1) Sign = -Sign;  // negate col 1
			SrcMat.M[r][c] = Sign * UEMat.M[r][c];
		}
	}

	// Extract Source Euler angles from the Source rotation matrix
	// Source AngleMatrix produces:
	//   M[2][0] = -sin(pitch)
	//   M[2][1] = sin(roll) * cos(pitch)
	//   M[2][2] = cos(roll) * cos(pitch)
	//   M[0][0] = cos(pitch) * cos(yaw)
	//   M[1][0] = cos(pitch) * sin(yaw)
	float SP = -SrcMat.M[2][0];
	SP = FMath::Clamp(SP, -1.0f, 1.0f);
	float Pitch = FMath::RadiansToDegrees(FMath::Asin(SP));
	float CP = FMath::Cos(FMath::DegreesToRadians(Pitch));

	float Yaw, Roll;
	if (CP > 0.001f)
	{
		Yaw = FMath::RadiansToDegrees(FMath::Atan2(SrcMat.M[1][0], SrcMat.M[0][0]));
		Roll = FMath::RadiansToDegrees(FMath::Atan2(SrcMat.M[2][1], SrcMat.M[2][2]));
	}
	else
	{
		// Gimbal lock
		Yaw = FMath::RadiansToDegrees(FMath::Atan2(-SrcMat.M[0][1], SrcMat.M[1][1]));
		Roll = 0.0f;
	}

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
