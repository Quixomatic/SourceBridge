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
	TSet<FString> Visited;
	return GetResolvedInternal(ClassName, Visited);
}

FFGDEntityClass FFGDDatabase::GetResolvedInternal(const FString& ClassName, TSet<FString>& Visited) const
{
	const FFGDEntityClass* Class = FindClass(ClassName);
	if (!Class)
	{
		return FFGDEntityClass();
	}

	// Cycle detection: if we've already visited this class, stop recursion
	if (Visited.Contains(ClassName))
	{
		return *Class;
	}
	Visited.Add(ClassName);

	FFGDEntityClass Resolved = *Class;

	// Merge base classes (depth-first, earliest base = lowest priority)
	for (int32 i = Class->BaseClasses.Num() - 1; i >= 0; --i)
	{
		FFGDEntityClass BaseResolved = GetResolvedInternal(Class->BaseClasses[i], Visited);

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
	TArray<FString> ValidationWarnings;

	const FFGDEntityClass* Class = FindClass(ClassName);
	if (!Class)
	{
		ValidationWarnings.Add(FString::Printf(TEXT("Unknown entity class '%s'. Not found in FGD."), *ClassName));
		return ValidationWarnings;
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
			ValidationWarnings.Add(FString::Printf(
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
				ValidationWarnings.Add(FString::Printf(
					TEXT("Entity '%s': keyvalue '%s' = '%s' is not a valid choice."),
					*ClassName, *KV.Key, *KV.Value));
			}
		}
	}

	return ValidationWarnings;
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
// Modeled after ValveFGD Python parser (pyparsing-based).
// Grammar:
//   fgd       := (mapsize | include | material_ex | autovisgroup | entity)*
//   entity    := '@' class_type definitions '=' name [':' description] properties
//   definitions := (name ['(' args ')'])*
//   properties := '[' (input | output | property)* ']'
//   property  := name '(' type ')' [readonly] [report] [':' display] [':' default] [':' desc] ['=' choices]
//   choices   := '[' (choice)* ']'
//   choice    := value ':' quoted_string
//   input/output := 'input'/'output' name '(' type ')' ':' description
//   name      := [A-Za-z0-9_]+
//   quoted    := '"...' ('+' '"...')*  (concatenation)

// Helper: check if character is a valid FGD name character (alphanumeric + underscore)
static bool IsFGDNameChar(TCHAR Ch)
{
	return FChar::IsAlnum(Ch) || Ch == TEXT('_');
}

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

		if (Content[Pos] != TEXT('@'))
		{
			// Not a directive - skip character
			Pos++;
			continue;
		}

		// Read the directive name after @
		int32 DirStart = Pos;
		Pos++; // skip @
		FString Directive;
		while (Pos < Len && FChar::IsAlpha(Content[Pos]))
		{
			Directive += Content[Pos++];
		}

		if (Directive.Equals(TEXT("include"), ESearchCase::IgnoreCase))
		{
			// @include "filename.fgd"
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
		else if (Directive.Equals(TEXT("mapsize"), ESearchCase::IgnoreCase))
		{
			// @mapsize(-16384, 16384) - skip the whole thing
			SkipWhitespaceAndComments(Content, Pos);
			if (Pos < Len && Content[Pos] == TEXT('('))
			{
				Pos++;
				ReadUntil(Content, Pos, TEXT(')'));
				if (Pos < Len) Pos++; // skip )
			}
		}
		else if (Directive.Equals(TEXT("MaterialExclusion"), ESearchCase::IgnoreCase))
		{
			// @MaterialExclusion [ "material1" "material2" ... ]
			SkipWhitespaceAndComments(Content, Pos);
			if (Pos < Len && Content[Pos] == TEXT('['))
			{
				Pos++;
				// Skip everything until closing ]
				int32 Depth = 1;
				while (Pos < Len && Depth > 0)
				{
					if (Content[Pos] == TEXT('[')) Depth++;
					else if (Content[Pos] == TEXT(']')) Depth--;
					if (Depth > 0) Pos++;
				}
				if (Pos < Len) Pos++; // skip final ]
			}
		}
		else if (Directive.Equals(TEXT("AutoVisGroup"), ESearchCase::IgnoreCase))
		{
			// @AutoVisGroup = "name" [ "group" [ "entity" ... ] ... ]
			// Skip until we find and consume the outer [...]
			SkipWhitespaceAndComments(Content, Pos);
			// Skip past = and name
			while (Pos < Len && Content[Pos] != TEXT('['))
			{
				Pos++;
			}
			if (Pos < Len && Content[Pos] == TEXT('['))
			{
				Pos++;
				int32 Depth = 1;
				while (Pos < Len && Depth > 0)
				{
					if (Content[Pos] == TEXT('[')) Depth++;
					else if (Content[Pos] == TEXT(']')) Depth--;
					if (Depth > 0) Pos++;
				}
				if (Pos < Len) Pos++; // skip final ]
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
			// Unknown directive - skip to next line
			while (Pos < Len && Content[Pos] != TEXT('\n'))
			{
				Pos++;
			}
		}
	}
}

void FFGDParser::ParseEntityClass(const FString& Content, int32& Pos, FParseContext& Context)
{
	int32 Len = Content.Len();

	// Skip @
	if (Pos < Len && Content[Pos] == TEXT('@')) Pos++;

	// Read class type (BaseClass, PointClass, SolidClass, etc.)
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

	// Parse class definitions/options before '='
	// Format: name(args) name(args) ... = classname
	// Python: pp_entity_definition = pp_name + Optional('(' + args + ')')
	while (Pos < Len && Content[Pos] != TEXT('='))
	{
		if (Content[Pos] == TEXT('['))
		{
			// Some entities have [] directly without =
			break;
		}

		int32 LoopStart = Pos;

		// Read option name: alphanumeric + underscore (matches pp_name = Word(alphanums+'_'))
		FString OptionName;
		while (Pos < Len && IsFGDNameChar(Content[Pos]))
		{
			OptionName += Content[Pos++];
		}

		SkipWhitespaceAndComments(Content, Pos);

		// Read optional (args)
		if (Pos < Len && Content[Pos] == TEXT('('))
		{
			Pos++; // skip (
			FString OptionContent = ReadUntil(Content, Pos, TEXT(')'));
			if (Pos < Len && Content[Pos] == TEXT(')')) Pos++;

			// Process known options
			if (OptionName.Equals(TEXT("base"), ESearchCase::IgnoreCase))
			{
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
			else if (OptionName.Equals(TEXT("studio"), ESearchCase::IgnoreCase) ||
				OptionName.Equals(TEXT("studioprop"), ESearchCase::IgnoreCase))
			{
				OptionContent.TrimStartAndEndInline();
				OptionContent = OptionContent.Replace(TEXT("\""), TEXT(""));
				if (!OptionContent.IsEmpty())
				{
					EntityClass.EditorModel = OptionContent;
				}
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
				int32 CommaIdx;
				if (OptionContent.FindChar(TEXT(','), CommaIdx))
				{
					EntityClass.SizeMins = OptionContent.Left(CommaIdx).TrimStartAndEnd();
					EntityClass.SizeMaxs = OptionContent.Mid(CommaIdx + 1).TrimStartAndEnd();
				}
			}
			// Other options (sphere, line, lightcone, etc.) are silently ignored
		}

		SkipWhitespaceAndComments(Content, Pos);

		// Safety: if nothing was consumed, skip a character to prevent infinite loop
		if (Pos == LoopStart)
		{
			Pos++;
		}
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

	// Read optional description: : "Description text"
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

			int32 IterStart = Pos;

			// Read the first token (property name, "input", or "output")
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
				// Format: name(type) [readonly] [report] [: display] [: default] [: desc] [= choices]
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

					SkipWhitespaceAndComments(Content, Pos);

					// Check for readonly/report flags (matches Python's pp_property_readonly/report)
					while (Pos < Len)
					{
						FString PeekWord;
						int32 PeekPos = Pos;
						while (PeekPos < Len && FChar::IsAlpha(Content[PeekPos]))
						{
							PeekWord += Content[PeekPos++];
						}
						if (PeekWord.Equals(TEXT("readonly"), ESearchCase::IgnoreCase))
						{
							Prop.bReadOnly = true;
							Pos = PeekPos;
							SkipWhitespaceAndComments(Content, Pos);
						}
						else if (PeekWord.Equals(TEXT("report"), ESearchCase::IgnoreCase))
						{
							// report flag - acknowledged but not stored (matches Python parser)
							Pos = PeekPos;
							SkipWhitespaceAndComments(Content, Pos);
						}
						else
						{
							break;
						}
					}
				}

				SkipWhitespaceAndComments(Content, Pos);

				// Read optional : "Display Name" : "default" : "description"
				if (Pos < Len && Content[Pos] == TEXT(':'))
				{
					Pos++;
					SkipWhitespaceAndComments(Content, Pos);

					// Display name (quoted string)
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
						else if (Pos < Len && Content[Pos] != TEXT(':') && Content[Pos] != TEXT('[') &&
							Content[Pos] != TEXT(']') && Content[Pos] != TEXT('=') && Content[Pos] != TEXT('\n'))
						{
							// Unquoted default value (integer, float, etc.)
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

							int32 ChoiceStart = Pos;

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
								Flag.bDefaultOn = false;

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

							// Safety: if nothing was consumed, skip to prevent infinite loop
							if (Pos == ChoiceStart && Pos < Len)
							{
								Pos++;
							}
						}
					}
				}

				EntityClass.Properties.Add(Prop);
			}

			// Safety: if nothing was consumed this iteration, skip a character
			if (Pos == IterStart && Pos < Len)
			{
				Pos++;
			}
		}
	}

	// Store in database
	if (!EntityClass.ClassName.IsEmpty())
	{
		Context.Database.Classes.Add(EntityClass.ClassName.ToLower(), EntityClass);
	}
}

