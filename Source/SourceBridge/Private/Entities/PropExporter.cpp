#include "Entities/PropExporter.h"
#include "Utilities/SourceCoord.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"

TArray<FVMFKeyValues> FPropExporter::ExportProps(
	UWorld* World,
	int32& EntityIdCounter,
	const FPropExportSettings& Settings)
{
	TArray<FVMFKeyValues> Entities;

	if (!World) return Entities;

	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* Actor = *It;
		if (!Actor) continue;

		// Skip actors tagged with "noexport"
		bool bSkip = false;
		for (const FName& Tag : Actor->Tags)
		{
			if (Tag.ToString().Equals(TEXT("noexport"), ESearchCase::IgnoreCase))
			{
				bSkip = true;
				break;
			}
		}
		if (bSkip) continue;

		UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
		if (!MeshComp || !MeshComp->GetStaticMesh()) continue;

		EntityIdCounter++;
		FVMFKeyValues Entity = ExportProp(Actor, EntityIdCounter, Settings);
		Entities.Add(Entity);
	}

	if (Entities.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("SourceBridge: Exported %d prop entities."), Entities.Num());
	}

	return Entities;
}

FVMFKeyValues FPropExporter::ExportProp(
	AStaticMeshActor* Actor,
	int32 EntityId,
	const FPropExportSettings& Settings)
{
	EPropExportMode Mode = GetExportMode(Actor, Settings.DefaultMode);
	FString Classname = GetClassname(Mode);
	FString ModelPath = GetModelPath(Actor, Settings.ModelPathPrefix);

	FVector SrcPos = FSourceCoord::UEToSource(Actor->GetActorLocation());
	FVector SrcAngles = FSourceCoord::UERotationToSourceAngles(Actor->GetActorRotation());

	FVMFKeyValues Entity;
	Entity.ClassName = TEXT("entity");
	Entity.Properties.Add(TPair<FString, FString>(TEXT("id"), FString::FromInt(EntityId)));
	Entity.Properties.Add(TPair<FString, FString>(TEXT("classname"), Classname));
	Entity.Properties.Add(TPair<FString, FString>(TEXT("origin"),
		FSourceCoord::FormatVector(SrcPos)));
	Entity.Properties.Add(TPair<FString, FString>(TEXT("angles"),
		FString::Printf(TEXT("%g %g %g"), SrcAngles.X, SrcAngles.Y, SrcAngles.Z)));
	Entity.Properties.Add(TPair<FString, FString>(TEXT("model"), ModelPath));

	// Parse additional keyvalues from tags
	FString Skin = TEXT("0");
	FString Solid = TEXT("6"); // VPhysics by default for static
	FString TargetName;

	for (const FName& Tag : Actor->Tags)
	{
		FString TagStr = Tag.ToString();

		if (TagStr.StartsWith(TEXT("skin:"), ESearchCase::IgnoreCase))
		{
			Skin = TagStr.Mid(5).TrimStartAndEnd();
		}
		else if (TagStr.StartsWith(TEXT("solid:"), ESearchCase::IgnoreCase))
		{
			Solid = TagStr.Mid(6).TrimStartAndEnd();
		}
		else if (TagStr.StartsWith(TEXT("targetname:"), ESearchCase::IgnoreCase))
		{
			TargetName = TagStr.Mid(11).TrimStartAndEnd();
		}
		else if (TagStr.StartsWith(TEXT("kv:"), ESearchCase::IgnoreCase))
		{
			// kv:key:value format
			FString KVStr = TagStr.Mid(3);
			int32 ColonIdx;
			if (KVStr.FindChar(TEXT(':'), ColonIdx))
			{
				FString Key = KVStr.Left(ColonIdx);
				FString Value = KVStr.Mid(ColonIdx + 1);
				Entity.Properties.Add(TPair<FString, FString>(Key, Value));
			}
		}
	}

	Entity.Properties.Add(TPair<FString, FString>(TEXT("skin"), Skin));
	Entity.Properties.Add(TPair<FString, FString>(TEXT("solid"), Solid));

	if (!TargetName.IsEmpty())
	{
		Entity.Properties.Add(TPair<FString, FString>(TEXT("targetname"), TargetName));
	}

	// Scale - only if non-uniform (Source prop_static supports modelscale)
	FVector Scale = Actor->GetActorScale3D();
	if (!Scale.Equals(FVector::OneVector, 0.01f))
	{
		// Source only supports uniform scale for props
		float UniformScale = (Scale.X + Scale.Y + Scale.Z) / 3.0f;
		Entity.Properties.Add(TPair<FString, FString>(TEXT("modelscale"),
			FString::Printf(TEXT("%g"), UniformScale)));

		if (!FMath::IsNearlyEqual(Scale.X, Scale.Y, 0.01f) ||
			!FMath::IsNearlyEqual(Scale.Y, Scale.Z, 0.01f))
		{
			UE_LOG(LogTemp, Warning, TEXT("SourceBridge: Non-uniform scale on %s (%s). Source only supports uniform scale - using average."),
				*Actor->GetName(), *Scale.ToString());
		}
	}

	return Entity;
}

