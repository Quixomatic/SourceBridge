#include "Import/VPKReader.h"
#include "Misc/FileHelper.h"

// VPK header structures (packed, no alignment)
#pragma pack(push, 1)
struct FVPKHeaderV1
{
	uint32 Signature;   // 0x55aa1234
	uint32 Version;     // 1
	uint32 TreeSize;    // Size in bytes of the directory tree
};

struct FVPKHeaderV2
{
	uint32 Signature;                // 0x55aa1234
	uint32 Version;                  // 2
	uint32 TreeSize;
	uint32 FileDataSectionSize;      // Embedded file data size
	uint32 ArchiveMD5SectionSize;
	uint32 OtherMD5SectionSize;
	uint32 SignatureSectionSize;
};

struct FVPKDirectoryEntry
{
	uint32 CRC;
	uint16 PreloadBytes;
	uint16 ArchiveIndex;    // 0x7fff = data in dir file
	uint32 EntryOffset;
	uint32 EntryLength;
	uint16 Terminator;      // 0xffff
};
#pragma pack(pop)

static const uint32 VPK_SIGNATURE = 0x55aa1234;
static const uint16 VPK_DIR_ARCHIVE = 0x7fff;

FString FVPKReader::ReadNullString(const uint8* Data, int32 DataSize, int32& Offset)
{
	FString Result;
	while (Offset < DataSize)
	{
		char C = (char)Data[Offset++];
		if (C == '\0') break;
		Result += C;
	}
	return Result;
}

bool FVPKReader::Open(const FString& DirFilePath)
{
	Entries.Empty();
	bIsOpen = false;

	// Load entire directory file into memory
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *DirFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("VPKReader: Failed to read: %s"), *DirFilePath);
		return false;
	}

	if (FileData.Num() < (int32)sizeof(FVPKHeaderV1))
	{
		UE_LOG(LogTemp, Warning, TEXT("VPKReader: File too small: %s"), *DirFilePath);
		return false;
	}

	const uint8* Data = FileData.GetData();
	int32 Offset = 0;

	// Check for header (some very old v1 files have no header)
	uint32 FirstWord = *reinterpret_cast<const uint32*>(Data);
	uint32 TreeSize = 0;
	int32 HeaderSize = 0;

	if (FirstWord == VPK_SIGNATURE)
	{
		uint32 Version = *reinterpret_cast<const uint32*>(Data + 4);

		if (Version == 1)
		{
			const FVPKHeaderV1* Header = reinterpret_cast<const FVPKHeaderV1*>(Data);
			TreeSize = Header->TreeSize;
			HeaderSize = sizeof(FVPKHeaderV1);
		}
		else if (Version == 2)
		{
			if (FileData.Num() < (int32)sizeof(FVPKHeaderV2))
			{
				UE_LOG(LogTemp, Warning, TEXT("VPKReader: V2 header truncated: %s"), *DirFilePath);
				return false;
			}
			const FVPKHeaderV2* Header = reinterpret_cast<const FVPKHeaderV2*>(Data);
			TreeSize = Header->TreeSize;
			HeaderSize = sizeof(FVPKHeaderV2);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("VPKReader: Unknown VPK version %d: %s"), Version, *DirFilePath);
			return false;
		}
	}
	else
	{
		// No header (very old v1) - entire file is the tree
		TreeSize = FileData.Num();
		HeaderSize = 0;
	}

	// Store paths for archive access
	DirectoryFilePath = DirFilePath;
	EmbeddedDataOffset = HeaderSize + TreeSize;

	// Derive archive base path: "cstrike_pak_dir.vpk" → "cstrike_pak_"
	FString DirName = FPaths::GetBaseFilename(DirFilePath); // "cstrike_pak_dir"
	FString DirParent = FPaths::GetPath(DirFilePath);
	if (DirName.EndsWith(TEXT("_dir")))
	{
		// Strip "_dir" suffix → "cstrike_pak_"
		// But we need to keep the underscore, so strip just "dir"
		ArchiveBasePath = DirParent / DirName.Left(DirName.Len() - 3); // strips "dir" → "cstrike_pak_"
	}
	else
	{
		// Fallback: just use the name as-is
		ArchiveBasePath = DirParent / DirName + TEXT("_");
	}

	// Parse the directory tree
	Offset = HeaderSize;
	int32 TreeEnd = HeaderSize + TreeSize;

	while (Offset < TreeEnd)
	{
		// Level 1: extension
		FString Extension = ReadNullString(Data, TreeEnd, Offset);
		if (Extension.IsEmpty()) break;

		while (Offset < TreeEnd)
		{
			// Level 2: path
			FString Path = ReadNullString(Data, TreeEnd, Offset);
			if (Path.IsEmpty()) break;

			while (Offset < TreeEnd)
			{
				// Level 3: filename
				FString FileName = ReadNullString(Data, TreeEnd, Offset);
				if (FileName.IsEmpty()) break;

				// Read the directory entry
				if (Offset + (int32)sizeof(FVPKDirectoryEntry) > TreeEnd)
				{
					UE_LOG(LogTemp, Warning, TEXT("VPKReader: Tree truncated at entry"));
					break;
				}

				const FVPKDirectoryEntry* DirEntry = reinterpret_cast<const FVPKDirectoryEntry*>(Data + Offset);
				Offset += sizeof(FVPKDirectoryEntry);

				FVPKEntry Entry;
				Entry.CRC = DirEntry->CRC;
				Entry.PreloadBytes = DirEntry->PreloadBytes;
				Entry.ArchiveIndex = DirEntry->ArchiveIndex;
				Entry.EntryOffset = DirEntry->EntryOffset;
				Entry.EntryLength = DirEntry->EntryLength;

				// Read preload data if present
				if (Entry.PreloadBytes > 0 && Offset + Entry.PreloadBytes <= TreeEnd)
				{
					Entry.PreloadData.SetNumUninitialized(Entry.PreloadBytes);
					FMemory::Memcpy(Entry.PreloadData.GetData(), Data + Offset, Entry.PreloadBytes);
					Offset += Entry.PreloadBytes;
				}

				// Build the full file path
				// A single space represents nonexistent path/name components
				FString FullPath;
				if (Path == TEXT(" "))
				{
					FullPath = FileName + TEXT(".") + Extension;
				}
				else
				{
					FullPath = Path / FileName + TEXT(".") + Extension;
				}

				// Normalize: lowercase, forward slashes
				FullPath = FullPath.Replace(TEXT("\\"), TEXT("/")).ToLower();

				Entries.Add(FullPath, MoveTemp(Entry));
			}
		}
	}

	bIsOpen = true;
	UE_LOG(LogTemp, Log, TEXT("VPKReader: Opened '%s' - %d entries"), *DirFilePath, Entries.Num());
	return true;
}