void FFGDParser::ParseProperty(const FString& Content, int32& Pos, FFGDEntityClass& EntityClass, FParseContext& Context)
{
	// Called after the property name has already been read by ReadToken in ParseEntityClass.
	// We need the property name from the caller, so we re-read from the current Pos context.
	// Actually, the caller already consumed the token. We need to receive it differently.
	// The property name was the last ReadToken result in ParseEntityClass.
	// We'll restructure: ParseProperty receives the already-read name via EntityClass trick.
	// Better approach: ParseEntityClass passes name to us.
	// For now, the name is stored as the last property's name. Let's restructure.

	// NOTE: This is called from ParseEntityClass which already read the property name token.
	// The name is passed by adding it to EntityClass.Properties temporarily.
	// Actually, let's just inline it differently. ParseEntityClass calls us with Pos
	// right after the property name token was consumed.

	// The caller should set up the property name before calling us.
	// Since we can't change the header easily, we'll work with a convention:
	// ParseProperty is called with Pos right after the property name was read.
	// The property name is the last thing that was read. We'll get it from EntityClass.

	// CHANGED APPROACH: This function is called from ParseEntityClass inline.
	// The actual implementation is inline in ParseEntityClass. This function exists
	// to match the header declaration. See ParseEntityClass for the real implementation.
}

