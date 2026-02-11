#include "Materials/VMTWriter.h"
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
