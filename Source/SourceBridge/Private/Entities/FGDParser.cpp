#include "Entities/FGDParser.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// ---- FFGDEntityClass ----

const FFGDProperty* FFGDEntityClass::FindProperty(const FString& Name) const
{
	for (const FFGDProperty& Prop : Properties)
	{
		if (Prop.Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return &Prop;
		}
	}
	return nullptr;
}

const FFGDIODef* FFGDEntityClass::FindInput(const FString& Name) const
{
	for (const FFGDIODef& IO : Inputs)
	{
		if (IO.Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return &IO;
		}
	}
	return nullptr;
}

const FFGDIODef* FFGDEntityClass::FindOutput(const FString& Name) const
{
	for (const FFGDIODef& IO : Outputs)
	{
		if (IO.Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return &IO;
		}
	}
	return nullptr;
}

// ---- FFGDDatabase ----

const FFGDEntityClass* FFGDDatabase::FindClass(const FString& ClassName) const
{
	return Classes.Find(ClassName.ToLower());
}

TArray<FString> FFGDDatabase::GetPlaceableClassNames() const
{
	TArray<FString> Result;
	for (const auto& Pair : Classes)
	{
		if (!Pair.Value.bIsBase)
		{
			Result.Add(Pair.Value.ClassName);
		}
	}
	Result.Sort();
	return Result;
}

TArray<FString> FFGDDatabase::GetPointClassNames() const
{
	TArray<FString> Result;
	for (const auto& Pair : Classes)
	{
		if (!Pair.Value.bIsBase && !Pair.Value.bIsSolid)
		{
			Result.Add(Pair.Value.ClassName);
		}
	}
	Result.Sort();
	return Result;
}

TArray<FString> FFGDDatabase::GetSolidClassNames() const
{
	TArray<FString> Result;
	for (const auto& Pair : Classes)
	{
		if (!Pair.Value.bIsBase && Pair.Value.bIsSolid)
		{
			Result.Add(Pair.Value.ClassName);
		}
	}
	Result.Sort();
	return Result;
}

FFGDEntityClass FFGDDatabase::GetResolved(const FString& ClassName) const
{
	const FFGDEntityClass* Class = FindClass(ClassName);
	if (!Class)
	{
		return FFGDEntityClass();
	}

	FFGDEntityClass Resolved = *Class;

	// Merge base classes (depth-first, earliest base = lowest priority)
	for (int32 i = Class->BaseClasses.Num() - 1; i >= 0; --i)
	{
		FFGDEntityClass BaseResolved = GetResolved(Class->BaseClasses[i]);

		// Add base properties that don't already exist in Resolved
		for (const FFGDProperty& BaseProp : BaseResolved.Properties)
		{
			if (!Resolved.FindProperty(BaseProp.Name))
			{
				Resolved.Properties.Insert(BaseProp, 0);
			}
		}

		// Add base inputs that don't already exist
		for (const FFGDIODef& BaseIO : BaseResolved.Inputs)
		{
			if (!Resolved.FindInput(BaseIO.Name))
			{
				Resolved.Inputs.Add(BaseIO);
			}
		}

		// Add base outputs that don't already exist
		for (const FFGDIODef& BaseIO : BaseResolved.Outputs)
		{
			if (!Resolved.FindOutput(BaseIO.Name))
			{
				Resolved.Outputs.Add(BaseIO);
			}
		}
	}

	return Resolved;
}

