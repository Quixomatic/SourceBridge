#include "Materials/VMTWriter.h"
#include "Materials/MaterialAnalyzer.h"
#include "Materials/SurfaceProperties.h"

FVMTWriter::FVMTWriter()
	: ShaderName(TEXT("LightmappedGeneric"))
{
}

void FVMTWriter::SetShader(const FString& Shader)
{
	ShaderName = Shader;
}

void FVMTWriter::SetBaseTexture(const FString& TexturePath)
{
	Parameters.Add(TEXT("$basetexture"), TexturePath);
}

void FVMTWriter::SetSurfaceProp(const FString& SurfaceProp)
{
	Parameters.Add(TEXT("$surfaceprop"), SurfaceProp);
}

void FVMTWriter::SetBumpMap(const FString& BumpPath)
{
	Parameters.Add(TEXT("$bumpmap"), BumpPath);
}

void FVMTWriter::SetParameter(const FString& Key, const FString& Value)
{
	Parameters.Add(Key, Value);
}

FString FVMTWriter::Serialize() const
{
	FString Result;

	Result += ShaderName + TEXT("\n");
	Result += TEXT("{\n");

	// Sort parameters for consistent output
	TArray<FString> Keys;
	Parameters.GetKeys(Keys);
	Keys.Sort();

	for (const FString& Key : Keys)
	{
		const FString& Value = Parameters[Key];
		Result += FString::Printf(TEXT("\t\"%s\" \"%s\"\n"), *Key, *Value);
	}

	Result += TEXT("}\n");

	return Result;
}

FString FVMTWriter::GenerateBrushVMT(
	const FString& BaseTexturePath,
	const FString& SurfaceProp)
{
	FVMTWriter Writer;
	Writer.SetShader(TEXT("LightmappedGeneric"));
	Writer.SetBaseTexture(BaseTexturePath);

	// Auto-detect surface prop from texture path if using default
	FString ResolvedProp = SurfaceProp;
	if (ResolvedProp == TEXT("concrete") || ResolvedProp.IsEmpty())
	{
		ResolvedProp = FSurfacePropertiesDatabase::Get().DetectSurfaceProp(BaseTexturePath);
	}
	Writer.SetSurfaceProp(ResolvedProp);

	return Writer.Serialize();
}

FString FVMTWriter::GenerateModelVMT(
	const FString& BaseTexturePath,
	const FString& SurfaceProp)
{
	FVMTWriter Writer;
	Writer.SetShader(TEXT("VertexLitGeneric"));
	Writer.SetBaseTexture(BaseTexturePath);

	// Auto-detect surface prop from texture path if using default
	FString ResolvedProp = SurfaceProp;
	if (ResolvedProp == TEXT("metal") || ResolvedProp.IsEmpty())
	{
		ResolvedProp = FSurfacePropertiesDatabase::Get().DetectSurfaceProp(BaseTexturePath);
	}
	Writer.SetSurfaceProp(ResolvedProp);

	return Writer.Serialize();
}

FString FVMTWriter::GenerateFromAnalysis(
	const FSourceMaterialAnalysis& Analysis,
	const FString& SourceMaterialPath,
	const FString& NormalMapPath)
{
	FVMTWriter Writer;
	Writer.SetShader(TEXT("LightmappedGeneric"));
	Writer.SetBaseTexture(SourceMaterialPath);

	// Surface property from texture path
	FString SurfaceProp = FSurfacePropertiesDatabase::Get().DetectSurfaceProp(SourceMaterialPath);
	Writer.SetSurfaceProp(SurfaceProp);

	// Normal / bump map
	if (!NormalMapPath.IsEmpty())
	{
		Writer.SetBumpMap(NormalMapPath);
	}

	// Transparency
	if (Analysis.bIsMasked)
	{
		Writer.SetParameter(TEXT("$alphatest"), TEXT("1"));
		Writer.SetParameter(TEXT("$alphatestreference"), TEXT("0.5"));
	}
	else if (Analysis.bIsTranslucent)
	{
		Writer.SetParameter(TEXT("$translucent"), TEXT("1"));
		if (Analysis.Opacity < 1.0f)
		{
			Writer.SetParameter(TEXT("$alpha"), FString::Printf(TEXT("%.2f"), Analysis.Opacity));
		}
	}

	// Two-sided
	if (Analysis.bTwoSided)
	{
		Writer.SetParameter(TEXT("$nocull"), TEXT("1"));
	}

	// Emissive / self-illumination
	if (Analysis.EmissiveTexture)
	{
		Writer.SetParameter(TEXT("$selfillum"), TEXT("1"));
	}

	return Writer.Serialize();
}

FString FVMTWriter::GenerateFromStoredParams(
	const FString& Shader,
	const TMap<FString, FString>& Params)
{
	FVMTWriter Writer;
	Writer.SetShader(Shader.IsEmpty() ? TEXT("LightmappedGeneric") : Shader);

	for (const auto& Pair : Params)
	{
		Writer.SetParameter(Pair.Key, Pair.Value);
	}

	return Writer.Serialize();
}
