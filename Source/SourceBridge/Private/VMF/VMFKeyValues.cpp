#include "VMF/VMFKeyValues.h"

FVMFKeyValues::FVMFKeyValues(const FString& InClassName)
	: ClassName(InClassName)
{
}

void FVMFKeyValues::AddProperty(const FString& Key, const FString& Value)
{
	Properties.Emplace(Key, Value);
}

void FVMFKeyValues::AddProperty(const FString& Key, int32 Value)
{
	Properties.Emplace(Key, FString::FromInt(Value));
}

void FVMFKeyValues::AddProperty(const FString& Key, float Value)
{
	// Source engine uses integer-like floats in most places.
	// Use full precision only when the value has a fractional part.
	if (FMath::IsNearlyEqual(Value, FMath::RoundToFloat(Value)))
	{
		Properties.Emplace(Key, FString::FromInt(FMath::RoundToInt(Value)));
	}
	else
	{
		Properties.Emplace(Key, FString::SanitizeFloat(Value));
	}
}

FVMFKeyValues& FVMFKeyValues::AddChild(const FString& InClassName)
{
	FVMFKeyValues& Child = Children.Emplace_GetRef(InClassName);
	return Child;
}

FString FVMFKeyValues::Serialize(int32 IndentLevel) const
{
	FString Result;
	FString Indent = MakeIndent(IndentLevel);
	FString InnerIndent = MakeIndent(IndentLevel + 1);

	// Block name
	Result += Indent + ClassName + TEXT("\n");
	Result += Indent + TEXT("{\n");

	// Key-value pairs
	for (const TPair<FString, FString>& Prop : Properties)
	{
		Result += InnerIndent + TEXT("\"") + Prop.Key + TEXT("\" \"") + Prop.Value + TEXT("\"\n");
	}

	// Child blocks
	for (const FVMFKeyValues& Child : Children)
	{
		Result += Child.Serialize(IndentLevel + 1);
	}

	Result += Indent + TEXT("}\n");

	return Result;
}

FString FVMFKeyValues::MakeIndent(int32 Level)
{
	FString Indent;
	for (int32 i = 0; i < Level; ++i)
	{
		Indent += TEXT("\t");
	}
	return Indent;
}
