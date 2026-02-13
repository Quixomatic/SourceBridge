#include "Materials/MaterialAnalyzer.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/Material.h"
#include "Engine/Texture2D.h"

FSourceMaterialAnalysis FMaterialAnalyzer::Analyze(UMaterialInterface* Material)
{
	FSourceMaterialAnalysis Result;

	if (!Material)
	{
		return Result;
	}

	// ---- Read blend mode and two-sided from the material interface ----
	EBlendMode BlendMode = Material->GetBlendMode();
	Result.bIsMasked = (BlendMode == BLEND_Masked);
	Result.bIsTranslucent = (BlendMode == BLEND_Translucent || BlendMode == BLEND_Additive);
	Result.bTwoSided = Material->IsTwoSided();

	// ---- Try to find textures via parameter names ----

	// Common base color / diffuse parameter names
	static const TArray<FName> BaseColorNames = {
		FName("BaseColor"),
		FName("Base Color"),
		FName("BaseTexture"),
		FName("Base Texture"),
		FName("Diffuse"),
		FName("DiffuseTexture"),
		FName("Albedo"),
		FName("Color"),
		FName("Texture"),
		FName("MainTexture"),
		FName("Base_Color"),
	};

	Result.BaseColorTexture = FindTextureParameter(Material, BaseColorNames);

	// Common normal map parameter names
	static const TArray<FName> NormalNames = {
		FName("Normal"),
		FName("NormalMap"),
		FName("Normal Map"),
		FName("NormalTexture"),
		FName("Bump"),
		FName("BumpMap"),
		FName("Bump Map"),
		FName("Normal_Map"),
	};

	Result.NormalMapTexture = FindTextureParameter(Material, NormalNames);

	// Common emissive parameter names
	static const TArray<FName> EmissiveNames = {
		FName("Emissive"),
		FName("EmissiveColor"),
		FName("Emissive Color"),
		FName("EmissiveTexture"),
		FName("Glow"),
		FName("SelfIllum"),
	};

	Result.EmissiveTexture = FindTextureParameter(Material, EmissiveNames);

	// ---- Scalar parameters ----

	static const TArray<FName> OpacityNames = {
		FName("Opacity"),
		FName("OpacityValue"),
		FName("Alpha"),
		FName("Transparency"),
	};

	FindScalarParameter(Material, OpacityNames, Result.Opacity);

	// ---- Vector parameters ----

	static const TArray<FName> TintNames = {
		FName("TintColor"),
		FName("Tint"),
		FName("Tint Color"),
		FName("ColorTint"),
		FName("Color Tint"),
		FName("BaseColor"),  // Some materials use BaseColor as a vector param for tint
	};

	FindVectorParameter(Material, TintNames, Result.TintColor);

	// ---- Fallback: if no base texture found via parameters, use GetUsedTextures ----
	if (!Result.BaseColorTexture)
	{
		FallbackTextureDetection(Material, Result);
	}

	Result.bHasValidTexture = (Result.BaseColorTexture != nullptr);

	if (Result.bHasValidTexture)
	{
		UE_LOG(LogTemp, Log, TEXT("MaterialAnalyzer: %s -> BaseColor=%s, Normal=%s, Masked=%d, Translucent=%d, TwoSided=%d"),
			*Material->GetName(),
			*Result.BaseColorTexture->GetName(),
			Result.NormalMapTexture ? *Result.NormalMapTexture->GetName() : TEXT("none"),
			Result.bIsMasked, Result.bIsTranslucent, Result.bTwoSided);
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("MaterialAnalyzer: %s -> no usable texture found"), *Material->GetName());
	}

	return Result;
}

UTexture2D* FMaterialAnalyzer::FindTextureParameter(UMaterialInterface* Material, const TArray<FName>& Names)
{
	if (!Material) return nullptr;

	for (const FName& Name : Names)
	{
		UTexture* Tex = nullptr;
		if (Material->GetTextureParameterValue(FHashedMaterialParameterInfo(Name), Tex))
		{
			UTexture2D* Tex2D = Cast<UTexture2D>(Tex);
			if (Tex2D)
			{
				return Tex2D;
			}
		}
	}

	return nullptr;
}

bool FMaterialAnalyzer::FindScalarParameter(UMaterialInterface* Material, const TArray<FName>& Names, float& OutValue)
{
	if (!Material) return false;

	for (const FName& Name : Names)
	{
		float Value = 0.0f;
		if (Material->GetScalarParameterValue(FHashedMaterialParameterInfo(Name), Value))
		{
			OutValue = Value;
			return true;
		}
	}

	return false;
}

bool FMaterialAnalyzer::FindVectorParameter(UMaterialInterface* Material, const TArray<FName>& Names, FLinearColor& OutValue)
{
	if (!Material) return false;

	for (const FName& Name : Names)
	{
		FLinearColor Value;
		if (Material->GetVectorParameterValue(FHashedMaterialParameterInfo(Name), Value))
		{
			OutValue = Value;
			return true;
		}
	}

	return false;
}

void FMaterialAnalyzer::FallbackTextureDetection(UMaterialInterface* Material, FSourceMaterialAnalysis& Result)
{
	if (!Material) return;

	// Get all textures used by this material
	TArray<UTexture*> UsedTextures;
	Material->GetUsedTextures(UsedTextures);

	if (UsedTextures.Num() == 0) return;

	// Heuristic: classify textures as base color or normal based on name and compression
	for (UTexture* Tex : UsedTextures)
	{
		UTexture2D* Tex2D = Cast<UTexture2D>(Tex);
		if (!Tex2D) continue;

		FString TexName = Tex2D->GetName().ToLower();

		// Check if this is a normal map
		bool bIsNormal = false;
		if (Tex2D->CompressionSettings == TC_Normalmap)
		{
			bIsNormal = true;
		}
		else if (TexName.Contains(TEXT("normal")) || TexName.Contains(TEXT("_n")) ||
				 TexName.Contains(TEXT("bump")) || TexName.EndsWith(TEXT("_nrm")))
		{
			bIsNormal = true;
		}

		if (bIsNormal)
		{
			if (!Result.NormalMapTexture)
			{
				Result.NormalMapTexture = Tex2D;
			}
		}
		else
		{
			// First non-normal texture is assumed to be base color
			if (!Result.BaseColorTexture)
			{
				Result.BaseColorTexture = Tex2D;
			}
		}

		// Early out if we found both
		if (Result.BaseColorTexture && Result.NormalMapTexture)
		{
			break;
		}
	}
}
