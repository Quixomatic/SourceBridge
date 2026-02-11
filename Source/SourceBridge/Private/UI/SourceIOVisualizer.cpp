#include "UI/SourceIOVisualizer.h"
#include "Actors/SourceEntityActor.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"

USourceIOVisualizer::USourceIOVisualizer()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bAutoActivate = true;

#if WITH_EDITORONLY_DATA
	bTickInEditor = true;
	SetIsVisualizationComponent(true);
#endif
}

void USourceIOVisualizer::RefreshConnections()
{
	CachedWires.Empty();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	// Parse I/O tags: io:OutputName:targetname,inputname,param,delay,refire
	for (const FName& Tag : Owner->Tags)
	{
		FString TagStr = Tag.ToString();
		if (!TagStr.StartsWith(TEXT("io:"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		FString IOStr = TagStr.Mid(3); // After "io:"

		// Split on first colon to get output name
		int32 ColonIdx;
		if (!IOStr.FindChar(TEXT(':'), ColonIdx))
		{
			continue;
		}

		FString OutputName = IOStr.Left(ColonIdx);
		FString ValueStr = IOStr.Mid(ColonIdx + 1);

		// Parse: target,input,param,delay,refire
		TArray<FString> Parts;
		ValueStr.ParseIntoArray(Parts, TEXT(","));

		FIOWire Wire;
		Wire.OutputName = OutputName;
		Wire.TargetName = Parts.Num() > 0 ? Parts[0] : TEXT("");
		Wire.InputName = Parts.Num() > 1 ? Parts[1] : TEXT("");
		Wire.Parameter = Parts.Num() > 2 ? Parts[2] : TEXT("");
		Wire.Delay = Parts.Num() > 3 ? FCString::Atof(*Parts[3]) : 0.0f;
		Wire.RefireCount = Parts.Num() > 4 ? FCString::Atoi(*Parts[4]) : -1;

		if (Wire.TargetName.IsEmpty()) continue;

		// Resolve target actor by targetname
		Wire.bBroken = true;
		UWorld* World = GetWorld();
		if (World)
		{
			for (TActorIterator<ASourceEntityActor> It(World); It; ++It)
			{
				ASourceEntityActor* TargetActor = *It;
				if (TargetActor->TargetName == Wire.TargetName)
				{
					Wire.ResolvedTarget = TargetActor;
					Wire.bBroken = false;
					break;
				}
			}
		}

		CachedWires.Add(MoveTemp(Wire));
	}
}

#if WITH_EDITORONLY_DATA
void USourceIOVisualizer::OnRegister()
{
	Super::OnRegister();
	RefreshConnections();
}

void USourceIOVisualizer::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bDrawWires) return;
	if (!GetWorld()) return;
	if (!GetWorld()->IsEditorWorld()) return;

	AActor* Owner = GetOwner();
	if (!Owner) return;

	// Periodically refresh connections (every ~2 seconds to catch tag changes)
	static float RefreshTimer = 0.0f;
	RefreshTimer += DeltaTime;
	if (RefreshTimer > 2.0f)
	{
		RefreshTimer = 0.0f;
		RefreshConnections();
	}

	FVector Start = Owner->GetActorLocation();

	for (const FIOWire& Wire : CachedWires)
	{
		FColor WireColor;

		if (Wire.bBroken)
		{
			WireColor = FColor::Red;
		}
		else if (Wire.OutputName.Contains(TEXT("Touch")))
		{
			WireColor = FColor::Green;
		}
		else if (Wire.OutputName.Contains(TEXT("Trigger")) || Wire.OutputName.Contains(TEXT("Press")))
		{
			WireColor = FColor::Yellow;
		}
		else
		{
			WireColor = FColor::Cyan;
		}

		FVector End;
		if (Wire.ResolvedTarget.IsValid())
		{
			End = Wire.ResolvedTarget->GetActorLocation();
		}
		else
		{
			// Draw a short stub for broken connections
			End = Start + FVector(100, 0, 0);
		}

		// Draw the wire
		DrawDebugLine(GetWorld(), Start, End, WireColor, false, -1.0f, SDPG_World, WireThickness);

		// Draw a small arrow at the target end
		FVector Dir = (End - Start).GetSafeNormal();
		if (!Dir.IsNearlyZero())
		{
			FVector Right = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal() * 8.0f;
			FVector ArrowBase = End - Dir * 20.0f;
			DrawDebugLine(GetWorld(), End, ArrowBase + Right, WireColor, false, -1.0f, SDPG_World, WireThickness);
			DrawDebugLine(GetWorld(), End, ArrowBase - Right, WireColor, false, -1.0f, SDPG_World, WireThickness);
		}

		// Draw label at midpoint
		FVector MidPoint = (Start + End) * 0.5f + FVector(0, 0, 10);
		FString Label = FString::Printf(TEXT("%s -> %s.%s"),
			*Wire.OutputName, *Wire.TargetName, *Wire.InputName);

		DrawDebugString(GetWorld(), MidPoint, Label, nullptr, WireColor, 0.0f, true, 0.8f);
	}
}
#endif
