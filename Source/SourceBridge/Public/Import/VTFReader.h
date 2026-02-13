#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/**
 * Reads Valve Texture Format (VTF) files and creates UTexture2D objects.
 * Handles DXT1, DXT3, DXT5, BGRA8888, BGR888, RGB888, RGBA8888 formats.
 * VTF format spec: https://developer.valvesoftware.com/wiki/Valve_Texture_Format
 */
class SOURCEBRIDGE_API FVTFReader
{
public:
	/** Load a VTF file from disk and create a transient UTexture2D. Returns null on failure. */
	static UTexture2D* LoadVTF(const FString& FilePath);

	/** Load a VTF from raw bytes in memory. DebugName is used for log messages only. */
	static UTexture2D* LoadVTFFromMemory(const TArray<uint8>& FileData, const FString& DebugName);

	/**
	 * Decode a VTF file from raw bytes to BGRA8888 pixels (no UTexture2D created).
	 * All VTF formats are decompressed/converted to BGRA8.
	 * @param FileData Raw VTF file bytes
	 * @param DebugName Name for log messages
	 * @param OutBGRA Output BGRA8 pixel data
	 * @param OutWidth Output texture width
	 * @param OutHeight Output texture height
	 * @param bOutHasAlpha True if original format has an alpha channel (DXT3/5, BGRA, RGBA, ABGR)
	 * @return true if successful
	 */
	static bool DecodeToBGRA(const TArray<uint8>& FileData, const FString& DebugName,
		TArray<uint8>& OutBGRA, int32& OutWidth, int32& OutHeight, bool& bOutHasAlpha);

	/** Enable/disable debug texture dumping. When enabled, all loaded VTFs are saved as PNGs. */
	static bool bDebugDumpTextures;

	/** Directory where debug PNGs are saved. Set automatically to Saved/SourceBridge/Debug/Textures/. */
	static FString DebugDumpPath;

private:
	// VTF image format IDs (from VTF spec)
	enum EVTFFormat : uint32
	{
		VTF_RGBA8888 = 0,
		VTF_ABGR8888 = 1,
		VTF_RGB888 = 2,
		VTF_BGR888 = 3,
		VTF_I8 = 5,
		VTF_BGRA8888 = 12,
		VTF_DXT1 = 13,
		VTF_DXT3 = 14,
		VTF_DXT5 = 15,
	};

	/** Get the byte size of image data for a given format and dimensions. */
	static int32 CalcImageSize(uint32 Format, int32 Width, int32 Height);

	/** Convert uncompressed pixel data to BGRA8888. */
	static bool ConvertToBGRA8(const uint8* Src, int32 SrcSize, uint32 SrcFormat,
		int32 Width, int32 Height, TArray<uint8>& OutBGRA);

	/** Decompress DXT1 block data to BGRA8888 pixels. */
	static bool DecompressDXT1(const uint8* Src, int32 Width, int32 Height, TArray<uint8>& OutBGRA);

	/** Decompress DXT3 block data to BGRA8888 pixels. */
	static bool DecompressDXT3(const uint8* Src, int32 Width, int32 Height, TArray<uint8>& OutBGRA);

	/** Decompress DXT5 block data to BGRA8888 pixels. */
	static bool DecompressDXT5(const uint8* Src, int32 Width, int32 Height, TArray<uint8>& OutBGRA);

	/** Save BGRA pixel data as a PNG file. */
	static void SaveBGRAAsPNG(const TArray<uint8>& BGRAData, int32 Width, int32 Height, const FString& FilePath);
};