EPropExportMode FPropExporter::GetExportMode(AStaticMeshActor* Actor, EPropExportMode Default)
{
	for (const FName& Tag : Actor->Tags)
	{
		FString TagStr = Tag.ToString();

		if (TagStr.Equals(TEXT("prop_static"), ESearchCase::IgnoreCase))
			return EPropExportMode::PropStatic;
		if (TagStr.Equals(TEXT("prop_dynamic"), ESearchCase::IgnoreCase))
			return EPropExportMode::PropDynamic;
		if (TagStr.Equals(TEXT("prop_physics"), ESearchCase::IgnoreCase))
			return EPropExportMode::PropPhysics;
		if (TagStr.Equals(TEXT("func_detail"), ESearchCase::IgnoreCase))
			return EPropExportMode::FuncDetail;
	}

	// Physics-simulating actors default to prop_physics
	UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
	if (MeshComp && MeshComp->IsSimulatingPhysics())
	{
		return EPropExportMode::PropPhysics;
	}

	// Moveable actors default to prop_dynamic
	if (Actor->IsRootComponentMovable())
	{
		return EPropExportMode::PropDynamic;
	}

	return Default;
}

FString FPropExporter::GetModelPath(AStaticMeshActor* Actor, const FString& Prefix)
{
	// Check for mdl: tag override
	for (const FName& Tag : Actor->Tags)
	{
		FString TagStr = Tag.ToString();
		if (TagStr.StartsWith(TEXT("mdl:"), ESearchCase::IgnoreCase))
		{
			FString Path = TagStr.Mid(4).TrimStartAndEnd();
			// Ensure .mdl extension
			if (!Path.EndsWith(TEXT(".mdl")))
			{
				Path += TEXT(".mdl");
			}
			return Path;
		}
	}

	// Auto-generate from mesh name
	UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
	if (MeshComp && MeshComp->GetStaticMesh())
	{
		FString MeshName = MeshComp->GetStaticMesh()->GetName().ToLower();

		// Strip common UE prefixes
		if (MeshName.StartsWith(TEXT("sm_"))) MeshName = MeshName.Mid(3);
		else if (MeshName.StartsWith(TEXT("s_"))) MeshName = MeshName.Mid(2);

		return Prefix + MeshName + TEXT(".mdl");
	}

	return TEXT("models/error.mdl");
}

FString FPropExporter::GetClassname(EPropExportMode Mode)
{
	switch (Mode)
	{
	case EPropExportMode::PropStatic:  return TEXT("prop_static");
	case EPropExportMode::PropDynamic: return TEXT("prop_dynamic");
	case EPropExportMode::PropPhysics: return TEXT("prop_physics");
	case EPropExportMode::FuncDetail:  return TEXT("func_detail");
	default: return TEXT("prop_static");
	}
}
