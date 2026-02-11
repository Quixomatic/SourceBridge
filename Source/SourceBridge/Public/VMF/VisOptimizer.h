#pragma once

#include "CoreMinimal.h"
#include "VMF/VMFKeyValues.h"

class UWorld;
class ABrush;

/**
 * A suggested vis optimization action.
 */
struct FVisOptSuggestion
{
	enum class EType
	{
		HintBrush,      // Place a hint/skip brush at this location
		AreaPortal,      // Place a func_areaportal at this opening
		VisCluster       // Group this open area as func_viscluster
	};

	EType Type;
	FVector Location;
	FVector Extent;
	FVector Normal;      // For hint brushes: the cutting plane normal
	FString Description;
};

/**
 * Visibility optimization utilities for Source engine maps.
 *
 * Source VIS optimization uses three main tools:
 * 1. hint/skip brushes - Tell VVIS where to split visibility leaves
 *    (TOOLS/TOOLSHINT on cutting face, TOOLS/TOOLSSKIP on all others)
 * 2. func_areaportal - Brush entities at doorways that split visibility areas
 * 3. func_viscluster - Brush entities that merge visibility leaves in open areas
 *
 * Brushes tagged "hint" in UE are exported with appropriate hint/skip materials.
 * Brushes tagged "func_areaportal" or "func_viscluster" use the existing
 * brush entity tag system.
 */
class SOURCEBRIDGE_API FVisOptimizer
{
public:
	/**
	 * Generate a hint/skip brush solid.
	 * The brush is a thin slab: the face aligned with Normal gets TOOLSHINT,
	 * all other faces get TOOLSSKIP.
	 *
	 * @param SolidIdCounter Auto-incremented solid ID
	 * @param SideIdCounter Auto-incremented side ID
	 * @param Center Center point of the hint brush (Source coordinates)
	 * @param HalfExtent Half-size of the brush slab
	 * @param Normal Direction the hint plane faces
	 * @return VMF solid ready to add to worldspawn
	 */
	static FVMFKeyValues GenerateHintBrush(
		int32& SolidIdCounter,
		int32& SideIdCounter,
		const FVector& Center,
		const FVector& HalfExtent,
		const FVector& Normal);

	/**
	 * Check if a UE brush is tagged as a hint brush.
	 * Hint brushes are tagged with "hint" and get exported with
	 * TOOLS/TOOLSHINT on the primary face and TOOLS/TOOLSSKIP on others.
	 */
	static bool IsHintBrush(const ABrush* Brush);

	/**
	 * Check if a UE brush is tagged as an area portal.
	 * Area portals are already handled by the brush entity system
	 * (classname:func_areaportal), but this provides a convenience check.
	 */
	static bool IsAreaPortal(const ABrush* Brush);

	/**
	 * Check if a UE brush is tagged as a viscluster.
	 */
	static bool IsVisCluster(const ABrush* Brush);

	/**
	 * Analyze the world geometry and suggest vis optimization placements.
	 * Looks for:
	 * - Narrow passages between larger spaces (hint brush candidates)
	 * - Doorway-sized openings (area portal candidates)
	 * - Large open areas with many brush faces (viscluster candidates)
	 *
	 * @param World The UE world to analyze
	 * @return Array of optimization suggestions
	 */
	static TArray<FVisOptSuggestion> AnalyzeWorld(UWorld* World);

	/**
	 * Export all hint-tagged brushes from the world as worldspawn hint/skip solids.
	 * Called during VMF export to inject hint brushes into the worldspawn.
	 *
	 * @param World The UE world to scan
	 * @param SolidIdCounter Auto-incremented solid ID
	 * @param SideIdCounter Auto-incremented side ID
	 * @return Array of hint/skip brush solids for worldspawn
	 */
	static TArray<FVMFKeyValues> ExportHintBrushes(
		UWorld* World,
		int32& SolidIdCounter,
		int32& SideIdCounter);
};
