#include "Materials/SurfaceProperties.h"

FSurfacePropertiesDatabase& FSurfacePropertiesDatabase::Get()
{
	static FSurfacePropertiesDatabase Instance;
	return Instance;
}

FSurfacePropertiesDatabase::FSurfacePropertiesDatabase()
{
	InitializeDefaults();
}

void FSurfacePropertiesDatabase::InitializeDefaults()
{
	// ========================================================================
	// Source engine surface properties from surfaceproperties.txt
	// All values taken from CS:S / HL2 surfaceproperties.txt
	// ========================================================================

	auto Add = [this](const FString& Name, float Density, float Elasticity,
		float Friction, float Dampening, TCHAR GameMat, const FString& Base = TEXT(""))
	{
		FSourceSurfaceProperty Prop;
		Prop.Name = Name;
		Prop.BaseName = Base;
		Prop.Density = Density;
		Prop.Elasticity = Elasticity;
		Prop.Friction = Friction;
		Prop.Dampening = Dampening;
		Prop.GameMaterial = GameMat;
		Properties.Add(Name.ToLower(), Prop);
	};

	// Core types
	Add(TEXT("default"),            2000.0f,  0.25f, 0.8f, 0.0f, TEXT('C'));
	Add(TEXT("concrete"),           2400.0f,  0.2f,  0.8f, 0.0f, TEXT('C'));
	Add(TEXT("concrete_block"),     2400.0f,  0.2f,  0.8f, 0.0f, TEXT('C'), TEXT("concrete"));
	Add(TEXT("gravel"),             2400.0f,  0.2f,  0.8f, 0.0f, TEXT('C'), TEXT("concrete"));
	Add(TEXT("rock"),               2400.0f,  0.2f,  0.8f, 0.0f, TEXT('C'), TEXT("concrete"));

	// Metal types
	Add(TEXT("metal"),              2700.0f,  0.3f,  0.8f, 0.0f, TEXT('M'));
	Add(TEXT("metal_box"),          2700.0f,  0.3f,  0.8f, 0.0f, TEXT('M'), TEXT("metal"));
	Add(TEXT("metalpanel"),         2700.0f,  0.3f,  0.8f, 0.0f, TEXT('M'), TEXT("metal"));
	Add(TEXT("metal_bouncy"),       2700.0f,  0.8f,  0.8f, 0.0f, TEXT('M'), TEXT("metal"));
	Add(TEXT("metalvent"),          2700.0f,  0.3f,  0.8f, 0.0f, TEXT('M'), TEXT("metal"));
	Add(TEXT("metalgrate"),         2700.0f,  0.3f,  0.8f, 0.0f, TEXT('M'), TEXT("metal"));
	Add(TEXT("chain"),              2700.0f,  0.3f,  0.8f, 0.0f, TEXT('M'), TEXT("metal"));
	Add(TEXT("chainlink"),          2700.0f,  0.3f,  0.8f, 0.0f, TEXT('M'), TEXT("metal"));
	Add(TEXT("combine_metal"),      2700.0f,  0.3f,  0.8f, 0.0f, TEXT('M'), TEXT("metal"));

	// Wood types
	Add(TEXT("wood"),               700.0f,   0.3f,  0.8f, 0.0f, TEXT('W'));
	Add(TEXT("wood_box"),           700.0f,   0.3f,  0.8f, 0.0f, TEXT('W'), TEXT("wood"));
	Add(TEXT("wood_crate"),         700.0f,   0.3f,  0.8f, 0.0f, TEXT('W'), TEXT("wood"));
	Add(TEXT("wood_plank"),         700.0f,   0.3f,  0.8f, 0.0f, TEXT('W'), TEXT("wood"));
	Add(TEXT("wood_furniture"),     700.0f,   0.3f,  0.8f, 0.0f, TEXT('W'), TEXT("wood"));
	Add(TEXT("wood_panel"),         700.0f,   0.3f,  0.8f, 0.0f, TEXT('W'), TEXT("wood"));
	Add(TEXT("wood_solid"),         700.0f,   0.3f,  0.8f, 0.0f, TEXT('W'), TEXT("wood"));
	Add(TEXT("wood_lowdensity"),    300.0f,   0.3f,  0.8f, 0.0f, TEXT('W'), TEXT("wood"));

	// Glass types
	Add(TEXT("glass"),              2700.0f,  0.3f,  0.5f, 0.0f, TEXT('Y'));
	Add(TEXT("glassbottle"),        2700.0f,  0.3f,  0.5f, 0.0f, TEXT('Y'), TEXT("glass"));

	// Dirt/ground types
	Add(TEXT("dirt"),               1600.0f,  0.15f, 0.8f, 0.0f, TEXT('D'));
	Add(TEXT("grass"),              1600.0f,  0.15f, 0.8f, 0.0f, TEXT('D'), TEXT("dirt"));
	Add(TEXT("mud"),                1600.0f,  0.1f,  0.8f, 0.0f, TEXT('D'), TEXT("dirt"));
	Add(TEXT("sand"),               1600.0f,  0.15f, 0.8f, 0.0f, TEXT('D'), TEXT("dirt"));

	// Tile/ceramic
	Add(TEXT("tile"),               2400.0f,  0.2f,  0.8f, 0.0f, TEXT('T'));
	Add(TEXT("ceramic"),            2400.0f,  0.2f,  0.8f, 0.0f, TEXT('T'), TEXT("tile"));

	// Organic
	Add(TEXT("flesh"),              900.0f,   0.2f,  0.9f, 0.0f, TEXT('F'));
	Add(TEXT("bloodyflesh"),        900.0f,   0.2f,  0.9f, 0.0f, TEXT('B'), TEXT("flesh"));
	Add(TEXT("alienflesh"),         900.0f,   0.2f,  0.9f, 0.0f, TEXT('H'), TEXT("flesh"));
	Add(TEXT("antlion"),            900.0f,   0.2f,  0.9f, 0.0f, TEXT('H'), TEXT("flesh"));

	// Rubber/plastic
	Add(TEXT("rubber"),             1100.0f,  0.6f,  0.8f, 0.0f, TEXT('R'), TEXT("dirt"));
	Add(TEXT("rubbertire"),         1100.0f,  0.6f,  0.8f, 0.0f, TEXT('R'), TEXT("rubber"));
	Add(TEXT("plastic"),            1050.0f,  0.4f,  0.7f, 0.0f, TEXT('L'));
	Add(TEXT("plastic_barrel"),     1050.0f,  0.4f,  0.7f, 0.0f, TEXT('L'), TEXT("plastic"));
	Add(TEXT("plastic_box"),        1050.0f,  0.4f,  0.7f, 0.0f, TEXT('L'), TEXT("plastic"));

	// Cloth/carpet
	Add(TEXT("cloth"),              500.0f,   0.1f,  0.8f, 0.0f, TEXT('C'));
	Add(TEXT("carpet"),             500.0f,   0.1f,  0.9f, 0.0f, TEXT('C'), TEXT("cloth"));

	// Liquid
	Add(TEXT("water"),              1000.0f,  0.01f, 0.8f, 0.0f, TEXT('S'));
	Add(TEXT("slime"),              1200.0f,  0.01f, 0.9f, 0.0f, TEXT('S'), TEXT("water"));
	Add(TEXT("wade"),               1000.0f,  0.01f, 0.8f, 0.0f, TEXT('S'), TEXT("water"));
	Add(TEXT("slosh"),              1000.0f,  0.01f, 0.8f, 0.0f, TEXT('S'), TEXT("water"));

	// Special types
	Add(TEXT("ice"),                900.0f,   0.1f,  0.1f, 0.0f, TEXT('C'));
	Add(TEXT("snow"),               500.0f,   0.1f,  0.6f, 0.0f, TEXT('D'), TEXT("dirt"));
	Add(TEXT("plaster"),            1700.0f,  0.2f,  0.8f, 0.0f, TEXT('C'), TEXT("concrete"));
	Add(TEXT("brick"),              2400.0f,  0.2f,  0.8f, 0.0f, TEXT('C'), TEXT("concrete"));
	Add(TEXT("paper"),              500.0f,   0.1f,  0.7f, 0.0f, TEXT('L'));
	Add(TEXT("cardboard"),          500.0f,   0.1f,  0.7f, 0.0f, TEXT('L'), TEXT("paper"));
	Add(TEXT("foliage"),            400.0f,   0.1f,  0.5f, 0.0f, TEXT('D'));
	Add(TEXT("computer"),           2700.0f,  0.3f,  0.7f, 0.0f, TEXT('M'), TEXT("metal"));
	Add(TEXT("canister"),           2700.0f,  0.3f,  0.8f, 0.0f, TEXT('M'), TEXT("metal"));
	Add(TEXT("weapon"),             2700.0f,  0.3f,  0.8f, 0.0f, TEXT('M'), TEXT("metal"));
	Add(TEXT("porcelain"),          2400.0f,  0.2f,  0.5f, 0.0f, TEXT('T'), TEXT("tile"));
	Add(TEXT("ceiling_tile"),       400.0f,   0.1f,  0.5f, 0.0f, TEXT('C'));
	Add(TEXT("player"),             900.0f,   0.2f,  0.8f, 0.0f, TEXT('F'), TEXT("flesh"));
	Add(TEXT("player_control_clip"),900.0f,   0.2f,  0.8f, 0.0f, TEXT('F'), TEXT("player"));

	// Ladder
	Add(TEXT("ladder"),             2700.0f,  0.3f,  0.8f, 0.0f, TEXT('M'), TEXT("metal"));

	// GMod / CS:S soccer ball (custom)
	{
		FSourceSurfaceProperty Ball;
		Ball.Name = TEXT("gm_ps_soccerball");
		Ball.Density = 4500.0f;
		Ball.Elasticity = 80.0f; // intentionally high for bouncing
		Ball.Friction = 0.2f;
		Ball.Dampening = 0.0f;
		Ball.GameMaterial = TEXT('R');
		Properties.Add(TEXT("gm_ps_soccerball"), Ball);
	}
}

