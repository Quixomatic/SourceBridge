#include "Entities/EntityIOConnection.h"

FString FEntityIOConnection::FormatValue() const
{
	// Source format: "targetname,InputName,parameter,delay,refireCount"
	return FString::Printf(TEXT("%s,%s,%s,%g,%d"),
		*TargetEntity,
		*InputName,
		*Parameter,
		Delay,
		RefireCount);
}

bool FEntityIOConnection::ParseFromTag(const FString& TagString, FEntityIOConnection& OutConnection)
{
	// Expected format: "io:OutputName:targetname,InputName,parameter,delay,refireCount"
	// The "io:" prefix identifies this tag as an I/O connection

	if (!TagString.StartsWith(TEXT("io:"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	FString Remainder = TagString.Mid(3); // Skip "io:"

	// Split on first colon to get OutputName
	int32 ColonIdx;
	if (!Remainder.FindChar(TEXT(':'), ColonIdx))
	{
		return false;
	}

	OutConnection.OutputName = Remainder.Left(ColonIdx);
	FString ValuePart = Remainder.Mid(ColonIdx + 1);

	// Parse comma-separated values: target,input,param,delay,refire
	TArray<FString> Parts;
	ValuePart.ParseIntoArray(Parts, TEXT(","));

	if (Parts.Num() < 2)
	{
		return false;
	}

	OutConnection.TargetEntity = Parts[0];
	OutConnection.InputName = Parts[1];
	OutConnection.Parameter = Parts.Num() > 2 ? Parts[2] : TEXT("");
	OutConnection.Delay = Parts.Num() > 3 ? FCString::Atof(*Parts[3]) : 0.0f;
	OutConnection.RefireCount = Parts.Num() > 4 ? FCString::Atoi(*Parts[4]) : -1;

	return true;
}
