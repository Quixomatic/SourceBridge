#pragma once

#include "CoreMinimal.h"

/**
 * Represents a single Source engine entity I/O connection.
 *
 * Source format: "OutputName" "targetname,InputName,parameter,delay,refireCount"
 * Example: "OnStartTouch" "goal_counter,Add,1,0,-1"
 */
struct SOURCEBRIDGE_API FEntityIOConnection
{
	/** The output event name on this entity (e.g., "OnStartTouch") */
	FString OutputName;

	/** The targetname of the entity to fire the input on */
	FString TargetEntity;

	/** The input to fire on the target entity (e.g., "Add", "Trigger") */
	FString InputName;

	/** Parameter to pass with the input (can be empty) */
	FString Parameter;

	/** Delay in seconds before firing (0 = immediate) */
	float Delay;

	/** How many times to fire (-1 = infinite) */
	int32 RefireCount;

	FEntityIOConnection()
		: Delay(0.0f)
		, RefireCount(-1)
	{
	}

	FEntityIOConnection(
		const FString& InOutput,
		const FString& InTarget,
		const FString& InInput,
		const FString& InParam = TEXT(""),
		float InDelay = 0.0f,
		int32 InRefire = -1)
		: OutputName(InOutput)
		, TargetEntity(InTarget)
		, InputName(InInput)
		, Parameter(InParam)
		, Delay(InDelay)
		, RefireCount(InRefire)
	{
	}

	/** Format as Source I/O value string: "targetname,InputName,parameter,delay,refireCount" */
	FString FormatValue() const;

	/**
	 * Parse a Source I/O connection from tag format.
	 * Expected: "OutputName:targetname,InputName,parameter,delay,refireCount"
	 * Returns false if parsing fails.
	 */
	static bool ParseFromTag(const FString& TagString, FEntityIOConnection& OutConnection);
};