const FSourceSurfaceProperty* FSurfacePropertiesDatabase::Find(const FString& Name) const
{
	return Properties.Find(Name.ToLower());
}

TArray<FString> FSurfacePropertiesDatabase::GetAllNames() const
{
	TArray<FString> Names;
	Properties.GetKeys(Names);
	Names.Sort();
	return Names;
}

bool FSurfacePropertiesDatabase::IsValid(const FString& Name) const
{
	return Properties.Contains(Name.ToLower());
}

float FSurfacePropertiesDatabase::CalculateMass(const FString& SurfacePropName, float VolumeSourceUnits) const
{
	FSourceSurfaceProperty Resolved = GetResolved(SurfacePropName);
	// Volume is in cubic Source units. 1 Source unit ≈ 1.905 cm = 0.01905 m
	// Convert to cubic meters: vol * (0.01905)^3
	float CubicMetersPerCubicUnit = 0.01905f * 0.01905f * 0.01905f;
	float VolumeM3 = VolumeSourceUnits * CubicMetersPerCubicUnit;
	return VolumeM3 * Resolved.Density;
}

FString FSurfacePropertiesDatabase::DetectSurfaceProp(const FString& UEMaterialName) const
{
	FString Lower = UEMaterialName.ToLower();

	// Check for exact matches first (strip UE prefix)
	FString Stripped = Lower;
	if (Stripped.StartsWith(TEXT("m_"))) Stripped = Stripped.Mid(2);
	else if (Stripped.StartsWith(TEXT("mi_"))) Stripped = Stripped.Mid(3);
	else if (Stripped.StartsWith(TEXT("mat_"))) Stripped = Stripped.Mid(4);

	if (Properties.Contains(Stripped))
	{
		return Stripped;
	}

	// Keyword matching (ordered by specificity)
	struct FKeywordMatch
	{
		const TCHAR* Keyword;
		const TCHAR* SurfaceProp;
	};

	static const FKeywordMatch Matches[] = {
		// Specific first
		{ TEXT("concrete_block"), TEXT("concrete_block") },
		{ TEXT("metalgrate"),    TEXT("metalgrate") },
		{ TEXT("chainlink"),     TEXT("chainlink") },
		{ TEXT("wood_crate"),    TEXT("wood_crate") },
		{ TEXT("wood_plank"),    TEXT("wood_plank") },
		{ TEXT("glassbottle"),   TEXT("glassbottle") },
		{ TEXT("ceiling_tile"),  TEXT("ceiling_tile") },
		{ TEXT("rubbertire"),    TEXT("rubbertire") },
		// General
		{ TEXT("concrete"),  TEXT("concrete") },
		{ TEXT("cement"),    TEXT("concrete") },
		{ TEXT("stone"),     TEXT("rock") },
		{ TEXT("rock"),      TEXT("rock") },
		{ TEXT("brick"),     TEXT("brick") },
		{ TEXT("gravel"),    TEXT("gravel") },
		{ TEXT("plaster"),   TEXT("plaster") },
		{ TEXT("metal"),     TEXT("metal") },
		{ TEXT("steel"),     TEXT("metal") },
		{ TEXT("iron"),      TEXT("metal") },
		{ TEXT("aluminum"),  TEXT("metal") },
		{ TEXT("chrome"),    TEXT("metal") },
		{ TEXT("copper"),    TEXT("metal") },
		{ TEXT("wood"),      TEXT("wood") },
		{ TEXT("timber"),    TEXT("wood") },
		{ TEXT("plank"),     TEXT("wood_plank") },
		{ TEXT("crate"),     TEXT("wood_crate") },
		{ TEXT("glass"),     TEXT("glass") },
		{ TEXT("window"),    TEXT("glass") },
		{ TEXT("tile"),      TEXT("tile") },
		{ TEXT("ceramic"),   TEXT("ceramic") },
		{ TEXT("porcelain"), TEXT("porcelain") },
		{ TEXT("dirt"),      TEXT("dirt") },
		{ TEXT("earth"),     TEXT("dirt") },
		{ TEXT("mud"),       TEXT("mud") },
		{ TEXT("grass"),     TEXT("grass") },
		{ TEXT("sand"),      TEXT("sand") },
		{ TEXT("snow"),      TEXT("snow") },
		{ TEXT("ice"),       TEXT("ice") },
		{ TEXT("rubber"),    TEXT("rubber") },
		{ TEXT("plastic"),   TEXT("plastic") },
		{ TEXT("cloth"),     TEXT("cloth") },
		{ TEXT("fabric"),    TEXT("cloth") },
		{ TEXT("carpet"),    TEXT("carpet") },
		{ TEXT("paper"),     TEXT("paper") },
		{ TEXT("cardboard"), TEXT("cardboard") },
		{ TEXT("water"),     TEXT("water") },
		{ TEXT("foliage"),   TEXT("foliage") },
		{ TEXT("leaf"),      TEXT("foliage") },
		{ TEXT("flesh"),     TEXT("flesh") },
		{ TEXT("skin"),      TEXT("flesh") },
	};

	for (const auto& Match : Matches)
	{
		if (Lower.Contains(Match.Keyword))
		{
			return FString(Match.SurfaceProp);
		}
	}

	return TEXT("default");
}

FSourceSurfaceProperty FSurfacePropertiesDatabase::GetResolved(const FString& Name) const
{
	const FSourceSurfaceProperty* Prop = Find(Name);
	if (!Prop)
	{
		// Return default
		const FSourceSurfaceProperty* Default = Find(TEXT("default"));
		if (Default)
		{
			return *Default;
		}
		FSourceSurfaceProperty Fallback;
		return Fallback;
	}

	FSourceSurfaceProperty Result = *Prop;

	// Inherit from base if specified
	if (!Result.BaseName.IsEmpty())
	{
		const FSourceSurfaceProperty* Base = Find(Result.BaseName);
		if (Base)
		{
			// Only inherit values that weren't explicitly set
			// (In Source, children override parents)
			// For our purposes, the child values are already set in InitializeDefaults
		}
	}

	return Result;
}
