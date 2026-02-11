#pragma once

#include "CoreMinimal.h"

/**
 * Represents a node in Valve's KeyValues tree format.
 * Used by VMF, VMT, and other Valve text formats.
 *
 * Format:
 *   BlockName
 *   {
 *       "key" "value"
 *       ChildBlock
 *       {
 *           "key" "value"
 *       }
 *   }
 */
struct SOURCEBRIDGE_API FVMFKeyValues
{
	FString ClassName;
	TArray<TPair<FString, FString>> Properties;
	TArray<FVMFKeyValues> Children;

	FVMFKeyValues() = default;
	explicit FVMFKeyValues(const FString& InClassName);

	void AddProperty(const FString& Key, const FString& Value);
	void AddProperty(const FString& Key, int32 Value);
	void AddProperty(const FString& Key, float Value);

	FVMFKeyValues& AddChild(const FString& InClassName);

	/** Serialize this node and all children to Valve KeyValues text format. */
	FString Serialize(int32 IndentLevel = 0) const;

private:
	static FString MakeIndent(int32 Level);
};