TArray<FString> FFGDDatabase::ValidateEntity(
	const FString& ClassName,
	const TArray<TPair<FString, FString>>& KeyValues) const
{
	TArray<FString> Warnings;

	const FFGDEntityClass* Class = FindClass(ClassName);
	if (!Class)
	{
		Warnings.Add(FString::Printf(TEXT("Unknown entity class '%s'. Not found in FGD."), *ClassName));
		return Warnings;
	}

	FFGDEntityClass Resolved = GetResolved(ClassName);

	for (const auto& KV : KeyValues)
	{
		// Skip standard keys that all entities have
		if (KV.Key.Equals(TEXT("classname"), ESearchCase::IgnoreCase) ||
			KV.Key.Equals(TEXT("origin"), ESearchCase::IgnoreCase) ||
			KV.Key.Equals(TEXT("angles"), ESearchCase::IgnoreCase) ||
			KV.Key.Equals(TEXT("id"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FFGDProperty* Prop = Resolved.FindProperty(KV.Key);
		if (!Prop)
		{
			Warnings.Add(FString::Printf(
				TEXT("Entity '%s': unknown keyvalue '%s'. Not defined in FGD."),
				*ClassName, *KV.Key));
		}
		else if (Prop->Type == EFGDPropertyType::Choices && Prop->Choices.Num() > 0)
		{
			// Validate choices value
			bool bFound = false;
			for (const FFGDChoice& Choice : Prop->Choices)
			{
				if (Choice.Value.Equals(KV.Value, ESearchCase::IgnoreCase))
				{
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				Warnings.Add(FString::Printf(
					TEXT("Entity '%s': keyvalue '%s' = '%s' is not a valid choice."),
					*ClassName, *KV.Key, *KV.Value));
			}
		}
	}

	return Warnings;
}

FString FFGDDatabase::ValidateIOConnection(
	const FString& SourceClass,
	const FString& OutputName,
	const FString& TargetClass,
	const FString& InputName) const
{
	// Validate output exists on source entity
	if (!SourceClass.IsEmpty())
	{
		FFGDEntityClass SourceResolved = GetResolved(SourceClass);
		if (!SourceResolved.FindOutput(OutputName))
		{
			return FString::Printf(
				TEXT("Entity '%s' has no output '%s'."),
				*SourceClass, *OutputName);
		}
	}

	// Validate input exists on target entity
	if (!TargetClass.IsEmpty())
	{
		FFGDEntityClass TargetResolved = GetResolved(TargetClass);
		if (!TargetResolved.FindInput(InputName))
		{
			return FString::Printf(
				TEXT("Entity '%s' has no input '%s'."),
				*TargetClass, *InputName);
		}
	}

	return FString(); // Valid
}

// ---- FFGDParser ----

FFGDDatabase FFGDParser::ParseFile(const FString& FilePath)
{
	FFGDDatabase Database;

	FString AbsPath = FPaths::ConvertRelativePathToFull(FilePath);
	FString Content;

	if (!FFileHelper::LoadFileToString(Content, *AbsPath))
	{
		Database.Warnings.Add(FString::Printf(TEXT("Failed to read FGD file: %s"), *AbsPath));
		return Database;
	}

	FParseContext Context{ Database, FPaths::GetPath(AbsPath), {} };
	Context.IncludedFiles.Add(AbsPath.ToLower());

	ParseContent(Content, Context);

	UE_LOG(LogTemp, Log, TEXT("SourceBridge: Parsed FGD '%s': %d entity classes (%d warnings)."),
		*FPaths::GetCleanFilename(FilePath), Database.Classes.Num(), Database.Warnings.Num());

	return Database;
}

FFGDDatabase FFGDParser::ParseString(const FString& Content, const FString& BaseDirectory)
{
	FFGDDatabase Database;
	FParseContext Context{ Database, BaseDirectory, {} };
	ParseContent(Content, Context);
	return Database;
}

void FFGDParser::ParseContent(const FString& Content, FParseContext& Context)
{
	int32 Pos = 0;
	int32 Len = Content.Len();

	while (Pos < Len)
	{
		SkipWhitespaceAndComments(Content, Pos);
		if (Pos >= Len) break;

		// Check for @include
		if (Content[Pos] == TEXT('@'))
		{
			// Read the directive
			int32 DirStart = Pos;
			Pos++; // skip @
			FString Directive;
			while (Pos < Len && FChar::IsAlpha(Content[Pos]))
			{
				Directive += Content[Pos++];
			}

			if (Directive.Equals(TEXT("include"), ESearchCase::IgnoreCase))
			{
				SkipWhitespaceAndComments(Content, Pos);
				FString IncludePath = ReadQuotedString(Content, Pos);

				if (!IncludePath.IsEmpty() && !Context.BaseDirectory.IsEmpty())
				{
					FString FullPath = FPaths::Combine(Context.BaseDirectory, IncludePath);
					FullPath = FPaths::ConvertRelativePathToFull(FullPath);
					FString Key = FullPath.ToLower();

					if (!Context.IncludedFiles.Contains(Key))
					{
						Context.IncludedFiles.Add(Key);

						FString IncludeContent;
						if (FFileHelper::LoadFileToString(IncludeContent, *FullPath))
						{
							FString OldBase = Context.BaseDirectory;
							Context.BaseDirectory = FPaths::GetPath(FullPath);
							ParseContent(IncludeContent, Context);
							Context.BaseDirectory = OldBase;
						}
						else
						{
							Context.Database.Warnings.Add(FString::Printf(
								TEXT("@include: failed to read '%s'"), *FullPath));
						}
					}
				}
			}
			else if (Directive.Equals(TEXT("BaseClass"), ESearchCase::IgnoreCase) ||
				Directive.Equals(TEXT("PointClass"), ESearchCase::IgnoreCase) ||
				Directive.Equals(TEXT("SolidClass"), ESearchCase::IgnoreCase) ||
				Directive.Equals(TEXT("NPCClass"), ESearchCase::IgnoreCase) ||
				Directive.Equals(TEXT("KeyFrameClass"), ESearchCase::IgnoreCase) ||
				Directive.Equals(TEXT("MoveClass"), ESearchCase::IgnoreCase) ||
				Directive.Equals(TEXT("FilterClass"), ESearchCase::IgnoreCase))
			{
				// Reset position to start of @ for entity class parsing
				Pos = DirStart;
				ParseEntityClass(Content, Pos, Context);
			}
			else
			{
				// Unknown directive, skip to next line
				while (Pos < Len && Content[Pos] != TEXT('\n'))
				{
					Pos++;
				}
			}
		}
		else
		{
			// Unknown token, skip character
			Pos++;
		}
	}
}

void FFGDParser::ParseEntityClass(const FString& Content, int32& Pos, FParseContext& Context)
{
	int32 Len = Content.Len();

	// Skip @
	if (Pos < Len && Content[Pos] == TEXT('@')) Pos++;

	// Read class type
	FString ClassType;
	while (Pos < Len && FChar::IsAlpha(Content[Pos]))
	{
		ClassType += Content[Pos++];
	}

	FFGDEntityClass EntityClass;
	EntityClass.ClassType = ClassType;
	EntityClass.bIsBase = ClassType.Equals(TEXT("BaseClass"), ESearchCase::IgnoreCase);
	EntityClass.bIsSolid = ClassType.Equals(TEXT("SolidClass"), ESearchCase::IgnoreCase);

	SkipWhitespaceAndComments(Content, Pos);

	// Parse class options: base(X, Y), studio("model.mdl"), iconsprite("path"), color(R G B), size(mins, maxs)
	while (Pos < Len && Content[Pos] != TEXT('='))
	{
		if (Content[Pos] == TEXT('[') || Content[Pos] == TEXT('\n'))
		{
			break;
		}

		FString OptionName;
		while (Pos < Len && FChar::IsAlpha(Content[Pos]))
		{
			OptionName += Content[Pos++];
		}

		SkipWhitespaceAndComments(Content, Pos);

		if (Pos < Len && Content[Pos] == TEXT('('))
		{
			Pos++; // skip (
			FString OptionContent = ReadUntil(Content, Pos, TEXT(')'));
			if (Pos < Len && Content[Pos] == TEXT(')')) Pos++;

			if (OptionName.Equals(TEXT("base"), ESearchCase::IgnoreCase))
			{
				// Parse comma-separated base class names
				TArray<FString> Bases;
				OptionContent.ParseIntoArray(Bases, TEXT(","));
				for (FString& Base : Bases)
				{
					Base.TrimStartAndEndInline();
					if (!Base.IsEmpty())
					{
						EntityClass.BaseClasses.Add(Base);
					}
				}
			}
			else if (OptionName.Equals(TEXT("studio"), ESearchCase::IgnoreCase))
			{
				OptionContent.TrimStartAndEndInline();
				OptionContent = OptionContent.Replace(TEXT("\""), TEXT(""));
				EntityClass.EditorModel = OptionContent;
			}
			else if (OptionName.Equals(TEXT("iconsprite"), ESearchCase::IgnoreCase))
			{
				OptionContent.TrimStartAndEndInline();
				OptionContent = OptionContent.Replace(TEXT("\""), TEXT(""));
				EntityClass.IconSprite = OptionContent;
			}
			else if (OptionName.Equals(TEXT("color"), ESearchCase::IgnoreCase))
			{
				EntityClass.Color = OptionContent.TrimStartAndEnd();
			}
			else if (OptionName.Equals(TEXT("size"), ESearchCase::IgnoreCase))
			{
				// size(-8 -8 -8, 8 8 8)
				int32 CommaIdx;
				if (OptionContent.FindChar(TEXT(','), CommaIdx))
				{
					EntityClass.SizeMins = OptionContent.Left(CommaIdx).TrimStartAndEnd();
					EntityClass.SizeMaxs = OptionContent.Mid(CommaIdx + 1).TrimStartAndEnd();
				}
			}
		}

		SkipWhitespaceAndComments(Content, Pos);
	}

	// Skip '='
	if (Pos < Len && Content[Pos] == TEXT('='))
	{
		Pos++;
	}

	SkipWhitespaceAndComments(Content, Pos);

	// Read classname
	EntityClass.ClassName = ReadToken(Content, Pos);

	SkipWhitespaceAndComments(Content, Pos);

	// Read optional description: "Description text"
	if (Pos < Len && Content[Pos] == TEXT(':'))
	{
		Pos++; // skip :
		SkipWhitespaceAndComments(Content, Pos);
		EntityClass.Description = ReadQuotedString(Content, Pos);
	}

	SkipWhitespaceAndComments(Content, Pos);

	// Parse property body [...]
	if (Pos < Len && Content[Pos] == TEXT('['))
	{
		Pos++; // skip [

		while (Pos < Len)
		{
			SkipWhitespaceAndComments(Content, Pos);

			if (Pos >= Len) break;

			if (Content[Pos] == TEXT(']'))
			{
				Pos++; // skip ]
				break;
			}

			// Check for input/output
			FString Token = ReadToken(Content, Pos);

			if (Token.Equals(TEXT("input"), ESearchCase::IgnoreCase))
			{
				ParseIODef(Content, Pos, true, EntityClass);
			}
			else if (Token.Equals(TEXT("output"), ESearchCase::IgnoreCase))
			{
				ParseIODef(Content, Pos, false, EntityClass);
			}
			else if (!Token.IsEmpty())
			{
				// It's a property name - parse the property definition
				// Put the token back conceptually by handling it in ParseProperty
				FFGDProperty Prop;
				Prop.Name = Token;

				SkipWhitespaceAndComments(Content, Pos);

				// Read property type: name(type)
				if (Pos < Len && Content[Pos] == TEXT('('))
				{
					Pos++; // skip (
					FString TypeStr = ReadUntil(Content, Pos, TEXT(')'));
					if (Pos < Len && Content[Pos] == TEXT(')')) Pos++;

					Prop.Type = ParsePropertyType(TypeStr);

					// Check for readonly flag
					SkipWhitespaceAndComments(Content, Pos);
					if (Pos + 8 < Len)
					{
						FString Next = Content.Mid(Pos, 8);
						if (Next.Equals(TEXT("readonly"), ESearchCase::IgnoreCase))
						{
							Prop.bReadOnly = true;
							Pos += 8;
						}
					}
				}

				SkipWhitespaceAndComments(Content, Pos);

				// Read optional : "Display Name" : "default" : "description"
				// Format: : "Display Name" : "default" : "help text"
				if (Pos < Len && Content[Pos] == TEXT(':'))
				{
					Pos++;
					SkipWhitespaceAndComments(Content, Pos);

					// Display name
					if (Pos < Len && Content[Pos] == TEXT('"'))
					{
						Prop.DisplayName = ReadQuotedString(Content, Pos);
					}

					SkipWhitespaceAndComments(Content, Pos);

					// Default value
					if (Pos < Len && Content[Pos] == TEXT(':'))
					{
						Pos++;
						SkipWhitespaceAndComments(Content, Pos);
						if (Pos < Len && Content[Pos] == TEXT('"'))
						{
							Prop.DefaultValue = ReadQuotedString(Content, Pos);
						}
						else if (Pos < Len && Content[Pos] != TEXT(':') && Content[Pos] != TEXT('[') && Content[Pos] != TEXT('\n'))
						{
							// Unquoted default value (integer)
							Prop.DefaultValue = ReadToken(Content, Pos);
						}
					}

					SkipWhitespaceAndComments(Content, Pos);

					// Description
					if (Pos < Len && Content[Pos] == TEXT(':'))
					{
						Pos++;
						SkipWhitespaceAndComments(Content, Pos);
						if (Pos < Len && Content[Pos] == TEXT('"'))
						{
							Prop.Description = ReadQuotedString(Content, Pos);
						}
					}
				}

				SkipWhitespaceAndComments(Content, Pos);

				// Parse choices or flags block: = [...]
				if (Pos < Len && Content[Pos] == TEXT('='))
				{
					Pos++;
					SkipWhitespaceAndComments(Content, Pos);

					if (Pos < Len && Content[Pos] == TEXT('['))
					{
						Pos++; // skip [

						while (Pos < Len)
						{
							SkipWhitespaceAndComments(Content, Pos);

							if (Pos >= Len || Content[Pos] == TEXT(']'))
							{
								if (Pos < Len) Pos++; // skip ]
								break;
							}

							if (Prop.Type == EFGDPropertyType::Flags)
							{
								// Flag format: bit : "description" : defaultOn
								FString BitStr;
								if (Pos < Len && Content[Pos] == TEXT('"'))
								{
									BitStr = ReadQuotedString(Content, Pos);
								}
								else
								{
									BitStr = ReadToken(Content, Pos);
								}

								SkipWhitespaceAndComments(Content, Pos);

								FFGDFlag Flag;
								Flag.Bit = FCString::Atoi(*BitStr);

								if (Pos < Len && Content[Pos] == TEXT(':'))
								{
									Pos++;
									SkipWhitespaceAndComments(Content, Pos);
									Flag.DisplayName = ReadQuotedString(Content, Pos);
								}

								SkipWhitespaceAndComments(Content, Pos);

								if (Pos < Len && Content[Pos] == TEXT(':'))
								{
									Pos++;
									SkipWhitespaceAndComments(Content, Pos);
									FString DefaultStr = ReadToken(Content, Pos);
									Flag.bDefaultOn = (DefaultStr == TEXT("1"));
								}

								Prop.Flags.Add(Flag);
							}
							else
							{
								// Choice format: value : "description"
								FString Value;
								if (Pos < Len && Content[Pos] == TEXT('"'))
								{
									Value = ReadQuotedString(Content, Pos);
								}
								else
								{
									Value = ReadToken(Content, Pos);
								}

								SkipWhitespaceAndComments(Content, Pos);

								FFGDChoice Choice;
								Choice.Value = Value;

								if (Pos < Len && Content[Pos] == TEXT(':'))
								{
									Pos++;
									SkipWhitespaceAndComments(Content, Pos);
									Choice.DisplayName = ReadQuotedString(Content, Pos);
								}

								Prop.Choices.Add(Choice);
							}
						}
					}
				}

				EntityClass.Properties.Add(Prop);
			}
		}
	}

	// Store in database
	if (!EntityClass.ClassName.IsEmpty())
	{
		Context.Database.Classes.Add(EntityClass.ClassName.ToLower(), EntityClass);
	}
}

void FFGDParser::ParseIODef(const FString& Content, int32& Pos, bool bIsInput, FFGDEntityClass& EntityClass)
{
	SkipWhitespaceAndComments(Content, Pos);

	FFGDIODef IO;
	IO.Name = ReadToken(Content, Pos);

	SkipWhitespaceAndComments(Content, Pos);

	// Read parameter type in parens: (void), (float), (string), etc.
	if (Pos < Content.Len() && Content[Pos] == TEXT('('))
	{
		Pos++;
		IO.ParamType = ReadUntil(Content, Pos, TEXT(')'));
		IO.ParamType.TrimStartAndEndInline();
		if (Pos < Content.Len() && Content[Pos] == TEXT(')')) Pos++;
	}

	SkipWhitespaceAndComments(Content, Pos);

	// Description: : "text"
	if (Pos < Content.Len() && Content[Pos] == TEXT(':'))
	{
		Pos++;
		SkipWhitespaceAndComments(Content, Pos);
		if (Pos < Content.Len() && Content[Pos] == TEXT('"'))
		{
			IO.Description = ReadQuotedString(Content, Pos);
		}
	}

	if (bIsInput)
	{
		EntityClass.Inputs.Add(IO);
	}
	else
	{
		EntityClass.Outputs.Add(IO);
	}
}

void FFGDParser::SkipWhitespaceAndComments(const FString& Content, int32& Pos)
{
	int32 Len = Content.Len();

	while (Pos < Len)
	{
		// Skip whitespace
		if (FChar::IsWhitespace(Content[Pos]))
		{
			Pos++;
			continue;
		}

		// Skip // comments
		if (Pos + 1 < Len && Content[Pos] == TEXT('/') && Content[Pos + 1] == TEXT('/'))
		{
			while (Pos < Len && Content[Pos] != TEXT('\n'))
			{
				Pos++;
			}
			continue;
		}

		break;
	}
}

FString FFGDParser::ReadToken(const FString& Content, int32& Pos)
{
	SkipWhitespaceAndComments(Content, Pos);

	int32 Len = Content.Len();
	if (Pos >= Len) return FString();

	// Quoted string
	if (Content[Pos] == TEXT('"'))
	{
		return ReadQuotedString(Content, Pos);
	}

	// Unquoted token: read until whitespace or special character
	FString Token;
	while (Pos < Len && !FChar::IsWhitespace(Content[Pos]) &&
		Content[Pos] != TEXT('(') && Content[Pos] != TEXT(')') &&
		Content[Pos] != TEXT('[') && Content[Pos] != TEXT(']') &&
		Content[Pos] != TEXT(':') && Content[Pos] != TEXT('=') &&
		Content[Pos] != TEXT('"') && Content[Pos] != TEXT(','))
	{
		Token += Content[Pos++];
	}

	return Token;
}

FString FFGDParser::ReadQuotedString(const FString& Content, int32& Pos)
{
	int32 Len = Content.Len();
	if (Pos >= Len || Content[Pos] != TEXT('"'))
	{
		return FString();
	}

	FString Result;

	// Handle "string" + "string" concatenation pattern used in FGD files
	while (true)
	{
		if (Pos >= Len || Content[Pos] != TEXT('"'))
		{
			break;
		}

		Pos++; // skip opening quote

		while (Pos < Len && Content[Pos] != TEXT('"'))
		{
			// Handle escaped quotes
			if (Content[Pos] == TEXT('\\') && Pos + 1 < Len && Content[Pos + 1] == TEXT('"'))
			{
				Result += TEXT('"');
				Pos += 2;
			}
			else
			{
				Result += Content[Pos++];
			}
		}

		if (Pos < Len) Pos++; // skip closing quote

		// Check for + continuation: skip whitespace, look for +, skip whitespace, expect "
		int32 SavePos = Pos;
		SkipWhitespaceAndComments(Content, Pos);

		if (Pos < Len && Content[Pos] == TEXT('+'))
		{
			Pos++; // skip +
			SkipWhitespaceAndComments(Content, Pos);

			if (Pos < Len && Content[Pos] == TEXT('"'))
			{
				// Continue concatenation
				continue;
			}
		}

		// No continuation - restore position to after last closing quote
		Pos = SavePos;
		break;
	}

	return Result;
}

FString FFGDParser::ReadUntil(const FString& Content, int32& Pos, TCHAR StopChar)
{
	int32 Len = Content.Len();
	FString Result;

	while (Pos < Len && Content[Pos] != StopChar)
	{
		Result += Content[Pos++];
	}

	return Result;
}

EFGDPropertyType FFGDParser::ParsePropertyType(const FString& TypeStr)
{
	FString Lower = TypeStr.ToLower().TrimStartAndEnd();

	if (Lower == TEXT("string"))           return EFGDPropertyType::String;
	if (Lower == TEXT("integer"))          return EFGDPropertyType::Integer;
	if (Lower == TEXT("float"))            return EFGDPropertyType::Float;
	if (Lower == TEXT("choices"))          return EFGDPropertyType::Choices;
	if (Lower == TEXT("flags"))            return EFGDPropertyType::Flags;
	if (Lower == TEXT("color255"))         return EFGDPropertyType::Color255;
	if (Lower == TEXT("studio"))           return EFGDPropertyType::Studio;
	if (Lower == TEXT("sprite"))           return EFGDPropertyType::Sprite;
	if (Lower == TEXT("sound"))            return EFGDPropertyType::Sound;
	if (Lower == TEXT("decal"))            return EFGDPropertyType::Decal;
	if (Lower == TEXT("material"))         return EFGDPropertyType::Material;
	if (Lower == TEXT("scene"))            return EFGDPropertyType::Scene;
	if (Lower == TEXT("sidelist"))         return EFGDPropertyType::SideList;
	if (Lower == TEXT("origin"))           return EFGDPropertyType::Origin;
	if (Lower == TEXT("vecline"))          return EFGDPropertyType::VecLine;
	if (Lower == TEXT("axis"))             return EFGDPropertyType::Axis;
	if (Lower == TEXT("angle"))            return EFGDPropertyType::Angle;
	if (Lower == TEXT("npcclass"))         return EFGDPropertyType::NPCClass;
	if (Lower == TEXT("filterclass"))      return EFGDPropertyType::FilterClass;
	if (Lower == TEXT("pointentityclass")) return EFGDPropertyType::PointEntityClass;
	if (Lower == TEXT("target_source"))    return EFGDPropertyType::TargetSource;
	if (Lower == TEXT("target_destination"))return EFGDPropertyType::TargetDestination;

	return EFGDPropertyType::Unknown;
}
