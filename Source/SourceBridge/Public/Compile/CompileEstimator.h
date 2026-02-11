#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Estimated compile times for each stage.
 */
struct FCompileTimeEstimate
{
	/** Estimated seconds for vbsp (geometry compilation). */
	float VBSPSeconds = 0.0f;

	/** Estimated seconds for vvis (visibility). */
	float VVISSeconds = 0.0f;

	/** Estimated seconds for vrad (lighting). */
	float VRADSeconds = 0.0f;

	/** Total estimated seconds. */
	float TotalSeconds = 0.0f;

	/** Confidence level: "low", "medium", "high". */
	FString Confidence;

	/** Scene complexity metrics used for estimation. */
	int32 BrushCount = 0;
	int32 BrushSideCount = 0;
	int32 EntityCount = 0;
	int32 LightCount = 0;
	bool bHasDisplacements = false;
	bool bFastCompile = true;

	/** Get a human-readable summary. */
	FString GetSummary() const;
};

/**
 * Estimates Source engine map compile time based on scene complexity.
 *
 * Heuristics are based on typical Source engine compile characteristics:
 * - vbsp: Proportional to brush count and complexity (~0.5s per 100 brushes)
 * - vvis: Highly variable, proportional to structural brush count squared
 * - vrad: Proportional to surface area * light count
 *
 * Fast compile (-fast flag) is ~10x faster for vvis and ~5x for vrad.
 */
class SOURCEBRIDGE_API FCompileEstimator
{
public:
	/**
	 * Estimate compile time for a UE world.
	 * @param World The UE world to analyze
	 * @param bFastCompile Whether -fast flag will be used
	 * @param bFinalCompile Whether -final flag will be used (high quality)
	 * @return Time estimates per stage and total
	 */
	static FCompileTimeEstimate EstimateCompileTime(
		UWorld* World,
		bool bFastCompile = true,
		bool bFinalCompile = false);
};
