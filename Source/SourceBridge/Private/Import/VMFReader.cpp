#include "Import/VMFReader.h"
#include "Misc/FileHelper.h"

TArray<FVMFKeyValues> FVMFReader::ParseFile(const FString& FilePath)
{
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("VMFReader: Failed to read file '%s'"), *FilePath);
		return {};
	}

	return ParseString(Content);
}

TArray<FVMFKeyValues> FVMFReader::ParseString(const FString& Content)
{
	TArray<FVMFKeyValues> Blocks;
	int32 Pos = 0;
	int32 Len = Content.Len();

	while (Pos < Len)
	{
		SkipWhitespaceAndComments(Content, Pos);
		if (Pos >= Len) break;

		// Expect a class name token
		if (Content[Pos] == TEXT('{') || Content[Pos] == TEXT('}'))
		{
			Pos++;
			continue;
		}

		FString ClassName = ReadUnquotedToken(Content, Pos);
		if (ClassName.IsEmpty()) break;

		SkipWhitespaceAndComments(Content, Pos);
		if (Pos >= Len) break;

		if (Content[Pos] == TEXT('{'))
		{
			Blocks.Add(ParseBlock(Content, Pos, ClassName));
		}
	}

	return Blocks;
}

FVMFKeyValues FVMFReader::ParseBlock(const FString& Content, int32& Pos, const FString& ClassName)
{
	FVMFKeyValues Block(ClassName);
	int32 Len = Content.Len();

	// Skip opening brace
	if (Pos < Len && Content[Pos] == TEXT('{'))
	{
		Pos++;
	}

	while (Pos < Len)
	{
		SkipWhitespaceAndComments(Content, Pos);
		if (Pos >= Len) break;

		// Closing brace ends this block
		if (Content[Pos] == TEXT('}'))
		{
			Pos++;
			break;
		}

		// Quoted string = key-value pair
		if (Content[Pos] == TEXT('"'))
		{
			FString Key = ReadQuotedString(Content, Pos);
			SkipWhitespaceAndComments(Content, Pos);

			if (Pos < Len && Content[Pos] == TEXT('"'))
			{
				FString Value = ReadQuotedString(Content, Pos);
				Block.Properties.Emplace(Key, Value);
			}
		}
		else
		{
			// Unquoted token = child class name
			FString ChildName = ReadUnquotedToken(Content, Pos);
			if (ChildName.IsEmpty()) break;

			SkipWhitespaceAndComments(Content, Pos);
			if (Pos < Len && Content[Pos] == TEXT('{'))
			{
				Block.Children.Add(ParseBlock(Content, Pos, ChildName));
			}
		}
	}

	return Block;
}

void FVMFReader::SkipWhitespaceAndComments(const FString& Content, int32& Pos)
{
	int32 Len = Content.Len();
	while (Pos < Len)
	{
		TCHAR Ch = Content[Pos];

		if (Ch == TEXT(' ') || Ch == TEXT('\t') || Ch == TEXT('\r') || Ch == TEXT('\n'))
		{
			Pos++;
			continue;
		}

		// Skip // line comments
		if (Ch == TEXT('/') && Pos + 1 < Len && Content[Pos + 1] == TEXT('/'))
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

FString FVMFReader::ReadQuotedString(const FString& Content, int32& Pos)
{
	int32 Len = Content.Len();
	if (Pos >= Len || Content[Pos] != TEXT('"'))
	{
		return FString();
	}

	Pos++; // skip opening quote
	FString Result;

	while (Pos < Len && Content[Pos] != TEXT('"'))
	{
		if (Content[Pos] == TEXT('\\') && Pos + 1 < Len)
		{
			TCHAR Next = Content[Pos + 1];
			if (Next == TEXT('"') || Next == TEXT('\\'))
			{
				Result += Next;
				Pos += 2;
				continue;
			}
			if (Next == TEXT('n'))
			{
				Result += TEXT('\n');
				Pos += 2;
				continue;
			}
		}
		Result += Content[Pos++];
	}

	if (Pos < Len) Pos++; // skip closing quote
	return Result;
}

FString FVMFReader::ReadUnquotedToken(const FString& Content, int32& Pos)
{
	int32 Len = Content.Len();
	FString Token;

	while (Pos < Len)
	{
		TCHAR Ch = Content[Pos];
		if (FChar::IsAlnum(Ch) || Ch == TEXT('_') || Ch == TEXT('-') || Ch == TEXT('.'))
		{
			Token += Ch;
			Pos++;
		}
		else
		{
			break;
		}
	}

	return Token;
}
