#pragma once

#include "CoreMinimal.h"

/**
 * Reads Valve VPK (Valve PacK) archive files.
 * Supports VPK v1 and v2 directory formats.
 *
 * VPK consists of a directory file (*_dir.vpk) containing a file tree,
 * and archive files (*_000.vpk, *_001.vpk, etc.) containing file data.
 *
 * Format spec: https://developer.valvesoftware.com/wiki/VPK_(file_format)
 */
class SOURCEBRIDGE_API FVPKReader
{
public:
	/** Open and parse a VPK directory file. Returns true on success. */
	bool Open(const FString& DirFilePath);

	/** Check if a file path exists in the VPK. Path uses forward slashes, no leading slash. */
	bool Contains(const FString& FilePath) const;

	/** Extract a file's contents from the VPK archives. Returns true on success. */
	bool ReadFile(const FString& FilePath, TArray<uint8>& OutData) const;

	/** Get the number of entries in the directory. */
	int32 GetEntryCount() const { return Entries.Num(); }

	/** Check if the VPK is open and parsed. */
	bool IsOpen() const { return bIsOpen; }

	/** Log a sample of entries that match a filter. For debugging. */
	void LogEntriesMatching(const FString& Filter, int32 MaxCount = 20) const;

private:
	struct FVPKEntry
	{
		uint32 CRC = 0;
		uint16 PreloadBytes = 0;
		uint16 ArchiveIndex = 0;
		uint32 EntryOffset = 0;
		uint32 EntryLength = 0;
		TArray<uint8> PreloadData;
	};

	/** All file entries keyed by normalized path (lowercase, forward slashes). */
	TMap<FString, FVPKEntry> Entries;

	/** Base path for constructing archive file paths (e.g., "C:/.../cstrike/cstrike_pak_"). */
	FString ArchiveBasePath;

	/** Path to the directory file itself (for embedded file data). */
	FString DirectoryFilePath;

	/** Offset where embedded file data starts in the directory file. */
	int32 EmbeddedDataOffset = 0;

	bool bIsOpen = false;

	/** Read a null-terminated string from a byte buffer at the given offset. Advances offset. */
	static FString ReadNullString(const uint8* Data, int32 DataSize, int32& Offset);
};