void FFGDParser::ParseIODef(const FString& Content, int32& Pos, bool bIsInput, FFGDEntityClass& EntityClass)
{
	int32 Len = Content.Len();

	SkipWhitespaceAndComments(Content, Pos);

	FFGDIODef IO;
	IO.Name = ReadToken(Content, Pos);

	SkipWhitespaceAndComments(Content, Pos);

	// Read parameter type in parens: (void), (float), (string), etc.
	if (Pos < Len && Content[Pos] == TEXT('('))
	{
		Pos++;
		IO.ParamType = ReadUntil(Content, Pos, TEXT(')'));
		IO.ParamType.TrimStartAndEndInline();
		if (Pos < Len && Content[Pos] == TEXT(')')) Pos++;
	}

	SkipWhitespaceAndComments(Content, Pos);

	// Description: : "text"
	if (Pos < Len && Content[Pos] == TEXT(':'))
	{
		Pos++;
		SkipWhitespaceAndComments(Content, Pos);
		if (Pos < Len && Content[Pos] == TEXT('"'))
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

		// Skip // comments to end of line
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
	// Matches Python's pp_name = Word(alphanums+'_') for names,
	// but also handles numeric tokens like "0", "-1", "255"
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
	// Matches Python's: pp_quoted = Combine(QuotedString('"') + Optional(OneOrMore(
	//     Suppress('+') + QuotedString('"'))), adjacent=False)
	while (true)
	{
		if (Pos >= Len || Content[Pos] != TEXT('"'))
		{
			break;
		}

		Pos++; // skip opening quote

		while (Pos < Len && Content[Pos] != TEXT('"'))
		{
			if (Content[Pos] == TEXT('\\') && Pos + 1 < Len)
			{
				TCHAR Next = Content[Pos + 1];
				if (Next == TEXT('"'))
				{
					Result += TEXT('"');
					Pos += 2;
					continue;
				}
				else if (Next == TEXT('n'))
				{
					Result += TEXT('\n');
					Pos += 2;
					continue;
				}
				else if (Next == TEXT('\\'))
				{
					Result += TEXT('\\');
					Pos += 2;
					continue;
				}
			}
			Result += Content[Pos++];
		}

		if (Pos < Len) Pos++; // skip closing quote

		// Check for + continuation: "str" + "str"
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
	if (Lower == TEXT("bool"))             return EFGDPropertyType::Integer; // bool treated as integer
	if (Lower == TEXT("void"))             return EFGDPropertyType::String;  // void (used in I/O)
	if (Lower == TEXT("color1"))           return EFGDPropertyType::Color255;
	if (Lower == TEXT("node_dest"))        return EFGDPropertyType::Integer; // node destination
	if (Lower == TEXT("script"))           return EFGDPropertyType::String;  // script code
	if (Lower == TEXT("scriptlist"))       return EFGDPropertyType::String;  // script file list
	if (Lower == TEXT("target_name_or_class")) return EFGDPropertyType::String;

	return EFGDPropertyType::Unknown;
}