bool FVPKReader::Contains(const FString& FilePath) const
{
	FString Normalized = FilePath.Replace(TEXT("\\"), TEXT("/")).ToLower();
	return Entries.Contains(Normalized);
}

bool FVPKReader::ReadFile(const FString& FilePath, TArray<uint8>& OutData) const
{
	FString Normalized = FilePath.Replace(TEXT("\\"), TEXT("/")).ToLower();
	const FVPKEntry* Entry = Entries.Find(Normalized);
	if (!Entry)
	{
		return false;
	}

	int32 TotalSize = Entry->PreloadBytes + Entry->EntryLength;
	if (TotalSize == 0)
	{
		OutData.Empty();
		return true;
	}

	OutData.SetNumUninitialized(TotalSize);

	// Copy preload data first
	int32 WriteOffset = 0;
	if (Entry->PreloadBytes > 0 && Entry->PreloadData.Num() > 0)
	{
		FMemory::Memcpy(OutData.GetData(), Entry->PreloadData.GetData(), Entry->PreloadBytes);
		WriteOffset = Entry->PreloadBytes;
	}

	// Read archive data if present
	if (Entry->EntryLength > 0)
	{
		FString ArchivePath;
		if (Entry->ArchiveIndex == VPK_DIR_ARCHIVE)
		{
			// Data is embedded in the directory file
			ArchivePath = DirectoryFilePath;
		}
		else
		{
			// Data is in a numbered archive file
			ArchivePath = FString::Printf(TEXT("%s%03d.vpk"), *ArchiveBasePath, Entry->ArchiveIndex);
		}

		// Read just the needed portion from the archive
		IFileHandle* File = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*ArchivePath);
		if (!File)
		{
			UE_LOG(LogTemp, Warning, TEXT("VPKReader: Failed to open archive: %s"), *ArchivePath);
			return false;
		}

		int64 SeekPos = Entry->EntryOffset;
		if (Entry->ArchiveIndex == VPK_DIR_ARCHIVE)
		{
			SeekPos += EmbeddedDataOffset;
		}

		if (!File->Seek(SeekPos))
		{
			UE_LOG(LogTemp, Warning, TEXT("VPKReader: Failed to seek in archive: %s"), *ArchivePath);
			delete File;
			return false;
		}

		if (!File->Read(OutData.GetData() + WriteOffset, Entry->EntryLength))
		{
			UE_LOG(LogTemp, Warning, TEXT("VPKReader: Failed to read from archive: %s"), *ArchivePath);
			delete File;
			return false;
		}

		delete File;
	}

	return true;
}
