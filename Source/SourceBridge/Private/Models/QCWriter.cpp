#include "Models/QCWriter.h"

FString FQCWriter::GenerateQC(const FQCSettings& Settings)
{
	FString QC;
	QC.Reserve(1024);

	// Model output path
	QC += FString::Printf(TEXT("$modelname \"%s.mdl\"\n"), *Settings.ModelName);

	// Reference mesh body
	QC += FString::Printf(TEXT("$body studio \"%s\"\n"), *Settings.BodySMD);

	// Material search path
	if (!Settings.CDMaterials.IsEmpty())
	{
		QC += FString::Printf(TEXT("$cdmaterials \"%s\"\n"), *Settings.CDMaterials);
	}

	// Surface property
	if (!Settings.SurfaceProp.IsEmpty())
	{
		QC += FString::Printf(TEXT("$surfaceprop \"%s\"\n"), *Settings.SurfaceProp);
	}

	// Scale override
	if (!FMath::IsNearlyEqual(Settings.Scale, 1.0f))
	{
		QC += FString::Printf(TEXT("$scale %.4f\n"), Settings.Scale);
	}

	// Static prop flag
	if (Settings.bStaticProp)
	{
		QC += TEXT("$staticprop\n");
	}

	// Idle sequence (required even for static props)
	if (!Settings.IdleSMD.IsEmpty())
	{
		QC += FString::Printf(TEXT("$sequence idle \"%s\" fps %.0f\n"), *Settings.IdleSMD, Settings.AnimFPS);
	}
	else
	{
		// Use reference mesh as idle if no separate idle SMD
		QC += FString::Printf(TEXT("$sequence idle \"%s\" fps %.0f\n"), *Settings.BodySMD, Settings.AnimFPS);
	}

	// Additional animation sequences
	for (const auto& AnimSeq : Settings.AnimationSequences)
	{
		QC += FString::Printf(TEXT("$sequence \"%s\" \"%s\" fps %.0f\n"),
			*AnimSeq.Key, *AnimSeq.Value, Settings.AnimFPS);
	}

	// Collision model
	if (Settings.bHasCollision && !Settings.CollisionSMD.IsEmpty())
	{
		QC += FString::Printf(TEXT("\n$collisionmodel \"%s\"\n"), *Settings.CollisionSMD);
		QC += TEXT("{\n");

		if (Settings.bConcaveCollision)
		{
			QC += TEXT("\t$concave\n");
		}

		if (Settings.MassOverride > 0.0f)
		{
			QC += FString::Printf(TEXT("\t$mass %.1f\n"), Settings.MassOverride);
		}

		QC += TEXT("}\n");
	}

	return QC;
}

FQCSettings FQCWriter::MakeDefaultSettings(const FString& MeshName)
{
	FQCSettings Settings;

	// Clean mesh name
	FString CleanName = MeshName.ToLower();
	if (CleanName.StartsWith(TEXT("SM_"))) CleanName = CleanName.Mid(3);
	else if (CleanName.StartsWith(TEXT("S_"))) CleanName = CleanName.Mid(2);

	Settings.ModelName = FString::Printf(TEXT("props/%s"), *CleanName);
	Settings.BodySMD = FString::Printf(TEXT("%s_ref.smd"), *CleanName);
	Settings.CollisionSMD = FString::Printf(TEXT("%s_phys.smd"), *CleanName);
	Settings.IdleSMD = FString::Printf(TEXT("%s_idle.smd"), *CleanName);
	Settings.CDMaterials = FString::Printf(TEXT("models/props/%s"), *CleanName);
	Settings.SurfaceProp = TEXT("default");
	Settings.bStaticProp = true;
	Settings.bHasCollision = true;

	return Settings;
}
