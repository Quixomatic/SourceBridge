#pragma once

#include "CoreMinimal.h"
#include "VMF/VMFKeyValues.h"

/**
 * Parses VMF (Valve Map Format) text files into FVMFKeyValues tree structure.
 * This is the reverse of FVMFKeyValues::Serialize().
 */
class SOURCEBRIDGE_API FVMFReader
{
public:
	/** Parse a VMF file from disk. Returns the top-level blocks. */
	static TArray<FVMFKeyValues> ParseFile(const FString& FilePath);

	/** Parse VMF text content. Returns the top-level blocks. */
	static TArray<FVMFKeyValues> ParseString(const FString& Content);

private:
	static void SkipWhitespaceAndComments(const FString& Content, int32& Pos);
	static FString ReadQuotedString(const FString& Content, int32& Pos);
	static FString ReadUnquotedToken(const FString& Content, int32& Pos);
	static FVMFKeyValues ParseBlock(const FString& Content, int32& Pos, const FString& ClassName);
};
