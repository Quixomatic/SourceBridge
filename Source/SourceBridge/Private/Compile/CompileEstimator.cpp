#include "Compile/CompileEstimator.h"
#include "Engine/Brush.h"
#include "Engine/World.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Landscape/LandscapeProxy.h"
#include "GameFramework/Volume.h"
#include "EngineUtils.h"

FString FCompileTimeEstimate::GetSummary() const
{
	auto FormatTime = [](float Seconds) -> FString
	{
		if (Seconds < 60.0f)
		{
			return FString::Printf(TEXT("%.0fs"), Seconds);
		}
		else if (Seconds < 3600.0f)
		{
			int32 Mins = FMath::FloorToInt(Seconds / 60.0f);
			int32 Secs = FMath::FloorToInt(FMath::Fmod(Seconds, 60.0f));
			return FString::Printf(TEXT("%dm %ds"), Mins, Secs);
		}
		else
		{
			int32 Hours = FMath::FloorToInt(Seconds / 3600.0f);
			int32 Mins = FMath::FloorToInt(FMath::Fmod(Seconds, 3600.0f) / 60.0f);
			return FString::Printf(TEXT("%dh %dm"), Hours, Mins);
		}
	};

	FString Result = FString::Printf(
		TEXT("Estimated compile time: %s (%s confidence)\n")
		TEXT("  VBSP: %s  |  VVIS: %s  |  VRAD: %s\n")
		TEXT("  Scene: %d brushes, %d faces, %d entities, %d lights%s"),
		*FormatTime(TotalSeconds),
		*Confidence,
		*FormatTime(VBSPSeconds),
		*FormatTime(VVISSeconds),
		*FormatTime(VRADSeconds),
		BrushCount, BrushSideCount, EntityCount, LightCount,
		bHasDisplacements ? TEXT(", has displacements") : TEXT(""));

	if (bFastCompile)
	{
		Result += TEXT("\n  Mode: Fast (-fast)");
	}
	else
	{
		Result += TEXT("\n  Mode: Full quality");
	}

	return Result;
}

FCompileTimeEstimate FCompileEstimator::EstimateCompileTime(
	UWorld* World,
	bool bFastCompile,
	bool bFinalCompile)
{
	FCompileTimeEstimate Est;
	Est.bFastCompile = bFastCompile;

	if (!World) return Est;

	// Count scene elements
	for (TActorIterator<ABrush> It(World); It; ++It)
	{
		ABrush* Brush = *It;
		if (Brush == World->GetDefaultBrush()) continue;
		if (Brush->IsA<AVolume>()) continue;

		Est.BrushCount++;

		// Estimate brush sides from the brush model
		if (Brush->Brush && Brush->Brush->Polys)
		{
			Est.BrushSideCount += Brush->Brush->Polys->Element.Num();
		}
		else
		{
			Est.BrushSideCount += 6; // Assume simple box
		}
	}

	// Count entities (actors with Source tags or Source entity actors)
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->Tags.Num() > 0) Est.EntityCount++;
	}

	// Count lights
	for (TActorIterator<APointLight> It(World); It; ++It) Est.LightCount++;
	for (TActorIterator<ASpotLight> It(World); It; ++It) Est.LightCount++;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It) Est.LightCount++;

	// Check for landscapes/displacements
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		Est.bHasDisplacements = true;
		break;
	}

	// Count static meshes (exported as props, affect compile)
	int32 PropCount = 0;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It) PropCount++;

	// --- Estimation heuristics ---
	// Based on typical Source engine compile behavior on modern hardware

	// VBSP: Fast, proportional to brush count
	// ~0.3 seconds base + 0.005 seconds per brush face
	Est.VBSPSeconds = 0.3f + (Est.BrushSideCount * 0.005f);
	if (Est.bHasDisplacements) Est.VBSPSeconds *= 1.5f;

	// VVIS: Highly variable, depends on structural brush layout
	// Simple maps: a few seconds. Complex open maps: hours.
	// Fast mode is ~10x faster.
	float StructuralBrushes = FMath::Max(1.0f, (float)Est.BrushCount * 0.6f); // ~60% structural
	if (bFastCompile)
	{
		// Fast vis: ~0.5s base + linear with brush count
		Est.VVISSeconds = 0.5f + (StructuralBrushes * 0.02f);
	}
	else
	{
		// Full vis: grows faster than linearly with structural brushes
		// Small maps (~50 brushes): ~5s
		// Medium maps (~200 brushes): ~60s
		// Large maps (~500+ brushes): minutes to hours
		Est.VVISSeconds = 1.0f + (StructuralBrushes * StructuralBrushes * 0.001f);
		Est.VVISSeconds = FMath::Min(Est.VVISSeconds, 7200.0f); // Cap at 2 hours
	}

	// VRAD: Proportional to surface area and light count
	// Lights add bounced light calculations
	float LightMultiplier = FMath::Max(1.0f, (float)Est.LightCount);
	float SurfaceArea = (float)Est.BrushSideCount * 256.0f; // Rough surface area per face

	if (bFastCompile)
	{
		// Fast rad: ~1s base + small per-face cost
		Est.VRADSeconds = 1.0f + (Est.BrushSideCount * 0.01f);
	}
	else if (bFinalCompile)
	{
		// Final quality: much slower (extra bounce passes, higher resolution)
		Est.VRADSeconds = 5.0f + (SurfaceArea * LightMultiplier * 0.0001f);
		Est.VRADSeconds *= 3.0f; // Final is ~3x slower than normal
	}
	else
	{
		// Normal quality
		Est.VRADSeconds = 3.0f + (SurfaceArea * LightMultiplier * 0.0001f);
	}

	if (Est.bHasDisplacements) Est.VRADSeconds *= 2.0f;
	if (PropCount > 10) Est.VRADSeconds *= 1.0f + (PropCount * 0.01f);

	Est.TotalSeconds = Est.VBSPSeconds + Est.VVISSeconds + Est.VRADSeconds;

	// Confidence level based on how typical the scene is
	if (Est.BrushCount < 10)
	{
		Est.Confidence = TEXT("high"); // Simple maps are predictable
	}
	else if (Est.BrushCount < 200 && !Est.bHasDisplacements)
	{
		Est.Confidence = TEXT("medium");
	}
	else
	{
		Est.Confidence = TEXT("low"); // Complex maps have unpredictable vvis
	}

	return Est;
}
